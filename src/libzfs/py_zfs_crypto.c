#include "truenas_pylibzfs.h"

#define ZFS_ENC_STR "<" PYLIBZFS_MODULE_NAME \
    ".ZFSCrypto(name=%U, pool=%U, type=%U)>"

#define PBKDF2_MIN_ITERS 1300000  // Based on owasp guidelines
#define PBKDF2_MIN_ITERS_STR "1300000"
#define ZFS_URI_PREFIX_FILE "file://"
#define ZFS_URI_PREFIX_HTTPS "https://"

// Min length and max length are defined in libzfs_crypto.c in libzfs
#define MIN_PASSPHRASE_LEN 8
#define MAX_PASSPHRASE_LEN 512
#define WRAPPING_KEY_LEN 32  // defined in zio_crypt.h
#define NULL_OR_NONE(x) ((x == NULL) || (x == Py_None))

PyDoc_STRVAR(py_zfs_crypto_encroot__doc__,
"The encryption_root shows the name of the ZFS resource that this resource\n"
"inherits its encryption key from. When you load or unload the key for the\n"
"encryption_root, the system also loads or unloads the key for all datasets\n"
"that inherit from it.\n"
"For more information, see the zfs-load-key(8) man page.\n"
);

PyDoc_STRVAR(py_zfs_crypto_keylocation__doc__,
"This is the default location from which the ZFS encryption key is loaded.\n"
"The library uses this location if the ZFS resource is mounted with\n"
"load_encryption_key=True or if the load_key method is used and no key or \n"
"alternative key location is given.\n\n"
"Valid keylocation values include:\n"
"* Absolute paths in the local file system, starting with file://\n"
"* HTTPS URLs (fetched using fetch(3) from libcurl)\n"
"* The word 'prompt', which tells the server to ask the user for the key\n\n"
"NOTE: This field is valid only if the resource is an encryption root.\n"
);

PyDoc_STRVAR(py_zfs_crypto_keystatus__doc__,
"This shows if an encryption key is currently loaded into ZFS for this resource.\n"
"If the keystatus property of the resource is available, the value is True.\n"
"If not, the value is False.\n"
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

PyDoc_STRVAR(py_zfs_crypto_keyformat__doc__,
"This setting controls the format in which the user provides the encryption key.\n"
"Valid keyformat values include: \"raw\", \"hex\", and \"passphrase\".\n"
"* \"raw\" and \"hex\" keys must be 32 bytes long, no matter which encryption suite\n"
"  is used. These keys must be randomly generated.\n"
"* \"passphrase\" must be between 8 and 512 bytes long. The library processes them\n"
"  through PBKDF2 before using them (see the \"pbkdf2iters\" setting).\n\n"
);

PyDoc_STRVAR(py_zfs_crypto_keylocation_uri__doc__,
"This is the default location from which the ZFS encryption key is loaded.\n"
"The library uses this location if the ZFS resource is mounted with\n"
"load_encryption_key=True or if the load_key method is used and no key or \n"
"alternative key location is given.\n\n"
"Valid keylocation values include:\n"
"* Absolute paths in the local file system, starting with file://\n"
"* HTTPS URLs (fetched using fetch(3) from libcurl)\n"
"* None - this ZFS will be ask you for a password when unlocking the resource.\n"
"NOTE: If \"keylocation\" is None, you must provide the encryption key or password\n"
"in the cryptography configuration payload.\n"
);

PyDoc_STRVAR(py_zfs_crypto_key__doc__,
"This sets the password or key used to unlock the ZFS resource.\n"
"This is valid only if the \"keylocation\" is set to None.\n"
);

PyDoc_STRVAR(py_zfs_crypto_pbkdf2_iters__doc__,
"This setting controls the number of PBKDF2 iterations used to turn a passphrase\n"
"into an encryption key. The minimum allowed value is " PBKDF2_MIN_ITERS_STR ".\n"
"Valid pbkdf2iters values include:\n"
"* An integer greater than or equal to the minimum allowed value.\n"
"* The value None. If set to None, the library uses the minimum allowed value.\n"
"NOTE: This setting is valid only if the \"passphrase\" \"keyformat\" is selected.\n"
);

PyDoc_STRVAR(py_zfs_crypto_change__doc__,
"This data structure is used to define (on resource creation) or change resource\n"
"encryption settings. ZFS can either prompt the user for the key or retrieve it from\n"
"a specified key location.\n"
"* If \"key_location_uri\" is set to None, ZFS prompts the user for the key. In \n"
"  this case, the \"key\" field is required.\n"
"* If \"key_location_uri\" is not set to None, then the \"key\" field must be None.\n"
);
PyStructSequence_Field struct_zfs_crypto_change [] = {
	{"keyformat", py_zfs_crypto_keyformat__doc__},
	{"keylocation", py_zfs_crypto_keylocation_uri__doc__},
	{"key", py_zfs_crypto_key__doc__},
	{"pbkdf2iters", py_zfs_crypto_pbkdf2_iters__doc__},
	{0},
};

PyStructSequence_Desc struct_zfs_crypto_change_desc = {
        .name = PYLIBZFS_MODULE_NAME ".struct_zfs_crypto_config",
        .fields = struct_zfs_crypto_change,
        .doc = py_zfs_crypto_change__doc__,
        .n_in_sequence = 4
};

typedef struct {
	zfs_keyformat_t format;
	const char *format_str;
	const char *key_location_uri;
	char key[MAX_PASSPHRASE_LEN + 1];  // crypto key if keylocation is prompt
	Py_ssize_t key_len;
	uint64_t iters;  // pbkdf2 iters (for passphrase keyformat)
} zfs_crypto_change_info_t;

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
boolean_t validate_keylocation(PyObject *py_keyloc, zfs_crypto_change_info_t *info)
{
	const char *key_location_uri;

	key_location_uri = PyUnicode_AsUTF8(py_keyloc);
	if (key_location_uri == NULL)
		return B_FALSE;

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
		return B_FALSE;
	}

	info->key_location_uri = key_location_uri;
	return B_TRUE;
}

