#include "py_zfs_events.h"
#include "../truenas_pylibzfs.h"


#define ZEVENT_NONBLOCK 0x1
#define ZEVENT_SEEK_END UINT64_MAX


PyObject *
py_zfs_event_iter_create(py_zfs_t *pyzfs, boolean_t blocking,
    boolean_t seek_end)
{
	py_zfs_event_iter_t *self = NULL;
	py_zfs_error_t zfs_err;
	int fd = -1, error;

	self = (py_zfs_event_iter_t *)ZFSEventIterator.tp_alloc(
	    &ZFSEventIterator, 0);
	if (self == NULL)
		return NULL;

	self->zevent_fd = -1;
	self->blocking = B_FALSE;
	self->pylibzfsp = NULL;

	Py_BEGIN_ALLOW_THREADS
	fd = open(ZFS_DEV, O_RDWR);
	Py_END_ALLOW_THREADS

	if (fd < 0) {
		PyErr_Format(PyExc_OSError,
		    "Failed to open %s: %s", ZFS_DEV, strerror(errno));
		Py_DECREF(self);
		return NULL;
	}

	if (seek_end) {
		Py_BEGIN_ALLOW_THREADS
		PY_ZFS_LOCK(pyzfs);
		error = zpool_events_seek(pyzfs->lzh, ZEVENT_SEEK_END, fd);
		if (error)
			py_get_zfs_error(pyzfs->lzh, &zfs_err);
		PY_ZFS_UNLOCK(pyzfs);
		Py_END_ALLOW_THREADS

		if (error) {
			set_exc_from_libzfs(&zfs_err, "zpool_events_seek() failed");
			close(fd);
			Py_DECREF(self);
			return NULL;
		}
	}

	self->zevent_fd = fd;
	self->blocking = blocking;
	self->pylibzfsp = (py_zfs_t *)Py_NewRef(pyzfs);
	return (PyObject *)self;
}

/*
 * Deallocate the ZFSEventIterator
 * Closes the file descriptor if open
 */
static void
py_zfs_event_iter_dealloc(py_zfs_event_iter_t *self)
{
	if (self->zevent_fd > 0) {
		Py_BEGIN_ALLOW_THREADS
		close(self->zevent_fd);
		Py_END_ALLOW_THREADS
		self->zevent_fd = -1;
	}
	Py_CLEAR(self->pylibzfsp);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

/*
 * Return self as iterator object
 */
static PyObject *
py_zfs_event_iter_iter(PyObject *self)
{
	Py_INCREF(self);
	return self;
}

/*
 * Get next event from zpool_events_next()
 * Returns a dictionary with 'event' (nvlist as dict) and 'dropped' (int)
 */
static PyObject *
py_zfs_event_iter_next(PyObject *self_obj)
{
	py_zfs_event_iter_t *self = (py_zfs_event_iter_t *)self_obj;
	nvlist_t *nvl = NULL;
	int dropped = 0;
	int error;
	PyObject *event_dict = NULL;
	PyObject *result = NULL;
	PyObject *dropped_obj = NULL;
	py_zfs_error_t zfs_err;

	if (self->zevent_fd <= 0) {
		PyErr_SetString(PyExc_RuntimeError,
		    "Event file descriptor is not open");
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(self->pylibzfsp);
	error = zpool_events_next(self->pylibzfsp->lzh, &nvl, &dropped,
	    self->blocking ? 0 : ZEVENT_NONBLOCK, self->zevent_fd);
	if (error) {
		py_get_zfs_error(self->pylibzfsp->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(self->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (error) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			// No more events in non-blocking mode
			PyErr_SetNone(PyExc_StopIteration);
			return NULL;
		} else if (errno == EINTR) {
			// Interrupted, let Python handle it
			PyErr_SetFromErrno(PyExc_OSError);
			return NULL;
		} else {
			set_exc_from_libzfs(&zfs_err, "zpool_events_next() failed");
			return NULL;
		}
	}

	if (nvl == NULL) {
		// No event returned, stop iteration
		PyErr_SetNone(PyExc_StopIteration);
		return NULL;
	}

	event_dict = py_nvlist_to_dict(nvl);

	Py_BEGIN_ALLOW_THREADS
	fnvlist_free(nvl);
	Py_END_ALLOW_THREADS

	if (event_dict == NULL)
		return NULL;

	// Create result dictionary with 'event' and 'dropped' keys
	result = PyDict_New();
	if (result == NULL) {
		Py_DECREF(event_dict);
		return NULL;
	}

	if (PyDict_SetItemString(result, "event", event_dict) < 0) {
		Py_DECREF(event_dict);
		Py_DECREF(result);
		return NULL;
	}
	Py_DECREF(event_dict);

	dropped_obj = PyLong_FromLong((long)dropped);
	if (dropped_obj == NULL) {
		Py_DECREF(result);
		return NULL;
	}

	if (PyDict_SetItemString(result, "dropped", dropped_obj) < 0) {
		Py_DECREF(dropped_obj);
		Py_DECREF(result);
		return NULL;
	}
	Py_DECREF(dropped_obj);

	return result;
}

PyDoc_STRVAR(py_zfs_event_iter__doc__,
"ZFSEventIterator(zfs_handle, *, blocking=False)\n"
"------------------------------------------------\n\n"
"Iterator for ZFS pool events.\n\n"
"This iterator wraps zpool_events_next() from libzfs to yield ZFS pool\n"
"events. Each iteration returns a dictionary containing the event data\n"
"(as a nested dictionary from the nvlist) and the number of dropped events.\n\n"
"Parameters\n"
"----------\n"
"zfs_handle: truenas_pylibzfs.ZFS\n"
"    The ZFS handle object returned by open_handle()\n"
"blocking: bool, optional, default=False\n"
"    If True, the iterator will block waiting for new events.\n"
"    If False, the iterator will raise StopIteration when no events\n"
"    are immediately available.\n\n"
"Yields\n"
"------\n"
"dict\n"
"    A dictionary with two keys:\n"
"    - 'event': dict containing the event data from the nvlist\n"
"    - 'dropped': int indicating the number of events dropped since last read\n\n"
"Raises\n"
"------\n"
"OSError:\n"
"    Failed to open the ZFS event device or read events\n"
"StopIteration:\n"
"    No more events available (in non-blocking mode)\n\n"
"Example\n"
"-------\n"
">>> import truenas_pylibzfs\n"
">>> zfs = truenas_pylibzfs.open_handle()\n"
">>> for event_data in zfs.iter_events(blocking=False):\n"
"...     print(f\"Event: {event_data['event']}\")\n"
"...     print(f\"Dropped: {event_data['dropped']}\")\n"
);

PyTypeObject ZFSEventIterator = {
	.tp_name = PYLIBZFS_TYPES_MODULE_NAME ".ZFSEventIterator",
	.tp_basicsize = sizeof(py_zfs_event_iter_t),
	.tp_itemsize = 0,
	.tp_dealloc = (destructor)py_zfs_event_iter_dealloc,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = py_zfs_event_iter__doc__,
	.tp_iter = py_zfs_event_iter_iter,
	.tp_iternext = py_zfs_event_iter_next,
	.tp_new = py_no_new_impl,
};
