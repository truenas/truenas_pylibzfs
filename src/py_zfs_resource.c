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

static
PyObject *py_zfs_resource_get_dependents(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_resource_update_properties(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
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

static
PyMethodDef zfs_resource_methods[] = {
	{
		.ml_name = "get_dependents",
		.ml_meth = py_zfs_resource_get_dependents,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "iter_filesystems",
		.ml_meth = (PyCFunction)py_zfs_resource_iter_filesystems,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_iter_filesystems__doc__
	},
	{
		.ml_name = "update_properties",
		.ml_meth = py_zfs_resource_update_properties,
		.ml_flags = METH_VARARGS
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