static
boolean_t validate_keyformat(PyObject *py_keyformat,
			     PyObject *py_iters,
			     zfs_crypto_change_info_t *info)
{

	Py_ssize_t iters;
	zfs_keyformat_t format;
	const char *keyformat_str;

        if (NULL_OR_NONE(py_keyformat)) {
		PyErr_SetString(PyExc_ValueError,
				"keyformat is required.");
		return B_FALSE;
	}

	if (!PyUnicode_Check(py_keyformat)) {
		PyErr_SetString(PyExc_TypeError,
				"keyformat must be one of \"raw\", \"hex\", "
				"or \"passphrase\".");
		return B_FALSE;
	}

	keyformat_str = PyUnicode_AsUTF8(py_keyformat);
	if (keyformat_str == NULL)
		return B_FALSE;

	if (!parse_key_format(keyformat_str, &format))
		return B_FALSE;

	if ((format != ZFS_KEYFORMAT_PASSPHRASE) || NULL_OR_NONE(py_iters)) {
		info->format = format;
		info->format_str = keyformat_str;
		if (format != ZFS_KEYFORMAT_PASSPHRASE) {
			// ensure that iterations are omitted if not passphrase
			info->iters = 0;
		}
		return B_TRUE;
	}

	// passphase with iters specified. Need to make sure it's sane
	if (!PyLong_Check(py_iters)) {
		PyErr_SetString(PyExc_ValueError,
				"Number of pbkdf2 iterations is required.");
		return B_FALSE;
	}

	iters = PyLong_AsSsize_t(py_iters);
	if ((iters == -1) && PyErr_Occurred())
		return B_FALSE;

	if (iters < PBKDF2_MIN_ITERS) {
		PyErr_Format(PyExc_ValueError,
			     "Number of pbkdf2 iterations must exceed %d.",
			     PBKDF2_MIN_ITERS);
		return B_FALSE;
	}

	info->iters = iters;
	info->format = format;
	info->format_str = keyformat_str;

	return B_TRUE;
}

