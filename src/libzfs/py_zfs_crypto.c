#include "../truenas_pylibzfs.h"

#define ZFS_ENC_STR "<" PYLIBZFS_MODULE_NAME \
    ".ZFSCrypto(name=%U, pool=%U, type=%U)>"

#define PBKDF2_MIN_ITERS 1300000  // Based on owasp guidelines
#define ZFS_URI_PREFIX_FILE "file://"
#define ZFS_URI_PREFIX_HTTPS "https://"

// Min length and max length are defined in libzfs_crypto.c in libzfs
#define MIN_PASSPHRASE_LEN 8
#define MAX_PASSPHRASE_LEN 512

PyDoc_STRVAR(py_zfs_crypto_encroot__doc__,
"The encryption_root indicates the name of the ZFS resource from which the\n"
"this ZFS resource inherits its encryption key. Loading or unloading the\n"
"key for athe encyption_root will implicitly load or unload the key from\n"
"all inheriting datasets. See manpage for zfs-load-key(8).\n"
);

PyDoc_STRVAR(py_zfs_crypto_keylocation__doc__,
"Default location from which the ZFS encrpytion key will be loaded if the\n"
"ZFS resource is mounted with \"load_encyption_key=True\" or through the\n"
"\"load_key\" method if a key or alternative keylocation is not provided.\n"
"This field is only populated when the resource is an encyption_root.\n"
);

PyDoc_STRVAR(py_zfs_crypto_keystatus__doc__,
"Indicates if an encryption key is currently loaded into ZFS for this\n"
"ZFS resource. If the ZFS keystatus property for this resource is\n"
"\"available\" the value will be True, otherwise it will be False.\n"
);

PyStructSequence_Field struct_zfs_crypto_info [] = {
	{"is_root", "ZFS Resource is an encryption root."},
	{"encryption_root", py_zfs_crypto_encroot__doc__},
	{"key_location", py_zfs_crypto_keylocation__doc__},
	{"key_is_loaded", py_zfs_crypto_keystatus__doc__},
	{0},
};

PyStructSequence_Desc struct_zfs_crypto_info_desc = {
        .name = PYLIBZFS_MODULE_NAME ".struct_zfs_crypto_info",
        .fields = struct_zfs_crypto_info,
        .doc = "Python ZFS cryptography information.",
        .n_in_sequence = 4
};

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

/*
 * @brief common method to get basic crypto properties
 *
 * This function retrieves ZFS crypto properties from a ZFS object.
 *
 * @param[in]	obj - pointer to valid py_zfs_obj_t object.
 * @param[in]	encroot - pointer to buffer to hold ZFS encryption root.
 * @param[in]	encroot_sz - size of buffer for encryption root.
 * @param[in]	keylocation - pointer to buffer to hold ZFS key location.
 * @param[in]	keylocation_sz - size of buffer for key location.
 * @param[out]	is_encroot_out - whether the handle is an encryption root.
 * @param[out]  key_is_loaded_out - whether an encryption key
 * 		has been loaded for the handle.
 *
 * @return	B_TRUE on success or B_FALSE on failure
 *
 * @note: GIL must be held when calling this function
 *
 * @note: This function assumes prior validation that `obj` is encrypted.
 *
 * @note: On failure an exception will be set.
 */
static
boolean_t zfs_obj_crypto_info(py_zfs_obj_t *obj,
			      char *encroot,
			      size_t encroot_sz,
			      char *keylocation,
			      size_t keylocation_sz,
			      boolean_t *is_encroot_out,
			      boolean_t *key_is_loaded_out)
{
	boolean_t is_encroot;
	uint64_t keystatus, encrypt;
	py_zfs_error_t zfs_err;
	int err = 0;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);

	/*
	 * Deliberately avoid using zfs_crypto_get_encryption_root
	 * because it uses an unsafe strcpy
	 */
	encrypt = zfs_prop_get_int(obj->zhp, ZFS_PROP_ENCRYPTION);
	err = zfs_prop_get(obj->zhp,
			   ZFS_PROP_ENCRYPTION_ROOT,
			   encroot,
			   encroot_sz,
			   NULL, NULL, 0, B_TRUE);
	if (err) {
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	} else {
		is_encroot = strcmp(encroot, zfs_get_name(obj->zhp)) == 0;
		keystatus = zfs_prop_get_int(obj->zhp, ZFS_PROP_KEYSTATUS);

		if (is_encroot) {
			err = zfs_prop_get(obj->zhp,
					   ZFS_PROP_KEYLOCATION,
					   keylocation,
					   keylocation_sz,
					   NULL, NULL, 0, B_TRUE);

			if (err) {
				py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
			}
		} else {
			// follow libzfs behavior and only report keylocation
			// of encryption roots.
			*keylocation = '\0';
		}
	}

	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	PYZFS_ASSERT((encrypt != ZIO_CRYPT_OFF), "Encryption unexpectedly disabled");

	if (err) {
		set_exc_from_libzfs(&zfs_err, "Failed to get crypto information.");
		return B_FALSE;
	}

	*is_encroot_out = is_encroot;
	*key_is_loaded_out = keystatus == ZFS_KEYSTATUS_AVAILABLE;

	return B_TRUE;
}

