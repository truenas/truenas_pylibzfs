#include "../truenas_pylibzfs.h"

/*
 * Low-level list builders: take a typed C array + count and return a PyList.
 * Caller is responsible for setting a Python exception on NULL return.
 */
static PyObject *
bool_arr_to_pylist(boolean_t *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyBool_FromLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
long_arr_to_pylist(long long *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyLong_FromLongLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
ulong_arr_to_pylist(unsigned long long *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyLong_FromUnsignedLongLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
str_arr_to_pylist(const char **arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyUnicode_FromString(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
nvlist_arr_to_pylist(nvlist_t **arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = py_nvlist_to_dict(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
int8_arr_to_pylist(int8_t *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyLong_FromLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
int16_arr_to_pylist(int16_t *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyLong_FromLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
int32_arr_to_pylist(int32_t *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyLong_FromLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
byte_arr_to_pylist(uchar_t *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyLong_FromUnsignedLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
uint8_arr_to_pylist(uint8_t *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyLong_FromUnsignedLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
uint16_arr_to_pylist(uint16_t *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyLong_FromUnsignedLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
uint32_arr_to_pylist(uint32_t *arr, uint_t n)
{
	PyObject *list = PyList_New(n);
	if (list == NULL)
		return NULL;
	for (uint_t i = 0; i < n; i++) {
		PyObject *item = PyLong_FromUnsignedLong(arr[i]);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}
		PyList_SET_ITEM(list, i, item);
	}
	return list;
}

static PyObject *
nvpair_signed_arr_to_pylist(nvpair_t *pair)
{
	uint_t n;

	switch (nvpair_type(pair)) {
	case DATA_TYPE_INT8_ARRAY: {
		int8_t *arr;
		if (nvpair_value_int8_array(pair, &arr, &n) != 0)
			break;
		return int8_arr_to_pylist(arr, n);
	}
	case DATA_TYPE_INT16_ARRAY: {
		int16_t *arr;
		if (nvpair_value_int16_array(pair, &arr, &n) != 0)
			break;
		return int16_arr_to_pylist(arr, n);
	}
	case DATA_TYPE_INT32_ARRAY: {
		int32_t *arr;
		if (nvpair_value_int32_array(pair, &arr, &n) != 0)
			break;
		return int32_arr_to_pylist(arr, n);
	}
	case DATA_TYPE_INT64_ARRAY: {
		int64_t *arr;
		if (nvpair_value_int64_array(pair, &arr, &n) != 0)
			break;
		return long_arr_to_pylist((long long *)arr, n);
	}
	default:
		return NULL;
	}
	PyErr_Format(PyExc_RuntimeError,
	    "nvpair_value failed for key '%s'", nvpair_name(pair));
	return NULL;
}

static PyObject *
nvpair_unsigned_arr_to_pylist(nvpair_t *pair)
{
	uint_t n;

	switch (nvpair_type(pair)) {
	case DATA_TYPE_BYTE_ARRAY: {
		uchar_t *arr;
		if (nvpair_value_byte_array(pair, &arr, &n) != 0)
			break;
		return byte_arr_to_pylist(arr, n);
	}
	case DATA_TYPE_UINT8_ARRAY: {
		uint8_t *arr;
		if (nvpair_value_uint8_array(pair, &arr, &n) != 0)
			break;
		return uint8_arr_to_pylist(arr, n);
	}
	case DATA_TYPE_UINT16_ARRAY: {
		uint16_t *arr;
		if (nvpair_value_uint16_array(pair, &arr, &n) != 0)
			break;
		return uint16_arr_to_pylist(arr, n);
	}
	case DATA_TYPE_UINT32_ARRAY: {
		uint32_t *arr;
		if (nvpair_value_uint32_array(pair, &arr, &n) != 0)
			break;
		return uint32_arr_to_pylist(arr, n);
	}
	case DATA_TYPE_UINT64_ARRAY: {
		uint64_t *arr;
		if (nvpair_value_uint64_array(pair, &arr, &n) != 0)
			break;
		return ulong_arr_to_pylist((unsigned long long *)arr, n);
	}
	default:
		return NULL;
	}
	PyErr_Format(PyExc_RuntimeError,
	    "nvpair_value failed for key '%s'", nvpair_name(pair));
	return NULL;
}

/*
 * Convert a single nvpair to a Python object.
 * Returns NULL without an exception set for unknown/unrepresentable types
 * (caller should skip). Returns NULL with an exception set on error.
 */
static PyObject *
nvpair_to_pyobject(nvpair_t *pair)
{
	switch (nvpair_type(pair)) {
	case DATA_TYPE_BOOLEAN:
		Py_RETURN_TRUE;
	case DATA_TYPE_BOOLEAN_VALUE:
		return PyBool_FromLong(fnvpair_value_boolean_value(pair));
	case DATA_TYPE_BYTE:
		return PyLong_FromLong(fnvpair_value_byte(pair));
	case DATA_TYPE_INT8:
		return PyLong_FromLong(fnvpair_value_int8(pair));
	case DATA_TYPE_INT16:
		return PyLong_FromLong(fnvpair_value_int16(pair));
	case DATA_TYPE_INT32:
		return PyLong_FromLong(fnvpair_value_int32(pair));
	case DATA_TYPE_INT64:
		return PyLong_FromLongLong(fnvpair_value_int64(pair));
	case DATA_TYPE_UINT8:
		return PyLong_FromUnsignedLong(fnvpair_value_uint8(pair));
	case DATA_TYPE_UINT16:
		return PyLong_FromUnsignedLong(fnvpair_value_uint16(pair));
	case DATA_TYPE_UINT32:
		return PyLong_FromUnsignedLong(fnvpair_value_uint32(pair));
	case DATA_TYPE_UINT64:
		return PyLong_FromUnsignedLongLong(fnvpair_value_uint64(pair));
	case DATA_TYPE_HRTIME: {
		hrtime_t v;
		if (nvpair_value_hrtime(pair, &v) != 0) {
			PyErr_Format(PyExc_RuntimeError,
			    "nvpair_value_hrtime failed for key '%s'",
			    nvpair_name(pair));
			return NULL;
		}
		return PyLong_FromLongLong((long long)v);
	}
	case DATA_TYPE_STRING:
		return PyUnicode_FromString(fnvpair_value_string(pair));
	case DATA_TYPE_DOUBLE: {
		double v;
		if (nvpair_value_double(pair, &v) != 0) {
			PyErr_Format(PyExc_RuntimeError,
			    "nvpair_value_double failed for key '%s'",
			    nvpair_name(pair));
			return NULL;
		}
		return PyFloat_FromDouble(v);
	}
	case DATA_TYPE_NVLIST:
		return py_nvlist_to_dict(fnvpair_value_nvlist(pair));
	case DATA_TYPE_BOOLEAN_ARRAY: {
		boolean_t *arr;
		uint_t n;
		if (nvpair_value_boolean_array(pair, &arr, &n) != 0) {
			PyErr_Format(PyExc_RuntimeError,
			    "nvpair_value failed for key '%s'",
			    nvpair_name(pair));
			return NULL;
		}
		return bool_arr_to_pylist(arr, n);
	}
	case DATA_TYPE_INT8_ARRAY:
	case DATA_TYPE_INT16_ARRAY:
	case DATA_TYPE_INT32_ARRAY:
	case DATA_TYPE_INT64_ARRAY:
		return nvpair_signed_arr_to_pylist(pair);
	case DATA_TYPE_BYTE_ARRAY:
	case DATA_TYPE_UINT8_ARRAY:
	case DATA_TYPE_UINT16_ARRAY:
	case DATA_TYPE_UINT32_ARRAY:
	case DATA_TYPE_UINT64_ARRAY:
		return nvpair_unsigned_arr_to_pylist(pair);
	case DATA_TYPE_STRING_ARRAY: {
		const char **arr;
		uint_t n;
		if (nvpair_value_string_array(pair, &arr, &n) != 0) {
			PyErr_Format(PyExc_RuntimeError,
			    "nvpair_value failed for key '%s'",
			    nvpair_name(pair));
			return NULL;
		}
		return str_arr_to_pylist(arr, n);
	}
	case DATA_TYPE_NVLIST_ARRAY: {
		nvlist_t **arr;
		uint_t n;
		if (nvpair_value_nvlist_array(pair, &arr, &n) != 0) {
			PyErr_Format(PyExc_RuntimeError,
			    "nvpair_value failed for key '%s'",
			    nvpair_name(pair));
			return NULL;
		}
		return nvlist_arr_to_pylist(arr, n);
	}
	default:
		/* Unknown or unrepresentable type — skip (no exception set) */
		return NULL;
	}
}

/*
 * Directly convert an nvlist_t to a Python dict by iterating nvpairs.
 * Supports nested nvlists and all common nvpair types.
 * Returns NULL with exception set on failure.
 */
PyObject *py_nvlist_to_dict(nvlist_t *nvl)
{
	PyObject *d = NULL;
	nvpair_t *pair = NULL;

	d = PyDict_New();
	if (d == NULL)
		return NULL;

	for (pair = nvlist_next_nvpair(nvl, NULL);
	    pair != NULL;
	    pair = nvlist_next_nvpair(nvl, pair)) {
		const char *name = nvpair_name(pair);
		PyObject *val = nvpair_to_pyobject(pair);

		if (val == NULL) {
			if (PyErr_Occurred()) {
				Py_DECREF(d);
				return NULL;
			}
			/* No exception — unrepresentable type, skip */
			continue;
		}

		if (PyDict_SetItemString(d, name, val) < 0) {
			Py_DECREF(val);
			Py_DECREF(d);
			return NULL;
		}
		Py_DECREF(val);
	}

	return d;
}

/*
 * Serialize nvl to a JSON string allocated with PyMem_RawMalloc.
 * Returns NULL on failure — no Python exception is set, so this is
 * safe to call outside the GIL. Caller must free with PyMem_RawFree.
 */
char *nvlist_to_json_str(nvlist_t *nvl)
{
	FILE *fp = NULL;
	char *buf = NULL;
	char *out = NULL;
	size_t bufsz = 0;

	fp = open_memstream(&buf, &bufsz);
	if (fp == NULL)
		return NULL;

	if (nvlist_print_json(fp, nvl) != 0 || fflush(fp) != 0) {
		fclose(fp);
		free(buf);
		return NULL;
	}

	fclose(fp);

	out = PyMem_RawMalloc(bufsz + 1);
	if (out != NULL) {
		memcpy(out, buf, bufsz);
		out[bufsz] = '\0';
	}

	free(buf);
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
