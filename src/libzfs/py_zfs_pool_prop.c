#include "../truenas_pylibzfs.h"
#include <zfs_prop.h>

/*
 * ZFS pool property implementation.
 *
 * Pool properties differ from dataset properties in the following ways:
 * - Many are read-only statistics (e.g. SIZE, CAPACITY, FRAGMENTATION).
 * - None are inheritable; ZPROP_SRC_INHERITED never appears.
 * - There is no batch-set API — zpool_set_prop() is called per property.
 *
 * The return type of get_properties() is struct_zpool_property, a
 * dynamically-created PyStructSequence with one slot per ZPOOL_NUM_PROPS
 * entry (indexed 0..ZPOOL_NUM_PROPS-1, excluding ZPOOL_PROP_INVAL=-1).
 * Unrequested slots are set to Py_None; requested slots contain the
 * parsed property value directly (int for numeric properties, str otherwise).
 */

/*
 * Generate a doc string for a pool property field.
 * The returned string is allocated via PyMem_New and must be freed with
 * PyMem_Free() when the struct sequence type is cleaned up.
 */
static
char *py_create_zpool_prop_doc(const char *name, zpool_prop_t prop)
{
	char buf[512] = {0};
	char *out;

	if (zpool_prop_readonly(prop)) {
		snprintf(buf, sizeof(buf),
			 "%s: this property is read-only.", name);
	} else if (zpool_prop_setonce(prop)) {
		snprintf(buf, sizeof(buf),
			 "%s: this property can only be set at pool creation.",
			 name);
	} else {
		snprintf(buf, sizeof(buf), "%s: pool property.", name);
	}

	out = PyMem_New(char, strlen(buf) + 1);
	if (out == NULL)
		return NULL;

	memcpy(out, buf, strlen(buf) + 1);
	return out;
}

/*
 * Build the PyStructSequence_Field array and create the
 * struct_zpool_property type.  Called once during module init.
 */
void init_py_struct_zpool_prop_state(pylibzfs_state_t *state)
{
	size_t i;
	PyTypeObject *obj;

	for (i = 0; i < ZPOOL_NUM_PROPS; i++) {
		zpool_prop_t prop = (zpool_prop_t)i;
		const char *name = zpool_prop_to_name(prop);
		char *doc = py_create_zpool_prop_doc(name, prop);
		char *heap_name = pymem_strdup(name);

		/*
		 * If either allocation failed, bail out hard — we can't
		 * operate correctly without these fields.
		 */
		PYZFS_ASSERT(heap_name, "Malloc failure in pool prop fields.");
		PYZFS_ASSERT(doc, "Malloc failure in pool prop fields.");

		state->struct_zpool_prop_fields[i].name = heap_name;
		state->struct_zpool_prop_fields[i].doc  = doc;
	}

	/* NULL sentinel required by PyStructSequence_NewType */
	state->struct_zpool_prop_fields[ZPOOL_NUM_PROPS] =
	    (PyStructSequence_Field){0};

	state->struct_zpool_prop_desc = (PyStructSequence_Desc){
		.name         = PYLIBZFS_MODULE_NAME ".struct_zpool_property",
		.fields       = state->struct_zpool_prop_fields,
		.n_in_sequence = ZPOOL_NUM_PROPS,
	};

	obj = PyStructSequence_NewType(&state->struct_zpool_prop_desc);
	PYZFS_ASSERT(obj, "Failed to allocate struct_zpool_props_type");
	state->struct_zpool_props_type = obj;
}

/*
 * Retrieve a single pool property and return a struct_zfs_property_data
 * (value, raw, source).
 *
 * source is always None: pool properties have no inheritance hierarchy.
 *
 * This is an internal helper; caller must hold the GIL.
 */
