#include "py_zfs_iter.h"

/*
 * Implementation of python wrappers of libzfs iterators.
 *
 * py_iter_filesystems() should be used as a prototype for writing
 * new iterators.
 */


/*
 * Macros for toggling GIL within an iterator
 * Application will dump core if python method called without GIL held.
 */
#define ITER_ALLOW_THREADS(state) { state->_save = PyEval_SaveThread(); }
#define ITER_END_ALLOW_THREADS(state) {PyEval_RestoreThread(state->_save); }

/**
 * @brief Common callback function
 *
 * This callback function should be be called from within zfs_iter_f functions
 * to call the requried python method specified by python API caller.
 *
 * NOTE: currently this function is called while the GIL is held. We can
 * refactor if-needed, but since it's primarily a wrapper around calling a
 * python function, GIL is generally needed here.
 *
 * @param[in] new_hdl	ZFSDataset, ZFSSnapshot, etc object to be passed as
 *     as argument to python callback
 *
 * @param[in] state	py_zfs iterator state structure
 *
 * @return		int - either:
 *			(0) Continue iteration,
 *			(-1) ZFS iterator error,
 *			(-2) Stop iteration,
 *			(-3) Error from callback function
 */
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

	// Deallocate and free handle if callback didn't increment it
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

	// We're done with the result and so we should free it
	Py_DECREF(result);

	PyErr_SetString(PyExc_TypeError,
			"Excepted boolean result from callback function.");

	return ITER_RESULT_ERROR;
}

static int
filesystem_callback(zfs_handle_t *zhp, void *private)
{
	int result = ITER_RESULT_ERROR;
	py_iter_state_t *state = (py_iter_state_t *)private;
	py_zfs_dataset_t *new_ds = NULL;

	// re-enable GIL because we're creating a new python
	// object and then calling the callback function.
	ITER_END_ALLOW_THREADS(state);

	new_ds = init_zfs_dataset(state->pylibzfsp, zhp);
	if (new_ds == NULL) {
		// we only explicitly zfs_close() in error
		// path because new_ds owns it afterwards and
		// will close in dealloc
		zfs_close(zhp);
		goto out;
	}

	result = common_callback((PyObject *)new_ds, state);
out:
	// drop GIL because we're going back to iterating in ZFS
	ITER_ALLOW_THREADS(state);
	return result;
}

#if 0
static int
snapshot_callback(zfs_handle_t *zhp, void *private)
{
	int result = ITER_RESULT_ERROR;
	py_iter_state_t *state = (py_iter_state_t *)private;
	py_zfs_snapshot_t *new_snap = NULL;

	new_snap = init_zfs_snapshot(state->pylibzfsp, zhp);
	if (new_snap == NULL) {
		zfs_close(zhp);
		goto out;
	}

	result = common_callback((PyObject *)new_ds, state);
out:
	ITER_ALLOW_THREADS(state);
	return result;
}

#endif

int
py_iter_filesystems(py_iter_state_t *state)
{
	int iter_ret;
	py_zfs_error_t zfs_err;

	ITER_ALLOW_THREADS(state);
	PY_ZFS_LOCK(state->pylibzfsp);

	iter_ret = zfs_iter_filesystems_v2(state->target,
					   state->iter_config.filesystem.flags,
					   filesystem_callback,
					   (void *)state);
	if (iter_ret == ITER_RESULT_IOCTL_ERROR) {
		py_get_zfs_error(state->pylibzfsp->lzh, &zfs_err);
	}

	PY_ZFS_UNLOCK(state->pylibzfsp);
	ITER_END_ALLOW_THREADS(state);

	if (iter_ret == ITER_RESULT_IOCTL_ERROR) {
		set_exc_from_libzfs(&zfs_err, "zfs_iter_filesystems_v2() failed");
	}

	return iter_ret;
}

#if 0

int
py_iter_snapshots(py_iter_state_t *state)
{
	int iter_ret;
	py_zfs_error_t zfs_err;
	iter_conf_snapshot_t conf = state->iter_config.snapshot;

	ITER_ALLOW_THREADS(state);
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
		py_get_zfs_error(state->pylibzfsp->lzh, &zfs_err);
	}

	PY_ZFS_UNLOCK(state->pylibzfsp);
	ITER_END_ALLOW_THREADS(state);

	if (iter_ret == ITER_RESULT_IOCTL_ERROR) {
		set_exc_from_libzfs(&zfs_err, "zfs_iter_filesystems_v2() failed");
	}

	return iter_ret;
}

#endif
