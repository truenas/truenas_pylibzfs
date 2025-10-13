#include "../truenas_pylibzfs.h"


PyObject *py_zfs_promote(py_zfs_obj_t *obj)
{
	int err;
	py_zfs_error_t zfs_err;


	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSResource.promote",
			"O", obj->name) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);
	err = zfs_promote(obj->zhp);
	if (err) {
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	}
	zfs_refresh_properties(obj->zhp);
	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_promote() failed");
		return NULL;
	}

	err = py_log_history_fmt(obj->pylibzfsp, "zfs promote %s",
				 zfs_get_name(obj->zhp));

	if (err) {
		return NULL;
	}

	Py_RETURN_NONE;
}
