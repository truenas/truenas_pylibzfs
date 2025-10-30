#include "zfs.h"

/*
 * This function converts the nvlist containing user props
 * into a python dictionary of form:
 * {"<nvpair_name>": "<nvpair_value_string"}
 */
PyObject *user_props_nvlist_to_py_dict(nvlist_t *userprops)
{
	PyObject *d = NULL;
	nvpair_t *elem;

	d = PyDict_New();
	if (d == NULL)
		return NULL;

	for (elem = nvlist_next_nvpair(userprops, NULL);
	    elem != NULL;
	    elem = nvlist_next_nvpair(userprops, elem)) {
		const char *name = nvpair_name(elem);
		const char *cval;
		nvlist_t *nvl;

		PYZFS_ASSERT(
			(nvpair_type(elem) == DATA_TYPE_NVLIST),
			"Unexpected nvpair data type in user props"

		);

		nvl = fnvpair_value_nvlist(elem);
		cval = fnvlist_lookup_string(nvl, ZPROP_VALUE);

		PyObject *val = PyUnicode_FromString(cval);
		if (val == NULL) {
			Py_DECREF(d);
			return NULL;
		}

		if (PyDict_SetItemString(d, name, val) < 0)  {
			Py_DECREF(d);
			Py_DECREF(val);
			return NULL;
		}

		Py_DECREF(val);
	}

	return d;
}

/*
 * Convert a python dictionary of user props into an nvlist
 * for insertion as user properties.
 */
nvlist_t *py_userprops_dict_to_nvlist(PyObject *pyprops)
{
	nvlist_t *nvl = fnvlist_alloc();
	PyObject *key, *value;
	Py_ssize_t pos = 0;

	if (!PyDict_Check(pyprops)) {
		PyErr_SetString(PyExc_TypeError,
				"Not a dictionary.");
		fnvlist_free(nvl);
		return NULL;
	}

	while (PyDict_Next(pyprops, &pos, &key, &value)) {
		const char *name = NULL;
		const char *cval = NULL;

		name = PyUnicode_AsUTF8(key);
		if (name == NULL) {
			fnvlist_free(nvl);
			return NULL;
		}

		if (strstr(name, ":") == NULL) {
			PyErr_Format(PyExc_ValueError,
				     "%s: user properties must "
				     "contain a colon (:) in their "
				     "name.", name);

			fnvlist_free(nvl);
			return NULL;
		} else if (strlen(name) > ZAP_MAXNAMELEN) {
			PyErr_Format(PyExc_ValueError,
				     "%s: property name exceeds max "
				     "length of %d.", name,
				     ZAP_MAXNAMELEN);

			fnvlist_free(nvl);
			return NULL;
		}

		cval = PyUnicode_AsUTF8(value);
		if (cval == NULL) {
			fnvlist_free(nvl);
			return NULL;
		}

		fnvlist_add_string(nvl, name, cval);
	}

	return nvl;
}

/*
 * Below are functions that convert various forms of python ZFS properties
 * into an nvlist suitable for bulk changes.
 */
PyObject *py_nvlist_names_tuple(nvlist_t *nvl)
{
	PyObject *l = NULL;
	PyObject *out = NULL;
	nvpair_t *elem;

	l = PyList_New(0);
	if (l == NULL)
		return NULL;

	for (elem = nvlist_next_nvpair(nvl, NULL); elem != NULL;
	    elem = nvlist_next_nvpair(nvl, elem)) {
		PyObject *name = PyUnicode_FromString(nvpair_name(elem));
		if (name == NULL) {
			Py_DECREF(l);
			return NULL;
		}

		if (PyList_Append(l, name) < 0) {
			Py_DECREF(name);
			Py_DECREF(l);
			return NULL;
		}
		Py_DECREF(name);
	}

	out = PyList_AsTuple(l);
	Py_DECREF(l);
	return out;
}

PyObject *py_dump_nvlist(nvlist_t *nvl, boolean_t json)
{
	PyObject *out = NULL;
	FILE *target = NULL;
	char *buf;
	size_t bufsz;
	boolean_t success = B_FALSE;

	Py_BEGIN_ALLOW_THREADS
	target = open_memstream(&buf, &bufsz);
	if (target != NULL) {
		if (json) {
			if (nvlist_print_json(target, nvl) == 0)
				success = B_TRUE;
		} else {
			nvlist_print(target, nvl);
			success = B_TRUE;
		}
		fflush(target);
	}
	Py_END_ALLOW_THREADS

	if (!success) {
		PyErr_Format(PyExc_RuntimeError,
			     "Failed to dump nvlist: %s",
			     strerror(errno));
		if (target) {
			fclose(target);
			free(buf);
		}

		return NULL;
	}

	out = PyUnicode_FromStringAndSize(buf, bufsz);
	Py_BEGIN_ALLOW_THREADS
	fclose(target);
	free(buf);
	Py_END_ALLOW_THREADS
	return out;
}

/*
 * Add python string value to nvlist. nvlist API makes copy of string.
 * Does not impact refcnt of val
 */
static
boolean_t nvlist_add_py_str(nvlist_t *nvl, const char *key, PyObject *val)
{
	const char *cval = PyUnicode_AsUTF8(val);
	if (cval == NULL) {
		return B_FALSE;
	}

	fnvlist_add_string(nvl, key, cval);
	return B_TRUE;
}