static
PyObject *py_zfs_crypto_info_struct(py_zfs_obj_t *obj)
{
	PyObject *out = NULL;
	pylibzfs_state_t *state = py_get_module_state(obj->pylibzfsp);
	char keylocation[ZFS_MAXPROPLEN];
	char encroot[ZFS_MAXPROPLEN];
	boolean_t is_encroot, is_loaded;
	PyObject *pykeyloc, *pyencroot;

	if (!zfs_obj_crypto_info(obj,
				 encroot, sizeof(encroot),
				 keylocation, sizeof(keylocation),
				 &is_encroot, &is_loaded)) {

		return NULL;
	}

	out = PyStructSequence_New(state->struct_zfs_crytpo_info_type);
	if (out == NULL)
		return NULL;

	pyencroot = PyUnicode_FromString(encroot);
	if (pyencroot == NULL) {
		Py_DECREF(out);
		return NULL;
	}

	PyStructSequence_SET_ITEM(out, 1, pyencroot);

	if (is_encroot) {
		PyStructSequence_SET_ITEM(out, 0, Py_NewRef(Py_True));
		pykeyloc = PyUnicode_FromString(keylocation);
		if (pykeyloc == NULL) {
			Py_DECREF(out);
			return NULL;
		}
		PyStructSequence_SET_ITEM(out, 2, pykeyloc);
	} else {
		PyStructSequence_SET_ITEM(out, 0, Py_NewRef(Py_False));
		PyStructSequence_SET_ITEM(out, 2, Py_NewRef(Py_None));

	}

	PyStructSequence_SET_ITEM(out, 3, PyBool_FromLong(is_loaded));

	return out;
}

/* Convert our info struct to a dictionary */
PyObject *py_zfs_crypto_info_dict(py_zfs_obj_t *obj)
{
	PyObject *out = NULL;
	PyObject *info_struct = NULL;
	uint64_t encrypt;
	int idx;

	/* Do not require caller to pre-check encryption status */
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);
	encrypt = zfs_prop_get_int(obj->zhp, ZFS_PROP_ENCRYPTION);
	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (encrypt == ZIO_CRYPT_OFF)
		Py_RETURN_NONE;

	info_struct = py_zfs_crypto_info_struct(obj);
	if (info_struct == NULL)
		return NULL;

	out = PyDict_New();
	if (out == NULL) {
		Py_DECREF(info_struct);
		return NULL;
	}

	for (idx = 0; idx < struct_zfs_crypto_info_desc.n_in_sequence; idx++) {
		const char *key = struct_zfs_crypto_info[idx].name;
		PyObject *val = PyStructSequence_GET_ITEM(info_struct, idx);

		if (PyDict_SetItemString(out, key, val) == -1) {
			Py_DECREF(out);
			Py_DECREF(info_struct);
			return NULL;
		}
	}

	Py_DECREF(info_struct);
	return out;
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
"Load the encrpytion key for the ZFS filesystem (dataset or zvol). This\n"
"allows it and all children that inherit the \"keylocation\" property to be\n"
"accessed. Loading a key will not automatically mount a dataset.\n"
""
"Parameters\n"
"----------\n"
"key: str, optional, default=None\n"
"    Optional parameter to specify the password or key to use\n"
"    to unlock the ZFS resource. This is required if the ZFS\n"
"    resource (dataset or zvol) has the keylocation set to \"prompt\".\n"
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
"Unload the encryption key from ZFS, removing the ability to access the\n"
"resource (dataset or zvol) and al of its children that inherit the\n"
"\"keylocation\" property. This requires that the dataset is not currently\n"
"open or mounted.\n\n"
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

