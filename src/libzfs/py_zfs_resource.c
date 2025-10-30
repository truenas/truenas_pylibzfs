#include "truenas_pylibzfs.h"
#include "py_zfs_iter.h"

#define ZFS_RESOURCE_STR "<" PYLIBZFS_MODULE_NAME \
    ".ZFSResource(name=%U, pool=%U, type=%U)>"


static PyObject *py_zfs_resoucre_iter(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_resource_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_resource_t *self = NULL;
	self = (py_zfs_resource_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_resource_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_resource_dealloc(py_zfs_resource_t *self) {
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *py_repr_zfs_resource(PyObject *self)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;

	return py_repr_zfs_obj_impl(&res->obj, ZFS_RESOURCE_STR);
}

PyDoc_STRVAR(py_zfs_resource_refresh_props__doc__,
"refresh_properties() -> None\n"
"--------------------------------------------------------\n"
"Refresh the properties for a ZFSResource. ZFS properties may be internally\n"
"cached in the zfs_handle_t object underlying the python object.\n"
"Parameters\n"
"----------\n"
"None\n\n"
""
"Returns\n"
"-------\n"
"None\n\n"
""
"Raises\n"
"------\n"
"Does not raise an exception.\n"
);
static
PyObject *py_zfs_resource_refresh_props(PyObject *self, PyObject *args_unused)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;
	py_zfs_props_refresh(res);
	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_resource_get_mount__doc__,
"get_mountpoint() -> str | None\n"
"--------------------------------------------------------\n"
"Check whether the ZFS resource is mounted and if it is mounted, return\n"
"the path at which it is mounted, otherwise return None type\n\n"
""
"Parameters\n"
"----------\n"
"None\n\n"
""
"Returns\n"
"-------\n"
"str: mountpoint of the dataset if mounted otherwise None type\n"
""
"Raises\n"
"------\n"
"MemoryError: failed to allocate memory for mountpoint string.\n"
);
static
PyObject *py_zfs_resource_get_mount(PyObject *self, PyObject *args_unused)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;
	boolean_t mounted;
	char *mp = NULL;
	PyObject *out = NULL;

	// PY_ZFS_LOCK needs held due to interaction with libzfs mnttab
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(res->obj.pylibzfsp);
	mounted = zfs_is_mounted(res->obj.zhp, &mp);
	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (!mounted)
		Py_RETURN_NONE;

	if (mp == NULL)
		// strdup failed
		return PyErr_NoMemory();

	out = PyUnicode_FromString(mp);
	free(mp);
	return out;
}

