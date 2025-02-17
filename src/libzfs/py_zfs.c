#include "../truenas_pylibzfs.h"
#include "py_zfs_iter.h"

#define	ZFS_STR	"<" PYLIBZFS_MODULE_NAME ".ZFS>"
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

	Py_CLEAR(self->module);
	self->lzh = NULL;
	pthread_mutex_destroy(&self->zfs_lock);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *py_zfs_asdict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
boolean_t py_zfs_create(py_zfs_t *self,
			const char *name,
			zfs_type_t allowed_types,
			PyObject *pyzfstype,
			PyObject *pyprops,
			PyObject *pyuserprops)
{
	long ztype;
	nvlist_t *props = NULL;
	nvlist_t *userprops = NULL;
	pylibzfs_state_t *state = NULL;
	py_zfs_error_t zfs_err;
	int err;

	state = py_get_module_state(self);

	if (!PyObject_IsInstance(pyzfstype, state->zfs_type_enum)) {
		PyObject *repr = PyObject_Repr(pyzfstype);
		PyErr_Format(PyExc_TypeError,
			     "%V: not a valid ZFSType",
			     repr, "UNKNOWN");
		Py_XDECREF(repr);
		return B_FALSE;
	}

	ztype = PyLong_AsLong(pyzfstype);
	PYZFS_ASSERT(
		((ztype > ZFS_TYPE_INVALID) && (ztype <= ZFS_TYPE_VDEV)),
		"Unexpected ZFSType enum value"
	);

	if ((ztype & allowed_types) == 0) {
		PyErr_Format(PyExc_TypeError,
			     "%s: not a permitted ZFS type.",
			     get_dataset_type(ztype));
		return B_FALSE;
	}

	if (pyprops != NULL) {
		props = py_zfsprops_to_nvlist(state,
					      pyprops,
					      ztype,
					      B_TRUE);
		if (props == NULL)
			return B_FALSE;

	}
	if (pyuserprops != NULL) {
		userprops = py_userprops_dict_to_nvlist(pyuserprops);
		if (userprops == NULL) {
			fnvlist_free(props);
			return B_FALSE;
		}

		if (props == NULL) {
			props = userprops;
		} else {
			/*
			 * Merge the two nvlists for the ZFS
			 * create operation
			 */
			fnvlist_merge(props, userprops);
			fnvlist_free(userprops);
		}

	}
	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".create_resource", "s",
			name) < 0) {
		fnvlist_free(props);
		return B_FALSE;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(self);
	err = zfs_create(self->lzh, name, ztype, props);
	if (err) {
		py_get_zfs_error(self->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(self);
	Py_END_ALLOW_THREADS

	if (props) {
		const char *json_str = NULL;
		PyObject *dump = py_dump_nvlist(props, B_TRUE);
		if (dump != NULL) {
			json_str = PyUnicode_AsUTF8(dump);
		}
		err = py_log_history_fmt(self, "zfs create %s with properties: %s",
					 name, json_str ? json_str: "UNKNOWN");
		Py_XDECREF(dump);
		if (err) {
			fnvlist_free(props);
			return B_FALSE;
		}
	} else {
		err = py_log_history_fmt(self, "zfs create %s", name);
		if (err) {
			fnvlist_free(props);
			return B_FALSE;
		}
	}

	fnvlist_free(props);

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_open() failed");
		return B_FALSE;
	}

	return B_TRUE;
}

static
PyObject *py_zfs_resource_create(PyObject *self,
				 PyObject *args_unused,
				 PyObject *kwargs)
{
	py_zfs_t *plz = (py_zfs_t *)self;
	char *name = NULL;
	PyObject *pyprops = NULL;
	PyObject *pyuprops = NULL;
	PyObject *pyzfstype = NULL;
	boolean_t created;

	char *kwnames [] = {
		"name",
		"type",
		"properties",
		"user_properties",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$sOOO",
					 kwnames,
					 &name,
					 &pyzfstype,
					 &pyprops,
					 &pyuprops)) {
		return NULL;

	}

	if (name == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"The name of the resource to create must be "
				"passed to this method through the "
				"\"name\" keyword argument.");
		return NULL;
	} else if (pyzfstype == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"The \"type\" keyword argument is required.");
		return NULL;
	}

	created = py_zfs_create(plz, name, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME,
	    pyzfstype, pyprops, pyuprops);
	if (!created)
		return NULL;

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
		out = (PyObject *)init_zfs_dataset(plz, zfsp, B_FALSE);
		break;
	case ZFS_TYPE_VOLUME:
		out = (PyObject *)init_zfs_volume(plz, zfsp, B_FALSE);
		break;
	case ZFS_TYPE_SNAPSHOT:
		out = (PyObject *)init_zfs_snapshot(plz, zfsp, B_FALSE);
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