/* Validate key material and set it in the provided `info` */
static
boolean_t validate_key_material(PyObject *py_key, zfs_crypto_change_info_t *info)
{
	const char *key = NULL;
	PyObject *tmp;

	PYZFS_ASSERT((info->format < ZFS_KEYFORMAT_FORMATS), "Invalid keyformat");

	switch (info->format) {
	case ZFS_KEYFORMAT_RAW:
		char *raw_bytes = NULL;

		if (!PyBytes_Check(py_key)) {
			PyErr_SetString(PyExc_TypeError,
					"Raw key material must be "
					"presented as a bytes object.");
			return B_FALSE;
		}
		if (PyBytes_AsStringAndSize(py_key, &raw_bytes, &info->key_len))
			return B_FALSE;

		if (info->key_len != WRAPPING_KEY_LEN) {
			PyErr_Format(PyExc_ValueError,
				     "The raw key must be %d bytes long.",
				     WRAPPING_KEY_LEN);
		}

		// Raw bytes may theoretically contain embedded null
		memcpy(info->key, raw_bytes, WRAPPING_KEY_LEN);
		break;
	case ZFS_KEYFORMAT_HEX:
		tmp = PyLong_FromUnicodeObject(py_key, 16);
		if (tmp == NULL) {
			// Clear generic exception and set something
			//more specific
			PyErr_Clear();
			PyErr_SetString(PyExc_TypeError,
					"You must provide a valid hex string "
					"when the ZFS key format is set to hex.");
			return B_FALSE;
		}

		Py_DECREF(tmp);
		key = PyUnicode_AsUTF8AndSize(py_key, &info->key_len);
		if (key == NULL)
			return B_FALSE;

		if (info->key_len != WRAPPING_KEY_LEN * 2) {
			PyErr_Format(PyExc_ValueError,
				     "The hex key must be %d characters long.",
				     WRAPPING_KEY_LEN * 2);
		}
		strlcpy(info->key, key, sizeof(info->key));
		break;
	case ZFS_KEYFORMAT_PASSPHRASE:
		if (!PyUnicode_Check(py_key)) {
			PyErr_SetString(PyExc_TypeError,
					"Passphrase must be a valid "
					"unicode string.");
			return B_FALSE;
		}

		key = PyUnicode_AsUTF8AndSize(py_key, &info->key_len);
		if (key == NULL)
			return B_FALSE;

		if (info->key_len < MIN_PASSPHRASE_LEN) {
			PyErr_Format(PyExc_ValueError,
				     "The passphrase must have at least %d "
				     "characters.", MIN_PASSPHRASE_LEN);
			return B_FALSE;
		} else if (info->key_len > MAX_PASSPHRASE_LEN) {
			PyErr_Format(PyExc_ValueError,
				     "The passphrase must have at most %d "
				     "characters.", MAX_PASSPHRASE_LEN);
			return B_FALSE;
		}
		strlcpy(info->key, key, sizeof(info->key));
		break;
	default:
		PyErr_SetString(PyExc_ValueError,
				"The ZFS key format is required.");
		return B_FALSE;
	}

	return B_TRUE;
}

