#include "../truenas_pylibzfs.h"

#define ZFS_ENC_STR "<" PYLIBZFS_MODULE_NAME \
    ".ZFSEncrypt(name=%U, pool=%U, type=%U)>"

static
PyObject *py_zfs_enc_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_enc_t *self = NULL;
	self = (py_zfs_enc_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_enc_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_enc_dealloc(py_zfs_enc_t *self) {
	switch(self->ctype) {
	case ZFS_TYPE_FILESYSTEM:
		Py_CLEAR(self->rsrc_obj.ds);
		break;
	case ZFS_TYPE_VOLUME:
		Py_CLEAR(self->rsrc_obj.vol);
		break;
	default:
		// may be 0 (ZFS_TYPE_INVALID)
		// if this was allocated then deallocated
		// without actually being used
		break;
	};

	Py_TYPE(self)->tp_free((PyObject *)self);
}

/* STEAL a reference to the underlying resource object */
static inline
py_zfs_obj_t *py_enc_get_zfs_obj(py_zfs_enc_t *enc)
{
	py_zfs_obj_t *obj = NULL;

	switch(enc->ctype) {
	case ZFS_TYPE_FILESYSTEM:
		obj = &enc->rsrc_obj.ds->rsrc.obj;
		break;
	case ZFS_TYPE_VOLUME:
		obj = &enc->rsrc_obj.vol->rsrc.obj;
		break;
	default:
		PyErr_Format(PyExc_TypeError,
			     "%d: unsupported zfs_type_t type",
			     enc->ctype);
		break;
	};

	return obj;
}

static
PyObject *py_repr_zfs_enc(PyObject *self)
{
	py_zfs_obj_t *obj = py_enc_get_zfs_obj((py_zfs_enc_t *)self);
	PYZFS_ASSERT(obj, "invalid zfs_type_t type");

	return py_repr_zfs_obj_impl(obj, ZFS_ENC_STR);
}

#define ZFS_MEM_KEYFILE "truenas_pylibzfs_keyfile"
static
FILE *get_mem_keyfile(void)
{
	FILE *out = NULL;
	int fd;

	fd = memfd_create(ZFS_MEM_KEYFILE, 0);
	if (fd == -1) {
		return NULL;
	}

	out = fdopen(fd, "w+");
	if (out == NULL) {
		close(fd);
		return NULL;
	}
	return out;
}

static
PyObject *py_load_key_memory(py_zfs_obj_t *obj,
			     const char *key,
			     boolean_t test)
{
	FILE *keyfile = NULL;
	char pbuf[42] = { 0 };  // "file://" + "/proc/self/fd/" + strlen(2^64) + \0
	boolean_t success = B_FALSE;
	py_zfs_error_t zfs_err;
	int err = 0;

	Py_BEGIN_ALLOW_THREADS
	// Copy user-provided key into an in-memory FILE
	keyfile = get_mem_keyfile();
	if (keyfile != NULL) {
		size_t written;
		written = fwrite(key, 1, strlen(key), keyfile);
		if (written == strlen(key)) {
			fflush(keyfile);
			success = B_TRUE;
		}
	}

	if (success) {
		// generate a procfd path for libzfs
		snprintf(
			pbuf, sizeof(pbuf),
			"file:///proc/self/fd/%d",
			fileno(keyfile)
		);
		PY_ZFS_LOCK(obj->pylibzfsp);
		err = zfs_crypto_load_key(obj->zhp, test, pbuf);
		if (err) {
			py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
		} else {
			zfs_refresh_properties(obj->zhp);
		}
		PY_ZFS_UNLOCK(obj->pylibzfsp);
	}

	// Free temporary resources
	if (keyfile) {
		fclose(keyfile);
	}
	Py_END_ALLOW_THREADS

	/* First check for ZFS error */
	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_load_key() failed");
		return NULL;
	}

	/* File op failed and so errno should be set */
	if (!success) {
		PyErr_Format(PyExc_RuntimeError,
			     "Failed to load key into memory: %s",
			     strerror(errno));
		return NULL;
	}

	Py_RETURN_NONE;
}

