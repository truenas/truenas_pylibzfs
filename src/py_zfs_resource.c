#include "pylibzfs2.h"

#define	ZFS_RESOURCE_STR	"<libzfs2.ZFSResource(name=%U, pool=%U, type=%U)>"


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

static
PyMethodDef zfs_resource_methods[] = {
	{
		.ml_name = "get_dependents",
		.ml_meth = py_zfs_resource_get_dependents,
		.ml_flags = METH_VARARGS
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
