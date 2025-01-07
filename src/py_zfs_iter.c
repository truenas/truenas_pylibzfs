#include "py_zfs_iter.h"

static int
common_callback(PyObject *new_hdl, py_iter_state_t *state)
{
	PyObject *result = NULL;

	/*
	 * unlock resource before callback because the callback may perform
	 * operations that acquire this lock.
	 */
	PY_ZFS_UNLOCK(state->pylibzfsp);

	result = PyObject_CallFunctionObjArgs(state->callback_fn,
					      new_hdl,
					      state->private_data,
					      NULL);

	/*
	 * re-aquire lock because we're going back to iterating
	 */
	PY_ZFS_LOCK(state->pylibzfsp);

	Py_DECREF(new_hdl);
	if (result == NULL) {
		// Exception raised
		return ITER_RESULT_ERROR;
	}

	if (result == Py_True) {
		Py_DECREF(result);
		return ITER_RESULT_SUCCESS;
	}

	if (result == Py_False) {
		Py_DECREF(result);
		return ITER_RESULT_STOP;
	}

	Py_DECREF(result);

	PyErr_SetString(PyExc_TypeError,
			"Excepted boolean result from callback function.");

	return ITER_RESULT_ERROR;
}

static int
filesystem_callback(zfs_handle_t *zhp, void *private)
{
	int result;
	py_iter_state_t *state = (py_iter_state_t *)private;
	py_zfs_dataset_t *new_ds = NULL;

	new_ds = init_zfs_dataset(state->pylibzfsp, zhp);
	if (new_ds == NULL) {
		zfs_close(zhp);
		return ITER_RESULT_ERROR;
	}

	result = common_callback((PyObject *)new_ds, state);
	zfs_close(zhp);
	return result;
}

#if 0
static int
snapshot_callback(zfs_handle_t *zhp, void *private)
{
	int result;
	py_iter_state_t *state = (py_iter_state_t *)private;
	py_zfs_snapshot_t *new_snap = NULL;

	new_snap = init_zfs_snapshot(state->pylibzfsp, zhp);
	if (new_snap == NULL) {
		return ITER_RESULT_ERROR;
	}

	result = common_callback((PyObject *)new_ds, state);
	zfs_close(zhp);
	return result;
}

#endif

int
py_iter_filesystems(py_iter_state_t *state)
{
	int iter_ret;
	py_zfs_error_t zfs_err;

	PY_ZFS_LOCK(state->pylibzfsp);

	iter_ret = zfs_iter_filesystems_v2(state->target,
					   state->iter_config.filesystem.flags,
					   filesystem_callback,
					   (void *)state);
	if (iter_ret == ITER_RESULT_IOCTL_ERROR) {
		set_exc_from_libzfs(&zfs_err, "zfs_iter_filesystems_v2() failed");
	}

	PY_ZFS_UNLOCK(state->pylibzfsp);

	return iter_ret;
}

#if 0

int
py_iter_snapshots(py_iter_state_t *state)
{
	int iter_ret;
	py_zfs_error_t zfs_err;
	iter_conf_snapshot_t conf = state->iter_config.snapshot;

	PY_ZFS_LOCK(state->pylibzfsp);

	if (conf->sorted) {
		iter_ret = zfs_iter_snapshots_sorted_v2(state->target,
							conf.flags,
							snapshot_callback,
							(void *)state,
							conf.min_txg,
							conf.max_txg);
	} else {
		iter_ret = zfs_iter_snapshots_v2(state->target,
						 conf.flags,
						 snapshot_callback,
						 (void *)state,
						 conf.min_txg,
						 conf.max_txg);
	}
	if (iter_ret == ITER_RESULT_IOCTL_ERROR) {
		set_exc_from_libzfs(&zfs_err, "zfs_iter_filesystems_v2() failed");
	}

	PY_ZFS_UNLOCK(state->pylibzfsp);

	return iter_ret;
}

#endif
