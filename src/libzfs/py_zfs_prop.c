#include "../truenas_pylibzfs.h"

/*
 * ZFS property implementation for module
 *
 * The properties are implemented as Struct Sequence Objects in the C API.
 * There are three basic Struct Sequence Object types for the purposes of this
 * implementation:
 *
 * truenas_pylibzfs.struct_zfs_property_data:
 * ------------------------------------------
 *     This Struct Sequence Object contains the actual property value
 *     and a reference to the source of the property (if requested).
 *
 *     It is statically defined in this file and has the following fields:
 *         value: typed python object for the value.
 *         raw: string representing the value
 *         source: either Struct Sequence Object for source or None type.
 *
 * truenas_pylibzfs.struct_zfs_property_source:
 * --------------------------------------------
 *     This Struct Sequence Object contains information about the source
 *     of the ZFS property.
 *
 *     It is statically defined in this file and has the following fields:
 *         type: truenas_pylibzfs.PropertySource IntEnum instance representing
 *             the underlying zprop_source_t for the property.
 *         value: str indicating source of the property if it is inherited.
 *
 * truenas_pylibzfs.struct_zfs_property:
 * -------------------------------------
 *     This Struct Sequence Object contains requested property information
 *     for a particular ZFS dataset (filesystem, volume, snapshot).
 *
 *     It is *dyanmically created* when the py_zfs_state is initialized
 *     on module load. This is an absolute requirement since we do not
 *     have most of our property information until the first libzfs_t handle
 *     is opened (which initializes the ZFS properties structs).
 */

PyStructSequence_Field struct_zfs_prop [] = {
	{"value", "Parsed value of the ZFS property"},
	{"raw", "Raw value of the ZFS property (string)"},
	{"source", "Source dataset of the property"},
	{0},
};

PyStructSequence_Desc struct_zfs_prop_type_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_zfs_property_data",
	.fields = struct_zfs_prop,
	.doc = "Python ZFS property structure.",
	.n_in_sequence = 3
};

PyStructSequence_Field struct_zfs_prop_src [] = {
	{"type", "Source type of the ZFS property"},
	{"value", "Source string of ZFS property"},
	{0},
};

PyStructSequence_Desc struct_zfs_prop_src_type_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_zfs_property_source",
	.fields = struct_zfs_prop_src,
	.doc = "Python ZFS property source structure.",
	.n_in_sequence = 2
};

/*
 * Generate some reasonable-ish documentation for our attributes based on ZFS
 * information. This function deliberately uses python memory interface to
 * simplify cleanup when the state is freed. Do not change malloc methods
 * without changing how the state file is cleaned up.
 */
static
char *py_create_prop_doc(const char *name, zfs_prop_t prop)
{
	const char *v = zfs_prop_values(prop);
	boolean_t ro = zfs_prop_readonly(prop);
	char buf[1024] = {0};
	char *out = NULL;

	if (ro) {
		snprintf(buf, sizeof(buf),
			 "%s: this property is read-only.",
			 name);
	} else if (v) {
		snprintf(buf, sizeof(buf),
			"%s: property may be set to one of following "
			"values: %s.", name, v);
	} else if (zfs_prop_is_string(prop)) {
		snprintf(buf, sizeof(buf), "%s: property is string.", name);
	} else {
		snprintf(buf, sizeof(buf), "%s: property is numeric.", name);
	}

	out = PyMem_New(char, strlen(buf) + 1);
	if (out == NULL)
		return NULL;

	memcpy(out, buf, strlen(buf) + 1);
	return out;
}

/* strdup using python memory allocator. This function deliberately uses
 * the python memory interface. If this gets replaced by system malloc,
 * the developer should also change how the memory is freed on state cleanup.
 */
static
char *pymem_strdup(const char *s)
{
	size_t len = strlen(s) + 1;
	void *new = PyMem_Malloc(len);
	if (new == NULL)
		return NULL;

	return (char *)memcpy(new, s, len);
}

