#ifndef _PY_ZFS_H
#define _PY_ZFS_H

#include "zfs.h"

#define	ZFS_STR	"<libzfs2.ZFS>"

typedef struct {
	PyObject_HEAD
	libzfs_handle_t *lzh;
	boolean_t mnttab_cache_enable;
	int history;
	const char *history_prefix;
	PyObject *proptypes;
} py_zfs_t;

PyObject *py_zfs_str(PyObject *self) {
	return (PyUnicode_FromFormat(ZFS_STR));
}

PyObject *py_zfs_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_t *self = NULL;
	self = (py_zfs_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

int py_zfs_init(PyObject *type, PyObject *args, PyObject *kwds) {
	py_zfs_t *zfs = (py_zfs_t *)type;
	PyObject *history = Py_True;
	PyObject *history_prefix = PyUnicode_FromString("py-libzfs:");
	PyObject *mnttab_cache = Py_True;
	char *kwlist[] = {"history", "history_prefix", "mnttab_cache", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOO", kwlist, &history,
	    &history_prefix, &mnttab_cache)) {
		return (-1);
	}
	if (!PyBool_Check(history)) {
		PyErr_SetString(PyExc_TypeError, "history is a boolean parameter");
		return (-1);
	}
	if (!PyUnicode_Check(history_prefix)) {
		PyErr_SetString(PyExc_TypeError, "history_prefix is a string parameter");
		return (-1);
	}	
	if (!PyBool_Check(mnttab_cache)) {
		PyErr_SetString(PyExc_TypeError, "mnttab_cache is a boolean parameter");
		return (-1);
	}

	zfs->history = PyObject_IsTrue(history);
	zfs->history_prefix = PyUnicode_AsUTF8(history_prefix);
	zfs->mnttab_cache_enable = PyObject_IsTrue(mnttab_cache);
	zfs->lzh = libzfs_init();
	return (0);
}

void py_zfs_dealloc(py_zfs_t *self) {
	if (self->lzh != NULL)
		libzfs_fini(self->lzh);
	self->lzh = NULL;
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *py_zfs_get_proptypes(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_asdict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyGetSetDef zfs_getsetters[] = {
	{
		.name	= "proptypes",
		.get	= (getter)py_zfs_get_proptypes
	},
	{ .name = NULL }
};

PyMethodDef zfs_methods[] = {
	{
		.ml_name = "asdict",
		.ml_meth = py_zfs_asdict,
		.ml_flags = METH_VARARGS
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFS = {
	.tp_name = "ZFS",
	.tp_basicsize = sizeof (py_zfs_t),
	.tp_methods = zfs_methods,
	.tp_getset = zfs_getsetters,
	.tp_new = py_zfs_new,
	.tp_init = py_zfs_init,
	.tp_doc = "ZFS",
	.tp_dealloc = (destructor)py_zfs_dealloc,
	.tp_str = py_zfs_str,
	.tp_repr = py_zfs_str,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};

#endif /* _PY_ZFS_H */
