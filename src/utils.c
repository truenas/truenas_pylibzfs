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

static
PyObject *zfs_err_table_to_dict(void)
{
	PyObject *zfserr_dict = NULL;
	int err;
	uint i;

	zfserr_dict = PyDict_New();
	if (zfserr_dict == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zfserr_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zfserr_table[i].error);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(zfserr_dict,
					   zfserr_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return zfserr_dict;
fail:
	Py_XDECREF(zfserr_dict);
	return NULL;
}

static
PyObject *zpool_status_table_to_dict(void)
{
	PyObject *status_dict = NULL;
	int err;
	uint i;

	status_dict = PyDict_New();
	if (status_dict == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zpool_status_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zpool_status_table[i].status);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(status_dict,
					   zpool_status_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return status_dict;
fail:
	Py_XDECREF(status_dict);
	return NULL;
}

static
PyObject *build_args_tuple_enum(const char *class_name,
				PyObject *(*get_dict)(void))
{
	PyObject *name = NULL;
	PyObject *attrs = NULL;
	PyObject *args_out = NULL;

	name = PyUnicode_FromString(class_name);
	if (name == NULL)
		goto out;

	attrs = get_dict();
	if (attrs == NULL)
		goto out;

	args_out = PyTuple_Pack(2, name, attrs);

out:
	Py_XDECREF(name);
	Py_XDECREF(attrs);
	return args_out;
}

static
int add_enum(PyObject *module,
	     PyObject *enum_type,
	     const char *class_name,
	     PyObject *(*get_dict)(void))
{
	PyObject *args = NULL;
	PyObject *enum_obj = NULL;

	args = build_args_tuple_enum(class_name, get_dict);
	if (args == NULL)
		return -1;

	// Create the enum via the functional API for python enums
	// https://docs.python.org/3/howto/enum.html#functional-api
	enum_obj = PyObject_Call(enum_type, args, NULL);
	Py_DECREF(args);

	// steals reference to enum_obj
	return PyModule_AddObject(module, class_name, enum_obj);
}

/*
 * This function is used to add lookup tables form pylibzfs_enum.h
 * to the module as enum.IntEnum.
 */
int
py_add_zfs_enums(PyObject *module)
{
	int err = -1;
	PyObject *enum_mod = NULL;
	PyObject *enum_type = NULL;

	enum_mod = PyImport_ImportModule("enum");
	if (enum_mod == NULL)
		goto out;

	enum_type = PyObject_GetAttrString(enum_mod, "IntEnum");
	if (enum_type == NULL)
		goto out;


	err = add_enum(module, enum_type, "ZFSError",
		       zfs_err_table_to_dict);
	if (err)
		goto out;

	err = add_enum(module, enum_type, "ZPOOLStatus",
		       zpool_status_table_to_dict);
	if (err)
		goto out;

out:
	Py_XDECREF(enum_type);
	Py_XDECREF(enum_mod);
	return err;
}
