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
			PyObject *pyuserprops,
			PyObject *pycrypto)
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

	if (pycrypto) {
		// pyzfs_creat_crypto() will handle the history entry and
		// freeing `props`
		return pyzfs_create_crypto(self, name, ztype, props, pycrypto);
	} else {
		Py_BEGIN_ALLOW_THREADS
		PY_ZFS_LOCK(self);
		err = zfs_create(self->lzh, name, ztype, props);
		if (err) {
			py_get_zfs_error(self->lzh, &zfs_err);
		}
		PY_ZFS_UNLOCK(self);
		Py_END_ALLOW_THREADS
	}

	// Error out early to avoid inserting ZFS history on failed dataset creation
	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_create() failed");
		fnvlist_free(props);
		return B_FALSE;
	}

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

	return B_TRUE;
}

PyDoc_STRVAR(py_zfs_create__doc__,
"create_resource(*, name, type, properties=None, crypto=None) -> None\n"
"--------------------------------------------------------------------\n\n"
"Create a new ZFS FILESYSTEM or VOLUME with the given properties and\n"
"encryption settings.\n"
"\n"
"See man (7) zfsconcepts for description of the ZFS File System Hierarchy\n"
"and man (7) zfsprops for descriptions of various possible ZFS properties\n"
""
"Parameters\n"
"----------\n"
"name: str, required\n"
"    The name of the ZFS resource to create. \n"
"    It must:\n"
"    - Begin with the name of an existing ZFS pool.\n"
"    - Be a maximum of 255 ASCII characters long.\n"
"    - Identify a unique path in the ZFS namespace.\n"
"    - Use only the characters [A-Za-z_:/ -].\n"
"    - Contain no more than 50 components.\n"
"    - All path components, except the last component, must already exist.\n\n"
"type: " PYLIBZFS_MODULE_NAME ".ZFSType, required\n"
"    The ZFS resource type to create. The value must be either "
     PYLIBZFS_MODULE_NAME ".ZFSType.ZFS_TYPE_FILESYSTEM\n"
"    or " PYLIBZFS_MODULE_NAME ".ZFSType.ZFS_TYPE_VOLUME\n\n"
"properties: dict | " PYLIBZFS_MODULE_NAME "struct_zfs_property, optional\n"
"    Optional ZFS dataset property configuration.\n"
"    This can be:\n"
"    - A dictionary with keys from "  PYLIBZFS_MODULE_NAME ".ZFSProperty\n"
"    - A struct_zfs_property sequence object returned when getting properties\n"
"      of an existing ZFS resource.\n"
"\n"
"    NOTE: dictionary values may be specified in the following ways:\n"
"    - simplified: \"on\"\n"
"    - as dictionary like returned by asdict(): {\"value\": \"on\"}\n"
"    WARNING: not all ZFSProperty properties are valid for a given ZFSType.\n\n"
"crypto: " PYLIBZFS_MODULE_NAME ".struct_zfs_crypto_config, optional\n"
"    Optional encryption configuration to establish the volume as a new\n"
"    encryption root. If omitted, then the new resource will inherit\n"
"    the encryption configuration of its parent dataset. A crypto config\n"
"    object may be created by calling the `resource_cryptography_config`\n"
"    method on an existing libzfs handle object.\n\n"
"Returns\n"
"-------\n"
"    None\n"
""
"Raises:\n"
"-------\n"
"ValueError:\n"
"    Invalid combination of parameters.\n"
"TypeError:\n"
"    Invaild ZFS type was specified.\n"
"ZFSException:\n"
"    The ZFS operation failed for one of a variety of library-specific reasons\n"
);
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
	PyObject *pycrypto = NULL;
	boolean_t created;

	char *kwnames [] = {
		"name",
		"type",
		"properties",
		"user_properties",
		"crypto",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$sOOOO",
					 kwnames,
					 &name,
					 &pyzfstype,
					 &pyprops,
					 &pyuprops,
					 &pycrypto)) {
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
	    pyzfstype, pyprops, pyuprops, pycrypto);
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