// Validate the contents of the crypto change object and use it to
// populate the zfs_crypto_change_info_t struct
//
// WARNING: the python obj `obj` MUST NOT be deallocated until all
// use of the zfs_crypto_change_info_t struct is complete.
static
boolean_t py_validate_crypto_change(pylibzfs_state_t *state,
				    PyObject *obj,
				    zfs_crypto_change_info_t *info)
{
	PyObject *py_keyformat, *py_keyloc, *py_key, *py_iters;

	if (!PyObject_TypeCheck(obj, state->struct_zfs_crypto_change_type)) {
		PyErr_SetString(PyExc_TypeError,
				"Expected " PYLIBZFS_MODULE_NAME
				".struct_zfs_crypto_config");
		return B_FALSE;
	}

	py_keyformat = PyStructSequence_GET_ITEM(obj, 0);
	py_keyloc = PyStructSequence_GET_ITEM(obj, 1);
	py_key = PyStructSequence_GET_ITEM(obj, 2);
	py_iters = PyStructSequence_GET_ITEM(obj, 3);

	if (!validate_keyformat(py_keyformat, py_iters, info))
		return B_FALSE;

	if (NULL_OR_NONE(py_keyloc) && NULL_OR_NONE(py_key)){
		PyErr_SetString(PyExc_ValueError,
				"Either a key location URI or an encryption "
				"key material is required.");
		return B_FALSE;
	}

	if (NULL_OR_NONE(py_keyloc)) {
		if (!validate_key_material(py_key, info))
			return B_FALSE;
	} else {
		if (!NULL_OR_NONE(py_key)) {
			PyErr_SetString(PyExc_ValueError,
					"Encryption key location URI and "
					"encryption key material material may "
					"not be specified at the same time.");
			return B_FALSE;
		}
		if (!validate_keylocation(py_keyloc, info))
			return B_FALSE;
	}

	return B_TRUE;
}

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

	out = PyStructSequence_New(state->struct_zfs_crypto_info_type);
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
	int idx;

	if (obj->encrypted == Py_False)
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

// Copy user-provided key into in-memory FILE
static boolean_t write_key_to_memfile(const char *key, size_t keylen,
				      char *pbuf, size_t pbuflen,
				      FILE **keyfile_out)
{
	FILE *keyfile = NULL;
	size_t written;

	keyfile = get_mem_keyfile();
	if (!keyfile)
		return B_FALSE;

	written = fwrite(key, 1, keylen, keyfile);
	if (written != keylen)
		return B_FALSE;

	fflush(keyfile);

	snprintf(pbuf, pbuflen, "file:///proc/self/fd/%d", fileno(keyfile));

	*keyfile_out = keyfile;
	return B_TRUE;
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
	success = write_key_to_memfile(key, strlen(key), pbuf, sizeof(pbuf),
				       &keyfile);
	if (success) {
		// generate a procfd path for libzfs
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
		if (!test || (zfs_err.code != EZFS_CRYPTOFAILED)) {
			set_exc_from_libzfs(&zfs_err, "zfs_load_key() failed");
			return NULL;
		}
		Py_RETURN_FALSE;
	}

	/* File op failed and so errno should be set */
	if (!success) {
		PyErr_Format(PyExc_RuntimeError,
			     "Failed to load key into memory: %s",
			     strerror(errno));
		return NULL;
	}

	Py_RETURN_TRUE;
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
		if (!test || (zfs_err.code != EZFS_CRYPTOFAILED)) {
			set_exc_from_libzfs(&zfs_err, "zfs_load_key() failed");
			return NULL;
		}
		Py_RETURN_FALSE;
	}

	Py_RETURN_TRUE;
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