PyDoc_STRVAR(py_zfs_enc_info__doc__,
"info() -> truenas_pylibzfs.struct_zfs_crypto_info\n"
"-------------------------------------------------\n\n"
"Get a truenas_pylibzfs.struct_zfs_crypto_info object containing basic\n"
"encryption-related properties of the underlying ZFS object.\n\n"
""
"Parameters\n"
"----------\n"
"    None\n\n"
"Returns\n"
"-------\n"
"    New truenas_pylibzfs.struct_zfs_crypto_info object\n\n"
"Raises:\n"
"-------\n"
"ZFSError:\n"
"    Failure to read ZFS properties.\n"
);
PyObject *py_zfs_enc_info(PyObject *self, PyObject *args_unused)
{
	py_zfs_obj_t *obj = py_enc_get_zfs_obj(((py_zfs_enc_t *)self));

	return py_zfs_crypto_info_struct(obj);
}

PyDoc_STRVAR(py_zfs_enc_inherit_key__doc__,
"inherit_key() -> None\n"
"---------------------\n\n"
"Inherit the encryption key of an encrypted parent dataset.\n\n"
""
"Parameters\n"
"----------\n"
"    None\n\n"
"Returns\n"
"-------\n"
"    None\n\n"
"Raises:\n"
"-------\n"
"ValueError:\n"
"    The underlying ZFS dataset or volume is not an encryption root.\n\n"
"ValueError:\n"
"    The underlying ZFS dataset is currently locked.\n\n"
"ZFSError:\n"
"    Operation to inherit the encryption key failed. This may\n"
"    happen for a variety of reasons and is explained further in the\n"
"    error information contained in the exception.\n"
);
PyObject *py_zfs_enc_inherit_key(PyObject *self, PyObject *args_unused)
{
	py_zfs_obj_t *obj = py_enc_get_zfs_obj(((py_zfs_enc_t *)self));
	char keylocation[ZFS_MAXPROPLEN];
	char encroot[ZFS_MAXPROPLEN];
	boolean_t is_encroot, is_loaded;
	py_zfs_error_t zfs_err;
	int err;

	if (!zfs_obj_crypto_info(obj,
				 encroot, sizeof(encroot),
				 keylocation, sizeof(keylocation),
				 &is_encroot, &is_loaded)) {
		return NULL;
	}

	if (!is_encroot) {
		PyErr_SetString(PyExc_ValueError,
				"This operation is only valid for ZFS "
				"resources that are an encryption root.");
		return NULL;
	} else if (!is_loaded) {
		PyErr_SetString(PyExc_ValueError,
				"Encryption key must be loaded for "
				"ZFS resource before changing its encryption "
				"settings.");
		return NULL;
	}
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);
	err = zfs_crypto_rewrap(obj->zhp, NULL, B_TRUE);
	if (err) {
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_inherit_key() failed");
		return NULL;
	}

	err = py_log_history_fmt(obj->pylibzfsp,
				 "zfs change-key -i %s",
				 zfs_get_name(obj->zhp));
	if (err)
		return NULL;


	Py_RETURN_NONE;
}

static
nvlist_t *get_change_key_params(const char *key_location_uri,
				zfs_keyformat_t key_format,
				uint64_t pbkdf2_iters)
{
	nvlist_t *out = fnvlist_alloc();

	fnvlist_add_string(out, zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
	    key_location_uri);

	fnvlist_add_uint64(out, zfs_prop_to_name(ZFS_PROP_KEYFORMAT),
	    key_format);

	if (pbkdf2_iters)
		fnvlist_add_uint64(out, zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS),
		    pbkdf2_iters);

	return out;
}