PyDoc_STRVAR(py_zfs_resource_iter_filesystems__doc__,
"iter_filesystems(*, callback, state, fast=False) -> bool\n\n"
"--------------------------------------------------------\n\n"
"List all child filesystems of this ZFSResource. Arguments are keyword-only\n\n"
"Parameters\n"
"----------\n"
"callback: callable\n"
"    Callback function that will be called for every child dataset.\n\n"
"state: object, optional\n"
"    Optional python object (for example dictionary) passed as an argument\n"
"    to the callback function for each dataset child.\n\n"
"fast: bool, optional, default=False\n"
"    Optional boolean flag to perform faster filesystem iteration.\n"
"    The speedup is accomplished by generating simple ZFS dataset handles\n"
"    that do not contain full property information\n\n"
"Returns\n"
"-------\n"
"bool\n"
"    Value indicates that iteration completed without being stopp by the\n"
"    callback fuction returning False.\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    An error occurred during iteration of the dataset. Note that this\n"
"    exception type may also be raised within the callback function.\n\n"
"NOTE regarding \"callback\":\n"
"--------------------------\n"
"Minimally the function signature must take a single argument for each ZFS\n"
"object. If the \"state\" keyword is specified then the callback function\n"
"should take two arguments. The callback function must return bool value\n"
"indicating whether iteration should continue.\n\n"
"Example \"callback\":\n"
"-------------------\n"
"def my_callback(ds, state):\n"
"    print(f'{ds.name}: {state}')\n"
"    return True\n"
);
static
PyObject *py_zfs_resource_iter_filesystems(PyObject *self,
					   PyObject *args_unused,
					   PyObject *kwargs)
{
	int err;
	py_zfs_resource_t *rsrc = (py_zfs_resource_t *)self;
	py_zfs_obj_t *obj = &rsrc->obj;

	py_iter_state_t iter_state = (py_iter_state_t){
		.pylibzfsp = obj->pylibzfsp,
		.target = obj->zhp
	};
	int simple_handle = 0;

	char *kwnames [] = {
		"callback",
		"state",
		"fast",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$OOp",
					 kwnames,
					 &iter_state.callback_fn,
					 &iter_state.private_data,
					 &simple_handle)) {
		return NULL;
	}

	if (!iter_state.callback_fn) {
		PyErr_SetString(PyExc_ValueError,
				"`callback` keyword argument is required.");
		return NULL;
	}

	if (!PyCallable_Check(iter_state.callback_fn)) {
		PyErr_SetString(PyExc_TypeError,
				"callback function must be callable.");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSResource.iter_filesystems",
			"OO", obj->name, kwargs) < 0) {
		return NULL;
	}

	if (simple_handle) {
		iter_state.iter_config.filesystem.flags |= ZFS_ITER_SIMPLE;
	}

	err = py_iter_filesystems(&iter_state);
	if ((err == ITER_RESULT_ERROR) || (err == ITER_RESULT_IOCTL_ERROR)) {
		// Exception is set by callback function
		return NULL;
	}

	if (err == ITER_RESULT_SUCCESS) {
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyDoc_STRVAR(py_zfs_resource_iter_snapshots__doc__,
"iter_snapshots(*, callback, state, fast=False,\n"
"               min_transaction_group=0,\n"
"               max_transaction_group=0,\n"
"               order_by_transaction_group=False) -> bool\n"
"--------------------------------------------------------\n\n"
"List all snapshots of this ZFSResource. Arguments are keyword-only\n\n"
"Parameters\n"
"----------\n"
"callback: callable\n"
"    Callback function that will be called for every child dataset.\n\n"
"state: object, optional\n"
"    Optional python object (for example dictionary) passed as an argument\n"
"    to the callback function for each dataset child.\n\n"
"fast: bool, optional, default=False\n"
"    Optional boolean flag to perform faster filesystem iteration.\n"
"    The speedup is accomplished by generating simple ZFS dataset handles\n"
"    that do not contain full property information\n\n"
"min_transaction_group: int, optional\n"
"    Only include snapshots newer than the specified transaction group\n\n"
"max_transaction_group: int, optional\n"
"    Only include snapshots older than the specified transaction group\n\n"
"order_by_transaction_group: bool, optional, default=False\n"
"    Pre-sort the snapshots by transaction group prior to calling\n"
"    the specified callback function\n\n"
"Returns\n"
"-------\n"
"bool\n"
"    Value indicates that iteration completed without being stopped by the\n"
"    callback fuction returning False.\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    An error occurred during iteration of the dataset. Note that this\n"
"    exception type may also be raised within the callback function.\n\n"
"NOTE regarding \"callback\":\n"
"--------------------------\n"
"Minimally the function signature must take a single argument for each ZFS\n"
"object. If the \"state\" keyword is specified then the callback function\n"
"should take two arguments. The callback function must return bool value\n"
"indicating whether iteration should continue.\n\n"
"Example \"callback\":\n"
"-------------------\n"
"def my_callback(ds, state):\n"
"    print(f'{ds.name}: {state}')\n"
"    return True\n"
);
static
PyObject *py_zfs_resource_iter_snapshots(PyObject *self,
					 PyObject *args_unused,
					 PyObject *kwargs)
{
	int err;
	py_zfs_resource_t *rsrc = (py_zfs_resource_t *)self;
	py_zfs_obj_t *obj = &rsrc->obj;
	boolean_t simple_handle = B_FALSE;

	py_iter_state_t iter_state = (py_iter_state_t){
		.pylibzfsp = obj->pylibzfsp,
		.target = obj->zhp
	};

	char *kwnames [] = {
		"callback",
		"state",
		"fast",
		"min_transaction_group",
		"max_transaction_group",
		"order_by_transaction_group",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$OOpkkp",
					 kwnames,
					 &iter_state.callback_fn,
					 &iter_state.private_data,
					 &simple_handle,
					 &iter_state.iter_config.snapshot.min_txg,
					 &iter_state.iter_config.snapshot.max_txg,
					 &iter_state.iter_config.snapshot.sorted)) {
		return NULL;
	}

	if (!iter_state.callback_fn) {
		PyErr_SetString(PyExc_ValueError,
				"`callback` keyword argument is required.");
		return NULL;
	}

	if (!PyCallable_Check(iter_state.callback_fn)) {
		PyErr_SetString(PyExc_TypeError,
				"callback function must be callable.");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSResource.iter_snapshots",
			"OO", obj->name, kwargs) < 0) {
		return NULL;
	}

	if (simple_handle) {
		iter_state.iter_config.filesystem.flags |= ZFS_ITER_SIMPLE;
	}

	err = py_iter_snapshots(&iter_state);
	if ((err == ITER_RESULT_ERROR) || (err == ITER_RESULT_IOCTL_ERROR)) {
		// Exception is set by callback function
		return NULL;
	}

	if (err == ITER_RESULT_SUCCESS) {
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyDoc_STRVAR(py_zfs_resource_get_properties__doc__,
"get_properties(*, properties, get_source=False) -> "
"truenas_pylibzfs.struct_zfs_property\n\n"
"-----------------------------------------------------\n\n"
"Get the specified properties of a given ZFS resource.\n\n"
""
"Parameters\n"
"----------\n"
"properties: set, required\n"
"    Set of truenas_pylibzfs.ZFSProperty properties to retrieve.\n\n"
"get_source: bool, optional, default=False\n"
"    Non-default option to retrieve the source information for the returned\n"
"    propeties.\n\n"
""
"Returns\n"
"-------\n"
"truenas_pylibzfs.struct_zfs_property\n"
"    Struct sequence object containing the requested property information.\n"
"    The requested properties will be represented by truenas_pylibzfs.struct_zfs_property_data\n"
"    objects under the respective attributes. Properties that were not\n"
"    requested will be set to None type.\n\n"
""
"Raises:\n"
"-------\n"
"TypeError:\n"
"    The specified properties is not a python set.\n\n"
"ValueError:\n"
"    One of the specified properties is not supported for the ZFS type of the\n"
"    underlying ZFS resource. For example, requesting a zvol property for a\n"
"    ZFS filesystem.\n\n"
"RuntimeError:\n"
"    An unexpected error occurred while retrieving the value of a ZFS property.\n"
"    This should not happen and API consumers experiencing this issue should\n"
"    file a bug report against this python library.\n\n"
);
static
PyObject *py_zfs_resource_get_properties(PyObject *self,
					 PyObject *args_unused,
					 PyObject *kwargs)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;
	PyObject *prop_set = NULL;
	boolean_t get_source = B_FALSE;
	char *kwnames [] = {
		"properties",
		"get_source",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$Op",
					 kwnames,
					 &prop_set,
					 &get_source)) {
					 return NULL;
	}
	if (prop_set == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"properties keyword is required.");
		return NULL;
	}
	if (!PyAnySet_Check(prop_set)) {
		PyErr_SetString(PyExc_TypeError,
				"properties must be a python set.");
		return NULL;
	}

	if (res->is_simple) {
		/*
		 * We have simple handle that lacks property information.
		 * This means we _must_ refresh properties before
		 * generating python object
		 */
		py_zfs_props_refresh(res);
	}

	return py_zfs_get_properties(&res->obj, prop_set, get_source);
}

