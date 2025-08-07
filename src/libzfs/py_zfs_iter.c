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
 *    zfs_iter_f:
 *    `typedef int (*zfs_iter_f)(zfs_handle_t *, void *);`
 *
 * 4. Within the function created in (1) call the new ZFS iterator (c)
 *    with parameters (2) that are provided in py_iter_state_t, and the
 *    callback function from (3).
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
 * to call the required python method specified by python API caller.
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
 *			(-2) Stop iteration,
 *			(-3) Error from callback function
 */
static int
common_callback(PyObject *new_hdl, py_iter_state_t *state)
{
	PyObject *result = NULL;

	 // unlock resource before callback because the callback may perform
	 // operations that acquire this lock.
	PY_ZFS_UNLOCK(state->pylibzfsp);

	result = PyObject_CallFunctionObjArgs(state->callback_fn,
					      new_hdl,
					      state->private_data,
					      NULL);

	// re-acquire lock because we're going back to iterating
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
			"Expected boolean result from callback function.");

	return ITER_RESULT_ERROR;
}

/**
 * @brief zfs_iter_f callback for zfs_iter_filesystems_*
 *
 * This function is passed as an argument to zfs_iter_filesystems_v2
 * and acts as a wrapper around common_callback().
 *
 * @param[in]	zhp - ZFS handle created by the libzfs iterator.
 * @param[in]	private - pointer to private data passed as argument
 * 		to the libzfs iterator. This will be the `private_data`
 * 		python object in py_iter_state_t. NULL is ok.
 *
 * @result	Returns an int with one of following values:
 * 		ITER_RESULT_SUCCESS (0)
 * 		ITER_RESULT_STOP (-2)
 * 		ITER_RESULT_ERROR (-3)
 */
static int
filesystem_callback(zfs_handle_t *zhp, void *private)
{
	int result = ITER_RESULT_ERROR;
	py_iter_state_t *state = (py_iter_state_t *)private;
	PyObject *new = NULL;
	boolean_t simple = B_FALSE;

	if (state->iter_config.filesystem.flags & ZFS_ITER_SIMPLE)
		simple = B_TRUE;

	// re-enable GIL because we're creating a new python
	// object and then calling the callback function.
	ITER_END_ALLOW_THREADS(state);

	switch(zfs_get_type(zhp)) {
	case ZFS_TYPE_FILESYSTEM:
		new = (PyObject *)init_zfs_dataset(state->pylibzfsp, zhp, simple);
		break;
	case ZFS_TYPE_VOLUME:
		new = (PyObject *)init_zfs_volume(state->pylibzfsp, zhp, simple);
		break;
	default:
		PYZFS_ASSERT(B_FALSE, "Unexpected ZFS type");
	}
	if (new == NULL) {
		// we only explicitly zfs_close() in error
		// path because new_ds owns it afterwards and
		// will close in dealloc
		zfs_close(zhp);
		goto out;
	}

	result = common_callback(new, state);
out:
	// drop GIL because we're going back to iterating in ZFS
	ITER_ALLOW_THREADS(state);
	return result;
}

static int
snapshot_callback(zfs_handle_t *zhp, void *private)
{
	int result = ITER_RESULT_ERROR;
	py_iter_state_t *state = (py_iter_state_t *)private;
	py_zfs_snapshot_t *new_snap = NULL;
	boolean_t simple = state->iter_config.snapshot.flags & ZFS_ITER_SIMPLE;

	ITER_END_ALLOW_THREADS(state);

	new_snap = init_zfs_snapshot(state->pylibzfsp, zhp, simple);
	if (new_snap == NULL) {
		zfs_close(zhp);
		goto out;
	}

	result = common_callback((PyObject *)new_snap, state);
out:
	ITER_ALLOW_THREADS(state);
	return result;
}

