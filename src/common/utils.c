#include "../truenas_pylibzfs.h"

#define PYMAXHISTORYLEN 4096

const char* get_dataset_type(zfs_type_t type) {
	const char *ret;
	switch(type) {
	case ZFS_TYPE_FILESYSTEM:
		ret = "ZFS_TYPE_FILESYSTEM";
		break;
	case ZFS_TYPE_SNAPSHOT:
		ret = "ZFS_TYPE_SNAPSHOT";
		break;
	case ZFS_TYPE_VOLUME:
		ret = "ZFS_TYPE_VOLUME";
		break;
	case ZFS_TYPE_POOL:
		ret = "ZFS_TYPE_POOL";
		break;
	case ZFS_TYPE_BOOKMARK:
		ret = "ZFS_TYPE_BOOKMARK";
		break;
	case ZFS_TYPE_VDEV:
		ret = "ZFS_TYPE_VDEV";
		break;
	default:
		ret = NULL;
		break;
	}
	return (ret);
}

static
PyObject *py_empty_str(void)
{
	return PyUnicode_FromString("<EMPTY>");
}

/*
 * Common function to generate a unicode python object for repr method
 */
PyObject *py_repr_zfs_obj_impl(py_zfs_obj_t *obj, const char *fmt)
{
	return PyUnicode_FromFormat(
		fmt,
		obj->name ? obj->name : py_empty_str(),
		obj->pool_name ? obj->pool_name : py_empty_str(),
		obj->type ? obj->type : py_empty_str()
	);
}

int py_log_history_impl(libzfs_handle_t *hdl_in,
			const char *prefix,
			const char *fmt, ...)
{
	char histbuf[PYMAXHISTORYLEN];
	va_list args;
	size_t sz = 0;
	int err;
	int async_err = 0;
	libzfs_handle_t *hdl = hdl_in;
	boolean_t must_close = B_FALSE;

	if (hdl == NULL) {
		Py_BEGIN_ALLOW_THREADS
		hdl = libzfs_init();
		Py_END_ALLOW_THREADS
		if (hdl == NULL) {
			PyErr_SetString(PyExc_RuntimeError,
					"Failed to create temporary "
					"libzfs handle to log history.");
			return -1;
		}
		must_close = B_TRUE;
	}

	if (prefix) {
		sz = strlcpy(histbuf, prefix, sizeof(histbuf));
	} else {
		sz = strlcpy(histbuf, DEFAULT_HISTORY_PREFIX, sizeof(histbuf));
	}

	PYZFS_ASSERT((sz < sizeof(histbuf)), "unexpected prefix size.");

	va_start(args, fmt);
	vsnprintf(histbuf + sz, sizeof(histbuf) - sz, fmt, args);
	va_end(args);

	do {
		// We do not need to PYZFS_LOCK(pyzfs) because
		// zpool_log_history does not set a ZFS errno
		Py_BEGIN_ALLOW_THREADS
		err = zpool_log_history(hdl, histbuf);
		Py_END_ALLOW_THREADS
	} while (err < 0 && errno == EINTR && !(async_err = PyErr_CheckSignals()));

	if (async_err) {
		// Python has already set exception
		if (must_close) {
			libzfs_fini(hdl);
		}
		return -1;
	} else if (err) {
		PyErr_Format(PyExc_RuntimeError,
			     "[%s]: attempt to log action to zpool history "
			     "failed with error [%d]: %s. Since logging occurs "
			     "after the action completes, this means that the "
			     "specificed action completed successfully; however "
			     "it will not be logged in the normal zpool history "
			     "log. NOTE: the action will still be logged in some "
			     "capacity in the internal zpool log.",
			     histbuf, errno, strerror(errno));
		if (must_close) {
			libzfs_fini(hdl);
		}
		return -1;
	}

	if (must_close) {
		libzfs_fini(hdl);
	}

	return 0;
}
