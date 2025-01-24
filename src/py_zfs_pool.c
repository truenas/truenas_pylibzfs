#include "truenas_pylibzfs.h"

#define ZFS_POOL_STR "<" PYLIBZFS_MODULE_NAME ".ZFSPool(name=%U)>"

static
PyObject *py_repr_zfs_pool(PyObject *self) {
	py_zfs_pool_t *pool = (py_zfs_pool_t *) self;

	return PyUnicode_FromFormat(ZFS_POOL_STR, pool->name);
}

static
PyObject *py_zfs_pool_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_pool_t *self = NULL;
	self = (py_zfs_pool_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_pool_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_pool_dealloc(py_zfs_pool_t *self) {
	if (self->zhp != NULL) {
		Py_BEGIN_ALLOW_THREADS
		zpool_close(self->zhp);
		Py_END_ALLOW_THREADS
		self->zhp = NULL;
	}
	Py_CLEAR(self->name);
	Py_CLEAR(self->pylibzfsp);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_zfs_pool_get_name(py_zfs_pool_t *self, void *extra) {
	if (self == NULL)
		Py_RETURN_NONE;
	return Py_NewRef(self->name);
}

static
PyObject *py_zfs_pool_asdict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_root_dataset(PyObject *self, PyObject *args) {
	PyObject *out = NULL;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	PyObject *open_rsrc;
	PyObject *fargs = NULL;
	PyObject *fkwargs = NULL;

	fargs = PyTuple_New(0);
	if (fargs == NULL)
		return (NULL);

	fkwargs = Py_BuildValue("{s:O}", "name", p->name);
	if (fkwargs == NULL) {
		Py_DECREF(fargs);
		return (NULL);
	}

	open_rsrc = PyObject_GetAttrString((PyObject*)p->pylibzfsp,
					   "open_resource");
	if (open_rsrc != NULL)
		out = PyObject_Call(open_rsrc, fargs, fkwargs);

	Py_XDECREF(open_rsrc);
	Py_DECREF(fargs);
	Py_DECREF(fkwargs);

	return (out);
}

static
PyObject *py_zfs_pool_root_vdev(PyObject *self, PyObject *args) {
	PyObject *out = NULL;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	nvlist_t *nvroot;

	Py_BEGIN_ALLOW_THREADS
	nvroot = fnvlist_lookup_nvlist(zpool_get_config(p->zhp, NULL),
	    ZPOOL_CONFIG_VDEV_TREE);
	Py_END_ALLOW_THREADS

	out = (PyObject *)init_zfs_vdev(p, nvroot, NULL);
	return (out);
}

static
PyObject *py_zfs_pool_clear(PyObject *self, PyObject *args) {
	int ret = 0;
	nvlist_t *policy = NULL;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_error_t err;

	Py_BEGIN_ALLOW_THREADS
	policy = fnvlist_alloc();
	/* Force nvlist wrapper wraps the nvlist_* functions with assertions
	 * that assume the operation is successful. We can avoid checking for
	 * NULL here since as assert would be hit from libspl in case the
	 * operation failed.
	 */
	fnvlist_add_uint32(policy, ZPOOL_LOAD_REWIND_POLICY, ZPOOL_NO_REWIND);
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_clear(p->zhp, NULL, policy);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	fnvlist_free(policy);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_clear() failed");
		return (NULL);
	}

	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_upgrade(PyObject *self, PyObject *args) {
	int ret = 0;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_error_t err;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_upgrade(p->zhp, SPA_VERSION);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_upgrade() failed");
		return (NULL);
	}

	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_delete(PyObject *self, PyObject *args, PyObject *kwargs) {
	int ret, force;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_error_t err;
	ret = force = 0;
	char *kwnames[] = {"force", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|p", kwnames, &force)) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_disable_datasets(p->zhp, force);
	if (ret == 0)
		ret = zpool_destroy(p->zhp, "destroy");
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_delete() failed");
		return (NULL);
	}

	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_ddt_prefetch(PyObject *self, PyObject *args) {
	int ret = 0;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_error_t err;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_prefetch(p->zhp, ZPOOL_PREFETCH_DDT);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_ddt_prefetch() failed");
		return (NULL);
	}

	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_ddt_prune(PyObject *self,
				PyObject *args,
				PyObject *kwargs) {
	int ret;
	unsigned int days, percentage;
	uint64_t value = 0;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_error_t err;
	zpool_ddt_prune_unit_t unit = ZPOOL_DDT_PRUNE_NONE;
	char *kwnames[] = {"days", "percentage", NULL};
	ret = days = percentage = 0;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|II", kwnames, &days,
					 &percentage)) {
		return NULL;
	}

	if (percentage > 100) {
		PyErr_SetString(PyExc_ValueError,
			"days must be >= 1, and percentage must be between 1 and 100");
	} else if (days > 0 && percentage > 0) {
		PyErr_SetString(PyExc_ValueError,
			"Only one of days or percentage should be set");
	} else if (days == 0 && percentage == 0) {
		PyErr_SetString(PyExc_ValueError,
			"Either days or percentage must be set");
	}

	if (percentage != 0) {
		unit = ZPOOL_DDT_PRUNE_PERCENTAGE;
		value = percentage;
	} else if (days != 0) {
		unit = ZPOOL_DDT_PRUNE_AGE;
		value = days;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_ddt_prune(p->zhp, unit, value);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_ddt_prune() failed");
		return (NULL);
	}

	Py_RETURN_NONE;
}

PyGetSetDef zfs_pool_getsetters[] = {
	{
		.name	= "name",
		.get	= (getter)py_zfs_pool_get_name
	},
	{ .name = NULL }
};

PyMethodDef zfs_pool_methods[] = {
	{
		.ml_name = "asdict",
		.ml_meth = py_zfs_pool_asdict,
		.ml_flags = METH_NOARGS
	},
	{
		.ml_name = "root_dataset",
		.ml_meth = py_zfs_pool_root_dataset,
		.ml_flags = METH_NOARGS
	},
	{
		.ml_name = "root_vdev",
		.ml_meth = py_zfs_pool_root_vdev,
		.ml_flags = METH_NOARGS
	},
	{
		.ml_name = "clear",
		.ml_meth = py_zfs_pool_clear,
		.ml_flags = METH_NOARGS
	},
	{
		.ml_name = "upgrade",
		.ml_meth = py_zfs_pool_upgrade,
		.ml_flags = METH_NOARGS
	},
	{
		.ml_name = "delete",
		.ml_meth = (PyCFunction)py_zfs_pool_delete,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{
		.ml_name = "ddt_prefetch",
		.ml_meth = py_zfs_pool_ddt_prefetch,
		.ml_flags = METH_NOARGS
	},
	{
		.ml_name = "ddt_prune",
		.ml_meth = (PyCFunction)py_zfs_pool_ddt_prune,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSPool = {
	.tp_name = "ZFSPool",
	.tp_basicsize = sizeof (py_zfs_pool_t),
	.tp_methods = zfs_pool_methods,
	.tp_getset = zfs_pool_getsetters,
	.tp_new = py_zfs_pool_new,
	.tp_init = py_zfs_pool_init,
	.tp_doc = "ZFSPool",
	.tp_dealloc = (destructor)py_zfs_pool_dealloc,
	.tp_repr = py_repr_zfs_pool,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};

py_zfs_pool_t *init_zfs_pool(py_zfs_t *lzp, zpool_handle_t *zhp)
{
	py_zfs_pool_t *out = NULL;
	const char *name;

	out = (py_zfs_pool_t *)PyObject_CallFunction((PyObject *)&ZFSPool, NULL);
	if (out == NULL) {
		return (NULL);
	}

	out->pylibzfsp = lzp;
	Py_INCREF(lzp);

	Py_BEGIN_ALLOW_THREADS
	name = zpool_get_name(zhp);
	Py_END_ALLOW_THREADS

	out->name = PyUnicode_FromString(name);
	if (out->name == NULL) {
		Py_DECREF(out);
		return (NULL);
	}

	out->zhp = zhp;
	return (out);
}