PyObject *py_load_key_impl(py_zfs_obj_t *obj,
			   const char *key,
			   const char *key_location,
			   boolean_t test)
{
	py_zfs_error_t zfs_err;
	int err = 0;

	if (key && key_location) {
		PyErr_SetString(PyExc_ValueError,
				"key and key_location may not "
				"be specified simultanesouly.");
		return NULL;
	}

	if (key) {
		// Library user has provided a key.
		// We'll write it to an in-memory file and have
		// libzfs load it
		return py_load_key_memory(obj, key, test);
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);
	err = zfs_crypto_load_key(obj->zhp, test, NULL);
	if (err) {
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	} else {
		// We need to refresh properties otherwise
		// lock followed by unlock and vice-versa is buggered
		zfs_refresh_properties(obj->zhp);
	}
	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_load_key() failed");
		return NULL;
	}

	Py_RETURN_NONE;
}

static
boolean_t py_validate_key_location(py_zfs_obj_t *obj)
{
	uint64_t keyformat = ZFS_KEYFORMAT_NONE;
	char keyloc[MAXNAMELEN];
	py_zfs_error_t zfs_err;
	int err;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);
	keyformat = zfs_prop_get_int(obj->zhp, ZFS_PROP_KEYFORMAT);
	err = zfs_prop_get(obj->zhp, ZFS_PROP_KEYLOCATION, keyloc,
			   sizeof(keyloc), NULL, NULL, 0, B_TRUE);
	if (err)
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "Failed to validate key location");
		return B_FALSE;
	}

	// This should never happen since unencrypted resources shouldn't be
	// able to generate this ZFSEncrypt objects
	PYZFS_ASSERT(
		keyformat != ZFS_KEYFORMAT_NONE,
		"Not an encryped dataset"
	);

	if (strcmp(keyloc, "prompt") == 0) {
		// We can't support prompting for key
		PyErr_SetString(PyExc_ValueError,
				"ZFS resource has been configured to "
				"prompt for a password and no password "
				"was provided through the \"key\" argument.");
		return B_FALSE;
	}

	return B_TRUE;
}

PyDoc_STRVAR(py_zfs_enc_load_key__doc__,
"load_key(*, key=None, key_location=None, test=False) -> None\n"
"------------------------------------------------------------\n\n"
"Load the encrpytion key for the ZFS resource.\n"
""
"Parameters\n"
"----------\n"
"key: str, optional, default=None\n"
"    Optional parameter to specify the password or key to use\n"
"    to unlock the ZFS resource. This is required if the ZFS\n"
"    resource (dataset or zvol) has the keylocation set to \"prompt\".\n"
"    which to mount the datasets. If this is omitted then the\n"
"    mountpoint specied in the ZFS mountpoint property will be used.\n\n"
"    Generally the mountpoint should be not be specified and the\n"
"    library user should rely on the ZFS mountpoint property.\n\n"
"key_location: str, optional, default=None\n"
"    Optional parameter to override the ZFS key location specified\n"
"    in the ZFS dataset settings. This must be None when \"key\" is\n"
"    specified.\n\n"
"test: bool, optional, default=False\n"
"    Perform a dry-run to check whether the ZFS resource can be unlocked\n"
"    with the specified parameters.\n\n"
""
"Returns\n"
"-------\n"
"None\n\n"
""
"Raises:\n"
"-------\n"
"ValueError:\n"
"    Invalid combination of key and key_location parameters.\n\n"
"ValueError:\n"
"    ZFS resource is configured to prompt for key and no key was provided.\n\n"
"RuntimeError:\n"
"    An OS error occurred when create an in-memory file for user-provided key\n\n"
"ZFSError:\n"
"    The zfs_load_key() operation failed.\n\n"
);
static
PyObject *py_zfs_enc_load_key(PyObject *self,
			      PyObject *args_unused,
			      PyObject *kwargs)
{
	py_zfs_obj_t *obj = py_enc_get_zfs_obj(((py_zfs_enc_t *)self));
	const char *alt_keylocation = NULL;
	const char *key = NULL;
	boolean_t test = B_FALSE;

	char *kwnames [] = {
		"key",
		"key_location",
		"test",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
                                         "|$ssp",
                                         kwnames,
					 &key,
					 &alt_keylocation,
                                         &test)) {
                return NULL;
        }

	if (!key && !alt_keylocation) {
		if (!py_validate_key_location(obj))
			return NULL;
	}

	return py_load_key_impl(obj,
				key,
				alt_keylocation,
				test);
}

