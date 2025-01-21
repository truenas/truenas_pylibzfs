#ifndef _PY_ZFS_H
#define _PY_ZFS_H

#include "pylibzfs2.h"

#define	ZFS_STR	"<libzfs2.ZFS>"
#define	DEFAULT_HISTORY_PREFIX	"truenas-pylibzfs: "

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
	const char *history_prefix = DEFAULT_HISTORY_PREFIX;
	char *kwlist[] = {"history", "history_prefix", "mnttab_cache", NULL};

	zfs->history = B_TRUE;
	zfs->mnttab_cache_enable = B_TRUE;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|psp", kwlist,
	    &zfs->history, &history_prefix, &zfs->mnttab_cache_enable)) {
		return (-1);
	}

	if (strlen(history_prefix) > MAX_HISTORY_PREFIX_LEN) {
		PyErr_Format(PyExc_ValueError,
			     "%s: history prefix exceeds maximum "
			     "supported length of %d characters.",
			     history_prefix, MAX_HISTORY_PREFIX_LEN);
		return (-1);
	}

	Py_BEGIN_ALLOW_THREADS
	strlcpy(zfs->history_prefix, history_prefix, MAX_HISTORY_PREFIX_LEN);
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

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".open_resource", "s",
			name) < 0) {
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

PyObject *py_zfs_pool_open(PyObject *self,
                             PyObject *args_unused,
                             PyObject *kwargs)
{
	PyObject *out = NULL;
	py_zfs_t *plz = (py_zfs_t *)self;
	zpool_handle_t *zhp = NULL;
	char *name = NULL;
	py_zfs_error_t zfs_err;

	char *kwnames [] = { "pool_name", NULL };

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					"$s",
					kwnames,
					&name)) {
			return (NULL);
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(plz);
	zhp = zpool_open(plz->lzh, name);
	if (zhp == NULL) {
			py_get_zfs_error(plz->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(plz);
	Py_END_ALLOW_THREADS

	if (zhp == NULL) {
			set_exc_from_libzfs(&zfs_err, "zfs_open() failed");
			return (NULL);
	}

	out = (PyObject *)init_zfs_pool(plz, zhp);
	if (out == NULL)
		zpool_close(zhp);

	return (out);
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
	{
		.ml_name = "open_pool",
		.ml_meth = (PyCFunction)py_zfs_pool_open,
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
