#include "../truenas_pylibzfs.h"

static
PyObject *PyExc_ZFSError;

PyDoc_STRVAR(py_zfs_exception__doc__,
"ZFSException(exception)\n"
"-----------------------\n\n"
"Python wrapper around libzfs errors. A libzfs error will have potentially\n"
"the following information:\n\n"
"libzfs errno:\n"
"    One of the numeric error codes defined in truenas_pylibzfs.ZFSError enum.\n\n"
"libzfs error description:\n"
"    libzfs equivalent of strerror for the libzfs errno.\n\n"
"libzfs error action:\n"
"    brief description of action leading up the error.\n\n"
"attributes:\n"
"-----------\n"
"code: int\n"
"    libzfs errno (one of ZFSError)\n"
"err_str: str\n"
"    human-readable description of what happened\n"
"name: str\n"
"    human-readable name of the libzfs errno\n"
"description: str\n"
"    description returned by libzfs (often of libzfs errno)\n"
"action: str\n"
"    action causing error (as returned by libzfs)\n"
"location: str\n"
"    line of file in uncompiled source of this module\n\n"
"NOTE: the libzfs error may wrap around conventional OS errno. In this case\n"
"it will be mapped to equivalent libzfs errno, but if that's not possible the\n"
"libzfs errno will be set to EZFS_UNKNOWN and strerror output written to\n"
"the error description field.\n"
);
PyObject *setup_zfs_exception(void)
{
	PyObject *dict = NULL;

	dict = Py_BuildValue("{s:i,s:s,s:s,s:s,s:s,s:s}",
			     "code", EZFS_UNKNOWN,
			     "err_str", "",
			     "name", "",
			     "action", "",
			     "description", "",
			     "location", "");
	if (dict == NULL)
		return NULL;

	PyExc_ZFSError = PyErr_NewExceptionWithDoc(PYLIBZFS_MODULE_NAME
						   ".ZFSException",
						   py_zfs_exception__doc__,
						   PyExc_RuntimeError,
						   dict);

	Py_DECREF(dict);
	return PyExc_ZFSError;
}

const char *zfs_error_name(zfs_error_t error)
{
	uint i;

	for (i=0; i < ARRAY_SIZE(zfserr_table); i++) {
		if (error == zfserr_table[i].error) {
			return zfserr_table[i].name;
		}
	}

	return "UNKNOWN";
}

void
py_get_zfs_error(libzfs_handle_t *lz, py_zfs_error_t *out)
{
	const char *description = libzfs_error_description(lz);
	const char *action = libzfs_error_action(lz);
	zfs_error_t code = libzfs_errno(lz);

	out->code = code;
	strlcpy(out->description, description, sizeof(out->description));
	strlcpy(out->action, action, sizeof(out->action));
}

void _set_exc_from_libzfs(py_zfs_error_t *zfs_err,
			  const char *additional_info,
			  const char *location)
{
	PyObject *v = NULL;
	PyObject *args = NULL;
	PyObject *attrs = NULL;
	PyObject *action = NULL;
	PyObject *desc = NULL;
	const char *name = NULL;
	PyObject *errstr = NULL;
	int err;

	name = zfs_error_name(zfs_err->code);

	action = PyUnicode_FromString(zfs_err->action);
	if (action == NULL) {
		goto simple_err;
	}

	desc = PyUnicode_FromString(zfs_err->description);

	// description may actually prompt UnicodeDecodeError due to strings
	// like following:
	// "invalid character '\357' in name"
	// because zfs accidently munges a multibyte character.
	if (desc == NULL) {
		if (zfs_err->code == EZFS_INVALIDNAME) {
			desc = PyUnicode_FromString(
			    "Invalid multibyte character in name"
			);
		} else {
			// Converting via PyUnicode_FromFormat will basically
			// escape the invalid sequence.
			desc = PyUnicode_FromFormat(
			    "Python failed to parse the ZFS error "
			    "description: [%s]. Please report an issue "
			    "against the truenas_pylibzfs repository "
			    "with this message.", zfs_err->description);
		}

		if (desc == NULL) {
			// Pass whatever error is happening back up to user.
			// Something is deeply wrong in python land
			Py_DECREF(action);
			return;
		}

		// We've handled the error condition so clear the PyErr
		PyErr_Clear();
	}

	if (additional_info) {
		errstr = PyUnicode_FromFormat(
			"[%s]: %s - %U: %U",
			name, additional_info,
			action, desc
		);
	} else {
		errstr = Py_BuildValue("[%s]: %O", name, desc);
	}
	if (errstr == NULL) {
		goto simple_err;
	}

	/* Build tuple containing our attributes. This converts to PyObject
	 * NOTE: this steals reference to errstr
	 */
	args = Py_BuildValue("(N)", errstr);
	if (args == NULL) {
		Py_CLEAR(errstr);
		goto simple_err;
	}

	v = PyObject_Call(PyExc_ZFSError, args, NULL);
	if (v == NULL) {
		Py_CLEAR(args);
		return;
	}

	attrs = Py_BuildValue(
		"(iOss)",
		zfs_err->code,
		errstr,
		name,
		location
	);

	if (attrs == NULL) {
		// Failed so we need to decref errstr
		Py_CLEAR(v);
		goto simple_err;
	}

	err = PyObject_SetAttrString(v, "code", PyTuple_GetItem(attrs, 0));
	if (err == -1) {
		goto err_out;
	}

	err = PyObject_SetAttrString(v, "err_str", PyTuple_GetItem(attrs, 1));
	if (err == -1) {
		goto err_out;
	}

	err = PyObject_SetAttrString(v, "name", PyTuple_GetItem(attrs, 2));
	if (err == -1) {
		goto err_out;
	}

	err = PyObject_SetAttrString(v, "action", action);
	if (err == -1) {
		goto err_out;
	}

	err = PyObject_SetAttrString(v, "description", desc);
	if (err == -1) {
		goto err_out;
	}

	err = PyObject_SetAttrString(v, "location", PyTuple_GetItem(attrs, 3));
	Py_CLEAR(args);
	if (err == -1) {
		goto err_out;
	}

	PyErr_SetObject((PyObject *) Py_TYPE(v), v);

err_out:
	Py_XDECREF(action);
	Py_XDECREF(desc);
	Py_XDECREF(v);
	Py_XDECREF(attrs);
	return;

simple_err:
	// Absolute minimum for error format
	Py_XDECREF(action);
	Py_XDECREF(desc);
	PyErr_Format(PyExc_ZFSError, "[%d]: %s", zfs_err->code, zfs_err->description);
	return;

}