static int
userspace_callback(void *private,
		   const char *dom,
		   uid_t xid,
		   uint64_t val,
		   uint64_t default_quota)
{
	int result = ITER_RESULT_ERROR;
	py_iter_state_t *state = (py_iter_state_t *)private;
	iter_conf_userspace_t conf = state->iter_config.userspace;
	PyObject *pyquota = NULL;

	ITER_END_ALLOW_THREADS(state);

	// intentionally omit domain from python layer
	pyquota = py_zfs_userquota(conf.pyuserquota_struct,
				   conf.pyqtype, xid, val, default_quota);
	if (pyquota == NULL)
		goto out;

	result = common_callback(pyquota, state);
out:
	ITER_ALLOW_THREADS(state);
	return result;
}

/**
 * @brief iterate ZFS filesystems and zvols from python
 *
 * This function iterates the child filesystems (non-recursive) of the
 * zfs_handle_t specified as `target` in the py_iter_state_t struct,
 * and calls the python callback function specified as `callback_fn` in
 * the aforementioned struct.
 *
 * NOTE: GIL must be held before calling this function, and
 * state->pylibzfsp->zfs_lock must *not* be held.
 *
 * @param[in] state	py_zfs iterator state structure
 *
 * @return		int - either:
 *			(0) Continue iteration,
 *			(-1) ZFS iterator error,
 *			(-2) Stop iteration,
 *			(-3) Error from callback function
 *			Python exception will be set on error.
 */
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

int
py_iter_snapshots(py_iter_state_t *state)
{
	int iter_ret;
	py_zfs_error_t zfs_err;
	iter_conf_snapshot_t conf = state->iter_config.snapshot;

	ITER_ALLOW_THREADS(state);
	PY_ZFS_LOCK(state->pylibzfsp);

	if (conf.sorted) {
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
		set_exc_from_libzfs(&zfs_err, "zfs_iter_snapshots() failed");
	}

	return iter_ret;
}


#define MAX_ZFS_USERSPACE_RETRIES 50  // number of retries with 0.1 sec sleep
int
py_iter_userspace(py_iter_state_t *state)
{
	int iter_ret, tries;
	py_zfs_error_t zfs_err;
	iter_conf_userspace_t conf = state->iter_config.userspace;

	ITER_ALLOW_THREADS(state);
	PY_ZFS_LOCK(state->pylibzfsp);

	/*
	 * zfs_ioctl() may fail with EBUSY if dataset is unmounted due to
	 * zfsvfs_hold() return. In this case we can retry here a few
	 * times while the GIL is released and the ZFS lock is held.
	 */
	for (tries = 0; tries < MAX_ZFS_USERSPACE_RETRIES; tries++) {
		iter_ret = zfs_userspace(state->target,
					 conf.qtype,
					 userspace_callback,
					 (void *)state);

		if (iter_ret != ITER_RESULT_IOCTL_ERROR)
			break;

		// store libzfs error state in case this is our last retry
		py_get_zfs_error(state->pylibzfsp->lzh, &zfs_err);
		if (zfs_err.code != EZFS_BUSY) {
			// only retry if failed with EBUSY
			break;
		}

		usleep(100000);
	}

	PY_ZFS_UNLOCK(state->pylibzfsp);
	ITER_END_ALLOW_THREADS(state);

	if (iter_ret == ITER_RESULT_IOCTL_ERROR) {
		set_exc_from_libzfs(&zfs_err, "zfs_iter_userspace() failed");
	}

	return iter_ret;
}

int
py_iter_root_filesystems(py_iter_state_t *state)
{
	int iter_ret;
	py_zfs_error_t zfs_err;

	ITER_ALLOW_THREADS(state);
	PY_ZFS_LOCK(state->pylibzfsp);

	// We can use our generic filesystem_callback here
	// since we're producing only dataset handles
	iter_ret = zfs_iter_root(state->pylibzfsp->lzh,
				 filesystem_callback,
				 (void *)state);
	if (iter_ret == ITER_RESULT_IOCTL_ERROR) {
		py_get_zfs_error(state->pylibzfsp->lzh, &zfs_err);
	}

	PY_ZFS_UNLOCK(state->pylibzfsp);
	ITER_END_ALLOW_THREADS(state);

	if (iter_ret == ITER_RESULT_IOCTL_ERROR) {
		set_exc_from_libzfs(&zfs_err, "zfs_iter_root() failed");
	}

	return iter_ret;
}
