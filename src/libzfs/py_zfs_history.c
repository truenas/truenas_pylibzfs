#include "../truenas_pylibzfs.h"

/*
 * ZFSHistoryIterator
 *
 * Python iterator that yields one history record dict per call.
 * Internally it buffers one ~1 MiB batch from zpool_get_history() and
 * drains it record-by-record before fetching the next batch.
 *
 * Timestamp filtering (since / until) is applied directly against the
 * ZPOOL_HIST_TIME nvpair on the raw nvlist — before dict conversion —
 * so that discarded records never produce Python objects.
 *
 * History record dicts use raw nvlist key names, e.g.:
 *   "history_time"     (uint64)
 *   "history_command"  (string) - user-visible commands
 *   "history_who"      (uint64)
 *   "history_hostname" (string)
 *   "history_internal_event" (uint32) - present only for internal events
 */

typedef struct {
	PyObject_HEAD
	py_zfs_pool_t	*pool;		  /* Py_INCREF'd on create, DECREF'd on dealloc */
	uint64_t	 offset;	  /* cursor for zpool_get_history             */
	boolean_t	 eof;		  /* set when libzfs signals end of history   */
	nvlist_t	*batch_nvl;	  /* nvlist returned by zpool_get_history     */
	nvlist_t	**records;	  /* array inside batch_nvl (not separately owned) */
	uint_t		 num_records;
	uint_t		 record_idx;
	boolean_t	 skip_internal;	  /* skip ZPOOL_HIST_INT_EVENT records        */
	uint64_t	 since;		  /* 0 = no lower bound; skip rec if ts < since */
	uint64_t	 until;		  /* 0 = no upper bound; skip rec if ts > until */
} py_zfs_history_iter_t;


static void
py_zfs_history_iter_dealloc(py_zfs_history_iter_t *self)
{
	if (self->batch_nvl != NULL) {
		nvlist_free(self->batch_nvl);
		self->batch_nvl = NULL;
	}
	self->records = NULL;
	Py_CLEAR(self->pool);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
py_zfs_history_iter_iter(PyObject *self)
{
	Py_INCREF(self);
	return (self);
}

static PyObject *
py_zfs_history_iter_next(PyObject *self_obj)
{
	py_zfs_history_iter_t *self = (py_zfs_history_iter_t *)self_obj;
	py_zfs_error_t zfs_err;
	int err;

	while (1) {
		/* Drain the current batch */
		while (self->record_idx < self->num_records) {
			nvlist_t *rec = self->records[self->record_idx++];

			/* Timestamp filter — checked before any Python allocation */
			if (self->since != 0 || self->until != 0) {
				uint64_t ts = 0;
				if (nvlist_lookup_uint64(rec,
				    ZPOOL_HIST_TIME, &ts) == 0) {
					if (self->since != 0 && ts < self->since)
						continue;
					if (self->until != 0 && ts > self->until)
						continue;
				}
			}

			/*
			 * "Internal" means any record that lacks a user
			 * command (ZPOOL_HIST_CMD).  This matches the default
			 * behaviour of `zpool history`: records with
			 * ZPOOL_HIST_INT_EVENT, ZPOOL_HIST_INT_NAME,
			 * ZPOOL_HIST_IOCTL, etc. are all suppressed unless
			 * the caller opts in via skip_internal=False.
			 */
			if (self->skip_internal &&
			    !nvlist_exists(rec, ZPOOL_HIST_CMD)) {
				continue;
			}

			return py_nvlist_to_dict(rec);
		}

		/* Batch exhausted */
		if (self->eof) {
			PyErr_SetNone(PyExc_StopIteration);
			return (NULL);
		}

		/* Fetch next batch */
		nvlist_t *new_batch = NULL;

		Py_BEGIN_ALLOW_THREADS
		PY_ZFS_LOCK(self->pool->pylibzfsp);
		err = zpool_get_history(self->pool->zhp, &new_batch,
		    &self->offset, &self->eof);
		if (err) {
			py_get_zfs_error(self->pool->pylibzfsp->lzh, &zfs_err);
		}
		PY_ZFS_UNLOCK(self->pool->pylibzfsp);
		Py_END_ALLOW_THREADS

		if (err) {
			set_exc_from_libzfs(&zfs_err,
			    "zpool_get_history() failed");
			return (NULL);
		}

		/* Replace old batch */
		if (self->batch_nvl != NULL) {
			nvlist_free(self->batch_nvl);
		}
		self->batch_nvl = new_batch;
		self->records = NULL;
		self->num_records = 0;
		self->record_idx = 0;

		if (self->batch_nvl != NULL) {
			(void) nvlist_lookup_nvlist_array(self->batch_nvl,
			    ZPOOL_HIST_RECORD, &self->records,
			    &self->num_records);
		}
	}
}

PyDoc_STRVAR(py_zfs_history_iter__doc__,
"ZFSHistoryIterator\n"
"------------------\n\n"
"Iterator yielding per-pool ZFS history records as dicts.\n\n"
"Each record is a dict with raw nvlist key names from the pool history log:\n"
"  'history time'     - Unix timestamp of the event (int)\n"
"  'history command'  - Command string (str, user-visible commands only)\n"
"  'history who'      - UID that ran the command (int)\n"
"  'history hostname' - Hostname (str)\n"
"  'history internal event' - present only on ZPOOL_HIST_INT_EVENT records\n\n"
"Timestamp filtering (since / until) is applied directly on the raw nvlist\n"
"before any Python object is allocated, so filtered records are free.\n\n"
"Raises\n"
"------\n"
"ZFSException\n"
"    A libzfs error occurred while reading history.\n"
);

PyTypeObject ZFSHistoryIterator = {
	.tp_name      = PYLIBZFS_TYPES_MODULE_NAME ".ZFSHistoryIterator",
	.tp_basicsize = sizeof (py_zfs_history_iter_t),
	.tp_itemsize  = 0,
	.tp_dealloc   = (destructor)py_zfs_history_iter_dealloc,
	.tp_new       = py_no_new_impl,
	.tp_flags     = Py_TPFLAGS_DEFAULT,
	.tp_doc       = py_zfs_history_iter__doc__,
	.tp_iter      = py_zfs_history_iter_iter,
	.tp_iternext  = py_zfs_history_iter_next,
};

/*
 * Factory: create a ZFSHistoryIterator for the given pool.
 * Called by py_zfs_pool_iter_history().
 *
 * since / until: Unix timestamps (uint64).  Use 0 to mean "no bound".
 * A record is yielded only when:
 *   (since == 0 || ts >= since) && (until == 0 || ts <= until)
 */
PyObject *
py_zfs_history_iter_create(py_zfs_pool_t *pool, boolean_t skip_internal,
    uint64_t since, uint64_t until)
{
	py_zfs_history_iter_t *it;

	it = (py_zfs_history_iter_t *)ZFSHistoryIterator.tp_alloc(
	    &ZFSHistoryIterator, 0);
	if (it == NULL)
		return (NULL);

	it->pool = pool;
	Py_INCREF(pool);
	it->offset = 0;
	it->eof = B_FALSE;
	it->batch_nvl = NULL;
	it->records = NULL;
	it->num_records = 0;
	it->record_idx = 0;
	it->skip_internal = skip_internal;
	it->since = since;
	it->until = until;

	return ((PyObject *)it);
}