PyDoc_STRVAR(py_zfs_resource_set_properties__doc__,
"set_properties(*, properties, remount=True) -> None\n\n"
"----------------------------------------------------\n\n"
"Set the specified properties on a given ZFS resource.\n\n"
""
"Parameters\n"
"----------\n"
"properties: dict | truenas_pylibzfs.struct_zfs_props\n"
"    Properties and values to set. This can be formatted as either\n\n"
"    a dictionary with the form: \n"
"    `{key: value}`\n"
"    or the form: \n"
"    `{key: {\"raw\": value, \"value\": value}}`\n"
"    in this case preference is given to the raw value\n"
"\n"
"    Alternatively the properties may also be provided in the form\n"
"    of a struct_zfs_props instance as returned by get_properties()\n"
"\n"
"remount bool, optional, default=True\n"
"    Non-default option to automatically remount the dataset on\n"
"    change of the mountpoint, sharenfs, or sharesmb properties.\n\n"
""
"Returns\n"
"-------\n"
"None\n"
"\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    Properties is not one of supported types listed above.\n\n"
"ValueError:\n"
"    One of the specified properties is not supported for the ZFS type of the\n"
"    underlying ZFS resource. For example, setting a zvol property for a\n"
"    ZFS filesystem.\n\n"
"ZFSError:\n"
"    The ZFS operation failed. This can happen for a variety of reasons.\n"
);
static
PyObject *py_zfs_resource_set_properties(PyObject *self,
					 PyObject *args_unused,
					 PyObject *kwargs)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;
	nvlist_t *nvl = NULL;
	PyObject *propsdict = NULL;
	PyObject *conv_str = NULL;
	pylibzfs_state_t *state = NULL;
	boolean_t remount = B_TRUE;
	py_zfs_error_t zfs_err;
	int err;

	char *kwnames [] = {
		"properties",
		"remount",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$Op",
					 kwnames,
					 &propsdict,
					 &remount)) {
					 return NULL;
	}

	if (propsdict == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"properties keyword argument is "
				"required.");
		return NULL;
	}

	state = py_get_module_state(res->obj.pylibzfsp);

	nvl = py_zfsprops_to_nvlist(state,
				    propsdict,
				    res->obj.ctype,
				    B_FALSE);
	if (nvl == NULL)
		return NULL;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME
			".ZFSResource.set_properties", "OO",
			res->obj.name, kwargs) < 0) {
		fnvlist_free(nvl);
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(res->obj.pylibzfsp);
	err = zfs_prop_set_list_flags(res->obj.zhp,
				      nvl,
				      remount ? 0 : ZFS_SET_NOMOUNT);
	if (err) {
		py_get_zfs_error(res->obj.pylibzfsp->lzh, &zfs_err);
	}

	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_set_properties() failed");
		fnvlist_free(nvl);
		return NULL;
	}

	/* Update operation succeeded. Write history */
	conv_str = py_dump_nvlist(nvl, B_TRUE);

	Py_BEGIN_ALLOW_THREADS
	fnvlist_free(nvl);
	Py_END_ALLOW_THREADS

	if (conv_str) {
		const char *json = PyUnicode_AsUTF8(conv_str);
		err = py_log_history_fmt(res->obj.pylibzfsp,
					 "zfs update %s with properties: %s",
					 zfs_get_name(res->obj.zhp),
					 json ? json : "UNKNOWN");
	} else {
		err = py_log_history_fmt(res->obj.pylibzfsp,
					 "zfs update %s",
					 zfs_get_name(res->obj.zhp));
	}

	Py_XDECREF(conv_str);

	// We may have encountered an error generating history message
	if (err)
		return NULL;

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_resource_inherit_prop__doc__,
"inherit_property(*, property, received=False) -> None\n\n"
"----------------------------------------------------\n\n"
"Clears the specified property, causing it to be inherited from an ancestor,\n"
"restored to default if no ancestor has the property set, or with \"received\"\n"
"option reverted to the received value if one exists.  See zfsprops(7) for a\n"
"listing of default values, and details on which properties can be inherited.\n"
""
"Parameters\n"
"----------\n"
"property: truenas_pylibzfs.ZFSProperty\n"
"    The ZFS property to inherit.\n"
"\n"
"received, optional, default=False\n"
"    Boolean value indicating whether to do alterate behavior of inheriting\n"
"    from received value rather than ancestor.\n"
""
"Returns\n"
"-------\n"
"None\n"
"\n"
"Raises:\n"
"-------\n"
"ValueError:\n"
"    No property was specified.\n"
"ValueError:\n"
"    One of the specified properties is not supported for the ZFS type of the\n"
"    underlying ZFS resource. For example, setting a zvol property for a\n"
"    ZFS filesystem.\n\n"
"ZFSError:\n"
"    The ZFS operation failed. This can happen for a variety of reasons.\n"
);
static
PyObject *py_zfs_resource_inherit_property(PyObject *self,
					   PyObject *args_unused,
					   PyObject *kwargs)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;
	PyObject *pyprop;
	const char *cprop;
	zfs_prop_t zprop;
	boolean_t received = B_FALSE;
	py_zfs_error_t zfs_err;
	pylibzfs_state_t *state = NULL;
	int err;

	char *kwnames [] = {
		"property",
		"received",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$Op",
					 kwnames,
					 &pyprop,
					 &received)) {
					 return NULL;
	}

	if (pyprop == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"property keyword argument is "
				"required.");
		return NULL;
	}

	state = py_get_module_state(res->obj.pylibzfsp);

	if (!py_object_to_zfs_prop_t(state->zfs_property_enum, pyprop, &zprop))
		return NULL;

	if (zprop == ZPROP_USERPROP) {
		cprop = PyUnicode_AsUTF8(pyprop);
		if (cprop == NULL)
			return NULL;
	} else {
		if (!py_zfs_prop_valid_for_type(zprop, res->obj.ctype))
			return NULL;

		cprop = zfs_prop_to_name(zprop);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME
			".ZFSResource.inherit_property", "OO",
			res->obj.name, kwargs) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(res->obj.pylibzfsp);
	err = zfs_prop_inherit(res->obj.zhp, cprop, received);
	if (err) {
		py_get_zfs_error(res->obj.pylibzfsp->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	err = py_log_history_fmt(res->obj.pylibzfsp,
				 "zfs inherit %s%s %s",
				 received ? "-S" : "",
				 cprop,
				 zfs_get_name(res->obj.zhp));
	if (err)
		return NULL;

	Py_RETURN_NONE;
}

static
PyObject *py_get_userprops(py_zfs_resource_t *res)
{
	PyObject *out = NULL;
	nvlist_t *nvl = NULL;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(res->obj.pylibzfsp);

	if (res->is_simple) {
		zfs_refresh_properties(res->obj.zhp);
		res->is_simple = B_FALSE;
	}

	nvl = zfs_get_user_props(res->obj.zhp);

	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	out = user_props_nvlist_to_py_dict(nvl);
	return out;
}

PyDoc_STRVAR(py_zfs_resource_get_user_properties__doc__,
"get_user_properties() -> dict\n"
"-------------------------------------\n\n"
"Get the user properties of the ZFS resource.\n\n"
""
"Parameters\n"
"----------\n"
"None\n\n"
""
"Returns\n"
"-------\n"
"Dictionary containing user properties as key-value pairs.\n"
);
static
PyObject *py_zfs_resource_get_user_properties(PyObject *self,
					      PyObject *args_unused)
{
	return py_get_userprops((py_zfs_resource_t *)self);
}

PyDoc_STRVAR(py_zfs_resource_set_user_properties__doc__,
"set_user_properties(*, user_properties) -> None\n"
"-----------------------------------------------\n\n"
"Set the user properties of the ZFS resource.\n\n"
""
"Parameters\n"
"user_properties: dict, required\n"
"        Dictionary containing user properties as key-value pairs.\n"
"\n"
"Returns\n"
"-------\n"
"None\n\n"
"NOTE: user properties may be created and updated through this method.\n"
"Remove requires inheriting the property from the parent dataset."
);
static
PyObject *py_zfs_resource_set_user_properties(PyObject *self,
					      PyObject *args_unused,
					      PyObject *kwargs)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;
	py_zfs_error_t zfs_err;
	PyObject *props_dict = NULL;
	PyObject *conv_str = NULL;
	nvlist_t *nvl;
	int err;

	char *kwnames [] = {
		"user_properties",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$O",
					 kwnames,
					 &props_dict)) {
		return NULL;
	}

	if (props_dict == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"user_properties dict is a required "
				"keyword argument.");
		return NULL;
	}

	if (!PyDict_Check(props_dict)) {
		PyErr_SetString(PyExc_TypeError,
				"user_properties must be a dictionary.");
		return NULL;
	}

	nvl = py_userprops_dict_to_nvlist(props_dict);
	if (nvl == NULL)
		return NULL;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSObject.set_user_propeties",
			"OO", res->obj.name, kwargs) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(res->obj.pylibzfsp);
	err = zfs_prop_set_list(res->obj.zhp, nvl);
	if (err)
		py_get_zfs_error(res->obj.pylibzfsp->lzh, &zfs_err);

	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_set_user_properties() failed");
		fnvlist_free(nvl);
		return NULL;
	}

	/* Update operation succeeded. Write history */
	conv_str = py_dump_nvlist(nvl, B_TRUE);

	Py_BEGIN_ALLOW_THREADS
	fnvlist_free(nvl);
	Py_END_ALLOW_THREADS

	if (conv_str) {
		const char *json = PyUnicode_AsUTF8(conv_str);
		err = py_log_history_fmt(res->obj.pylibzfsp,
					 "zfs update %s with user properties: %s",
					 zfs_get_name(res->obj.zhp),
					 json ? json : "UNKNOWN");
	} else {
		err = py_log_history_fmt(res->obj.pylibzfsp,
					 "zfs update %s",
					 zfs_get_name(res->obj.zhp));
	}

	Py_XDECREF(conv_str);

	// We may have encountered an error generating history message
	if (err)
		return NULL;

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_resource_asdict__doc__,
"asdict(*, properties, get_source=False, get_user_properties=False,\n"
"       get_crypto=False) -> dict\n\n"
"------------------------------------------------------------------\n\n"
"Get the specified properties of a given ZFS resource.\n\n"
""
"Parameters\n"
"----------\n"
"properties: set, optional\n"
"    Set of truenas_pylibzfs.ZFSProperty properties to retrieve.\n\n"
"get_source: bool, optional, default=False\n"
"    Non-default option to retrieve the source information for the returned\n"
"    propeties.\n\n"
"get_user_properties: bool, optional, default=False\n"
"    Non-default option to retrieve user properties.\n\n"
"get_crypto: bool, optional, default=False\n"
"    Non-default option to include encryption-related information.\n\n"
""
"Returns\n"
"-------\n"
"python dictionary containing the following keys:\n"
"    name: the name of this ZFS resource\n\n"
"    pool: the name of the pool with which this resource is associated\n\n"
"    type: the ZFS type of this resource\n\n"
"    type_enum: the truenas_pylibzfs.ZFSType enum for this resource\n\n"
"    createtxg: the ZFS transaction group number in which this resource was\n"
"        created\n\n"
"    guid: the GUID for this ZFS resource\n"
"    properties: dictionary containing property information for requested\n"
"        properties. This will be None type if no properties were requested.\n"
"\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    The specified properties is not a python set.\n\n"
"ValueError:\n"
"    One of the specified properties is not supported for the ZFS type of the\n"
"    underlying ZFS resource. For example, requesting a zvol property for a\n"
"    ZFS filesystem.\n\n"
"RuntimeError:\n"
"    An unexpected error occurred while retrieving the value of a ZFS property.\n"
"    This should not happen and API consumers experiencing this issue should\n"
"    file a bug report against this python library.\n\n"
);
static
PyObject *py_zfs_resource_asdict(PyObject *self,
				 PyObject *args_unused,
				 PyObject *kwargs)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;
	PyObject *prop_set = NULL;
	PyObject *props_dict = NULL;
	PyObject *userprops = NULL;
	PyObject *crypto = NULL;
	PyObject *out = NULL;
	boolean_t get_source = B_FALSE;
	boolean_t get_userprops = B_FALSE;
	boolean_t get_crypto = B_FALSE;
	char *kwnames [] = {
		"properties",
		"get_source",
		"get_user_properties",
		"get_crypto",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$Oppp",
					 kwnames,
					 &prop_set,
					 &get_source,
					 &get_userprops,
					 &get_crypto)) {
		return NULL;
	}

	if ((prop_set != NULL) && (prop_set != Py_None)) {
		PyObject *zfsprops = NULL;

		if (!PyAnySet_Check(prop_set)) {
			PyErr_SetString(PyExc_TypeError,
					"properties must be a set.");
			return NULL;
		}

		if (res->is_simple) {
			/*
			 * We have simple handle that lacks property information.
			 * This means we _must_ refresh properties before
			 * generating python object
			 */
			Py_BEGIN_ALLOW_THREADS
			PY_ZFS_LOCK(res->obj.pylibzfsp);
			zfs_refresh_properties(res->obj.zhp);
			PY_ZFS_UNLOCK(res->obj.pylibzfsp);
			res->is_simple = B_FALSE;
			Py_END_ALLOW_THREADS
		}

		zfsprops = py_zfs_get_properties(&res->obj, prop_set, get_source);
		if (zfsprops == NULL)
			return NULL;

		props_dict = py_zfs_props_to_dict(&res->obj, zfsprops);
		Py_CLEAR(zfsprops);
		if (props_dict == NULL)
			return NULL;
	}

	if (get_userprops) {
		userprops = py_get_userprops(res);
		if (userprops == NULL) {
			Py_CLEAR(props_dict);
			return NULL;
		}
	}

	if (get_crypto) {
		crypto = py_zfs_crypto_info_dict(&res->obj);
		if (crypto == NULL) {
			Py_CLEAR(props_dict);
			Py_CLEAR(userprops);
			return NULL;
		}
	}

	out = Py_BuildValue(
		"{s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O}",
		"name", res->obj.name,
		"pool", res->obj.pool_name,
		"type", res->obj.type,
		"type_enum", res->obj.type_enum,
		"createtxg", res->obj.createtxg,
		"guid", res->obj.guid,
		"properties", props_dict ? props_dict : Py_None,
		"user_properties", userprops ? userprops : Py_None,
		"crypto", crypto ? crypto : Py_None
	);

	Py_XDECREF(props_dict);
	Py_XDECREF(userprops);
	Py_XDECREF(crypto);
	return out;
}