static
PyObject *py_zpool_get_one_prop(pylibzfs_state_t *state,
				py_zfs_pool_t *p,
				zpool_prop_t prop)
{
	char propbuf[ZFS_MAXPROPLEN];
	int err;
	PyObject *raw = NULL;
	PyObject *parsed = NULL;

	int async_err = 0;

	do {
		Py_BEGIN_ALLOW_THREADS
		PY_ZFS_LOCK(p->pylibzfsp);
		err = zpool_get_prop(p->zhp, prop, propbuf, sizeof(propbuf),
		    NULL, B_TRUE);
		PY_ZFS_UNLOCK(p->pylibzfsp);
		Py_END_ALLOW_THREADS
	} while (err != 0 && errno == EINTR &&
	    !(async_err = PyErr_CheckSignals()));

	if (async_err)
		return NULL;

	if (err) {
		PyErr_Format(PyExc_RuntimeError,
			     "%s: failed to get pool property: %s",
			     zpool_prop_to_name(prop),
			     strerror(errno));
		return NULL;
	}

	raw = PyUnicode_FromString(propbuf);
	if (raw == NULL)
		return NULL;

	/*
	 * Parse the value based on property type.
	 *   NUMBER → PyLong (literal=B_TRUE guarantees a decimal integer string)
	 *   INDEX  → PyUnicode (on/off/etc.; literal has no effect on index)
	 *   STRING → PyUnicode
	 */
	if (zpool_prop_get_type(prop) == PROP_TYPE_NUMBER) {
		char *pend;
		parsed = PyLong_FromString(propbuf, &pend, 10);
		if (parsed == NULL) {
			/*
			 * Unexpected non-integer in the numeric property
			 * buffer.  Fall back to the raw string rather than
			 * propagating an exception.
			 */
			PyErr_Clear();
			parsed = Py_NewRef(raw);
		} else if (pend != (propbuf + strlen(propbuf))) {
			/* Partial parse — use string */
			Py_DECREF(parsed);
			parsed = Py_NewRef(raw);
		}
	} else {
		/* PROP_TYPE_STRING or PROP_TYPE_INDEX: use raw string */
		parsed = Py_NewRef(raw);
	}

	Py_DECREF(raw);
	return parsed;
}

/*
 * Build a struct_zpool_property struct sequence containing the requested
 * pool properties.
 *
 * prop_set – a Python set of ZPOOLProperty enum members to retrieve.
 *            Properties absent from the set are set to Py_None.
 */
PyObject *py_zpool_get_properties(py_zfs_pool_t *p,
				   PyObject *prop_set)
{
	pylibzfs_state_t *state = py_get_module_state(p->pylibzfsp);
	PyObject *out = NULL;
	size_t i;
	int rv;

	out = PyStructSequence_New(state->struct_zpool_props_type);
	if (out == NULL)
		return NULL;

	for (i = 0; i < ZPOOL_NUM_PROPS; i++) {
		PyObject *enum_obj = state->zpool_prop_enum_tbl[i].obj;
		PyObject *pyprop;

		if (enum_obj == NULL) {
			/*
			 * No enum member for this index — should not happen
			 * since all ZPOOL_NUM_PROPS indices have entries, but
			 * guard defensively.
			 */
			PyStructSequence_SET_ITEM(out, i, Py_NewRef(Py_None));
			continue;
		}

		rv = PySet_Contains(prop_set, enum_obj);
		if (rv == -1) {
			Py_DECREF(out);
			return NULL;
		} else if (rv == 0) {
			/* Not requested — leave as None */
			PyStructSequence_SET_ITEM(out, i, Py_NewRef(Py_None));
			continue;
		}

		/* Property was requested; retrieve it */
		pyprop = py_zpool_get_one_prop(state, p,
		    state->zpool_prop_enum_tbl[i].type);
		if (pyprop == NULL) {
			Py_DECREF(out);
			return NULL;
		}

		PyStructSequence_SET_ITEM(out, i, pyprop);
	}

	return out;
}

/* Storage for (name, value) C-string pairs, safe to use without GIL */
typedef struct { char name[ZFS_MAXPROPLEN]; char val[ZFS_MAXPROPLEN]; } pair_t;

/*
 * Set pool properties from a Python dict {ZPOOLProperty|str: str|int|bool}.
 *
 * All validation (readonly, setonce, type conversion) happens before the ZFS
 * lock is taken.  The actual zpool_set_prop() calls happen under a single
 * lock acquisition to minimise lock overhead.
 */