static
boolean_t parse_key_format(const char *key_format, zfs_keyformat_t *format_out)
{
	if (strcmp(key_format, "raw") == 0)
		*format_out = ZFS_KEYFORMAT_RAW;
	else if (strcmp(key_format, "hex") == 0)
		*format_out = ZFS_KEYFORMAT_HEX;
	else if (strcmp(key_format, "passphrase") == 0)
		*format_out = ZFS_KEYFORMAT_PASSPHRASE;
	else {
		PyErr_Format(PyExc_ValueError,
			     "%s: not a valid key format. Choices are: "
			     "\"raw\", \"hex\", and \"passphrase\".");
		return B_FALSE;
	}

	return B_TRUE;
}

static
boolean_t py_zfs_crypto_rewrap_key(py_zfs_obj_t *obj,
				   zfs_keyformat_t key_format,
				   PyObject *key_in,
				   uint64_t pbkdf2_iters)
{
	FILE *keyfile = NULL;
	const char *key = NULL;
	char pbuf[42] = { 0 };  // "file://" + "/proc/self/fd/" + strlen(2^64) + \0
	boolean_t success = B_FALSE;
	nvlist_t *props = NULL;
	py_zfs_error_t zfs_err;
	int err = 0;

	// We have already passed through type validation
	if (key_format == ZFS_KEYFORMAT_RAW) {
		key = PyBytes_AsString(key_in);
	} else {
		key = PyUnicode_AsUTF8(key_in);
	}

	// An error here is unexpected and so we need to pass exception up to
	// caller.
	if (key == NULL)
		// Python has already set exception
		return B_FALSE;

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

		props = get_change_key_params(pbuf, key_format, pbkdf2_iters);

		PY_ZFS_LOCK(obj->pylibzfsp);
		err = zfs_crypto_rewrap(obj->zhp, props, B_FALSE);
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

	fnvlist_free(props);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "Failed to rewrap crypto key.");
		return B_FALSE;
	}

	/* File op failed and so errno should be set */
	if (!success) {
		PyErr_Format(PyExc_RuntimeError,
			     "Failed to load key into memory: %s",
			     strerror(errno));
		return B_FALSE;
	}

	return B_TRUE;
}

static
boolean_t py_zfs_crypto_rewrap_loc(py_zfs_obj_t *obj,
				   const char *key_location_uri,
				   zfs_keyformat_t key_format,
				   uint64_t iters)
{
	nvlist_t *props = NULL;
	py_zfs_error_t zfs_err;
	int err;

	Py_BEGIN_ALLOW_THREADS
	// fnvlist API cannot fail to allocate
	props = get_change_key_params(key_location_uri, key_format, iters);

	PY_ZFS_LOCK(obj->pylibzfsp);
	err = zfs_crypto_rewrap(obj->zhp, props, B_FALSE);
	if (err) {
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	} else {
		zfs_refresh_properties(obj->zhp);
	}
	PY_ZFS_UNLOCK(obj->pylibzfsp);

	fnvlist_free(props);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "Failed to rewrap crypto key.");
		return B_FALSE;
	}

	return B_TRUE;
}

static
boolean_t py_zfs_crypto_rewrap(py_zfs_obj_t *obj,
			       const char *key_loc_uri,
			       zfs_keyformat_t key_format,
			       PyObject *key,
			       uint64_t iters)
{
	if (key != NULL)
		return py_zfs_crypto_rewrap_key(obj, key_format, key, iters);

	return py_zfs_crypto_rewrap_loc(obj, key_loc_uri, key_format, iters);
}

