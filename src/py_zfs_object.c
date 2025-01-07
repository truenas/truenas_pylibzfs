#include "pylibzfs2.h"

#define ZFS_OBJECT_STR "<libzfs2.ZFSObject(name=%U, pool=%U, type=%U)>"

static
PyObject *py_zfs_obj_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_obj_t *self = NULL;
	self = (py_zfs_obj_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_obj_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_obj_dealloc(py_zfs_obj_t *self) {
	if (self->zhp != NULL) {
		Py_BEGIN_ALLOW_THREADS
		zfs_close(self->zhp);
		Py_END_ALLOW_THREADS
		self->zhp = NULL;
	}
	Py_CLEAR(self->name);
	Py_CLEAR(self->pool_name);
	Py_CLEAR(self->type);
	Py_CLEAR(self->pylibzfsp);
	Py_CLEAR(self->guid);
	Py_CLEAR(self->createtxg);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_repr_zfs_object(PyObject *self)
{
	py_zfs_obj_t *obj = (py_zfs_obj_t *)self;

	return py_repr_zfs_obj_impl(obj, ZFS_OBJECT_STR);
}

static
PyObject *py_zfs_obj_rename(PyObject *self,
			    PyObject *args_unused,
			    PyObject *kwargs)
{
	py_zfs_obj_t *obj = (py_zfs_obj_t *)self;
	int err, recursive, nounmount, forceunmount;
	char *new_name = NULL;
	py_zfs_error_t zfs_err;
	renameflags_t flags;

	char *kwnames [] = {
		"new_name",
		"recursive",
		"no_unmount",
		"force_unmount",
		NULL
	};

	recursive = nounmount = forceunmount = 0;

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "s|$ppp",
					 kwnames,
					 &new_name,
					 &recursive,
					 &nounmount,
					 &forceunmount)) {
		return NULL;
	}

	flags = (renameflags_t) {
		.recursive = recursive,
		.nounmount = nounmount,
		.forceunmount = forceunmount
	};

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);
	err = zfs_rename(obj->zhp, new_name, flags);
	if (err) {
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_rename() failed");
		return NULL;
	}

	// swap out our name with new one
	Py_CLEAR(obj->name);
	obj->name = PyUnicode_FromString(new_name);

	Py_RETURN_NONE;
}

static
PyObject *py_get_prop(PyObject *obj)
{
	if (obj == NULL) {
		Py_RETURN_NONE;
	}

	return Py_NewRef(obj);
}

static
PyObject *py_zfs_obj_get_name(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->name);
}

static
PyObject *py_zfs_obj_get_type(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->type);
}

static
PyObject *py_zfs_obj_get_guid(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->guid);
}

static
PyObject *py_zfs_obj_get_createtxg(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->createtxg);
}

static
PyObject *py_zfs_obj_get_pool(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->pool_name);
}

static
PyGetSetDef zfs_obj_getsetters[] = {
	{
		.name	= "name",
		.get	= (getter)py_zfs_obj_get_name
	},
	{
		.name	= "type",
		.get	= (getter)py_zfs_obj_get_type
	},
	{
		.name	= "guid",
		.get	= (getter)py_zfs_obj_get_guid
	},
	{
		.name	= "createtxg",
		.get	= (getter)py_zfs_obj_get_createtxg
	},
	{
		.name	= "pool_name",
		.get	= (getter)py_zfs_obj_get_pool
	},
	{ .name = NULL }
};

static
PyMethodDef zfs_obj_methods[] = {
	{
		.ml_name = "rename",
		.ml_meth = (PyCFunction)py_zfs_obj_rename,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSObject = {
	.tp_name = "ZFSObject",
	.tp_basicsize = sizeof (py_zfs_obj_t),
	.tp_methods = zfs_obj_methods,
	.tp_getset = zfs_obj_getsetters,
	.tp_new = py_zfs_obj_new,
	.tp_init = py_zfs_obj_init,
	.tp_doc = "ZFSObject",
	.tp_dealloc = (destructor)py_zfs_obj_dealloc,
	.tp_repr = py_repr_zfs_object,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};
