#ifndef _PY_ZFS_ITER_H
#define _PY_ZFS_ITER_H
#include "truenas_pylibzfs.h"

/*
 * The following are return codes for our internal iterators
 * zfs_do_list_ioctl() returns -1 on ioctl errors and so we
 * use other numbers to indicate need to stop iteratation so
 * that they can be differentiated from the ioctl error
 */
#define ITER_RESULT_SUCCESS 0
#define ITER_RESULT_IOCTL_ERROR -1
#define ITER_RESULT_STOP -2
#define ITER_RESULT_ERROR -3

typedef struct {
	int flags;
} iter_conf_filesystem_t;

typedef struct {
	int flags;
	int sorted;
	uint64_t min_txg;
	uint64_t max_txg;
} iter_conf_snapshot_t;

typedef struct {
	int flags;
} iter_conf_bookmark_t;

union iter_config {
	iter_conf_filesystem_t filesystem;
	iter_conf_snapshot_t snapshot;
	iter_conf_bookmark_t bookmark;
};

/*
 * Common iter state for all ZFS iterators
 *
 * pylibzfsp: pointer to underlying libzfs object. This is required in order
 *     to create ZFS objects and do proper reference counting as well as
 *     locking.
 *
 * target: libzfs_handle_t handle to iterate.
 *
 * callback_fn: python callback function to be called in the zfs_iter_f
 *     callback in py_zfs_iter.c
 *
 * private_data: private data provided by python method caller. May be NULL.
 *     if non-null then is added as an argument to the callback.
 *
 * iter_config: iterator-specific configuration options
 *
 * _save - saved thread state to allow toggling GIL as part of iteration.
 */
typedef struct {
	py_zfs_t *pylibzfsp;
	zfs_handle_t *target;
	PyObject *callback_fn;
	PyObject *private_data;
	union iter_config iter_config;
	PyThreadState *_save;
} py_iter_state_t;

extern int py_iter_filesystems(py_iter_state_t *state);
#if 0
extern int py_iter_snapshots(py_iter_state_t *state);
#endif

#endif  /* _PY_ZFS_ITER_H */