PyDoc_STRVAR(py_zfs_enc_change_key__doc__,
"change_key(*, key_format=None, key_location_uri=None,\n"
"           pbkdf2_iters=1300000, key=None) -> None\n"
"---------------------------------------------------\n\n"
"Change the encryption key for the ZFS resource (dataset or zvol). This\n"
"will establish the resource as an encryption root if it is not already one.\n"
"See Encryption section of man (5) zfs-load-key for more information.\n\n"
""
"Parameters\n"
"----------\n"
"key_location: str, optional, default=None\n"
"    Optional parameter to specify the location in which key material\n"
"    may be found. This may be a local file or a path served over https.\n\n"
"key: str, optional, default=None\n"
"    Optional parameter to specify the password or key to use\n"
"    to unlock the ZFS resource. This is required if the ZFS\n"
"    resource (dataset or zvol) has the keylocation set to \"prompt\".\n"
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
PyObject *py_zfs_enc_change_key(PyObject *self,
				PyObject *args_unused,
				PyObject *kwargs)
{
	py_zfs_obj_t *obj = py_enc_get_zfs_obj(((py_zfs_enc_t *)self));
	char keylocation[ZFS_MAXPROPLEN];
	char encroot[ZFS_MAXPROPLEN];
	boolean_t is_encroot, is_loaded;
	int err;

	const char *key_format_str = NULL;
	const char *key_location_uri = NULL;
	zfs_keyformat_t key_format = ZFS_KEYFORMAT_NONE;
	PyObject *key = NULL;
	uint64_t iters = PBKDF2_MIN_ITERS;

	char *kwnames [] = {
		"key_format",
		"key_location_uri",
		"pbkdf2_iters",
		"key",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
                                         "|$sskO",
                                         kwnames,
					 &key_format_str,
					 &key_location_uri,
					 &iters,
					 &key)) {
		return NULL;
	}

	if ((key_location_uri == NULL) && (key == NULL)) {
		PyErr_SetString(PyExc_ValueError,
				"Either a key location URI or an encryption "
				"key material is required.");
		return NULL;
	}

	if (key_format_str && !parse_key_format(key_format_str, &key_format))
		return NULL;

	if (key_location_uri != NULL) {
		if (key != NULL) {
			PyErr_SetString(PyExc_ValueError,
					"Encryption key location URI and "
					"encryption key material material may "
					"not be specified at the same time.");
			return NULL;
		}

		// Check that key_location is prefixed by file:// or https://
		// Technically, libzfs supports reading key material over http,
		// but I don't consider this a reasonable feature to expose
		if ((strncmp(key_location_uri, ZFS_URI_PREFIX_FILE,
		     sizeof(ZFS_URI_PREFIX_FILE)) != 0) &&
		    (strncmp(key_location_uri, ZFS_URI_PREFIX_HTTPS,
		     sizeof(ZFS_URI_PREFIX_HTTPS)) != 0)) {
			PyErr_SetString(PyExc_ValueError,
					"Encryption key location URI must "
					"be prefixed with either file:// or "
					"https://");
			return NULL;
		}
	} else {
		switch (key_format) {
		case ZFS_KEYFORMAT_RAW:
			if (!PyBytes_Check(key)) {
				PyErr_SetString(PyExc_TypeError,
						"Raw key material must be "
						"presented as a bytes object.");
				return NULL;
			}
			break;
		case ZFS_KEYFORMAT_HEX: {
			PyObject *tmp = NULL;

			tmp = PyLong_FromUnicodeObject(key, 16);
			if (tmp == NULL) {
				// Clear generic exception and set something
				//more specific
				PyErr_Clear();
				PyErr_SetString(PyExc_TypeError,
						"A valid hex string must be "
						"provided when changing the "
						"ZFS crypto key with the "
						"\"hex\" key format.");
				return NULL;
			}
			Py_DECREF(tmp);
			};
			break;
		case ZFS_KEYFORMAT_PASSPHRASE: {
			Py_ssize_t len;
			if (!PyUnicode_Check(key)) {
				PyErr_SetString(PyExc_TypeError,
						"Passphrase must be a valid "
						"unicode string.");
				return NULL;
			}

			len = PyObject_Length(key);
			if (len == -1)
				// Python error. Exception is set.
				return NULL;

			if (len < MIN_PASSPHRASE_LEN) {
				PyErr_Format(PyExc_ValueError,
					     "Passphrase must contain at minimum "
					     "%d characters.", MIN_PASSPHRASE_LEN);
				return NULL;
			} else if (len > MAX_PASSPHRASE_LEN) {
				PyErr_Format(PyExc_ValueError,
					     "Passphrase must contain at maximum "
					     "%d characters.", MAX_PASSPHRASE_LEN);
				return NULL;
			}

			if (iters < PBKDF2_MIN_ITERS) {
				PyErr_Format(PyExc_ValueError,
					     "Number of pbdkf2 iterations must exceed %d.",
					     PBKDF2_MIN_ITERS);
				return NULL;
			}
			};
			break;
		default:
			PyErr_SetString(PyExc_ValueError,
					"The ZFS key format must be specified when "
					"setting a new ZFS crypyto key.");
			return NULL;
		};
	}

	if (!zfs_obj_crypto_info(obj,
				 encroot, sizeof(encroot),
				 keylocation, sizeof(keylocation),
				 &is_encroot, &is_loaded)) {
		return NULL;
	}

	if (!is_encroot && (key_format == ZFS_KEYFORMAT_NONE)) {
		PyErr_SetString(PyExc_ValueError,
				"Key format is required for new encryption "
				"root.");
		return NULL;
	}

	if (!is_loaded) {
		PyErr_SetString(PyExc_ValueError,
				"Encryption key must be loaded for "
				"ZFS resource before changing its encryption "
				"settings.");
		return NULL;
	}

	// Perform the ZFS operation
	if (!py_zfs_crypto_rewrap(obj, key_location_uri, key_format, key, iters))
		return NULL;

	if (key_location_uri != NULL) {
		err = py_log_history_fmt(obj->pylibzfsp,
					 "zfs change-key %s "
					 "keylocation=%s, "
					 "keyformat=%s",
					 zfs_get_name(obj->zhp),
					 key_location_uri,
					 key_format_str);
	} else {
		err = py_log_history_fmt(obj->pylibzfsp,
					 "zfs change-key %s "
					 "keylocation=prompt, "
					 "keyformat=%s",
					 zfs_get_name(obj->zhp),
					 key_format_str);
	}
	if (err)
		return NULL;


	Py_RETURN_NONE;
}