PyObject *py_zfs_resource_destroy(PyObject *self,
				  PyObject *args_unused,
				  PyObject *kwargs)
{
	py_zfs_t *plz = (py_zfs_t *)self;
	char *name = NULL;
	zfs_handle_t *zfsp = NULL;
	py_zfs_error_t zfs_err;
	boolean_t defer = B_FALSE;
	boolean_t destroyed = B_FALSE;
	int err;

	char *kwnames [] = { "name", "defer", NULL };

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$sp",
					 kwnames,
					 &name,
					 &defer)) {
		return NULL;
	}

	if (!name) {
		PyErr_SetString(PyExc_ValueError,
				"The name of the resource to destroy must be "
				"passed to this method through the "
				"\"name\" keyword argument.");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".destroy_resource", "s",
			name) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(plz);
	zfsp = zfs_open(plz->lzh, name, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zfsp == NULL) {
		py_get_zfs_error(plz->lzh, &zfs_err);
	} else {
		err = zfs_destroy(zfsp, defer);
		if (err) {
			py_get_zfs_error(plz->lzh, &zfs_err);
		} else {
			destroyed = B_TRUE;
		}
		zfs_close(zfsp);
	}
	PY_ZFS_UNLOCK(plz);
	Py_END_ALLOW_THREADS

	if (!destroyed) {
		set_exc_from_libzfs(&zfs_err, "zfs_destroy() failed");
		return NULL;
	}

	err = py_log_history_fmt(plz, "zfs destroy %s", name);
	if (err) {
		return NULL;
	}

	Py_RETURN_NONE;
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

	if (!name) {
		PyErr_SetString(PyExc_ValueError,
				"The name of the pool to open must be "
				"passed to this method through the "
				"\"pool_name\" keyword argument.");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".open_pool", "s",
			name) < 0) {
		return NULL;
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

PyDoc_STRVAR(py_zfs_iter_root_datasets__doc__,
"iter_root_datasets(*, callback, state) -> bool\n\n"
"----------------------------------------------\n\n"
"Iterate root datasets for all imported zpools\n\n"
"Parameters\n"
"----------\n"
"callback: callable\n"
"    Callback function that will be called for every child dataset.\n\n"
"state: object, optional\n"
"    Optional python object (for example dictionary) passed as an argument\n"
"    to the callback function for each root dataset.\n\n"
"Returns\n"
"-------\n"
"bool\n"
"    Value indicates that iteration completed without being stopped by the\n"
"    callback fuction returning False.\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    An error occurred during iteration of the datasetd. Note that this\n"
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
PyObject *py_zfs_iter_root_datasets(PyObject *self,
					   PyObject *args_unused,
					   PyObject *kwargs)
{
	int err;
	py_zfs_t *plz = (py_zfs_t *)self;

	py_iter_state_t iter_state = (py_iter_state_t){
		.pylibzfsp = plz,
	};

	char *kwnames [] = {"callback", "state", NULL};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$OO",
					 kwnames,
					 &iter_state.callback_fn,
					 &iter_state.private_data)) {
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

	// There aren't any useful arguments we can pass to sys.audit
	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".iter_root_datasets",
			"O", Py_None) < 0) {
		return NULL;
	}

	err = py_iter_root_datasets(&iter_state);
	if ((err == ITER_RESULT_ERROR) || (err == ITER_RESULT_IOCTL_ERROR)) {
		// Exception is set by callback function
		return NULL;
	}

	if (err == ITER_RESULT_SUCCESS) {
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyDoc_STRVAR(py_zfs_test_topology__doc__,
"test_topology(*, topology) -> None\n\n"
"--------------------------------------\n\n"
"Creates a VDEV Tree from given topology and returns the Python string\n"
"containing VDEV tree. This can be helpful while debugging issues in VDEV\n"
"tree creation.\n\n"
"Parameters\n"
"----------\n"
"topology: Iterable of Dictionaries\n"
"    Topology is an iterable of Dictionaries where each item of the list would\n"
"    specify the formation of each VDEV.\n"
"        \'root\': Str\n"
"            this key should specify the class of VDEV. This can be any of\n"
"            \'DATA\', \'LOG\', \'DEDUP\', \'SPECIAL\', \'SPARE\', or \'CACHE\'.\n"
"        \'type\': Str\n"
"            specify the type of VDEV, which can be any one of \'MIRROR\',\n"
"            \'STRIPE\', \'RAIDZ1\', \'RAIDZ2\', \'RAIDZ3\', \'DRAID1\'\n"
"            \'DRAID2\' or \'DRAID3\'.\n"
"        \'devices\': List of Str\n"
"            List of valid paths in string format for disks.\n\n"
"Returns\n"
"-------\n"
"Python String containing VDEV tree.\n\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    If there is an issue while parsing the topology, TypeError will be raised.\n"
);
static
PyObject *py_zfs_test_topology(PyObject *self, PyObject *args,
    PyObject *kwargs) {
	PyObject *topology = NULL;
	PyObject *ret;

	char *kwnames[] = {"topology", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$O", kwnames,
	    &topology)) {
		return (NULL);
	}

	if (topology == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "topology keyword argument is required.");
		return (NULL);
	}

	nvlist_t *tree = make_vdev_tree(topology, NULL);
	if (tree == NULL)
		return (NULL);
	ret = py_dump_nvlist(tree, B_TRUE);
	fnvlist_free(tree);
	return (ret);
}

