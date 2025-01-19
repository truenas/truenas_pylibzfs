#include "pylibzfs2.h"

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

int py_log_history_fmt(py_zfs_t *pyzfs, const char *fmt, ...)
{
	char histbuf[PYMAXHISTORYLEN];
	va_list args;
	size_t sz;

	sz = strlcpy(histbuf, pyzfs->history_prefix, sizeof(histbuf));
	PYZFS_ASSERT((sz < sizeof(histbuf)), "unexpected prefix size.");

	va_start(args, fmt);
	vsnprintf(histbuf + sz, sizeof(histbuf) - sz, fmt, args);
	va_end(args);

	return zpool_log_history(pyzfs->lzh, histbuf);
}