PyObject *py_zpool_set_properties(py_zfs_pool_t *p, PyObject *propsdict)
{
	pylibzfs_state_t *state = py_get_module_state(p->pylibzfsp);
	PyObject *key, *value;
	Py_ssize_t pos = 0;
	Py_ssize_t n, idx;
	int ret = 0;
	int log_err;
	py_zfs_error_t zfs_err;
	pair_t *pairs;
	int async_err;

	n = PyDict_Size(propsdict);
	if (n == 0)
		Py_RETURN_NONE;
	if (n < 0)
		return NULL;

	/* Storage for (name, value) C-string pairs, safe to use without GIL */
	pairs = PyMem_RawCalloc(n, sizeof(pair_t));
	if (pairs == NULL) {
		PyErr_NoMemory();
		return NULL;
	}

	idx = 0;
	while (PyDict_Next(propsdict, &pos, &key, &value)) {
		zpool_prop_t zprop;
		const char *propname;
		const char *vstr;

		/* ---- resolve key ---- */
		if (PyUnicode_Check(key)) {
			propname = PyUnicode_AsUTF8(key);
			if (propname == NULL)
				goto fail;
			zprop = zpool_name_to_prop(propname);
		} else if (PyObject_IsInstance(key, state->zpool_property_enum)) {
			long lval = PyLong_AsLong(key);
			if (lval == -1 && PyErr_Occurred())
				goto fail;
			zprop = (zpool_prop_t)lval;
			propname = zpool_prop_to_name(zprop);
		} else {
			PyObject *repr = PyObject_Repr(key);
			PyErr_Format(PyExc_TypeError,
				     "%V: unexpected key type. "
				     "Expected ZPOOLProperty or str.",
				     repr, "UNKNOWN");
			Py_XDECREF(repr);
			goto fail;
		}

		if (zprop == ZPOOL_PROP_INVAL) {
			PyErr_Format(PyExc_ValueError,
				     "invalid or unknown pool property");
			goto fail;
		}

		/* ---- validate ---- */
		if (zpool_prop_readonly(zprop)) {
			PyErr_Format(PyExc_ValueError,
				     "%s: property is read-only",
				     zpool_prop_to_name(zprop));
			goto fail;
		}

		if (zpool_prop_setonce(zprop)) {
			PyErr_Format(PyExc_ValueError,
				     "%s: property can only be set at pool "
				     "creation time",
				     zpool_prop_to_name(zprop));
			goto fail;
		}

		strlcpy(pairs[idx].name, propname, sizeof(pairs[idx].name));

		/* ---- convert value ---- */
		if (PyBool_Check(value)) {
			strlcpy(pairs[idx].val,
				(value == Py_True) ? "on" : "off",
				sizeof(pairs[idx].val));
		} else if (PyLong_Check(value)) {
			long long ival = PyLong_AsLongLong(value);
			if (ival == -1 && PyErr_Occurred())
				goto fail;
			snprintf(pairs[idx].val, sizeof(pairs[idx].val),
				 "%lld", ival);
		} else if (PyUnicode_Check(value)) {
			vstr = PyUnicode_AsUTF8(value);
			if (vstr == NULL)
				goto fail;
			strlcpy(pairs[idx].val, vstr,
				sizeof(pairs[idx].val));
		} else {
			PyErr_Format(PyExc_TypeError,
				     "property value must be str, int, "
				     "or bool");
			goto fail;
		}

		idx++;
	}

	n = idx; /* actual count after iteration (same as initial, but clean) */

	/* ---- set all properties under a single lock acquisition ---- */
	idx = 0;
eintr_retry:
	async_err = 0;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	for (; idx < n && ret == 0; idx++) {
		ret = zpool_set_prop(p->zhp,
				     pairs[idx].name,
				     pairs[idx].val);

		if ((ret == 0) && (errno != EINTR)) {
			py_get_zfs_error(p->pylibzfsp->lzh, &zfs_err);
		}
	}
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if ((ret != 0) && (errno == EINTR) && !(async_err = PyErr_CheckSignals())) {
		goto eintr_retry;
	}

	PyMem_RawFree(pairs);

	if (async_err)
		return NULL;

	if (ret) {
		set_exc_from_libzfs(&zfs_err, "zpool_set_prop() failed");
		return NULL;
	}

	log_err = py_log_history_fmt(p->pylibzfsp,
	    "zpool set (properties) %s", zpool_get_name(p->zhp));
	if (log_err)
		return NULL;

	Py_RETURN_NONE;

fail:
	PyMem_RawFree(pairs);
	return NULL;
}

/*
 * Convert a struct_zpool_property struct sequence to a plain Python dict.
 * Slots that are Py_None (unrequested) are skipped.
 * Each slot value is the parsed property value directly (int or str).
 */
PyObject *py_zpool_props_to_dict(py_zfs_pool_t *p, PyObject *pyprops)
{
	pylibzfs_state_t *state = py_get_module_state(p->pylibzfsp);
	PyObject *out;
	int idx;

	out = PyDict_New();
	if (out == NULL)
		return NULL;

	for (idx = 0; idx < (int)state->struct_zpool_prop_desc.n_in_sequence;
	    idx++) {
		const char *name = state->struct_zpool_prop_fields[idx].name;
		PyObject *slot_val;
		int err;

		if (name == NULL)
			continue;

		slot_val = PyStructSequence_GET_ITEM(pyprops, idx);
		if (slot_val == Py_None)
			continue;

		err = PyDict_SetItemString(out, name, slot_val);
		if (err) {
			Py_DECREF(out);
			return NULL;
		}
	}

	return out;
}
