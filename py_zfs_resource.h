#ifndef _PY_ZFS_RESOURCE_H
#define _PY_ZFS_RESOURCE_H

#include "zfs.h"
#include "py_zfs_object.h"

#define	ZFS_RESOURCE_STR	"<libzfs2.ZFSResource name %s type %s>"

typedef struct {
	py_zfs_obj_t obj;
} py_zfs_resource_t;

static PyObject *py_zfs_resoucre_iter(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_resource_str(PyObject *self) {
	py_zfs_resource_t *rsrc = (py_zfs_resource_t *)self;
	if (rsrc->obj.zhp) {
		return (PyUnicode_FromFormat(ZFS_RESOURCE_STR,
		    zfs_get_name(rsrc->obj.zhp),
		    get_dataset_type(zfs_get_type(rsrc->obj.zhp))));
	} else {
		return (PyUnicode_FromFormat(ZFS_RESOURCE_STR, "<EMPTY>",
		    "<EMPTY>"));
	}
}

PyObject *py_zfs_resource_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_resource_t *self = NULL;
	self = (py_zfs_resource_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

int py_zfs_resource_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

void py_zfs_resource_dealloc(py_zfs_resource_t *self) {
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *py_zfs_resource_get_dependents(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_resource_update_properties(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_resource_userspace(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

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
	.tp_str = py_zfs_resource_str,
	.tp_repr = py_zfs_resource_str,
	.tp_iter = (getiterfunc)py_zfs_resoucre_iter,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_base = &ZFSObject
};

#endif /* _PY_ZFS_RESOURCE_H */
