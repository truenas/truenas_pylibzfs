#include "../truenas_pylibzfs.h"

/*
 * ZFSHistoryIterator
 *
 * Python iterator that yields one history record dict per call.
 * Internally it buffers one ~1 MiB batch from zpool_get_history() and
 * drains it record-by-record before fetching the next batch.
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
	pylibzfs_state_t *state;
	py_zfs_error_t zfs_err;
	int err;

	state = py_get_module_state(self->pool->pylibzfsp);

	while (1) {
		/* Drain the current batch */
		while (self->record_idx < self->num_records) {
			nvlist_t *rec = self->records[self->record_idx++];

			if (self->skip_internal &&
			    nvlist_exists(rec, ZPOOL_HIST_INT_EVENT)) {
				continue;
			}

			/* Convert record nvlist to JSON string, then to dict */
			PyObject *nvl_json = py_dump_nvlist(rec, B_TRUE);
			if (nvl_json == NULL)
				return (NULL);

			PyObject *rec_dict = PyObject_CallFunction(
			    state->loads_fn, "O", nvl_json);
			Py_DECREF(nvl_json);

			return (rec_dict);
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
"ZFSHistoryIterator(pool, *, skip_internal=True)\n"
"-------------------------------------------------\n\n"
"Iterator yielding per-pool ZFS history records as dicts.\n\n"
"Each record is a dict with raw nvlist key names from the pool history log:\n"
"  'history_time'     - Unix timestamp of the event (int)\n"
"  'history_command'  - Command string (str, user-visible commands only)\n"
"  'history_who'      - UID that ran the command (int)\n"
"  'history_hostname' - Hostname (str)\n"
"  'history_internal_event' - present only for kernel-internal events\n\n"
"Parameters\n"
"----------\n"
"pool: truenas_pylibzfs.ZFSPool\n"
"    The pool whose history to iterate.\n"
"skip_internal: bool, optional, default=True\n"
"    When True (default) records containing 'history_internal_event' are\n"
"    suppressed, matching the default output of 'zpool history'.\n\n"
"Yields\n"
"------\n"
"dict\n"
"    One history record.  Keys depend on the event type.\n\n"
"Raises\n"
"------\n"
"ZFSException\n"
"    A libzfs error occurred while reading history.\n"
);

PyTypeObject ZFSHistoryIterator = {
	.tp_name      = PYLIBZFS_MODULE_NAME ".ZFSHistoryIterator",
	.tp_basicsize = sizeof (py_zfs_history_iter_t),
	.tp_itemsize  = 0,
	.tp_dealloc   = (destructor)py_zfs_history_iter_dealloc,
	.tp_flags     = Py_TPFLAGS_DEFAULT,
	.tp_doc       = py_zfs_history_iter__doc__,
	.tp_iter      = py_zfs_history_iter_iter,
	.tp_iternext  = py_zfs_history_iter_next,
};

/*
 * Factory: create a ZFSHistoryIterator for the given pool.
 * Called by py_zfs_pool_iter_history().
 */
PyObject *
py_zfs_history_iter_create(py_zfs_pool_t *pool, boolean_t skip_internal)
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

	return ((PyObject *)it);
}
