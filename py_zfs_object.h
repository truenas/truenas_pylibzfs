#ifndef _PY_ZFS_OBJECT_H
#define _PY_ZFS_OBJECT_H

#include "zfs.h"

#define	ZFS_OBJ_STR	"<libzfs2.ZFSObject name %s type %s>"

typedef struct {
	PyObject_HEAD
	zfs_handle_t *zhp;
	PyObject *name;
	PyObject *type;
	PyObject *properties;
	PyObject *root;
	PyObject *pool;
} py_zfs_obj_t;

PyObject *py_zfs_obj_str(PyObject *self) {
	py_zfs_obj_t *obj = (py_zfs_obj_t *)self;
	if (obj->zhp) {
		return (PyUnicode_FromFormat(ZFS_OBJ_STR,
		    zfs_get_name(obj->zhp),
		    get_dataset_type(zfs_get_type(obj->zhp))));
	} else {
		return (PyUnicode_FromFormat(ZFS_OBJ_STR, "<EMPTY>",
		    "<EMPTY>"));
	}
}

PyObject *py_zfs_obj_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_obj_t *self = NULL;
	self = (py_zfs_obj_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

int py_zfs_obj_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

void py_zfs_obj_dealloc(py_zfs_obj_t *self) {
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *py_zfs_obj_rename(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_obj_delete(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_obj_get_send_space(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_obj_get_name(py_zfs_obj_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_obj_get_type(py_zfs_obj_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_obj_get_props(py_zfs_obj_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_obj_get_root(py_zfs_obj_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_obj_get_pool(py_zfs_obj_t *self, void *extra) {
	Py_RETURN_NONE;
}

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
		.name	= "properties",
		.get	= (getter)py_zfs_obj_get_props
	},
	{
		.name	= "root",
		.get	= (getter)py_zfs_obj_get_root
	},
	{
		.name	= "pool",
		.get	= (getter)py_zfs_obj_get_pool
	},
	{ .name = NULL }
};

PyMethodDef zfs_obj_methods[] = {
	{
		.ml_name = "rename",
		.ml_meth = py_zfs_obj_rename,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "delete",
		.ml_meth = py_zfs_obj_delete,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "get_send_space",
		.ml_meth = py_zfs_obj_get_send_space,
		.ml_flags = METH_VARARGS
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
	.tp_str = py_zfs_obj_str,
	.tp_repr = py_zfs_obj_str,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};

#endif /* _PY_ZFS_OBJECT_H */
