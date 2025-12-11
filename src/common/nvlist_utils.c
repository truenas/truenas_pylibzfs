#include "../truenas_pylibzfs.h"

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

static
boolean_t get_prop_from_key(PyObject *zfs_property_enum,
			    PyObject *key,
			    zfs_prop_t *pprop)
{
	long lval;

	if (PyUnicode_Check(key)) {
		const char *val = PyUnicode_AsUTF8(key);
		zfs_prop_t prop;
		if (val == NULL)
			return B_FALSE;

		prop = zfs_name_to_prop(val);
		if (prop == ZPROP_INVAL) {
			PyErr_Format(PyExc_ValueError,
				     "%s: not a valid ZFS property.",
				     val);
			return B_FALSE;
		}
		*pprop = prop;
		return B_TRUE;
	}

	if (!PyObject_IsInstance(key, zfs_property_enum)) {
		PyObject *repr = PyObject_Repr(key);
		PyErr_Format(PyExc_TypeError,
			     "%V: unexpected key type. "
			     "Expected a truenas_pylibzfs.ZFSProperty "
			     "instance.", repr, "UNKNOWN");

		Py_XDECREF(repr);
		return B_FALSE;
	}

	lval = PyLong_AsLong(key);
	PYZFS_ASSERT(
		((lval >= 0) && (lval < ZFS_NUM_PROPS)),
		"Unexpected ZFSProperty enum value"
	);

	*pprop = lval;
	return B_TRUE;
}

static
PyObject *py_propdict_key_val(PyObject *value_in)
{
	/*
	 * we may have {"raw": "bob", "value": "bob"} or
	 * "bob" here. We'll return a PyUnicode object for
	 * the value
	 *
	 * Heavy lifting of PyUnicode conversion will be by
	 * PyObject_Str
	 */
	if (PyDict_Check(value)) {
		PyObject *pyval = NULL;
		PyObject *pykey = NULL;
		PyObject *out = NULL;

		// preference given to "raw" key since it should always
		// be a string
		pykey = PyUnicode_FromString("raw");
		if (pykey == NULL) {
			return NULL;
		}

		pyval = PyDict_GetItem(value, pykey);
		if (pyval == NULL) {
			/* raw key wasn't present, try with "value" */
			Py_DECREF(pykey);
			pykey = PyUnicode_FromString("value");
			if (pykey == NULL) {
				return NULL;
			}
			pyval = PyDict_GetItem(value, pykey);
		}
		Py_DECREF(pykey);
		if (pyval == NULL) {
			PyErr_SetString(PyExc_ValueError,
					"Property entry dict must "
					"contain either a raw or value "
					"key.");

			return NULL;
		}

		out = PyObject_Str(pyval);
		Py_DECREF(pyval);
		return out;
	}

	return PyObject_Str(value_in);
}

static
nvlist_t *py_zfsprops_dict_to_nvlist(pylibzfs_state_t *state,
				     PyObject *pyprops,
				     zfs_type_t type,
				     boolean_t allow_ro)
{
	nvlist_t *nvl = fnvlist_alloc();
	PyObject *key, *value, *rokey;
	Py_ssize_t pos = 0;
	boolean_t has_ro = B_FALSE;

	key = PyUnicode_FromString(zfs_prop_to_name(ZFS_PROP_READONLY));
	if (key == NULL) {
		fnvlist_free(nvl);
		return NULL;
	}

	// PyDict_GetItem retrieves a *borrowed* reference. Do not DECREF
	value = PyDict_GetItem(pyprops, key);
	Py_DECREF(key);

	if (value) {
		// readonly attribute is being changed, place at head of nvlist
		PyObject *pystrval;
		const char *cval;
		pystrval = py_propdict_key_val(value);
		if (pystrval == NULL) {
			fnvlist_free(nvl);
			return NULL;
		}

		cval = PyUnicode_AsUTF8(pystrval);
		if (cval == NULL) {
			fnvlist_free(nvl);
			Py_DECREF(pystrval);
		}
		fnvlist_add_string(nvl, zfs_prop_to_name(ZFS_PROP_READONLY), cval);
		Py_DECREF(pystrval);
	}

	while (PyDict_Next(pyprops, &pos, &key, &value)) {
		PyObject *pystrval = NULL;
		const char *cval = NULL;
		zfs_prop_t zprop;

		if (!get_prop_from_key(state->zfs_property_enum, key, &zprop)) {
			fnvlist_free(nvl);
			return NULL;
		}

		if (zprop == ZFS_PROP_READONLY) {
			// already handled above
			continue;
		}

		if (zfs_prop_readonly(zprop) && !allow_ro) {
			PyErr_Format(PyExc_ValueError,
				     "%s: ZFS property is readonly.",
				     zfs_prop_to_name(zprop));
			fnvlist_free(nvl);
			return NULL;
		}

		if (!py_zfs_prop_valid_for_type(zprop, type)) {
			fnvlist_free(nvl);
			return NULL;
		}

		/*
		 * we support either {"raw": "on"} or "raw" for
		 * the dictionary value. This is to make it so
		 * that all variants of getting properties can be
		 * converted directly into nvlist
		 */
		pystrval = py_propdict_key_val(value);
		if (pystrval == NULL) {
			return NULL;
		}

		cval = PyUnicode_AsUTF8(pystrval);
		if (cval == NULL) {
			fnvlist_free(nvl);
			Py_DECREF(pystrval);
			return NULL;
		}

		fnvlist_add_string(nvl, zfs_prop_to_name(zprop), cval);
		Py_DECREF(pystrval);
	}

	return nvl;
}

