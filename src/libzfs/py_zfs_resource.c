#include "../truenas_pylibzfs.h"
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

static
PyObject *py_zfs_resource_userspace(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
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
"asdict(*, properties, get_source=False) ->"
"truenas_pylibzfs.struct_zfs_property\n\n"
"-------------------------------------\n\n"
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
		Py_BEGIN_ALLOW_THREADS
		PY_ZFS_LOCK(res->obj.pylibzfsp);
		zfs_refresh_properties(res->obj.zhp);
		PY_ZFS_UNLOCK(res->obj.pylibzfsp);
		res->is_simple = B_FALSE;
		Py_END_ALLOW_THREADS
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
	fnvlist_free(nvl);

	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_set_properties() failed");
	} else {
		const char *props = NULL;
		PyObject *dictstr = PyObject_Str(propsdict);
		if (dictstr != NULL) {
			props = PyUnicode_AsUTF8(dictstr);
		}

                err = py_log_history_fmt(res->obj.pylibzfsp,
                                         "zfs set properties %s: %s",
                                         zfs_get_name(res->obj.zhp),
                                         props ? props : "UNKNOWN");
		Py_XDECREF(dictstr);
	}

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
	fnvlist_free(nvl);
	if (err)
		py_get_zfs_error(res->obj.pylibzfsp->lzh, &zfs_err);

	PY_ZFS_UNLOCK(res->obj.pylibzfsp);
	Py_END_ALLOW_THREADS


	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_set_user_properties() failed");
		return NULL;
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_resource_asdict__doc__,
"asdict(*, properties, get_source=False) -> dict\n\n"
"-----------------------------------------------\n\n"
"Get the specified properties of a given ZFS resource.\n\n"
""
"Parameters\n"
"----------\n"
"properties: set, optional\n"
"    Set of truenas_pylibzfs.ZFSProperty properties to retrieve.\n\n"
"get_source: bool, optional, default=False\n"
"    Non-default option to retrieve the source information for the returned\n"
"    propeties.\n\n"
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
	PyObject *out = NULL;
	boolean_t get_source = B_FALSE;
	boolean_t get_userprops = B_FALSE;
	char *kwnames [] = {
		"properties",
		"get_source",
		"get_user_properties",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$Opp",
					 kwnames,
					 &prop_set,
					 &get_source,
					 &get_userprops)) {
		return NULL;
	}

	if (prop_set != NULL) {
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

	out = Py_BuildValue(
		"{s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O}",
		"name", res->obj.name,
		"pool", res->obj.pool_name,
		"type", res->obj.type,
		"type_enum", res->obj.type_enum,
		"createtxg", res->obj.createtxg,
		"guid", res->obj.guid,
		"properties", props_dict ? props_dict : Py_None,
		"user_properties", userprops ? userprops : Py_None
	);

	Py_XDECREF(props_dict);
	return out;
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
		.ml_name = "asdict",
		.ml_meth = (PyCFunction)py_zfs_resource_asdict,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_asdict__doc__
	},
	{
		.ml_name = "userspace",
		.ml_meth = py_zfs_resource_userspace,
		.ml_flags = METH_VARARGS
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