void init_py_struct_prop_state(pylibzfs_state_t *state)
{
	size_t i;
	PyTypeObject *obj;

	for (i = 0; i < ARRAY_SIZE(zfs_prop_table); i++) {
		/* Leave hidden properties unnamed in our property struct */
		const char *doc = NULL;
		const char *name = PyStructSequence_UnnamedField;
		zfs_prop_t prop = zfs_prop_table[i].prop;

		if (zfs_prop_visible(prop)) {
			name = zfs_prop_to_name(prop);
			doc = py_create_prop_doc(name, prop);
		}

		/*
		 * WARNING: name field must be heap-allocated otherwise
		 * repr method of sequence was seen to fail in runtime
		 */
		state->struct_prop_fields[i].name = pymem_strdup(name);
		state->struct_prop_fields[i].doc = doc;

		/*
		 * If this malloc failed then we'll be unable to read ZFS
		 * properties. It's better to just fail spectacularly rather
		 * than be semi-broken.
                 */
		PYZFS_ASSERT(state->struct_prop_fields[i].name, "Malloc failure.");
	}

	state->struct_zfs_prop_desc = (PyStructSequence_Desc) {
		.name = PYLIBZFS_MODULE_NAME ".struct_zfs_property",
		.fields = state->struct_prop_fields,
		.n_in_sequence = ARRAY_SIZE(zfs_prop_table)
	};

	obj = PyStructSequence_NewType(&state->struct_zfs_prop_desc);
	PYZFS_ASSERT(obj, "Failed to allocate struct_zfs_props_type");
	state->struct_zfs_props_type = obj;

	obj = PyStructSequence_NewType(&struct_zfs_prop_type_desc);
	PYZFS_ASSERT(obj, "Failed to allocate struct_zfs_prop_type");

	state->struct_zfs_prop_type = obj;

	obj = PyStructSequence_NewType(&struct_zfs_prop_src_type_desc);
	PYZFS_ASSERT(obj, "Failed to allocate struct_zfs_prop_src_type");

	state->struct_zfs_prop_src_type = obj;
}

boolean_t py_zfs_prop_valid_for_type(zfs_prop_t prop, zfs_type_t zfs_type)
{
	if (zfs_prop_valid_for_type(prop, zfs_type, B_FALSE))
		return B_TRUE;

	PyErr_Format(
		PyExc_ValueError,
		"%s: property is invalid for zfs type: %s",
		zfs_prop_to_name(prop),
		get_dataset_type(zfs_type)
	);

	return B_FALSE;
}

static PyObject*
py_zfs_parse_source(pylibzfs_state_t *state,
		    py_zfs_obj_t *pyzfs,
		    zprop_source_t sourcetype,
		    const char *source)
{
	PyObject *pysrc_type = NULL;
	PyObject *pysrc = NULL;
	PyObject *out = NULL;

	pysrc_type = py_get_property_source(pyzfs->pylibzfsp, sourcetype);
	if (sourcetype == ZPROP_SRC_INHERITED) {
		pysrc = PyUnicode_FromString(source);
		if (pysrc == NULL) {
			Py_DECREF(pysrc_type);
			return NULL;
		}
	} else {
		pysrc = Py_NewRef(Py_None);
	}

	out = PyStructSequence_New(state->struct_zfs_prop_src_type);
	if (out == NULL) {
		Py_DECREF(pysrc_type);
		Py_DECREF(pysrc);
		return NULL;
	}

	PyStructSequence_SET_ITEM(out, 0, pysrc_type);
	PyStructSequence_SET_ITEM(out, 1, pysrc);
	return out;
}

static
PyObject *py_parse_zfs_prop(zfs_prop_t prop, char *propbuf, PyObject *raw)
{
	PyObject *out = NULL;
	char *pend;

	/* literal "none", convert to None type */
	if (strcmp(propbuf, LIBZFS_NONE_VALUE) == 0) {
		Py_RETURN_NONE;
	}

	/* Begin custom property parsers */
	switch (prop) {
	case ZFS_PROP_MOUNTED:
		if (strcmp(propbuf, "yes") == 0)
			Py_RETURN_TRUE;

		Py_RETURN_FALSE;
		break;
	default:
		break;
	}

	/* End custom property parsers */

	/* Generic parser based on property type */
	if (zfs_prop_is_string(prop)) {
		// For strings, just create new reference to original value
		return Py_NewRef(raw);
	} else {
		/*
		 * If for some reason we have a decimal point in raw string
		 * convert it to a python float
		 */
		if (strstr(propbuf, "."))
			return PyFloat_FromString(raw);

		out = PyLong_FromString(propbuf, &pend, 10);
		if (out == NULL) {
			return NULL;
		}

		if (pend != (propbuf + strlen(propbuf))) {
			PyErr_Format(
				PyExc_ValueError,
				"%s: failed to parse value [%s] as "
				"a numeric value.",
				zfs_prop_to_name(prop),
				propbuf
			);
			Py_DECREF(out);
			return NULL;
		}
	}

	return out;
}