PyDoc_STRVAR(py_zfs_pool_create__doc__,
"create_pool(*, name, topology) -> None\n\n"
"--------------------------------------\n\n"
"Creates a new pool with given name and topology.\n\n"
"Parameters\n"
"----------\n"
"name: str\n"
"    name of the new pool to create.\n\n"
"topology: Iterable of Dictionaries\n"
"    Topology is an iterable of Dictionaries where each item of the list would\n"
"    specify the formation of each VDEV.\n"
"        \'root\': Str\n"
"            this key should specify the class of VDEV. This can be any of\n"
"            \'DATA\', \'LOG\', \'DEDUP\', \'SPECIAL\', \'SPARE\', or \'CACHE\'.\n"
"        \'type\': Str\n"
"            specify the type of VDEV, which can be any one of \'MIRROR\',\n"
"            \'STRIPE\', \'RAIDZ1\', \'RAIDZ2\', \'RAIDZ3\', \'DRAID1\'\n"
"            \'DRAID2\' or \'DRAID3\'.\n"
"        \'devices\': List of Str\n"
"            List of valid paths in string format for disks.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    If there is an issue while parsing the topology, TypeError will be raised.\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error that occurred while trying to perform the operation.\n\n"
);
static
PyObject *py_zfs_create_pool(PyObject *self, PyObject *args,
    PyObject *kwargs)
{
	int ret = 0;
	py_zfs_t *plz = (py_zfs_t *)self;
	py_zfs_error_t zfs_err;
	char *name = NULL;
	PyObject *topology = NULL;

	char *kwnames[] = {"name", "topology", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$sO", kwnames,
	    &name, &topology)) {
		return (NULL);
	}

	if (name == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "name keyword argument is required");
		return (NULL);
	}

	if (topology == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "topology keyword argument is required.");
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".create_pool", "sO",
	    name, topology) < 0) {
		return (NULL);
	}

	nvlist_t *tree = make_vdev_tree(topology, NULL);
	if (tree == NULL)
		return (NULL);
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(plz);
	ret = zpool_create(plz->lzh, name, tree, NULL, NULL);
	if (ret != 0) {
		py_get_zfs_error(plz->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(plz);
	Py_END_ALLOW_THREADS

	if (ret != 0) {
		set_exc_from_libzfs(&zfs_err, "zpool_create() failed");
		fnvlist_free(tree);
		return (NULL);
	} else {
		PyObject *dump = py_dump_nvlist(tree, B_TRUE);
		ret = py_log_history_fmt(plz, "zpool create %s %s", name,
		    PyUnicode_AsUTF8(dump));
		fnvlist_free(tree);
		Py_XDECREF(dump);
		if (ret) {
			// exception should be set since we failed to log
			// history
			return (NULL);
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_destroy__doc__,
"destroy_pool(*, name, force=False) -> None\n\n"
"-----------------\n\n"
"Destroys the given pool, freeing up any devices for other use.\n\n"
"Parameters\n"
"----------\n"
"force: bool, optional\n"
"    Forcefully unmount all active datasets.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error that occurred while trying to perform the operation.\n"
);
static
PyObject *py_zfs_pool_destroy(PyObject *self, PyObject *args, PyObject *kwargs) {
	int ret = 0, force = 0;
	char *name = NULL;
	py_zfs_t *plz = (py_zfs_t *)self;
	py_zfs_error_t err;
	zpool_handle_t *zhp = NULL;
	boolean_t destroyed = B_FALSE;
	char *kwnames[] = {"name", "force", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$sp", kwnames, &name,
	    &force)) {
		return NULL;
	}

	if (name == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "name keyword argument is required.");
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".destroy", "si",
	    name, force) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(plz);
	zhp = zpool_open(plz->lzh, name);
	if (zhp == NULL) {
		py_get_zfs_error(plz->lzh, &err);
	} else {
		ret = zpool_disable_datasets(zhp, force);
		if (ret == 0)
			ret = zpool_destroy(zhp, "destroy");
		if (ret)
			py_get_zfs_error(plz->lzh, &err);
		else
			destroyed = B_TRUE;
		zpool_close(zhp);
	}
	PY_ZFS_UNLOCK(plz);
	Py_END_ALLOW_THREADS

	if (!destroyed) {
		set_exc_from_libzfs(&err, "zpool_destroy() failed");
		return (NULL);
	}
	Py_RETURN_NONE;
}

PyGetSetDef zfs_getsetters[] = {
	{ .name = NULL }
};

PyMethodDef zfs_methods[] = {
	{
		.ml_name = "create_resource",
		.ml_meth = (PyCFunction)py_zfs_resource_create,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{
		.ml_name = "open_resource",
		.ml_meth = (PyCFunction)py_zfs_resource_open,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{
		.ml_name = "destroy_resource",
		.ml_meth = (PyCFunction)py_zfs_resource_destroy,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{
		.ml_name = "iter_root_datasets",
		.ml_meth = (PyCFunction)py_zfs_iter_root_datasets,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_iter_root_datasets__doc__
	},
	{
		.ml_name = "open_pool",
		.ml_meth = (PyCFunction)py_zfs_pool_open,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{
		.ml_name = "test_topology",
		.ml_meth = (PyCFunction)py_zfs_test_topology,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_test_topology__doc__
	},
	{
		.ml_name = "create_pool",
		.ml_meth = (PyCFunction)py_zfs_create_pool,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_create__doc__
	},
	{
		.ml_name = "destroy_pool",
		.ml_meth = (PyCFunction)py_zfs_pool_destroy,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_destroy__doc__
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
