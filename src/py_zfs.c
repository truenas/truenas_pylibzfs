#ifndef _PY_ZFS_H
#define _PY_ZFS_H

#include "pylibzfs2.h"

#define	ZFS_STR	"<libzfs2.ZFS>"

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
	int err;
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
	Py_BEGIN_ALLOW_THREADS
	zfs->lzh = libzfs_init();
	err = pthread_mutex_init(&zfs->zfs_lock, NULL);
	Py_END_ALLOW_THREADS

	if (err) {
		PyErr_Format(PyExc_RuntimeError,
			     "Failed to initialize pthread mutex: %s",
			     strerror(errno));
		return (-1);
	}
	return (0);
}

void py_zfs_dealloc(py_zfs_t *self) {
	if (self->lzh != NULL) {
		Py_BEGIN_ALLOW_THREADS
		libzfs_fini(self->lzh);
		Py_END_ALLOW_THREADS
	}

	self->lzh = NULL;
	pthread_mutex_destroy(&self->zfs_lock);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *py_zfs_get_proptypes(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_asdict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}


PyObject *py_zfs_resource_open(PyObject *self,
			       PyObject *args_unused,
			       PyObject *kwargs)
{
	PyObject *out = NULL;
	py_zfs_t *plz = (py_zfs_t *)self;
	zfs_handle_t *zfsp = NULL;
	zfs_type_t type;
	char *name = NULL;
	py_zfs_error_t zfs_err;

	char *kwnames [] = { "name", NULL };

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$s",
					 kwnames,
					 &name)) {
		return NULL;
	}

	if (!name) {
		PyErr_SetString(PyExc_ValueError,
				"The name of the resource to open must be "
				"passed to this method through the "
				"\"name\" keyword argument.");
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(plz);
	zfsp = zfs_open(plz->lzh, name, SUPPORTED_RESOURCES);
	if (zfsp == NULL) {
		py_get_zfs_error(plz->lzh, &zfs_err);
	} else {
		type = zfs_get_type(zfsp);
	}
	PY_ZFS_UNLOCK(plz);
	Py_END_ALLOW_THREADS

	if (zfsp == NULL) {
		set_exc_from_libzfs(&zfs_err, "zfs_open() failed");
		return NULL;
	}

	switch (type) {
	case ZFS_TYPE_FILESYSTEM:
		out = (PyObject *)init_zfs_dataset(plz, zfsp);
		break;
	default:
		PyErr_SetString(PyExc_RuntimeError,
				"Unsupported ZFS type");
	}
	if (out == NULL) {
		// We encountered an error generating python object
		// and so we need to close the ZFS handle before
		// raising an exception
		zfs_close(zfsp);
	}
	return out;
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
		.ml_name = "open_resource",
		.ml_meth = (PyCFunction)py_zfs_resource_open,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
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