PyDoc_STRVAR(py_zfs_enc_unload_key__doc__,
"unload_key() -> None\n"
"--------------------\n\n"
"Unload the encrpytion key for the ZFS resource. Often this operation\n"
"is combined with a subsequent unmount operation. In this case it may\n"
"be more efficient to call the unmount(unload_encryption_key=True)\n"
"and perform the key unload and unmount simultaneously.\n\n"
""
"Parameters\n"
"----------\n"
"    None\n\n"
"Returns\n"
"-------\n"
"    None\n\n"
"Raises:\n"
"-------\n"
"ZFSError:\n"
"    The zfs_unload_key() operation failed.\n"
);
PyObject *py_zfs_enc_unload_key(PyObject *self, PyObject *args_unused)
{
	py_zfs_obj_t *obj = py_enc_get_zfs_obj(((py_zfs_enc_t *)self));
	int err;
	py_zfs_error_t zfs_err;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);
	err = zfs_crypto_unload_key(obj->zhp);
	if (err) {
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	} else {
		// We need to refresh properties otherwise
		// lock followed by unlock and vice-versa is buggered
		zfs_refresh_properties(obj->zhp);
	}
	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_unload_key() failed");
		return NULL;
	}

	Py_RETURN_NONE;
}

static
PyGetSetDef zfs_enc_getsetters[] = {
	{ .name = NULL }
};

static
PyMethodDef zfs_enc_methods[] = {
	{
		.ml_name = "load_key",
		.ml_meth = (PyCFunction)py_zfs_enc_load_key,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_enc_load_key__doc__
	},
	{
		.ml_name = "unload_key",
		.ml_meth = (PyCFunction)py_zfs_enc_unload_key,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_enc_unload_key__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSEncrypt = {
	.tp_name = PYLIBZFS_MODULE_NAME ".ZFSEncrypt",
	.tp_basicsize = sizeof (py_zfs_enc_t),
	.tp_methods = zfs_enc_methods,
	.tp_getset = zfs_enc_getsetters,
	.tp_new = py_zfs_enc_new,
	.tp_init = py_zfs_enc_init,
	.tp_doc = "ZFSEncrypt",
	.tp_dealloc = (destructor)py_zfs_enc_dealloc,
	.tp_repr = py_repr_zfs_enc,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};

/* initialize an object to control ZFS encryption settings */
PyObject *init_zfs_enc(zfs_type_t type, PyObject *rsrc)
{
	py_zfs_enc_t *out = NULL;
	PYZFS_ASSERT(rsrc, "volume or dataset is missing");

	out = (py_zfs_enc_t *)PyObject_CallFunction((PyObject *)&ZFSEncrypt, NULL);
	if (out == NULL) {
		return NULL;
	}

	switch(type) {
	case ZFS_TYPE_FILESYSTEM:
		out->rsrc_obj.ds = (py_zfs_dataset_t *)rsrc;
		if (out->rsrc_obj.ds->rsrc.is_simple)
			py_zfs_props_refresh(&out->rsrc_obj.ds->rsrc);

		break;
	case ZFS_TYPE_VOLUME:
		out->rsrc_obj.vol = (py_zfs_volume_t *)rsrc;
		if (out->rsrc_obj.vol->rsrc.is_simple)
			py_zfs_props_refresh(&out->rsrc_obj.vol->rsrc);
		break;
	default:
		PyErr_Format(PyExc_TypeError,
			     "%d: unsupported zfs_type_t type",
			     type);
		Py_DECREF(out);
		return NULL;
	};

	out->ctype = type;
	Py_INCREF(rsrc);
	return (PyObject *)out;
}
