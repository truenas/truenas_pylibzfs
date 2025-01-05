#include "pylibzfs2.h"

PyObject *PyExc_ZFSError;

static
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
	const char *name = NULL;
	PyObject *errstr = NULL;
	int err;

	name = zfs_error_name(zfs_err->code);

	if (additional_info) {
		errstr = PyUnicode_FromFormat(
			"[%s]: %s",
			name, additional_info
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
	args = Py_BuildValue(
		"(iNssss)",
		zfs_err->code,
		errstr,
		name,
		zfs_err->action,
		zfs_err->description,
		location
	);
	if (args == NULL) {
		// Failed so we need to decref errstr
		Py_XDECREF(errstr);
		goto simple_err;
	}

	v = PyObject_Call(PyExc_ZFSError, args, NULL);
	if (v == NULL) {
		Py_CLEAR(args);
		return;
	}

	err = PyObject_SetAttrString(v, "code", PyTuple_GetItem(args, 0));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "err_str", PyTuple_GetItem(args, 1));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "name", PyTuple_GetItem(args, 2));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "action", PyTuple_GetItem(args, 3));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "description", PyTuple_GetItem(args, 4));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		return;
	}

	err = PyObject_SetAttrString(v, "location", PyTuple_GetItem(args, 5));
	Py_CLEAR(args);
	if (err == -1) {
		Py_CLEAR(v);
		return;
	}

	PyErr_SetObject((PyObject *) Py_TYPE(v), v);
	Py_DECREF(v);
	return;

simple_err:
	// Absolute minimum for error format
	PyErr_Format(PyExc_ZFSError, "[%d]: %s", zfs_err->code, zfs_err->description);
	return;

}
