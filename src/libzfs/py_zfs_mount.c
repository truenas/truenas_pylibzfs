#include "../truenas_pylibzfs.h"
#include <sys/zfs_context.h>
#include <zone.h>


/* Various checks to see whether we should allow users to mount the dataset */
static
boolean_t py_is_mountable_internal(py_zfs_resource_t *res, boolean_t force)
{
	boolean_t is_zoned = B_FALSE;
	boolean_t is_redacted = B_FALSE;
	zfs_canmount_type_t canmount;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(res->obj.pylibzfsp);

	if (zfs_prop_get_int(res->obj.zhp, ZFS_PROP_ZONED) &&
	    getzoneid() == GLOBAL_ZONEID)
		is_zoned = B_FALSE;

	is_redacted = zfs_prop_get_int(res->obj.zhp, ZFS_PROP_REDACTED);
	canmount = zfs_prop_get_int(res->obj.zhp, ZFS_PROP_CANMOUNT);

	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (is_zoned) {
		PyErr_SetString(PyExc_PermissionError,
				"Dataset has zone configuration.");
		return B_FALSE;
	}

	if (is_redacted && !force) {
		PyErr_SetString(PyExc_PermissionError,
				"Dataset is redacted and force "
				"parameter was not specified.");
		return B_FALSE;
	}

	if ((canmount == ZFS_CANMOUNT_OFF) && !force) {
		PyErr_SetString(PyExc_ValueError,
				"Dataset canmount property is set to off "
				"and force parameter was not specified");
		return B_FALSE;
	}

	return B_TRUE;
}

static
boolean_t py_get_mountpoint(py_zfs_resource_t *res,
			    char *mountpoint,
			    size_t mp_sz)
{
	int err;
	py_zfs_error_t zfs_err;

	/*
	 * zfs_mount() fails if ZFS type isn't a filesystem
	 * while zfs_mount_at() does not. This enforces same
	 * behavior since we're looking up the mountpoint
	 * from ZFS property, which is equivalent to zfs_mount()
	 */
	if (res->obj.ctype != ZFS_TYPE_FILESYSTEM) {
		PyErr_SetString(PyExc_ValueError,
				"mountpoint is required if "
				"ZFS type is not a filesystem.");
		return B_FALSE;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(res->obj.pylibzfsp);
	err = zfs_prop_get(res->obj.zhp,
			   ZFS_PROP_MOUNTPOINT,
			   mountpoint,
			   mp_sz,
			   NULL, NULL, 0, B_FALSE);
	if (err)
		py_get_zfs_error(res->obj.pylibzfsp->lzh, &zfs_err);
	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "Failed to get mounpoint");
		return B_FALSE;
	}

	if (strcmp(mountpoint, ZFS_MOUNTPOINT_NONE) == 0) {
		PyErr_SetString(PyExc_ValueError,
				"Dataset mountpoint is set to none");
		return B_FALSE;
	}

	if (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) == 0) {
		PyErr_SetString(PyExc_ValueError,
				"Dataset has legacy mountpoint.");
		return B_FALSE;
	}

	return B_TRUE;
}

/*
 * This function converts a python list of mount options such as
 * ["noatime", "noexec", "ro"] to the format that do_mount() expects
 * in libzfs expects. See option_map in lib/libzfs/os/lniux/libzfs_mount_os.c
 *
 * Since this option map is not exposed publicly we rely on libzfs
 * for final validation of options.
 *
 * NOTE: returned string must be freed by PyMem_Free()
 */
static
char *py_mnt_opts_to_str(PyObject *py_mntopts)
{
	PyObject *sep = NULL;
	PyObject *py_joined = NULL;
	char *out = NULL;

	sep = PyUnicode_FromString(",");
	if (sep == NULL)
		return NULL;

	py_joined = PyUnicode_Join(sep, py_mntopts);
	Py_DECREF(sep);
	if (py_joined == NULL)
		return NULL;

	out = pymem_strdup(PyUnicode_AsUTF8(py_joined));
	Py_XDECREF(py_joined);
	return out;
}

PyObject *py_zfs_mount(py_zfs_resource_t *res,
		       PyObject *py_mp,
		       PyObject *py_mntopts,
		       int flags)
{
	char mountpoint[ZFS_MAXPROPLEN];
	char *mntopts = NULL;
	int err;
	py_zfs_error_t zfs_err;

	/* ensure we have a valid mountpoint */
	if (py_mp == NULL) {
		/*
		 * User did not specify a mountpoint and
		 * so we'll rely on the ZFS mountpoint property.
		 *
		 * This may be set to "none" or "legacy", in
		 * which case we need to raise an error.
		 */
		if (!py_get_mountpoint(res, mountpoint, sizeof(mountpoint)))
			return NULL;
	} else if (py_mp == Py_None) {
		PyErr_SetString(PyExc_ValueError,
				"mountpoint may not be set to None");
		return NULL;
	} else {
		/* Copy the user-provided mountpoint into our buffer */
		const char *mp = PyUnicode_AsUTF8(py_mp);
		if (mp == NULL)
			return NULL;

		strlcpy(mountpoint, mp, sizeof(mountpoint));
	}

	/*
	 * There are various dataset settings that may preclude
	 * manually mounting (especially, without force parameter
	 */
	if (!py_is_mountable_internal(res, flags & MS_FORCE))
		return NULL;

	if (*mountpoint != '/') {
		PyErr_Format(PyExc_ValueError,
			     "%s: mountpoint must be an absolute path.",
			     mountpoint);
		return NULL;
	} else if (strlen(mountpoint) == 1) {
		PyErr_SetString(PyExc_ValueError,
				"Mounting over / is not permitted.");
		return NULL;
	}

	/* do some very minimal parsing of the mount options */
	if (py_mntopts && (py_mntopts != Py_None)) {
		mntopts = py_mnt_opts_to_str(py_mntopts);
		if (mntopts == NULL)
			return NULL;
	}

	/* Now do the actual mounting */
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(res->obj.pylibzfsp);
	err = zfs_mount_at(res->obj.zhp,
			   mntopts,
			   flags,
			   mountpoint);
	if (err)
		py_get_zfs_error(res->obj.pylibzfsp->lzh, &zfs_err);
	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_mount_at() failed");
		PyMem_Free(mntopts);
		return NULL;
	}

	/* Note: libzfs wont generate log for mount / umount of DS */

	PyMem_Free(mntopts);
	Py_RETURN_NONE;
}
