#include "../truenas_pylibzfs.h"

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
 * Add a python list[str] to nvlist as DATA_TYPE_STRING_ARRAY.
 * Does not impact refcnt of list.
 */
static
boolean_t nvlist_add_py_str_array(nvlist_t *nvl, const char *key,
    PyObject *list, Py_ssize_t n)
{
	const char **arr = NULL;
	PyObject *elem = NULL;
	Py_ssize_t i;

	arr = PyMem_RawMalloc(n * sizeof(const char *));
	if (!arr) {
		PyErr_NoMemory();
		return B_FALSE;
	}
	for (i = 0; i < n; i++) {
		elem = PyList_GET_ITEM(list, i);
		if (!PyUnicode_Check(elem)) {
			PyErr_Format(PyExc_ValueError,
			    "%s: mixed types in list (expected str)", key);
			PyMem_RawFree(arr);
			return B_FALSE;
		}
		arr[i] = PyUnicode_AsUTF8(elem);
		if (!arr[i]) {
			PyMem_RawFree(arr);
			return B_FALSE;
		}
	}
	fnvlist_add_string_array(nvl, key, arr, n);
	PyMem_RawFree(arr);
	return B_TRUE;
}

/*
 * Add a python list[bool] to nvlist as DATA_TYPE_BOOLEAN_ARRAY.
 * Does not impact refcnt of list.
 */
static
boolean_t nvlist_add_py_bool_array(nvlist_t *nvl, const char *key,
    PyObject *list, Py_ssize_t n)
{
	boolean_t *arr = NULL;
	PyObject *elem = NULL;
	Py_ssize_t i;

	arr = PyMem_RawMalloc(n * sizeof(boolean_t));
	if (!arr) {
		PyErr_NoMemory();
		return B_FALSE;
	}
	for (i = 0; i < n; i++) {
		elem = PyList_GET_ITEM(list, i);
		if (!PyBool_Check(elem)) {
			PyErr_Format(PyExc_ValueError,
			    "%s: mixed types in list (expected bool)", key);
			PyMem_RawFree(arr);
			return B_FALSE;
		}
		arr[i] = (elem == Py_True) ? B_TRUE : B_FALSE;
	}
	fnvlist_add_boolean_array(nvl, key, arr, n);
	PyMem_RawFree(arr);
	return B_TRUE;
}

/*
 * Add a python list[int] to nvlist as DATA_TYPE_INT64_ARRAY or
 * DATA_TYPE_UINT64_ARRAY.  Two-pass: first validates types and detects
 * whether any value is negative (selects signed vs unsigned array type),
 * then builds the C array.  Does not impact refcnt of list.
 */
static
boolean_t nvlist_add_py_int_array(nvlist_t *nvl, const char *key,
    PyObject *list, Py_ssize_t n)
{
	boolean_t has_negative = B_FALSE;
	PyObject *elem = NULL;
	int overflow = 0;
	long cval;
	Py_ssize_t i;

	for (i = 0; i < n; i++) {
		elem = PyList_GET_ITEM(list, i);
		if (!PyLong_Check(elem) || PyBool_Check(elem)) {
			PyErr_Format(PyExc_ValueError,
			    "%s: mixed types in list (expected int)", key);
			return B_FALSE;
		}
		cval = PyLong_AsLongAndOverflow(elem, &overflow);
		if (overflow == -1) {
			PyErr_Format(PyExc_ValueError,
			    "%s: int value out of int64 range", key);
			return B_FALSE;
		}
		if (overflow == 0 && cval < 0)
			has_negative = B_TRUE;
	}
	if (PyErr_Occurred())
		return B_FALSE;

	if (has_negative) {
		int64_t *arr = NULL;

		arr = PyMem_RawMalloc(n * sizeof(int64_t));
		if (!arr) {
			PyErr_NoMemory();
			return B_FALSE;
		}
		for (i = 0; i < n; i++) {
			elem = PyList_GET_ITEM(list, i);
			cval = PyLong_AsLongAndOverflow(elem, &overflow);
			if (overflow != 0) {
				PyErr_Format(PyExc_ValueError,
				    "%s: int value out of int64 range", key);
				PyMem_RawFree(arr);
				return B_FALSE;
			}
			arr[i] = (int64_t)cval;
		}
		fnvlist_add_int64_array(nvl, key, arr, n);
		PyMem_RawFree(arr);
	} else {
		uint64_t *arr = NULL;
		uint64_t uval;

		arr = PyMem_RawMalloc(n * sizeof(uint64_t));
		if (!arr) {
			PyErr_NoMemory();
			return B_FALSE;
		}
		for (i = 0; i < n; i++) {
			elem = PyList_GET_ITEM(list, i);
			uval = PyLong_AsUnsignedLongLong(elem);
			if (uval == (uint64_t)-1 && PyErr_Occurred()) {
				PyMem_RawFree(arr);
				return B_FALSE;
			}
			arr[i] = uval;
		}
		fnvlist_add_uint64_array(nvl, key, arr, n);
		PyMem_RawFree(arr);
	}
	return B_TRUE;
}

