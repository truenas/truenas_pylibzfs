#ifndef _PY_ZFS_EVENTS_H
#define _PY_ZFS_EVENTS_H

#include "../truenas_pylibzfs.h"

/*
 * Iterator type for ZFS pool events
 *
 * This type implements Python's iterator protocol to yield
 * ZFS pool events using zpool_events_next() from libzfs.
 */
typedef struct {
	PyObject_HEAD
	py_zfs_t *pylibzfsp;
	int zevent_fd;
	boolean_t blocking;
} py_zfs_event_iter_t;

extern PyTypeObject ZFSEventIterator;
extern PyObject *py_zfs_event_iter_create(py_zfs_t *pyzfs, boolean_t blocking,
    boolean_t seek_end);

#endif  /* _PY_ZFS_EVENTS_H */
