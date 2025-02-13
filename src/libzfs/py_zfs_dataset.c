#include "../truenas_pylibzfs.h"
#include "py_zfs_iter.h"

#define ZFS_DATASET_STR "<" PYLIBZFS_MODULE_NAME \
    ".ZFSDataset(name=%U, pool=%U, type=%U)>"

static
PyObject *py_zfs_dataset_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_dataset_t *self = NULL;
	self = (py_zfs_dataset_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_dataset_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_dataset_dealloc(py_zfs_dataset_t *self) {
	free_py_zfs_obj(RSRC_TO_ZFS(self));
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_repr_zfs_dataset(PyObject *self)
{
	py_zfs_dataset_t *ds = (py_zfs_dataset_t *)self;

	return py_repr_zfs_obj_impl(RSRC_TO_ZFS(ds), ZFS_DATASET_STR);
}

PyDoc_STRVAR(py_zfs_dataset_iter_userspace__doc__,
"iter_userspace(*, callback, state, quotatype) -> bool\n\n"
"-----------------------------------------------------\n\n"
"Iterate all zfs userquotas on dataset. Arguments are keyword-only\n\n"
"Parameters\n"
"----------\n"
"callback: callable\n"
"    Callback function that will be called for every quota entry.\n\n"
"state: object, optional\n"
"    Optional python object (for example dictionary) passed as an argument\n"
"    to the callback function for each quota.\n\n"
"quota_type: truenas_pylibzfs.ZFSUserQuota\n"
"    Quota type to retrieve. For example: USER, GROUP, PROJECT, etc\n\n"
"Returns\n"
"-------\n"
"bool\n"
"    Value indicates that iteration completed without being stopped by the\n"
"    callback fuction returning False.\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    An error occurred during iteration of the quotas. Note that this\n"
"    exception type may also be raised within the callback function.\n\n"
"NOTE regarding \"callback\":\n"
"--------------------------\n"
"Minimally the function signature must take a single argument for each ZFS\n"
"quota. If the \"state\" keyword is specified then the callback function\n"
"should take two arguments. The callback function must return bool value\n"
"indicating whether iteration should continue.\n\n"
"Example \"callback\":\n"
"-------------------\n"
"def my_callback(quota, state):\n"
"    print(f'{quota.xid}: {quota.value}')\n"
"    return True\n"
);
static
PyObject *py_zfs_dataset_iter_userspace(PyObject *self,
					PyObject *args_unused,
					PyObject *kwargs)
{
	int err;
	py_zfs_obj_t *obj = RSRC_TO_ZFS(((py_zfs_dataset_t *)self));
	pylibzfs_state_t *state = py_get_module_state(obj->pylibzfsp);
	long qtype;
	PyObject *pyqtype = NULL;

	py_iter_state_t iter_state = (py_iter_state_t){
		.pylibzfsp = obj->pylibzfsp,
		.target = obj->zhp
	};

	char *kwnames [] = {
		"callback",
		"state",
		"quota_type",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$OOO",
					 kwnames,
					 &iter_state.callback_fn,
					 &iter_state.private_data,
					 &pyqtype)) {
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

	if (pyqtype == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"`quota_type` keyword argument is required.");
		return NULL;
	}

	if (!PyObject_IsInstance(pyqtype, state->zfs_uquota_enum)) {
		PyErr_SetString(PyExc_TypeError,
				"Not a valid ZFSUserQuota");
		return NULL;
	}

	qtype = PyLong_AsLong(pyqtype);
	PYZFS_ASSERT(
		((qtype >= 0) && (qtype < ZFS_NUM_USERQUOTA_PROPS)),
		"Invalid quota type"
	);

	iter_state.iter_config.userspace = (iter_conf_userspace_t) {
		.pyqtype = pyqtype,
		.qtype = qtype,
		.pyuserquota_struct = state->struct_zfs_userquota_type,
	};

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSDataset.iter_userspace",
                        "OO", obj->name, pyqtype) < 0) {
		return NULL;
	}

	err = py_iter_userspace(&iter_state);
	if ((err == ITER_RESULT_ERROR) || (err == ITER_RESULT_IOCTL_ERROR)) {
		// Exception is set by callback function
		return NULL;
	}

	if (err == ITER_RESULT_SUCCESS) {
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyDoc_STRVAR(py_zfs_dataset_set_userquotas__doc__,
"set_userquotas(*, quotas) -> None\n"
"---------------------------------\n"
"Set one or more struct_zfs_user_quota quotas on dataset.\n\n"
"Parameters\n"
"----------\n"
"quotas: list[dict]\n"
"    Quotas to set. This should be some iterable containing dictionaries\n"
"    with the following keys:\n"
"    - quota_type: truenas_pylibzfs.ZFSUserQuota instance\n"
"    - xid: numeric id of user, group, or project to which quota applies\n"
"    - value: numeric value of quota to set. NOTE: None and zero (0)\n"
"        have a special meaning denoting to remove the quota.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"truenas_pylibzfs.ZFSError:\n"
"- An error while setting the quota.\n\n"
"ValueError:\n"
"- Invalid quota value was specified.\n"
"- One of quota used counters was specified as quota_type.\n"
"- Required keyword argument was omitted.\n\n"
"TypeError:\n"
"- quota_type is not a valid truenas_pylibzfs.ZFSUserQuota.\n"
);
static
PyObject *py_zfs_dataset_set_userquotas(PyObject *self,
				        PyObject *args_unused,
				        PyObject *kwargs)
{
	py_zfs_obj_t *obj = RSRC_TO_ZFS(((py_zfs_dataset_t *)self));
	pylibzfs_state_t *state = py_get_module_state(obj->pylibzfsp);
	int err;
	py_zfs_error_t zfs_err;
	nvlist_t *nvl = NULL;
	PyObject *pyquotas = NULL;

	char *kwnames [] = {"quotas", NULL};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$O",
					 kwnames,
					 &pyquotas)) {
		return NULL;
	}

	if (pyquotas == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"`quotas` keyword argument is required.");
		return NULL;
	}

	/* constuct an nvlist of the quotas to be set */
	nvl = py_userquotas_to_nvlist(state, pyquotas);
	if (nvl == NULL)
		return NULL;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSDataset.set_userquotas",
                        "OO", obj->name, pyquotas) < 0) {
		fnvlist_free(nvl);
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);
	err = zfs_prop_set_list_flags(obj->zhp, nvl, ZFS_SET_NOMOUNT);
	if (err) {
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "set_userquotas() failed");
		fnvlist_free(nvl);
		return NULL;
	}

	err = py_log_history_fmt(obj->pylibzfsp, "set %zu ZFS userquotas on %s",
				 fnvlist_num_pairs(nvl), zfs_get_name(obj->zhp));
	fnvlist_free(nvl);
	if (err) {
		return NULL;
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_dataset_get_enc__doc__,
"get_encryption() -> truenas_pylibzfs.ZFSEncrypt | None\n"
"------------------------------------------------------\n"
"Get python object to control encryption settings of dataset.\n"
"Returns None if dataset is not encrypted.\n\n"
"Parameters\n"
"----------\n"
"    None\n\n"
""
"Returns\n"
"-------\n"
"    truenas_pylibzfs.ZFSEncrypt object if encrypted else None\n\n"
""
"Raises\n"
"------\n"
"    None"
);
static
PyObject *py_zfs_dataset_get_enc(PyObject *self, PyObject *args_unused)
{
	py_zfs_obj_t *obj = RSRC_TO_ZFS(((py_zfs_dataset_t *)self));
	uint64_t keyformat = ZFS_KEYFORMAT_NONE;

        // PY_ZFS_LOCK needs held due to interaction with libzfs mnttab
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);
	keyformat = zfs_prop_get_int(obj->zhp, ZFS_PROP_KEYFORMAT);
	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (keyformat == ZFS_KEYFORMAT_NONE)
		Py_RETURN_NONE;

	return init_zfs_enc(obj->ctype, self);
}