PyDoc_STRVAR(py_zfs_resource_mount__doc__,
"mount(*, mountpoint, mount_options=None, force=False, \n"
"      load_encryption_key=False) -> None\n"
"------------------------------------------------------\n\n"
"Mount the specified ZFS dataset with the specified options\n"
"Generally this method should be called without arguments,\n"
"which relies on internal dataset configuration for setting\n"
"appropriate mount options.\n\n"
""
"Parameters\n"
"----------\n"
"mountpoint: str, optional\n"
"    Optional parameter to manually specify the mountpoint at\n"
"    which to mount the datasets. If this is omitted then the\n"
"    mountpoint specied in the ZFS mountpoint property will be used.\n\n"
"    Generally the mountpoint should be not be specified and the\n"
"    library user should rely on the ZFS mountpoint property.\n\n"
"mount_options: list | None, optional, default=None\n"
"    List of mount options to use when mounting the ZFS dataset.\n"
"    These may be any of MNTOPT constants in the truenas_pylibzfs.constants\n"
"    module.\n\n"
"    NOTE: it's generally preferable to set these as ZFS properties rather\n"
"    than overriding via mount options\n\n"
"force: bool, optional, default=False\n"
"    Redacted datasets and ones with the CANMOUNT property set to off\n"
"    will fail to mount without explicitly passing the force option\n\n"
"load_encryption_key: bool, optional, default=False\n"
"    Load keys for encrypted filesystems as they are being mounted. This is \n"
"    equivalent to executing zfs load-key before mounting it.\n\n"
""
"Returns\n"
"-------\n"
"None\n\n"
""
"Raises:\n"
"-------\n"
"TypeError:\n"
"- The underlying ZFS type is not mountable.\n\n"
"ValueError:\n"
"- The dataset has mountpoint set to legacy or none and an explicit\n"
"  mountpoint was not passed to this method as an argument.\n"
"- A mountpoint was specified that was not an absolute path.\n"
"- / (root) was set as the mountpoint.\n"
"- The mountpoint property was explicitly set to None.\n"
"ZFSError:\n"
"- The ZFS mount operation failed.\n\n"
);
static
PyObject *py_zfs_resource_mount(PyObject *self,
				PyObject *args_unused,
				PyObject *kwargs)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;
	char *kwnames [] = {
		"mountpoint",
		"mount_options",
		"force",
		"load_encryption_key",
		NULL
	};
	PyObject *py_mp = NULL;
	PyObject *py_mntopts = NULL;
	/*
	 * In future we can add support for more MS_* flags
	 * as optional boolean arguments.
	 */
	boolean_t force = B_FALSE;
	boolean_t load = B_FALSE;
	int flags = 0;

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$OOpp",
					 kwnames,
					 &py_mp,
					 &py_mntopts,
					 &force,
					 &load)) {
		return NULL;
	}

	if (force)
		flags |= MS_FORCE;

	if (load)
		flags |= MS_CRYPT;

	return py_zfs_mount(res, py_mp, py_mntopts, flags);
}