/*
 * Add python bool value to nvlist. nvlist API makes copy of string.
 * Does not impact refcnt of val
 */
static
boolean_t nvlist_add_py_bool(nvlist_t *nvl, const char *key, PyObject *val)
{
	boolean_t cval = val == Py_True;
	fnvlist_add_boolean_value(nvl, key, cval);
	return B_TRUE;
}

/*
 * Add python float value to nvlist as double.
 * Does not impact refcnt of val
 */
static
boolean_t nvlist_add_py_float(nvlist_t *nvl, const char *key, PyObject *val)
{
	double cval;
	cval = PyFloat_AsDouble(val);
	if ((cval == (float)-1) && PyErr_Occurred())
		return B_FALSE;

	nvlist_add_double(nvl, key, cval);
	return B_TRUE;
}

/*
 * Add python int value to nvlist as uint64.
 * Does not impact refcnt of val
 */
static
boolean_t nvlist_add_py_int_unsigned(nvlist_t *nvl, const char *key, PyObject *val)
{
	Py_ssize_t cval;

	cval = PyLong_AsSsize_t(val);
	if ((cval == -1) && PyErr_Occurred())
		return B_FALSE;

	fnvlist_add_uint64(nvl, key, cval);
	return B_TRUE;
}

/*
 * Add python int value to nvlist. First tries as int64 and on overflow changes to
 * uint64 if the value overflows a long. Does not impact refcnt.
 */
static
boolean_t nvlist_add_py_int(nvlist_t *nvl, const char *key, PyObject *val)
{
	long cval;
	int overflow = 0;

	// start with assumption we have a long
	// Non-zero overflow indicates whether -1 is an error case
	cval = PyLong_AsLongAndOverflow(val, &overflow);
	if ((cval == -1) && (overflow != 0)) {
		if (overflow == 1)
			// Value exceeds long and so we need ulong
			return nvlist_add_py_int_unsigned(nvl, key, val);

		else if (overflow == -1) {
			// maybe need long long, but we're not supporting
			// that right now
			PyErr_Format(PyExc_ValueError,
				     "%s: value for key lower than minimum "
				     "allowed for nvlist.", key);
			return B_FALSE;
		}

		// Perhaps we had an exception that set an invalid
		// overflow value
		if (PyErr_Occurred())
			return B_FALSE;

		PyErr_Format(PyExc_RuntimeError,
			     "%s: unexpected failure converting python int.",
			      key);
		return B_FALSE;
	}

	fnvlist_add_int64(nvl, key, cval);
	return B_TRUE;
}

/*
 * Convert a python dictionary to an nvlist. This is primarily used for
 * handling kwargs for lua channel program. Should be kept to lua-safe types
 * if possible. Allocates a new nvlist. Does not impact refcnt of the dictionary
 */
nvlist_t *py_dict_to_nvlist(PyObject *dict_in)
{
	nvlist_t *nvl = fnvlist_alloc();
	PyObject *key, *value;
	Py_ssize_t pos = 0;

	if (!PyDict_Check(dict_in)) {
		PyErr_SetString(PyExc_TypeError, "Not a dictionary");
		fnvlist_free(nvl);
		return NULL;
	}

	while (PyDict_Next(dict_in, &pos, &key, &value)) {
		const char *ckey;
		if (!PyUnicode_Check(key)) {
			PyErr_SetString(PyExc_TypeError, "Key must be unicode string");
			fnvlist_free(nvl);
			return NULL;
		}

		ckey = PyUnicode_AsUTF8(key);
		if (!ckey) {
			fnvlist_free(nvl);
			return NULL;
		}

		/*
		 * We don't want to use macro Py_TYPE() to get
		 * PyTypeObject and switch() it because we want to
		 * catch subtypes as well.
		 */
		// python str
		if (PyUnicode_Check(value)) {
			// Python string
			if (!nvlist_add_py_str(nvl, ckey, value)) {
				fnvlist_free(nvl);
				return NULL;
			}
		// python bool
		} else if (PyBool_Check(value)) {
			if (!nvlist_add_py_bool(nvl, ckey, value)) {
				fnvlist_free(nvl);
				return NULL;
			}
		// python float
		} else if (PyFloat_Check(value)) {
			if (!nvlist_add_py_float(nvl, ckey, value)) {
				fnvlist_free(nvl);
				return NULL;
			}
		// python int
		} else if (PyLong_Check(value)) {
			// python int
			if (!nvlist_add_py_int(nvl, ckey, value)) {
				fnvlist_free(nvl);
				return NULL;
			}
		// python dict
		} else if (PyDict_Check(value)) {
			nvlist_t *subnvl = NULL;

			subnvl = py_dict_to_nvlist(value);
			if (subnvl == NULL) {
				fnvlist_free(nvl);
			}

			fnvlist_add_nvlist(nvl, ckey, subnvl);
			nvlist_free(subnvl);
		} else if (PyList_Check(value)) {
			PyErr_SetString(PyExc_ValueError, "Lists are not supported");
			fnvlist_free(nvl);
			return NULL;
		} else {
			PyErr_Format(PyExc_ValueError, "%s: unsupported type for key",
				     ckey);
			return NULL;
		}
	}

	return nvl;
}
