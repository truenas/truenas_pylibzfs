#include "../truenas_pylibzfs.h"

static
PyObject *PyExc_ZFSCoreError;

PyDoc_STRVAR(py_zfs_core_exception__doc__,
"ZFSCoreException(exception)\n"
"-----------------------\n\n"
"Python wrapper around error information returned by libzfs core\n\n"
"attributes:\n"
"-----------\n"
"code: int\n"
"    libzfs errno (one of ZFSError) or regular errno\n"
"name: str\n"
"    human-readable name of the libzfs errno\n"
"errors: tuple | None\n"
"    tuple of tuples containing error information returned by libzfs_core\n\n"
"NOTE: the libzfs error may wrap around conventional OS errno. In this case\n"
"it will be mapped to equivalent libzfs errno, but if that's not possible the\n"
"libzfs errno will be set to EZFS_UNKNOWN and strerror output written to\n"
"the error description field.\n"
);
PyObject *setup_zfs_core_exception(void)
{
	PyObject *dict = NULL;

	dict = Py_BuildValue("{s:i,s:s,s:s,s:O}",
			     "code", EZFS_UNKNOWN,
			     "msg", "",
			     "name", "",
			     "errors", Py_None);
	if (dict == NULL)
		return NULL;

	PyExc_ZFSCoreError = PyErr_NewExceptionWithDoc(PYLIBZFS_MODULE_NAME
						       "lzc.ZFSCoreException",
						       py_zfs_core_exception__doc__,
						       PyExc_RuntimeError,
						       dict);

	Py_DECREF(dict);
	return PyExc_ZFSCoreError;
}

static
void set_zfscore_exc(const char *msg, int code, PyObject *errors_tuple)
{
	PyObject *v = NULL;
	PyObject *args = NULL;
	PyObject *attrs = NULL;
	const char *name = NULL;
	int err;

	name = zfs_error_name(code);

	v = PyObject_CallFunction(PyExc_ZFSCoreError, "s", msg);
	if (v == NULL)
		return;

	attrs = Py_BuildValue(
		"(issO)",
		code,
		msg,
		name,
		errors_tuple
        );

	if (attrs == NULL) {
		Py_XDECREF(v);
		goto simple_err;
	}

	err = PyObject_SetAttrString(v, "code", PyTuple_GetItem(attrs, 0));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		Py_DECREF(attrs);
		return;
	}

	err = PyObject_SetAttrString(v, "msg", PyTuple_GetItem(attrs, 1));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		Py_DECREF(attrs);
		return;
	}

	err = PyObject_SetAttrString(v, "name", PyTuple_GetItem(attrs, 2));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		Py_DECREF(attrs);
		return;
	}

	err = PyObject_SetAttrString(v, "errors", PyTuple_GetItem(attrs, 3));
	if (err == -1) {
		Py_CLEAR(args);
		Py_CLEAR(v);
		Py_DECREF(attrs);
		return;
	}

	PyErr_SetObject((PyObject *) Py_TYPE(v), v);
	Py_DECREF(v);
	Py_DECREF(attrs);
	return;

simple_err:
	PyErr_SetString(PyExc_ZFSCoreError, msg);
}