PyDoc_STRVAR(py_zfs_resource_unmount__doc__,
"unmount(*, mountpoint, force=False, lazy=False, "
"        unload_encryption_key=False, follow_symlinks=False) -> None\n"
"-------------------------------------------------------------------\n\n"
"Unmount the specified dataset with the specified flags.\n"
"\n"
"NOTE: unmounting an encrypted dataset will automatically unload its\n"
"encryption key.\n"
""
"Parameters\n"
"----------\n"
"mountpoint: str, optional\n"
"    Optional parameter to manually specify the mountpoint at\n"
"    which the dataset is mounted. This may be required for datasets with\n"
"    legacy mountpoints and is benefical if the mountpoint is known apriori.\n\n"
"force: bool, optional, default=False\n"
"    Forcefully unmount the file system, even if it is currently in use.\n\n"
"lazy: bool, optional, default=False\n"
"    Perform a lazy unmount: make the mount unavailable for new accesses, \n"
"    immediately disconnect the filesystem and all filesystems mounted below \n"
"    it from each other and from the mount table, and actually perform the \n"
"    unmount when the mount ceases to be busy.\n\n"
"unload_encryption_key: bool, optional, default=False\n"
"    Unload keys for any encryption roots involved in this operation.\n"
"    The unload will occur regardless of whether any work was done to actually\n"
"    unmount the resource. The reason for this is to ensure that encryption\n"
"    keys are consistently unloaded.\n"
"follow_symlinks: bool, optional, default=False\n"
"    Don't dereference mountpoint if it is a symbolic link.\n"
"recursive: bool, optional, default=False\n"
"    Unmount any children inheriting the mountpoint property.\n\n"
""
"Returns\n"
"-------\n"
"None\n\n"
""
"Raises:\n"
"-------\n"
"ZFSError:\n"
"- The ZFS unmount operation failed.\n\n"
);
static
PyObject *py_zfs_resource_unmount(PyObject *self,
				  PyObject *args_unused,
				  PyObject *kwargs)
{
	py_zfs_resource_t *res = (py_zfs_resource_t *)self;
	char *kwnames [] = {
		"mountpoint",
		"force",
		"lazy",
		"unload_encryption_key",
		"follow_symlinks",
		"recursive",
		NULL
	};
	const char *mp = NULL;
	int err;
	int flags = 0;
	py_zfs_error_t zfs_err;
	boolean_t force = B_FALSE;
	boolean_t unload = B_FALSE;
	boolean_t lazy = B_FALSE;
	boolean_t follow = B_FALSE;
	boolean_t recurse = B_FALSE;

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$sppppp",
					 kwnames,
					 &mp,
					 &force,
					 &lazy,
					 &unload,
					 &follow,
					 &recurse)) {
		return NULL;
	}

	if (force)
		flags |= MS_FORCE;

	if (lazy)
		flags |= MS_DETACH;

	if (unload)
		flags |= MS_CRYPT;

	if (!follow)
		flags |= UMOUNT_NOFOLLOW;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(res->obj.pylibzfsp);
	if (recurse) {
		err = zfs_unmountall(res->obj.zhp, flags);
	} else {
		err = zfs_unmount(res->obj.zhp, mp, flags);
	}
	if (err) {
		py_get_zfs_error(res->obj.pylibzfsp->lzh, &zfs_err);
	} else {
		// the unmount may have altered encryption settings
		zfs_refresh_properties(res->obj.zhp);
	}

	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_umount() failed");
		return NULL;
	}

	Py_RETURN_NONE;
}

