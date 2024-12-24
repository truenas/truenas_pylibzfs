#ifndef _PY_ZFS_PROP_H
#define _PY_ZFS_PROP_H

#include "zfs.h"

#define	ZFS_PROP_STR	"<libzfs2.ZFSProperty name %s value %s>"

typedef struct {
	PyObject_HEAD
	int propid;
	const char *cname;
	char cvalue[ZFS_MAXPROPLEN + 1];
	char crawvalue[ZFS_MAXPROPLEN + 1];
	char csrcstr[ZFS_MAXPROPLEN + 1];
	zprop_source_t csource;
} py_zfs_prop_t;

typedef struct {
	py_zfs_prop_t super;
	PyObject *values;
	const char *name;
} py_zfs_user_prop_t;

PyObject *py_zfs_prop_str(PyObject *self) {
	py_zfs_prop_t *prop = (py_zfs_prop_t *)self;
	if (prop->cname == NULL) {
		return (PyUnicode_FromFormat(ZFS_PROP_STR, "<EMPTY>",
		    "<EMPTY>"));
	}
	return (PyUnicode_FromFormat(ZFS_PROP_STR, prop->cname, prop->cvalue));
}

PyObject *py_zfs_prop_get_propid(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_set_propid(py_zfs_prop_t *self, PyObject *args,
    void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_get_name(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_set_name(py_zfs_prop_t *self, PyObject *args,
    void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_get_rawvalue(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_get_source(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_set(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_get(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_asdict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_refresh(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_prop_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_prop_t *self = NULL;
	self = (py_zfs_prop_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

int py_zfs_prop_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

void py_zfs_prop_dealloc(py_zfs_prop_t *self) {
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyGetSetDef zfs_prop_getsetters[] = {
	{
		.name	= "propid",
		.get	= (getter)py_zfs_prop_get_propid,
		.set	= (setter)py_zfs_prop_set_propid
	},
	{
		.name	= "name",
		.get	= (getter)py_zfs_prop_get_name
	},
	{
		.name	= "value",
		.get	= (getter)py_zfs_prop_get_name,
		.set	= (setter)py_zfs_prop_set_name,
	},
	{
		.name	= "raw_value",
		.get	= (getter)py_zfs_prop_get_rawvalue
	},
	{
		.name	= "source",
		.get	= (getter)py_zfs_prop_get_source
	},
	{ .name = NULL }
};

PyMethodDef zfs_prop_methods[] = {
	{
		.ml_name = "set",
		.ml_meth = py_zfs_prop_set,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "get",
		.ml_meth = py_zfs_prop_get,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "asdict",
		.ml_meth = py_zfs_prop_asdict,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "refresh",
		.ml_meth = py_zfs_prop_refresh,
		.ml_flags = METH_VARARGS
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSProperty = {
	.tp_name = "ZFSProperty",
	.tp_basicsize = sizeof (py_zfs_prop_t),
	.tp_methods = zfs_prop_methods,
	.tp_getset = zfs_prop_getsetters,
	.tp_new = py_zfs_prop_new, //PyType_GenericNew,
	.tp_init = py_zfs_prop_init,
	.tp_doc = "ZFSProperty",
	.tp_dealloc = (destructor)py_zfs_prop_dealloc,
	.tp_str = py_zfs_prop_str,
	.tp_repr = py_zfs_prop_str,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};

#endif /* _PY_ZFS_PROP_H */