static
PyGetSetDef zfs_enc_getsetters[] = {
	{ .name = NULL }
};

static
PyMethodDef zfs_enc_methods[] = {
	{
		.ml_name = "info",
		.ml_meth = (PyCFunction)py_zfs_enc_info,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_enc_info__doc__
	},
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
	{
		.ml_name = "inherit_key",
		.ml_meth = (PyCFunction)py_zfs_enc_inherit_key,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_enc_inherit_key__doc__
	},
	{
		.ml_name = "change_key",
		.ml_meth = (PyCFunction)py_zfs_enc_change_key,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_enc_change_key__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyDoc_STRVAR(py_zfs_crypto__doc__,
"This provides methods to manipulate the crytpography settings of a\n"
"ZFS resource such as viewing status, loading / unloading keys, and changing\n"
"keys.\n\n"
"NOTE: ZFS encrypts file and volume data, file attributes, ACLs, permission\n"
"bits, directory listings, FUID mappings, and userused/groupused data.  ZFS\n"
"does not encrypt metadata related to the pool structure, including dataset\n"
"and snapshot names, dataset hierarchy, properties, file size, file holes,\n"
"and deduplication tables (though the deduplicated data itself is encrypted).\n"
"\n"
"For more information see manpages for zfsprops(7), zfs-load-key(8), and\n"
"zfs-unload-key(8).\n" 
);
PyTypeObject ZFSCrypto = {
	.tp_name = PYLIBZFS_MODULE_NAME ".ZFSCrypto",
	.tp_basicsize = sizeof (py_zfs_enc_t),
	.tp_methods = zfs_enc_methods,
	.tp_getset = zfs_enc_getsetters,
	.tp_new = py_zfs_enc_new,
	.tp_init = py_zfs_enc_init,
	.tp_doc = py_zfs_crypto__doc__,
	.tp_dealloc = (destructor)py_zfs_enc_dealloc,
	.tp_repr = py_repr_zfs_enc,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};

/* initialize an object to control ZFS encryption settings */
PyObject *init_zfs_crypto(zfs_type_t type, PyObject *rsrc)
{
	py_zfs_enc_t *out = NULL;
	PYZFS_ASSERT(rsrc, "volume or dataset is missing");

	out = (py_zfs_enc_t *)PyObject_CallFunction((PyObject *)&ZFSCrypto, NULL);
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

void module_init_zfs_crypto(PyObject *module)
{
	pylibzfs_state_t *state = NULL;
	PyTypeObject *obj;

	state = (pylibzfs_state_t *)PyModule_GetState(module);
	PYZFS_ASSERT(state, "Failed to get module state.");

	obj = PyStructSequence_NewType(&struct_zfs_crypto_info_desc);
	PYZFS_ASSERT(obj, "Failed to allocate struct_zfs_prop_type");

	state->struct_zfs_crytpo_info_type = obj;
}