static
boolean_t py_snapname_to_nvpair(nvlist_t *list,
				PyObject *item,
				PyObject *dsname_set,
				char *pool_name,
				size_t pool_name_sz)
{
	const char *snap;
	Py_ssize_t sz;
	char dsname[ZFS_MAX_DATASET_NAME_LEN];
	char *psep = NULL;
	PyObject *py_dsname = NULL;
	int err;

	if (!PyUnicode_Check(item)) {
		PyObject *repr = PyObject_Repr(item);
		if (repr == NULL)
			return B_FALSE;

		PyErr_Format(PyExc_TypeError,
			     "%R: item  is not a string",
			     repr);

		Py_DECREF(repr);
		return B_FALSE;
	}

	snap = PyUnicode_AsUTF8AndSize(item, &sz);
	if (snap == NULL)
		return B_FALSE;

	if (!zfs_name_valid(snap, ZFS_TYPE_SNAPSHOT)) {
		PyErr_Format(PyExc_TypeError,
			     "%s: not a valid snapshot name",
			     snap);
		return B_FALSE;
	}

	if (*pool_name == '\0') {
		/* pool name hasn't been set yet */
		strlcpy(pool_name, snap, pool_name_sz);
		pool_name[strcspn(pool_name, "/@")] = '\0';
	}

	if (strncmp(snap, pool_name, strlen(pool_name)) != 0) {
		PyErr_Format(PyExc_ValueError,
			     "%s: snapshot is not within "
			     "expected pool [%s]. All "
			     "snapshots must reside in the "
			     "same pool.",
			     snap, pool_name);
		return B_FALSE;
	}

	strlcpy(dsname, snap, sizeof(dsname));

	psep = strstr(dsname, "@");
	PYZFS_ASSERT(psep, "Missing snapshot separator");

	*psep = '\0';

	py_dsname = PyUnicode_FromString(dsname);
	if (!py_dsname)
		return B_FALSE;

	if (PySet_Contains(dsname_set, py_dsname)) {
		Py_DECREF(py_dsname);
		PyErr_Format(PyExc_ValueError,
			     "%s: multiple snapshots of the same "
			     "dataset is not permitted.",
			     snap);
		return B_FALSE;
	}

	err = PySet_Add(dsname_set, py_dsname);
	Py_DECREF(py_dsname);
	if (err)
		return B_FALSE;

	fnvlist_add_boolean(list, snap);

	return B_TRUE;
}


static
nvlist_t *py_iter_to_snaps(PyObject *obj)
{
	nvlist_t *out = NULL;
	PyObject *item = NULL;
	PyObject *iterator = NULL;
	PyObject *dsname_set = NULL;
	char pool[ZFS_MAX_DATASET_NAME_LEN] = { 0 }; // must be zero-initialized

	iterator = PyObject_GetIter(obj);
	if (iterator == NULL)
		return NULL;

	/*
	 * We're building a set of all dataset names encountered
	 * so that we can validate that they are unique in the list.
	 * This is to give more user-friendly error if list contains
	 * something like ["dozer@now", "dozer@now2"].
	 */
	dsname_set = PySet_New(NULL);
	if (dsname_set == NULL)
		return NULL;

	out = fnvlist_alloc();

	while ((item = PyIter_Next(iterator))) {
		boolean_t ok;
		ok = py_snapname_to_nvpair(out,
					   item,
					   dsname_set,
					   pool,
					   sizeof(pool));

		Py_DECREF(item);
		if (!ok) {
			fnvlist_free(out);
			Py_DECREF(dsname_set);
			Py_DECREF(iterator);
			return NULL;
		}
	}

	Py_DECREF(iterator);

	if (PySet_Size(dsname_set) == 0) {
		Py_DECREF(dsname_set);
		fnvlist_free(out);
		PyErr_SetString(PyExc_ValueError,
				"At least one snapshot name must "
				"be specified");
		return NULL;
	}

	Py_XDECREF(dsname_set);

	return out;
}

static
PyObject *nvlist_errors_to_err_tuple(nvlist_t *errors, int error)
{
	PyObject *err = NULL;
	PyObject *entry = NULL;
	PyObject *out = NULL;
	nvpair_t *elem;

	err = PyList_New(0);
	if (err == NULL)
		return NULL;

	for (elem = nvlist_next_nvpair(errors, NULL);
	    elem != NULL;
	    elem = nvlist_next_nvpair(errors, elem)) {
		const char *snap = NULL;
		int this_err;

		snap = nvpair_name(elem);
		this_err = fnvpair_value_int32(elem);

		entry = Py_BuildValue("(si)", snap, this_err);
		if (entry == NULL) {
			Py_XDECREF(err);
			return NULL;
		}
		if (PyList_Append(err, entry) < 0) {
			Py_XDECREF(entry);
			Py_XDECREF(err);
			return NULL;
		}

		Py_XDECREF(entry);
	}

	if (PyList_Size(err) == 0) {
		entry = Py_BuildValue("(%s, %i)", "Operation failed", error);
		if (entry == NULL) {
			Py_XDECREF(err);
			return NULL;
		}

		if (PyList_Append(err, entry) < 0) {
			Py_XDECREF(entry);
			Py_XDECREF(err);
			return NULL;
		}
	}

	out = PyList_AsTuple(err);
	Py_XDECREF(err);
	return out;
}

