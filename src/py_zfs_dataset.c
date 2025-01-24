#include "truenas_pylibzfs.h"
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
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_zfs_dataset_as_dict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_repr_zfs_dataset(PyObject *self)
{
	py_zfs_dataset_t *ds = (py_zfs_dataset_t *)self;

	return py_repr_zfs_obj_impl(RSRC_TO_ZFS(ds), ZFS_DATASET_STR);
}

PyDoc_STRVAR(py_zfs_dataset_iter_filesystems__doc__,
"iter_filesystems(*, callback, state, fast=False) -> bool\n\n"
"--------------------------------------------------------\n\n"
"List all child filesystems of this ZFSDataset. Arguments are keyword-only\n\n"
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
PyObject *py_zfs_dataset_iter_filesystems(PyObject *self,
					  PyObject *args_unused,
					  PyObject *kwargs)
{
	int err;
	py_zfs_dataset_t *ds = (py_zfs_dataset_t *)self;
	py_zfs_obj_t *obj = RSRC_TO_ZFS(ds);

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

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSDataset.iter_filesystems",
			"OO", RSRC_TO_ZFS(ds)->name, kwargs) < 0) {
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

static
PyGetSetDef zfs_dataset_getsetters[] = {
	{ .name = NULL }
};

static
PyMethodDef zfs_dataset_methods[] = {
	{
		.ml_name = "asdict",
		.ml_meth = py_zfs_dataset_as_dict,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "iter_filesystems",
		.ml_meth = (PyCFunction)py_zfs_dataset_iter_filesystems,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_dataset_iter_filesystems__doc__
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

py_zfs_dataset_t *init_zfs_dataset(py_zfs_t *lzp, zfs_handle_t *zfsp)
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
