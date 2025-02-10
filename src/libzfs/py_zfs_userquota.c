#include "../truenas_pylibzfs.h"

/*
 * Note we're intentionally leaving off Domain here to simplify API
 * We haven't seen any datasets in the wild using Solaris SMB-style user quotas
 */
PyStructSequence_Field struct_zfs_userquota [] = {
	{"quota_type", "ZFSUserQuota enum identifying quota type."},
	{"xid", "Either unix ID or RID (solaris SMB) to which quota applies."},
	{"value", "The numeric value of the quota."},
	{0},
};

PyStructSequence_Desc struct_zfs_userquota_type_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_zfs_userquota",
	.fields = struct_zfs_userquota,
	.doc = "Python ZFS user quota structure.",
	.n_in_sequence = 3
};

void init_py_struct_userquota_state(pylibzfs_state_t *state)
{
	PyTypeObject *obj;

	obj = PyStructSequence_NewType(&struct_zfs_userquota_type_desc);
	PYZFS_ASSERT(obj, "Failed to create ZFS userquota type");

	state->struct_zfs_userquota_type = obj;
}

PyObject *py_zfs_userquota(PyTypeObject *userquota_struct,
			   PyObject *pyqtype,
			   uid_t xid,
			   uint64_t value)
{
	PyObject *pyxid;
	PyObject *pyval;
	PyObject *out = NULL;

	pyxid = PyLong_FromLong(xid);
	if (pyxid == NULL) {
		return NULL;
	}

	pyval = PyLong_FromUnsignedLong(value);
	if (pyval == NULL) {
		Py_DECREF(pyxid);
		return NULL;
	}

	out = PyStructSequence_New(userquota_struct);
	if (out == NULL) {
		Py_DECREF(pyxid);
		Py_DECREF(pyval);
		return NULL;
	}

	PyStructSequence_SET_ITEM(out, 0, pyqtype);
	PyStructSequence_SET_ITEM(out, 1, pyxid);
	PyStructSequence_SET_ITEM(out, 2, pyval);

	return out;
}

static
boolean_t add_quota_to_nvlist(nvlist_t *nvl,
			      zfs_userquota_prop_t qtype,
			      uint64_t xid,
			      uint64_t value)
{
	char prop[ZFS_MAXPROPLEN];
	char quota[ZFS_MAXPROPLEN];
	const char *prefix = NULL;

        prefix = zfs_userquota_prop_prefixes[qtype];
        snprintf(prop, sizeof(prop), "%s%lu", prefix, xid);
        if (value)
                snprintf(quota, sizeof(quota), "%lu", value);
        else
                strlcpy(quota, LIBZFS_NONE_VALUE, sizeof(quota));

	fnvlist_add_string(nvl, prop, quota);

	return B_TRUE;
}

static
boolean_t py_add_quota_to_nvlist(pylibzfs_state_t *state,
				 PyObject *py_quota,
				 nvlist_t *nvl)
{
	PyObject *py_qtype = PyDict_GetItemString(py_quota, "quota_type");
	PyObject *py_xid = PyDict_GetItemString(py_quota, "xid");
	PyObject *py_val = PyDict_GetItemString(py_quota, "value");
	long qtype;
	unsigned long xid;
	unsigned long val = 0;
	boolean_t ret;

	if (py_qtype == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"quota_type key is required");
		return B_FALSE;
	}

	if (py_xid == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"xid key is required");
		return B_FALSE;
	}

	if (py_qtype == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"value key is required");
		return B_FALSE;
	}

	if (!PyObject_IsInstance(py_qtype, state->zfs_uquota_enum)) {
		PyErr_SetString(PyExc_TypeError,
				"Not a valid ZFSUserQuota");
		return B_FALSE;
	}

	/* First validate quota type */
	qtype = PyLong_AsLong(py_qtype);
	PYZFS_ASSERT(
		((qtype >= 0) && (qtype < ZFS_NUM_USERQUOTA_PROPS)),
		"Invalid quota type"
	);

	if ((qtype == ZFS_PROP_USERUSED) ||
	    (qtype == ZFS_PROP_USEROBJUSED) ||
	    (qtype == ZFS_PROP_GROUPUSED) ||
	    (qtype == ZFS_PROP_GROUPOBJUSED) ||
	    (qtype == ZFS_PROP_PROJECTUSED) ||
	    (qtype == ZFS_PROP_PROJECTOBJUSED)) {
	    PyErr_SetString(PyExc_ValueError,
			    "Specified quota property is readonly.");
		return B_FALSE;
	}

	/* now validate xid */
	xid = PyLong_AsUnsignedLong(py_xid);
	if ((xid == (unsigned long)-1) && PyErr_Occurred())
		return B_FALSE;

	if ((xid > MAXUID) &&
	    (qtype != ZFS_PROP_PROJECTQUOTA) &&
	    (qtype != ZFS_PROP_PROJECTOBJQUOTA)) {
		PyErr_SetString(PyExc_ValueError,
				"Value is too large for quota type.");
		return B_FALSE;
	}

	/* now validate value */
	if (py_val != Py_None) {
		val = PyLong_AsUnsignedLong(py_val);
		if ((val == (unsigned long)-1) && PyErr_Occurred())
			return B_FALSE;
	}

	Py_BEGIN_ALLOW_THREADS
	ret = add_quota_to_nvlist(nvl, qtype, xid, val);
	Py_END_ALLOW_THREADS
	return ret;
}

nvlist_t *py_userquotas_to_nvlist(pylibzfs_state_t *state, PyObject *uquotas)
{
	nvlist_t *out = NULL;
	PyObject *iterator = NULL;
	PyObject *item = NULL;

	iterator = PyObject_GetIter(uquotas);
	if (iterator == NULL)
		return NULL;

	out = fnvlist_alloc();

	while ((item = PyIter_Next(iterator))) {
		if (!PyDict_Check(item)) {
			PyObject *repr = PyObject_Repr(item);
			PyErr_Format(PyExc_TypeError,
				     "%V: expected dictionary",
				     repr, "UNKNOWN");

			Py_XDECREF(repr);
			fnvlist_free(out);
			Py_DECREF(item);
			return NULL;
		}
		if (!py_add_quota_to_nvlist(state, item, out)) {
			fnvlist_free(out);
			Py_DECREF(item);
			return NULL;
		}
		Py_DECREF(item);
	}
	Py_DECREF(iterator);

	return out;
}