/*
 * Add a python list[dict] to nvlist as DATA_TYPE_NVLIST_ARRAY.
 * Does not impact refcnt of list.
 */
static
boolean_t nvlist_add_py_nvlist_array(nvlist_t *nvl, const char *key,
    PyObject *list, Py_ssize_t n)
{
	nvlist_t **arr = NULL;
	PyObject *elem = NULL;
	Py_ssize_t i, j;

	arr = PyMem_RawMalloc(n * sizeof(nvlist_t *));
	if (!arr) {
		PyErr_NoMemory();
		return B_FALSE;
	}
	for (i = 0; i < n; i++)
		arr[i] = NULL;

	for (i = 0; i < n; i++) {
		elem = PyList_GET_ITEM(list, i);
		if (!PyDict_Check(elem)) {
			PyErr_Format(PyExc_ValueError,
			    "%s: mixed types in list (expected dict)", key);
			for (j = 0; j < i; j++)
				fnvlist_free(arr[j]);
			PyMem_RawFree(arr);
			return B_FALSE;
		}
		arr[i] = py_dict_to_nvlist(elem);
		if (!arr[i]) {
			for (j = 0; j < i; j++)
				fnvlist_free(arr[j]);
			PyMem_RawFree(arr);
			return B_FALSE;
		}
	}
	fnvlist_add_nvlist_array(nvl, key, (const nvlist_t * const *)arr, n);
	for (i = 0; i < n; i++)
		fnvlist_free(arr[i]);
	PyMem_RawFree(arr);
	return B_TRUE;
}

/*
 * Add a python list to nvlist as a typed array.
 * Element type is inferred from the first element; all elements must match.
 * Supported: str, bool, int, dict. Empty lists are rejected.
 * Does not impact refcnt of list.
 */
static
boolean_t nvlist_add_py_list(nvlist_t *nvl, const char *key, PyObject *list)
{
	PyObject *first = NULL;
	Py_ssize_t n;

	n = PyList_GET_SIZE(list);
	if (n == 0) {
		PyErr_Format(PyExc_ValueError,
		    "%s: empty lists are not supported", key);
		return B_FALSE;
	}

	first = PyList_GET_ITEM(list, 0);

	if (PyUnicode_Check(first))
		return nvlist_add_py_str_array(nvl, key, list, n);
	else if (PyBool_Check(first))
		return nvlist_add_py_bool_array(nvl, key, list, n);
	else if (PyLong_Check(first))
		return nvlist_add_py_int_array(nvl, key, list, n);
	else if (PyDict_Check(first))
		return nvlist_add_py_nvlist_array(nvl, key, list, n);

	PyErr_Format(PyExc_ValueError,
	    "%s: list elements must be str, bool, int, or dict", key);
	return B_FALSE;
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
				return NULL;
			}

			fnvlist_add_nvlist(nvl, ckey, subnvl);
			nvlist_free(subnvl);
		} else if (PyList_Check(value)) {
			if (!nvlist_add_py_list(nvl, ckey, value)) {
				fnvlist_free(nvl);
				return NULL;
			}
		} else {
			PyErr_Format(PyExc_ValueError, "%s: unsupported type for key",
				     ckey);
			return NULL;
		}
	}

	return nvl;
}