static
PyObject* py_zfs_get_prop(pylibzfs_state_t *state,
			  py_zfs_obj_t *pyzfs,
			  zfs_prop_t prop,
			  boolean_t get_source)
{
	PyObject *out = NULL;
	PyObject *raw = NULL;
	PyObject *parsed = NULL;
	PyObject *source = NULL;
	zprop_source_t sourcetype;
	char propbuf[ZFS_MAXPROPLEN];
	char sourcebuf[ZFS_MAX_DATASET_NAME_LEN] = {0};
	int err;

	Py_BEGIN_ALLOW_THREADS
	/*
	 * In some edge cases this libzfs function call
	 * will write a generally useless message into libzfs
	 * error buffer. This means we need to take lock
	 * in order to prevent corruption, but we don't need
	 * to actually look at it.
	 */
	PY_ZFS_LOCK(pyzfs->pylibzfsp);
	if (get_source) {
		err = zfs_prop_get(pyzfs->zhp,
				   prop,
				   propbuf,
				   sizeof(propbuf),
				   &sourcetype,
				   sourcebuf,
				   sizeof(sourcebuf),
				   B_TRUE);
	} else {
		err = zfs_prop_get(pyzfs->zhp,
				   prop,
				   propbuf,
				   sizeof(propbuf),
				   &sourcetype,
				   sourcebuf,
				   sizeof(sourcebuf),
				   B_TRUE);
	}
	PY_ZFS_UNLOCK(pyzfs->pylibzfsp);
	Py_END_ALLOW_THREADS

	if ((err == -1) && (
	    (prop == ZFS_PROP_SNAPSHOTS_CHANGED) ||
	    (prop == ZFS_PROP_ENCRYPTION_ROOT) ||
	    (prop == ZFS_PROP_KEYSTATUS) ||
	    (prop == ZFS_PROP_ORIGIN) ||
	    (prop == ZFS_PROP_REDACT_SNAPS)
	)) {
		/*
		 * Serveral libzfs properties return failure if they
		 * are unitialized and we should re-interpret as None
		 * This avoids spurious RuntimeError being raised.
		 */

		/* set value to "none" to ensure it's parsed to None type */
		strlcpy(propbuf, LIBZFS_NONE_VALUE, sizeof(propbuf));

		/*
		 * Make sure that our source buf is empty string.
		 * We zero-initialize the buffer above, but it's better
		 * to be extra sure.
		 */
		*sourcebuf = '\0';
		sourcetype = ZPROP_SRC_NONE;
	} else if (err) {
		if (!py_zfs_prop_valid_for_type(prop, pyzfs->ctype))
			return NULL;

		PyErr_Format(
			PyExc_RuntimeError,
			"%s: failed to get property for dataset.",
			zfs_prop_to_name(prop)
		);
		return NULL;
	}

	raw = PyUnicode_FromString(propbuf);
	if (raw == NULL)
		return NULL;

	parsed = py_parse_zfs_prop(prop, propbuf, raw);
	if (parsed == NULL) {
		Py_DECREF(raw);
		return NULL;
	}

	if (get_source) {
		source = py_zfs_parse_source(state, pyzfs, sourcetype, sourcebuf);
		if (source == NULL) {
			Py_DECREF(raw);
			Py_DECREF(parsed);
			return NULL;
		}
	} else {
		/*
		 * set to None type since we can't safely leave NULL values in
		 * sequence objects (for instance, can lead to this can cause
		 * json.dumps to segfault).
		 */
		source = Py_NewRef(Py_None);
	}

	out = PyStructSequence_New(state->struct_zfs_prop_type);
	if (out == NULL) {
		Py_DECREF(raw);
		Py_DECREF(parsed);
		Py_DECREF(source);
		return NULL;

	}

	PyStructSequence_SET_ITEM(out, 0, parsed);
	PyStructSequence_SET_ITEM(out, 1, raw);
	PyStructSequence_SET_ITEM(out, 2, source);

	return out;
}

