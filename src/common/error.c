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
	const char *name = NULL;
	PyObject *errstr = NULL;
	int err;

	name = zfs_error_name(zfs_err->code);

	if (additional_info) {
		errstr = PyUnicode_FromFormat(
			"[%s]: %s - %s: %s",
			name, additional_info,
			zfs_err->action,
			zfs_err->description
		);
	} else {
		errstr = Py_BuildValue("[%s]: %s", name, zfs_err->description);
	}
	if (errstr == NULL) {
		goto simple_err;
	}

	/* Build tuple containing our attributes. This converts to PyObject
	 * NOTE: this steals reference to errstr
	 */
	args = Py_BuildValue("(N)", errstr);
	if (args == NULL) {
		Py_DECREF(errstr);
		goto simple_err;
	}

	v = PyObject_Call(PyExc_ZFSError, args, NULL);
	if (v == NULL) {
		Py_CLEAR(args);
		return;
	}

	attrs = Py_BuildValue(
		"(iOssss)",
		zfs_err->code,
		errstr,
		name,
		zfs_err->action,
		zfs_err->description,
		location
	);

	if (attrs == NULL) {
		// Failed so we need to decref errstr
		Py_XDECREF(v);
		goto simple_err;
	}

	err = PyObject_SetAttrString(v, "code", PyTuple_GetItem(attrs, 0));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "err_str", PyTuple_GetItem(attrs, 1));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "name", PyTuple_GetItem(attrs, 2));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "action", PyTuple_GetItem(attrs, 3));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "description", PyTuple_GetItem(attrs, 4));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "location", PyTuple_GetItem(attrs, 5));
	Py_CLEAR(args);
	if (err == -1) {
		Py_CLEAR(v);
		return;
	}

	PyErr_SetObject((PyObject *) Py_TYPE(v), v);
	Py_DECREF(v);
	Py_DECREF(attrs);
	return;

simple_err:
	// Absolute minimum for error format
	PyErr_Format(PyExc_ZFSError, "[%d]: %s", zfs_err->code, zfs_err->description);
	return;

}