static
boolean_t py_prop_struct_to_nvlist(PyObject *propstruct,
				   zfs_prop_t prop,
				   nvlist_t *nvl)
{
	PyObject *value;
	PyObject *pystrval;
	const char *cval;

	// Preference is for raw value since we know ZFS is okay with it.
	value = PyStructSequence_GET_ITEM(propstruct, 1);
	if (value == Py_None) {
		// We don't have a raw value. This means someone hand-rolled
		// the struct and so we need the parsed value
		value = PyStructSequence_GET_ITEM(propstruct, 0);
	}

	if (value == Py_None) {
		pystrval = PyUnicode_FromString(LIBZFS_NONE_VALUE);
	} else {
		pystrval = PyObject_Str(value);
	}

	if (pystrval == NULL)
		return B_FALSE;

	cval = PyUnicode_AsUTF8(pystrval);
	if (cval == NULL) {
		Py_DECREF(pystrval);
		return B_FALSE;
	}

	fnvlist_add_string(nvl, zfs_prop_to_name(prop), cval);
	Py_DECREF(pystrval);
	return B_TRUE;
}

static
nvlist_t *py_zfsprops_struct_to_nvlist(pylibzfs_state_t *state,
				       PyObject *pyprops,
				       zfs_type_t type,
				       boolean_t allow_ro)
{
	nvlist_t *nvl = fnvlist_alloc();
	int idx;

	/*
	 * we need to iterate the prop list twice, once to put in the
	 * readonly property and once to do the remainder
	 */
	for (idx = 0; idx < state->struct_zfs_prop_desc.n_in_sequence; idx++) {
		PyObject *value = NULL;
		if (zfs_prop_table[idx].prop != ZFS_PROP_READONLY)
			continue;

		/* Py_None here means that the value of property isn't being set */
		value = PyStructSequence_GET_ITEM(pyprops, idx);
		if (value == Py_None)
			break;

		if (!py_prop_struct_to_nvlist(value, ZFS_PROP_READONLY, nvl)) {
			fnvlist_free(nvl);
			return NULL;
		}
		break;
	}

	for (idx = 0; idx < state->struct_zfs_prop_desc.n_in_sequence; idx++) {
		const char *name = state->struct_prop_fields[idx].name;
		zfs_prop_t zprop = zfs_prop_table[idx].prop;
		PyObject *value = NULL;

		// we handle ZFS_PROP_READONLY above
		if (zprop == ZFS_PROP_READONLY)
			continue;

                // Check if this is a hidden property
		if (strcmp(name, PyStructSequence_UnnamedField) == 0)
			continue;
		value = PyStructSequence_GET_ITEM(pyprops, idx);
		if (value == Py_None)
			continue;

		if (zfs_prop_readonly(zprop) && !allow_ro) {
			PyErr_Format(PyExc_ValueError,
				     "%s: ZFS property is readonly.",
				     zfs_prop_to_name(zprop));
			fnvlist_free(nvl);
			return NULL;
		}

		if (!py_zfs_prop_valid_for_type(zprop, type)) {
			fnvlist_free(nvl);
			return NULL;
		}

		if (!py_prop_struct_to_nvlist(value, zprop, nvl)) {
			fnvlist_free(nvl);
			return NULL;
		}
        }

	return nvl;
}

nvlist_t *py_zfsprops_to_nvlist(pylibzfs_state_t *state,
				PyObject *pyprops,
				zfs_type_t type,
				boolean_t ro)
{
	PyObject *repr;

	if (PyDict_Check(pyprops)) {
		return py_zfsprops_dict_to_nvlist(state, pyprops, type, ro);
	} else if (PyObject_TypeCheck(pyprops, state->struct_zfs_props_type)) {
		return py_zfsprops_struct_to_nvlist(state,
						    pyprops,
						    type,
						    ro);
	}

	repr = PyObject_Repr(pyprops);

	PyErr_Format(PyExc_TypeError,
		     "%V: unexpected properties type. Expected a dictionary or "
		     "a " PYLIBZFS_MODULE_NAME ".struct_zfs_property instance.",
		     repr, "UNKNOWN TYPE");

	Py_XDECREF(repr);
	return NULL;
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