PyDoc_STRVAR(py_zfs_core_create_snaps__doc__,
"create_snapshots(*, snapshot_names) -> None\n"
"-------------------------------------------\n\n"
"Bulk create ZFS snapshots. Arguments are keyword-only.\n\n"
""
"Parameters\n"
"----------\n"
"snapshot_names: iterable\n"
"    Iterable (set, list, tuple, etc) containing names of snapshots to create.\n\n"
""
"Returns\n"
"-------\n"
"None\n\n"
""
"Raises\n"
"------\n"
"TypeError:\n"
"    \"snapshot_names\" is not iterable.\n"
"    \"snapshot_names\" contains an entry that is not a valid snapshot name.\n"
"\n"
"ValueError:\n"
"    Multiple entries for same dataset were specified\n"
"    \"snapshot_names\" was omitted or is empty\n"
"    \"snapshot_names\" contains entries for multiple pools\n"
"\n"
"ZFSCoreException:\n"
"    Failed to create one or more of the specified snapshots.\n"
"    The failed snapshots and error numbers are reported by the\n"
"    exception's \"errors\" attribute.\n"
);
static PyObject *py_lzc_create_snaps(PyObject *self,
				     PyObject *args_unused,
				     PyObject *kwargs)
{
	PyObject *py_snaps = NULL;
	PyObject *py_errors;
	nvlist_t *snaps = NULL;
	nvlist_t *errors = NULL;
	int err;

	char *kwnames [] = { "snapshot_names", NULL };

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$O",
					 kwnames,
					 &py_snaps)) {
		return NULL;
	}

	if (py_snaps == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"snapshot_names parameter is required");
		return NULL;
	}

	snaps = py_iter_to_snaps(py_snaps);
	if (snaps == NULL)
		return NULL;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.create_snapshots", "O",
			kwargs) < 0) {
		return NULL;
	}

	/* For now we're not exposing nvlist of properties to set */
	Py_BEGIN_ALLOW_THREADS
	err = lzc_snapshot(snaps, NULL, &errors);
	fnvlist_free(snaps);
	Py_END_ALLOW_THREADS

	if (err) {
		/*
		 * lzc_snapshot will create an nvlist of snapshot names
		 * that failed and the error code for each of them.
		 * This converts it into a ZFSCoreException with a tuple
		 * of tuples (snapshot name, error code) so that API
		 * consumer can do something about it.
		 */
		nvlist_errors_to_err_tuple(errors, err);
		py_errors = nvlist_errors_to_err_tuple(errors, err);

		Py_BEGIN_ALLOW_THREADS
		fnvlist_free(errors);
		Py_END_ALLOW_THREADS

		if (py_errors == NULL)
			return NULL;

		set_zfscore_exc("lzc_snapshot() failed", err, py_errors);
		return NULL;
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_core_destroy_snaps__doc__,
"destroy_snapshots(*, snapshot_names, defer_destroy=False) -> None\n"
"------------------------------------------------------------------\n\n"
"Bulk destroy ZFS snapshots. Arguments are keyword-only.\n\n"
""
"Parameters\n"
"----------\n"
"snapshot_names: iterable\n"
"    Iterable (set, list, tuple, etc) containing names of snapshots to destroy\n"
"defer: bool, optional, default=False\n"
"    If a snapshot has user holds or clones, it will be marked for deferred\n"
"    destruction, and will be destroyed when the last hold or close is removed\n"
"    or destroyed.\n"
"\n"
"Returns\n"
"-------\n"
"None\n\n"
""
"Raises\n"
"------\n"
"TypeError:\n"
"    \"snapshot_names\" is not iterable.\n"
"    \"snapshot_names\" contains an entry that is not a valid snapshot name.\n"
"\n"
"ValueError:\n"
"    Multiple entries for same dataset were specified\n"
"    \"snapshot_names\" was omitted or is empty\n"
"    \"snapshot_names\" contains entries for multiple pools\n"
"\n"
"ZFSCoreException:\n"
"    Failed to destroy one or more of the specified snapshots.\n"
"    The failed snapshots and error numbers are reported by the\n"
"    exception's \"errors\" attribute.\n"
);
static PyObject *py_lzc_destroy_snaps(PyObject *self,
				      PyObject *args_unused,
				      PyObject *kwargs)
{
	PyObject *py_snaps = NULL;
	PyObject *py_errors;
	nvlist_t *snaps = NULL;
	nvlist_t *errors = NULL;
	boolean_t defer = B_FALSE;
	int err;

	char *kwnames [] = {"snapshot_names", "defer_destroy", NULL};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$Op",
					 kwnames,
					 &py_snaps,
					 &defer)) {
		return NULL;
	}

	if (py_snaps == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"snapshot_names parameter is required");
		return NULL;
	}

	snaps = py_iter_to_snaps(py_snaps);
	if (snaps == NULL)
		return NULL;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.destroy_snapshots", "O",
			kwargs) < 0) {
		return NULL;
	}

	/* For now we're not exposing nvlist of properties to set */
	Py_BEGIN_ALLOW_THREADS
	err = lzc_destroy_snaps(snaps, defer, &errors);
	fnvlist_free(snaps);
	Py_END_ALLOW_THREADS

	if (err) {
		/*
		 * lzc_snapshot will create an nvlist of snapshot names
		 * that failed and the error code for each of them.
		 * This converts it into a ZFSCoreException with a tuple
		 * of tuples (snapshot name, error code) so that API
		 * consumer can do something about it.
		 */
		nvlist_errors_to_err_tuple(errors, err);
		py_errors = nvlist_errors_to_err_tuple(errors, err);

		Py_BEGIN_ALLOW_THREADS
		fnvlist_free(errors);
		Py_END_ALLOW_THREADS

		if (py_errors == NULL)
			return NULL;

		set_zfscore_exc("lzc_destroy_snaps() failed", err, py_errors);
		return NULL;
	}

	Py_RETURN_NONE;
}


