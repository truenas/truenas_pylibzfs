#include "pylibzfs2.h"

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

PyObject *py_repr_zfs_obj_impl(py_zfs_obj_t *obj, const char *fmt)
{
	return PyUnicode_FromFormat(
		fmt,
		obj->name ? obj->name : py_empty_str(),
		obj->pool_name ? obj->pool_name : py_empty_str(),
		obj->type ? obj->type : py_empty_str()
	);
}