static
PyObject *py_load_key_common(py_zfs_obj_t *obj,
			     PyObject *args_unused,
			     PyObject *kwargs,
			     boolean_t test)
{
	const char *alt_keylocation = NULL;
	const char *key = NULL;

	char *kwnames [] = {
		"key",
		"key_location",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$ss",
					 kwnames,
					 &key,
					 &alt_keylocation)) {
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

PyDoc_STRVAR(py_zfs_enc_load_key__doc__,
"load_key(*, key=None, key_location=None) -> None\n"
"------------------------------------------------\n\n"
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
	py_load_key_common(obj, args_unused, kwargs, B_FALSE);
	Py_RETURN_NONE;
}


PyDoc_STRVAR(py_zfs_enc_check_key__doc__,
"check_key(*, key=None, key_location=None) -> bool\n"
"-------------------------------------------------\n\n"
"Test whether the provided key marterial can be loaded for the ZFS resource.\n"
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
""
"Returns\n"
"-------\n"
"True if the supplied key material can unlock the resource\n\n"
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
PyObject *py_zfs_enc_check_key(PyObject *self,
			       PyObject *args_unused,
			       PyObject *kwargs)
{
	py_zfs_obj_t *obj = py_enc_get_zfs_obj(((py_zfs_enc_t *)self));
	return py_load_key_common(obj, args_unused, kwargs, B_TRUE);
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
nvlist_t *get_change_key_params(zfs_crypto_change_info_t *info)
{
	nvlist_t *out = fnvlist_alloc();
	const char *keyformat_str = "none";

	fnvlist_add_string(out, zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
	    info->key_location_uri);

	if (info->format_str)
		keyformat_str = info->format_str;

	fnvlist_add_string(out, zfs_prop_to_name(ZFS_PROP_KEYFORMAT),
	    keyformat_str);

	if (info->iters)
		fnvlist_add_uint64(out, zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS),
		    info->iters);

	return out;
}

static
boolean_t py_zfs_crypto_rewrap_key(py_zfs_obj_t *obj,
				   zfs_crypto_change_info_t *info)
{
	FILE *keyfile = NULL;
	char pbuf[42] = { 0 };  // "file://" + "/proc/self/fd/" + strlen(2^64) + \0
	boolean_t success = B_FALSE;
	nvlist_t *props = NULL;
	py_zfs_error_t zfs_err;
	int err = 0;

	Py_BEGIN_ALLOW_THREADS
	// Copy user-provided key into an in-memory FILE
	success = write_key_to_memfile(info->key, info->key_len, pbuf, sizeof(pbuf),
				       &keyfile);
	if (success) {
		info->key_location_uri = pbuf;
		props = get_change_key_params(info);
		info->key_location_uri = NULL;

		PY_ZFS_LOCK(obj->pylibzfsp);
		err = zfs_crypto_rewrap(obj->zhp, props, B_FALSE);
		if (err) {
			py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
		} else {
			// Now we need to reset the keylocation to prompt
			zfs_prop_set(obj->zhp, zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
			    "prompt");
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
				   zfs_crypto_change_info_t *info)
{
	nvlist_t *props = NULL;
	py_zfs_error_t zfs_err;
	int err;

	Py_BEGIN_ALLOW_THREADS
	// fnvlist API cannot fail to allocate
	props = get_change_key_params(info);

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
			       zfs_crypto_change_info_t *info)
{
	if (info->key_len)
		return py_zfs_crypto_rewrap_key(obj, info);

	return py_zfs_crypto_rewrap_loc(obj, info);
}

PyDoc_STRVAR(py_zfs_enc_change_key__doc__,
"change_key(*, info) -> None\n"
"----------------------------\n\n"
"Change the encryption key for the ZFS resource (dataset or zvol). This\n"
"will establish the resource as an encryption root if it is not already one.\n"
"See Encryption section of man (5) zfs-load-key for more information.\n\n"
""
"Parameters\n"
"----------\n"
"info: truenas_libzfs.crypto_change_info\n"
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
);
PyObject *py_zfs_enc_change_key(PyObject *self,
				PyObject *args_unused,
				PyObject *kwargs)
{
	py_zfs_obj_t *obj = py_enc_get_zfs_obj(((py_zfs_enc_t *)self));
	pylibzfs_state_t *state = py_get_module_state(obj->pylibzfsp);
	char keylocation[ZFS_MAXPROPLEN];
	char encroot[ZFS_MAXPROPLEN];
	boolean_t is_encroot, is_loaded;
	int err;

	PyObject *py_info = NULL;
	zfs_crypto_change_info_t info = { .iters = PBKDF2_MIN_ITERS };
	char *kwnames [] = {"info", NULL};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
                                         "|$O",
                                         kwnames,
					 &py_info)) {
		return NULL;
	}

	if (py_info == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"info: keyword argument is required.");
		return NULL;
	}

	if (!py_validate_crypto_change(state, py_info, &info))
		return NULL;

	if (!zfs_obj_crypto_info(obj,
				 encroot, sizeof(encroot),
				 keylocation, sizeof(keylocation),
				 &is_encroot, &is_loaded)) {
		return NULL;
	}

	if (!is_encroot && (info.format == ZFS_KEYFORMAT_NONE)) {
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
	if (!py_zfs_crypto_rewrap(obj, &info))
		return NULL;

	if (info.key_location_uri != NULL) {
		err = py_log_history_fmt(obj->pylibzfsp,
					 "zfs change-key %s "
					 "keylocation=%s, "
					 "keyformat=%s",
					 zfs_get_name(obj->zhp),
					 info.key_location_uri,
					 info.format_str);
	} else {
		err = py_log_history_fmt(obj->pylibzfsp,
					 "zfs change-key %s "
					 "keylocation=prompt, "
					 "keyformat=%s",
					 zfs_get_name(obj->zhp),
					 info.format_str);
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
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_enc_change_key__doc__
	},
	{
		.ml_name = "check_key",
		.ml_meth = (PyCFunction)py_zfs_enc_check_key,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_enc_check_key__doc__
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

static boolean_t pyzfs_zfs_create_crypto_history(py_zfs_t *self,
						 const char *name,
						 nvlist_t *props)
{
	const char *json_str = NULL;
	PyObject *params = NULL;
	int err;

	params = py_dump_nvlist(props, B_TRUE);
	if (params != NULL) {
		json_str = PyUnicode_AsUTF8(params);
	}

	err = py_log_history_fmt(self, "zfs create %s with properties: %s",
				 name, json_str ? json_str : "UNKNOWN");

	return err ? B_FALSE : B_TRUE;
}

static boolean_t pyzfs_create_crypto_key(py_zfs_t *self,
					 const char *name,
					 zfs_type_t ztype,
					 nvlist_t *props,
					 zfs_crypto_change_info_t *info)
{
	boolean_t success;
	nvlist_t *crypto_props = NULL;
	FILE *keyfile = NULL;
	char pbuf[42] = { 0 };  // "file://" + "/proc/self/fd/" + strlen(2^64) + \0
	py_zfs_error_t zfs_err;
	int err;

	Py_BEGIN_ALLOW_THREADS
	// Copy user-provided key into an in-memory FILE
	success = write_key_to_memfile(info->key, info->key_len, pbuf, sizeof(pbuf),
				       &keyfile);
	if (success) {
		zfs_handle_t *tmp_hdl;
		const char *keyprop = zfs_prop_to_name(ZFS_PROP_KEYLOCATION);

		info->key_location_uri = pbuf;
		crypto_props = get_change_key_params(info);
		fnvlist_add_string(crypto_props, zfs_prop_to_name(ZFS_PROP_ENCRYPTION), "on");
		info->key_location_uri = NULL;
		if (props) {
			fnvlist_merge(props, crypto_props);
			fnvlist_free(crypto_props);
		} else {
			props = crypto_props;
		}

		PY_ZFS_LOCK(self);
		err = zfs_create(self->lzh, name, ztype, props);
		if (err) {
			py_get_zfs_error(self->lzh, &zfs_err);
		} else {
			// While lock is held we need to set the keylocation
			// to "prompt"
			tmp_hdl = zfs_open(self->lzh, name, ztype);
			if (tmp_hdl) {
				zfs_prop_set(tmp_hdl, keyprop, "prompt");
				zfs_close(tmp_hdl);
			}
		}

		PY_ZFS_UNLOCK(self);
		fclose(keyfile);
	}
	Py_END_ALLOW_THREADS

	if (!success) {
		// Failed to create the memfile / write key to it so we need
		// to raise a generic python exception. This can't be within
		// else above because we need to retake GIL before generating
		// exception
		PyErr_Format(PyExc_RuntimeError,
			     "Failed to load key into memory: %s",
			     strerror(errno));
		return B_FALSE;
	}

	// If we're here then we made attempt to create the resource
	// and err should reflect result of zfs_create attempt
	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_create() failed");
		return B_FALSE;
	}

	// remove our procfd path from properties for ZFS history commit
	fnvlist_remove(props, zfs_prop_to_name(ZFS_PROP_KEYLOCATION));

	return pyzfs_zfs_create_crypto_history(self, name, props);
}

static boolean_t pyzfs_create_crypto_loc(py_zfs_t *self,
					 const char *name,
					 zfs_type_t ztype,
					 nvlist_t *props,
					 zfs_crypto_change_info_t *info)
{
	nvlist_t *crypto_props = NULL;
	py_zfs_error_t zfs_err;
	int err;

	// convert our crypto properties to an nvlist then
	// merge into the user-provided props then pass to zfs_create
	crypto_props = get_change_key_params(info);
	if (!crypto_props)
		return B_FALSE;

	Py_BEGIN_ALLOW_THREADS
	fnvlist_add_string(crypto_props, zfs_prop_to_name(ZFS_PROP_ENCRYPTION), "on");
	fnvlist_merge(props, crypto_props);
	fnvlist_free(crypto_props);
	PY_ZFS_LOCK(self);
	err = zfs_create(self->lzh, name, ztype, props);
	if (err) {
		py_get_zfs_error(self->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(self);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_create() failed");
		return B_FALSE;
	}

	return pyzfs_zfs_create_crypto_history(self, name, props);
}

boolean_t pyzfs_create_crypto(py_zfs_t *pyzfs,
			      const char *name,
			      zfs_type_t ztype,
			      nvlist_t *props,
			      PyObject *pycrypto)
{
	boolean_t ok;
	pylibzfs_state_t *state = py_get_module_state(pyzfs);
	zfs_crypto_change_info_t info = { .iters = PBKDF2_MIN_ITERS };

	if (!py_validate_crypto_change(state, pycrypto, &info)) {
		fnvlist_free(props);
		return B_FALSE;
	}

	if (info.key_len) {
		ok = pyzfs_create_crypto_key(pyzfs, name, ztype, props, &info);
	} else {
		ok = pyzfs_create_crypto_loc(pyzfs, name, ztype, props, &info);
	}

	fnvlist_free(props);

	return ok;
}

PyObject *generate_crypto_config(py_zfs_t *pyzfs,
				 PyObject *py_keyformat,
				 PyObject *py_keyloc,
				 PyObject *py_key,
				 PyObject *py_iters)
{
	pylibzfs_state_t *state = py_get_module_state(pyzfs);
	PyObject *out = NULL;
	zfs_crypto_change_info_t info = { .iters = PBKDF2_MIN_ITERS };

	out = PyStructSequence_New(state->struct_zfs_crypto_change_type);
	if (!out)
		return NULL;

	PyStructSequence_SET_ITEM(out, 0, py_keyformat);
	PyStructSequence_SET_ITEM(out, 1, py_keyloc);
	PyStructSequence_SET_ITEM(out, 2, py_key);
	PyStructSequence_SET_ITEM(out, 3, py_iters);

	Py_XINCREF(py_keyformat);
	Py_XINCREF(py_keyloc);
	Py_XINCREF(py_key);
	Py_XINCREF(py_iters);

	// Apply our validation routins to user-provided info
	if (!py_validate_crypto_change(state, out, &info)) {
		Py_DECREF(out);
		return NULL;
	}

	return out;
}

void module_init_zfs_crypto(PyObject *module)
{
	pylibzfs_state_t *state = NULL;
	PyTypeObject *obj;

	state = (pylibzfs_state_t *)PyModule_GetState(module);
	PYZFS_ASSERT(state, "Failed to get module state.");

	obj = PyStructSequence_NewType(&struct_zfs_crypto_info_desc);
	PYZFS_ASSERT(obj, "Failed to allocate struct_zfs_crypto_info_type");

	state->struct_zfs_crypto_info_type = obj;

	obj = PyStructSequence_NewType(&struct_zfs_crypto_change_desc);
	PYZFS_ASSERT(obj, "Failed to allocate struct_zfs_crypto_info_type");

	state->struct_zfs_crypto_change_type = obj;
}