/* Module method table */
static PyMethodDef TruenasPylibzfsCoreMethods[] = {
	{
		.ml_name = "create_snapshots",
		.ml_meth = (PyCFunction)py_lzc_create_snaps,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_core_create_snaps__doc__
	},
	{
		.ml_name = "destroy_snapshots",
		.ml_meth = (PyCFunction)py_lzc_destroy_snaps,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_core_destroy_snaps__doc__
	},
	{NULL}
};

PyDoc_STRVAR(py_zfs_core_module__doc__,
PYLIBZFS_MODULE_NAME " provides python bindings for libzfs_core for TrueNAS.\n"
"This is currently used to provide ability to perform bulk snapshot operations\n"
"without having to use a libzfs handle, which results in greater efficiency and\n"
"performance in a multithreaded application.\n"
);
/* Module structure */
static struct PyModuleDef truenas_pylibzfs_core = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = PYLIBZFS_MODULE_NAME,
	.m_doc = py_zfs_core_module__doc__,
	.m_methods = TruenasPylibzfsCoreMethods,
};



PyObject *py_setup_lzc_module(void)
{
	PyObject *zfs_exc;
	PyObject *mlzc = PyModule_Create(&truenas_pylibzfs_core);
	if (mlzc == NULL)
		return NULL;

	zfs_exc = setup_zfs_core_exception();
	if (zfs_exc == NULL) {
		Py_DECREF(mlzc);
		return NULL;
	}

	PYZFS_ASSERT((libzfs_core_init() == 0), "Failed to open libzfs_core fd");

	if (PyModule_AddObject(mlzc, "ZFSCoreException", zfs_exc) < 0) {
		Py_DECREF(mlzc);
		return NULL;
	}

	return mlzc;
}