static
PyGetSetDef zfs_dataset_getsetters[] = {
	{ .name = NULL }
};

static
PyMethodDef zfs_dataset_methods[] = {
	{
		.ml_name = "iter_userspace",
		.ml_meth = (PyCFunction)py_zfs_dataset_iter_userspace,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_dataset_iter_userspace__doc__
	},
	{
		.ml_name = "set_userquotas",
		.ml_meth = (PyCFunction)py_zfs_dataset_set_userquotas,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_dataset_set_userquotas__doc__
	},
	{
		.ml_name = "get_encryption",
		.ml_meth = py_zfs_dataset_get_enc,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_dataset_get_enc__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSDataset = {
	.tp_name = PYLIBZFS_MODULE_NAME ".ZFSDataset",
	.tp_basicsize = sizeof (py_zfs_dataset_t),
	.tp_methods = zfs_dataset_methods,
	.tp_getset = zfs_dataset_getsetters,
	.tp_new = py_zfs_dataset_new,
	.tp_init = py_zfs_dataset_init,
	.tp_doc = "ZFSDataset",
	.tp_dealloc = (destructor)py_zfs_dataset_dealloc,
	.tp_repr = py_repr_zfs_dataset,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_base = &ZFSResource
};

py_zfs_dataset_t *init_zfs_dataset(py_zfs_t *lzp, zfs_handle_t *zfsp, boolean_t simple)
{
	py_zfs_dataset_t *out = NULL;
	py_zfs_obj_t *obj = NULL;
	const char *ds_name;
	const char *pool_name;
	zfs_type_t zfs_type;
	uint64_t guid, createtxg;

	out = (py_zfs_dataset_t *)PyObject_CallFunction((PyObject *)&ZFSDataset, NULL);
	if (out == NULL) {
		return NULL;
	}
	out->rsrc.is_simple = simple;
	obj = RSRC_TO_ZFS(out);
	obj->pylibzfsp = lzp;
	Py_INCREF(lzp);

	Py_BEGIN_ALLOW_THREADS
	ds_name = zfs_get_name(zfsp);
	zfs_type = zfs_get_type(zfsp);
	pool_name = zfs_get_pool_name(zfsp);
	guid = zfs_prop_get_int(zfsp, ZFS_PROP_GUID);
	createtxg = zfs_prop_get_int(zfsp, ZFS_PROP_CREATETXG);
	Py_END_ALLOW_THREADS

	obj->name = PyUnicode_FromString(ds_name);
	if (obj->name == NULL)
		goto error;

	obj->pool_name = PyUnicode_FromString(pool_name);
	if (obj->pool_name == NULL)
		goto error;

	obj->ctype = zfs_type;
	obj->type_enum = py_get_zfs_type(lzp, zfs_type, &obj->type);
	obj->guid = Py_BuildValue("k", guid);
	if (obj->guid == NULL)
		goto error;

	obj->createtxg = Py_BuildValue("k", createtxg);
	if (obj->createtxg == NULL)
		goto error;

	obj->zhp = zfsp;
	return out;

error:
	// This deallocates the new object and decrements refcnt on pylibzfsp
	Py_DECREF(out);
	return NULL;
}