PyObject *py_zfs_get_properties(py_zfs_obj_t *pyzfs,
			        PyObject *prop_set,
			        boolean_t get_source)
{
	pylibzfs_state_t *state = py_get_module_state(pyzfs->pylibzfsp);
	PyObject *out = NULL;
	size_t idx;
	int rv;

	out = PyStructSequence_New(state->struct_zfs_props_type);
	if (out == NULL)
		goto fail;

	/*
	 * Note that the following iterates the property table and checks
	 * whether the properties set contains the property. This means
	 * that set contents that are the wrong type are ignored, and was
	 * a deliberate choice to simplify logic and speed up the function.
	 *
	 * The key thing we want to avoid here is having any of the struct
	 * members set to NULL (since that will eventually cause a crash).
	 */
	for (idx = 0; idx < ARRAY_SIZE(zfs_prop_table); idx++) {
		PyObject *enum_obj = state->zfs_prop_enum_tbl[idx].obj;
		PyObject *pyprop;

		/*
		 * Requested properties will have type struct_zfs_property_data
		 * Unrequested properties will have python None type
		 *
		 * If the ZFS property has a value of None then you will have
		 * .<property_name>.raw = "none"
		 * .<property_name>.value = None
		 */

		if (enum_obj == NULL) {
			// property is not visible and so we don't expose it to
			// API consumers; however, we must not leave NULL otherwise
			// it can lead to segfault when doing JSON serialization.
			PyStructSequence_SET_ITEM(out, idx, Py_NewRef(Py_None));
			continue;
		}

		rv = PySet_Contains(prop_set, enum_obj);
		if (rv == -1) {
			goto fail;
		} else if (rv == 0) {
			// entry doesn't exist in set. Set to None
			PyStructSequence_SET_ITEM(out, idx, Py_NewRef(Py_None));
			continue;
		}

		// At this point the set contains the property
		pyprop = py_zfs_get_prop(state,
					 pyzfs,
					 state->zfs_prop_enum_tbl[idx].type,
					 get_source);
		if (pyprop == NULL) {
			goto fail;
		}

		PyStructSequence_SET_ITEM(out, idx, pyprop);
	}

	return out;
fail:
	Py_XDECREF(out);
	return NULL;
}

/*
 * The functions below are for conversion of the above struct sequence objects
 * into python dictionaries.
 */
static
PyObject *py_zfs_src_to_dict(PyObject *pysrc)
{
	int idx;
	PyObject *out = NULL;

	out = PyDict_New();
	if (out == NULL)
		return NULL;

	for (idx = 0; idx < struct_zfs_prop_src_type_desc.n_in_sequence; idx++) {
		const char *name = struct_zfs_prop_src[idx].name;
		PyObject *value = PyStructSequence_GET_ITEM(pysrc, idx);

		if (PyDict_SetItemString(out, name, value ? value : Py_None)) {
			Py_DECREF(out);
			return NULL;
		}
	}

	return out;
}

static
PyObject *py_zfs_prop_to_dict(PyObject *pyprop)
{
	int idx;
	PyObject *out = NULL;
	int err;

	out = PyDict_New();
	if (out == NULL)
		return NULL;

	for (idx = 0; idx < struct_zfs_prop_type_desc.n_in_sequence; idx++) {
		const char *name = struct_zfs_prop[idx].name;
		PyObject *value = PyStructSequence_GET_ITEM(pyprop, idx);

		if ((value != Py_None) && (strcmp(name, "source") == 0)) {
			PyObject *src_dict = py_zfs_src_to_dict(value);
			if (src_dict == NULL) {
				Py_DECREF(out);
				return NULL;
			}
			err = PyDict_SetItemString(out, name, src_dict);
			Py_DECREF(src_dict);
		} else {
			err = PyDict_SetItemString(out, name, value);
		}
		if (err) {
			Py_DECREF(out);
			return NULL;
		}
	}
	return out;
}

PyObject *py_zfs_props_to_dict(py_zfs_obj_t *pyzfs, PyObject *pyprops)
{
	int idx;
	PyObject *out = NULL;
	pylibzfs_state_t *state = py_get_module_state(pyzfs->pylibzfsp);

	out = PyDict_New();
	if (out == NULL)
		return NULL;

	for (idx = 0; idx < state->struct_zfs_prop_desc.n_in_sequence; idx++) {
		const char *name = state->struct_prop_fields[idx].name;
		PyObject *pyprop = NULL;
		PyObject *value = NULL;
		int err;

		// Check if this is a hidden property
		if (strcmp(name, PyStructSequence_UnnamedField) == 0)
			continue;

		value = PyStructSequence_GET_ITEM(pyprops, idx);
		if (value == Py_None)
			continue;

		pyprop = py_zfs_prop_to_dict(value);
		if (pyprop == NULL) {
			Py_DECREF(out);
			return NULL;
		}

		err = PyDict_SetItemString(out, name, pyprop);
		Py_DECREF(pyprop);

		if (err) {
			Py_DECREF(out);
			return NULL;

		}
	}

	return out;
}