static
PyMethodDef zfs_resource_methods[] = {
	{
		.ml_name = "iter_filesystems",
		.ml_meth = (PyCFunction)py_zfs_resource_iter_filesystems,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_iter_filesystems__doc__
	},
	{
		.ml_name = "iter_snapshots",
		.ml_meth = (PyCFunction)py_zfs_resource_iter_snapshots,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_iter_snapshots__doc__
	},
	{
		.ml_name = "get_properties",
		.ml_meth = (PyCFunction)py_zfs_resource_get_properties,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_get_properties__doc__
	},
	{
		.ml_name = "set_properties",
		.ml_meth = (PyCFunction)py_zfs_resource_set_properties,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_set_properties__doc__
	},
	{
		.ml_name = "get_user_properties",
		.ml_meth = (PyCFunction)py_zfs_resource_get_user_properties,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_resource_get_user_properties__doc__
	},
	{
		.ml_name = "set_user_properties",
		.ml_meth = (PyCFunction)py_zfs_resource_set_user_properties,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_set_user_properties__doc__
	},
	{
		.ml_name = "refresh_properties",
		.ml_meth = (PyCFunction)py_zfs_resource_refresh_props,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_resource_refresh_props__doc__
	},
	{
		.ml_name = "inherit_property",
		.ml_meth = (PyCFunction)py_zfs_resource_inherit_property,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_inherit_prop__doc__
	},
	{
		.ml_name = "asdict",
		.ml_meth = (PyCFunction)py_zfs_resource_asdict,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_asdict__doc__
	},
	{
		.ml_name = "mount",
		.ml_meth = (PyCFunction)py_zfs_resource_mount,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_mount__doc__
	},
	{
		.ml_name = "unmount",
		.ml_meth = (PyCFunction)py_zfs_resource_unmount,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_unmount__doc__
	},
	{
		.ml_name = "get_mountpoint",
		.ml_meth = (PyCFunction)py_zfs_resource_get_mount,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_resource_get_mount__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSResource = {
	.tp_name = "ZFSResource",
	.tp_basicsize = sizeof (py_zfs_resource_t),
	.tp_methods = zfs_resource_methods,
	.tp_new = py_zfs_resource_new,
	.tp_init = py_zfs_resource_init,
	.tp_doc = "ZFSResource",
	.tp_dealloc = (destructor)py_zfs_resource_dealloc,
	.tp_repr = py_repr_zfs_resource,
	.tp_iter = (getiterfunc)py_zfs_resoucre_iter,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_base = &ZFSObject
};