PyDoc_STRVAR(py_zfs_resource_destroy__doc__,
"destroy_resource(*, name) -> bool\n\n"
"----------------------------------------------\n\n"
"Destroy the ZFS FILESYSTEM or VOLUME. This is a destructive operation that\n"
"is irreversible.\n\n"
"Parameters\n"
"----------\n"
"name: str, required\n"
"    The name of the ZFS resource to destroy.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSException:\n"
"    The error code EZFS_NOENT is set if the resource does not exist.\n"
"truenas_pylibzfs.ZFSException:\n"
"    A library error occured while attempting to destroy the resource.\n\n"
"IMPLEMENTATION NOTE: this method calls zfs_open() followed by zfs_destroy()\n"
"on the handle. If there is a race in which the resource is renamed or\n"
"destroyed between the open and destroy library calls, libzfs ignores the\n"
"ENOENT and reports success.\n"
);
PyObject *py_zfs_resource_destroy(PyObject *self,
				  PyObject *args_unused,
				  PyObject *kwargs)
{
	py_zfs_t *plz = (py_zfs_t *)self;
	char *name = NULL;
	zfs_handle_t *zfsp = NULL;
	py_zfs_error_t zfs_err;
	boolean_t destroyed = B_FALSE;
	int err;

	char *kwnames [] = { "name", NULL };

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$s",
					 kwnames,
					 &name)) {
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
		err = zfs_destroy(zfsp, B_FALSE);
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

	char *kwnames [] = { "name", NULL };

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
				"\"name\" keyword argument.");
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

PyDoc_STRVAR(py_zfs_pool_destroy__doc__,
"destroy_pool(*, name, force=False) -> None\n\n"
"------------------------------------------\n\n"
"Destroys the given pool, freeing up any devices for other use.\n\n"
"Parameters\n"
"----------\n"
"name: str, required\n"
"    Name of the pool to destroy\n"
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
PyObject *py_zfs_pool_destroy(PyObject *self, PyObject *args, PyObject *kwargs)
{
	int err = -1;
	boolean_t force = B_FALSE;
	char *pool_name = NULL;
	py_zfs_t *plz = (py_zfs_t *)self;
	py_zfs_error_t zfs_err;
	zpool_handle_t *zhp;
	char history_entry[256];  // MAX_HISTORY_PREFIX_LEN + whatever we add here
	char *kwnames[] = {"name", "force", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs,
					 "|$sp",
					 kwnames,
					 &pool_name,
					 &force)) {
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".destroy_pool", "sO",
		pool_name, kwargs ? kwargs : Py_None) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(plz);
	zhp = zpool_open(plz->lzh, pool_name);
	if (zhp == NULL) {
		py_get_zfs_error(plz->lzh, &zfs_err);
	} else {
		err = zpool_disable_datasets(zhp, force);
		if (err == 0) {
			// Create a history message to be inserted if
			// the pool destroy goes sideways for some reason
			snprintf(history_entry, sizeof(history_entry),
				 "%s zpool destroy", plz->history_prefix);
			err = zpool_destroy(zhp, history_entry);
		}
		if (err)
			py_get_zfs_error(plz->lzh, &zfs_err);
	}
	zpool_close(zhp);
	PY_ZFS_UNLOCK(plz);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "destroy_pool() failed");
		return NULL;
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_iter_root_filesystems__doc__,
"iter_root_filesystems(*, callback, state) -> bool\n\n"
"----------------------------------------------\n\n"
"Iterate root filesystems for all imported zpools\n\n"
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
"    An error occurred during iteration of the dataset. Note that this\n"
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
PyObject *py_zfs_iter_root_filesystems(PyObject *self,
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
	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".iter_root_filesystems",
			"O", Py_None) < 0) {
		return NULL;
	}

	err = py_iter_root_filesystems(&iter_state);
	if ((err == ITER_RESULT_ERROR) || (err == ITER_RESULT_IOCTL_ERROR)) {
		// Exception is set by callback function
		return NULL;
	}

	if (err == ITER_RESULT_SUCCESS) {
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyDoc_STRVAR(py_zfs_iter_pools__doc__,
"iter_pools(*, callback, state) -> bool\n\n"
"----------------------------------------------\n\n"
"Iterate all pools on the system.\n\n"
"Parameters\n"
"----------\n"
"callback: callable\n"
"    Callback function that will be called for every pool.\n\n"
"state: object, optional\n"
"    Optional python object (for example dictionary) passed as an argument\n"
"    to the callback function for each pool.\n\n"
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
"Minimally the function signature must take a single argument for each zpool\n"
"object. If the \"state\" keyword is specified then the callback function\n"
"should take two arguments. The callback function must return bool value\n"
"indicating whether iteration should continue.\n\n"
"Example \"callback\":\n"
"-------------------\n"
"def my_callback(pool, state):\n"
"    print(f'{pool.name}: {state}')\n"
"    return True\n"
);
static
PyObject *py_zfs_iter_pools(PyObject *self,
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
	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".iter_pools",
			"O", Py_None) < 0) {
		return NULL;
	}

	err = py_iter_pools(&iter_state);
	if ((err == ITER_RESULT_ERROR) || (err == ITER_RESULT_IOCTL_ERROR)) {
		// Exception is set by callback function
		return NULL;
	}

	if (err == ITER_RESULT_SUCCESS) {
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyDoc_STRVAR(py_zfs_rsrc_crypto_config__doc__,
"resource_cryptography_config(*, keyformat=None, keylocation=None,\n"
"                             pbkdf2iters=1300000, key=None) -> None\n"
"--------------------------------------------------------------------\n\n"
"Create a truenas_pylibzfs.struct_zfs_crypto_config based on the specified \n"
"parameters for use when creating a new encrypted resource or re-keying \n"
"an existing encrypted resource.\n"
"See Encryption section of man (5) zfs-load-key for more information.\n\n"
""
"Parameters\n"
"----------\n"
"keyformat: str, required\n"
"    Required parameter specifying the format of the key material\n"
"    located at \"key_location_uri\" or provided as \"key\".\n"
"    Must be one of \"raw\", \"hex\", or \"passphrase\".\n\n"
"keylocation: str, optional, default=None\n"
"    Optional parameter to specify the location in which key material\n"
"    may be found. This may be a local file or a path served over https.\n"
"    This must be None when \"key\" is specified.\n\n"
"key: str, optional, default=None\n"
"    Optional parameter to specify the password or key to use\n"
"    to unlock the ZFS resource. This is required if the ZFS\n"
"    resource (dataset or zvol) has the keylocation set to \"prompt\"\n"
"    or if the \"key_location_uri\" in this payload is omitted.\n"
"pbkdf2iters: int, optional, default=1300000\n"
"    Optional parameter to specify the number of hashing iterations\n"
"    to peform on the key material when the \"key_format\" is \"passphrase\".\n"
"Returns\n"
"-------\n"
"new truenas_pylibzfs.struct_zfs_crypto_config object\n\n"
""
"Raises:\n"
"-------\n"
"ValueError:\n"
"    Invalid combination of parameters.\n\n"
);
static
PyObject *py_zfs_rsrc_crypto_config(PyObject *self,
				    PyObject *args_unused,
				    PyObject *kwargs)
{
	// WARNING: currently genrate_crypto_config will
	// INCREF these as expected, but any future code
	// changes here will need to make sure that reference
	// counting is appropriately handled.
	PyObject *py_keyformat = Py_None;
	PyObject *py_keyloc = Py_None;
	PyObject *py_key = Py_None;
	PyObject *py_iters = Py_None;

	char *kwnames [] = {
		"keyformat",
		"keylocation",
		"key",
		"pbkdf2_iters",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$OOOO",
					 kwnames,
					 &py_keyformat,
					 &py_keyloc,
					 &py_key,
					 &py_iters)) {
		return NULL;
	}

	return generate_crypto_config(
		(py_zfs_t *)self,
		py_keyformat,
		py_keyloc,
		py_key,
		py_iters
	);
}


