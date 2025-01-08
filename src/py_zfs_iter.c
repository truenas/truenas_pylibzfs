#include "py_zfs_iter.h"

/*
 * Implementation of python wrappers of libzfs iterators.
 *
 * py_iter_filesystems() should be used as a prototype for writing
 * new iterators.
 *
 * Explanation:
 * -----------
 * (USER_PYTHON_CALLBACK) refers to callable passed as argument to
 * ZFSDataset.iter_filesystems()
 *
 * py_zfs_dataset_iter_filesystems()		(a)
 *   ->py_iter_filesystems()			(b)
 *     ->ITER_ALLOW_THREADS
 *       ->zfs_iter_filesystems_v2()		(c)
 *         ->filesystem_callback()		(d)
 *           ->ITER_END_ALLOW_THREADS
 *             ->common_callback()
 *               ->USER_PYTHON_CALLBACK()
 *           ->ITER_ALLOW_THREADS
 *
 *         ->filesystem_callback()
 *           ->ITER_END_ALLOW_THREADS
 *             ->common_callback()
 *               ->USER_PYTHON_CALLBACK()
 *           ->ITER_ALLOW_THREADS
 *
 *         ->filesystem_callback()
 *           ->ITER_END_ALLOW_THREADS
 *             ->common_callback()
 *               ->USER_PYTHON_CALLBACK()
 *           ->ITER_END_ALLOW_THREADS
 *         ...
 *
 *     ->ITER_END_ALLOW_THREADS
 *
 * How to add a new python ZFS iterator:
 * -----------------------------
 * 1. Write a public function in this file that takes a pointer to
 *    an initialized py_iter_state_t struct as an argument. See
 *    `py_iter_filesystems()` as an example -- (b) above, especially
 *    for where to implement PY_ZFS_LOCK and when / how to release GIL.
 *
 * 2. Create a struct defining the configuration needed for the ZFS iterator
 *    and add it to `union iter_config` in py_zfs_iter.h.
 *    `iter_conf_filesystem_t` may be used as an example.
 *
 * 3. Create a `zfs_iter_f` callback function (copy of definition is below)
 *    using filesystem_callback() -- (d) above as an example.
 *
 *    The callback function must do two things:
 *    3a) Create python object wrapping around zfs_handle_t created by
 *        the ZFS iterator.
 *    3b) Call common_callback() with the python object from (3a) and the
 *        py_iter_state.
 *
 *    NOTE: zfs_close() should only be called on the zfs_handle_t handle
 *    if an error occurs in (3a). If an error occurs in (3b) then the
 *    zfs_handle_t handle will be automatically closed by python as the
 *    object is deallocated.
 *
 * 4. Within the function created in (1) call the new ZFS iterator (c)
 *    with parameters (2) that are provided in py_iter_state_t, and the
 *    callback function from (3).
 *
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

	if (state->iter_config.filesystem.flags & ZFS_ITER_SIMPLE) {
		new_ds->rsrc.is_simple = B_TRUE;
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