PyGetSetDef zfs_getsetters[] = {
	{ .name = NULL }
};

PyMethodDef zfs_methods[] = {
	{
		.ml_name = "create_resource",
		.ml_meth = (PyCFunction)py_zfs_resource_create,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_create__doc__
	},
	{
		.ml_name = "open_resource",
		.ml_meth = (PyCFunction)py_zfs_resource_open,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{
		.ml_name = "destroy_resource",
		.ml_meth = (PyCFunction)py_zfs_resource_destroy,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_resource_destroy__doc__
	},
	{
		.ml_name = "iter_root_filesystems",
		.ml_meth = (PyCFunction)py_zfs_iter_root_filesystems,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_iter_root_filesystems__doc__
	},
	{
		.ml_name = "iter_pools",
		.ml_meth = (PyCFunction)py_zfs_iter_pools,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_iter_pools__doc__
	},
	{
		.ml_name = "open_pool",
		.ml_meth = (PyCFunction)py_zfs_pool_open,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{
		.ml_name = "destroy_pool",
		.ml_meth = (PyCFunction)py_zfs_pool_destroy,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_destroy__doc__
	},
	{
		.ml_name = "resource_cryptography_config",
		.ml_meth = (PyCFunction)py_zfs_rsrc_crypto_config,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_rsrc_crypto_config__doc__
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
