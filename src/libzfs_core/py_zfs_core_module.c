#include "../truenas_pylibzfs.h"
typedef struct {
	PyObject *zc_exc;
	PyObject *errorcode;
	PyObject *parent_module;
} pylibzfs_core_state_t;


static
pylibzfs_core_state_t *get_lzc_mod_state(PyObject *module)
{
	pylibzfs_core_state_t *state = NULL;

	state = (pylibzfs_core_state_t *)PyModule_GetState(module);
	PYZFS_ASSERT(state, "Failed to get libzfs core module state.");

	return state;
}

static
PyObject *py_snap_history_msg(const char *op,
			      const char *target,
			      PyObject *snaps,
			      PyObject *user_props)
{
	PyObject *out = NULL;
	Py_ssize_t sz = PyObject_Length(snaps);

	if (sz == -1)
		return NULL;

	if (user_props) {
		out = PyUnicode_FromFormat("truenas_pylibzfs: %s %zi snapshots "
					   "of datasets within pool \"%s\" "
					   "with user properties: %U",
					   op, sz, target, user_props);
	} else {
		out = PyUnicode_FromFormat("truenas_pylibzfs: %s %zi snapshots "
					   "of datasets within pool \"%s\"",
					   op, sz, target);
	}

	return out;
}

static
boolean_t py_zfs_core_log_snap_history(const char *op,
				       const char *target,
				       PyObject *snaplist,
				       PyObject *user_props)
{
	libzfs_handle_t *lz = NULL;
	PyObject *logmsg;
	const char *msg;
	int err;

	logmsg = py_snap_history_msg(op, target, snaplist, user_props);
	if (logmsg == NULL)
		return B_FALSE;

	Py_BEGIN_ALLOW_THREADS
	lz = libzfs_init();
	Py_END_ALLOW_THREADS
	if (lz == NULL) {
		Py_DECREF(logmsg);
		return B_FALSE;
	}

	msg = PyUnicode_AsUTF8(logmsg);
	if (msg == NULL) {
		libzfs_fini(lz);
		Py_DECREF(logmsg);
		return B_FALSE;
	}

	Py_BEGIN_ALLOW_THREADS
	err = zpool_log_history(lz, msg);
	libzfs_fini(lz);
	Py_END_ALLOW_THREADS

	Py_DECREF(logmsg);
	return err ? B_FALSE : B_TRUE;
}


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
static
void setup_zfs_core_exception(PyObject *module)
{
	PyObject *dict = NULL;
	PyObject *errno_mod = NULL;
	pylibzfs_core_state_t *state = get_lzc_mod_state(module);

	dict = Py_BuildValue("{s:i,s:s,s:s,s:O}",
			     "code", EZFS_UNKNOWN,
			     "msg", "",
			     "name", "",
			     "errors", Py_None);
	PYZFS_ASSERT(dict, "Failed to set up ZFSCoreException");

	state->zc_exc = PyErr_NewExceptionWithDoc(PYLIBZFS_MODULE_NAME
						  ".lzc.ZFSCoreException",
						  py_zfs_core_exception__doc__,
						  PyExc_RuntimeError,
						  dict);

	Py_DECREF(dict);
	PYZFS_ASSERT(state->zc_exc, "Failed to set up ZFSCoreException");

	/* Get reference to errno.errorcode to convert errno to name */
	errno_mod = PyImport_ImportModule("errno");
	PYZFS_ASSERT(errno_mod, "Failed to import errno module.");

	state->errorcode = PyObject_GetAttrString(errno_mod, "errorcode");
	PYZFS_ASSERT(state->errorcode, "Failed to retrieve errorcode dict.");
	Py_DECREF(errno_mod);
}

static
void set_zfscore_exc(PyObject *module,
		     const char *msg,
		     int code,
		     PyObject *errors_tuple)
{
	pylibzfs_core_state_t *state = get_lzc_mod_state(module);
	PyObject *v = NULL;
	PyObject *attrs = NULL;
	const char *name = NULL;
	PyObject *error_name = NULL;
	PyObject *pycode = NULL;
	int err;

	pycode = Py_BuildValue("i", code);
	if (pycode == NULL)
		return;

	name = zfs_error_name(code);
	if (strcmp(name, "UNKNOWN") == 0) {
		error_name = PyDict_GetItem(state->errorcode, pycode);
		if ((error_name == NULL) && PyErr_Occurred()) {
			Py_DECREF(pycode);
			return;
		} else if (error_name != NULL) {
			Py_INCREF(error_name);
		}
	}

	/*
	 * NULL is possible in following scenarios
	 * 1. errorcode lacks an entry for the errno
	 * 2. ZFS errno was encoded in nvlist
	 */
	if (error_name == NULL) {
		error_name = PyUnicode_FromString(name);
		if (error_name == NULL) {
			Py_DECREF(pycode);
			return;
		}
	}

	v = PyObject_CallFunction(state->zc_exc, "s", msg);
	if (v == NULL)
		return;

	/* This steals references to pycode and error_name */
	attrs = Py_BuildValue(
		"(NsNO)",
		pycode,
		msg,
		error_name,
		errors_tuple
        );

	if (attrs == NULL) {
		Py_DECREF(v);
		Py_XDECREF(pycode);
		Py_XDECREF(error_name);
		return;
	}

	err = PyObject_SetAttrString(v, "code", PyTuple_GetItem(attrs, 0));
	if (err == -1) {
		Py_DECREF(v);
		Py_DECREF(attrs);
		return;
	}

	err = PyObject_SetAttrString(v, "msg", PyTuple_GetItem(attrs, 1));
	if (err == -1) {
		Py_DECREF(v);
		Py_DECREF(attrs);
		return;
	}

	err = PyObject_SetAttrString(v, "name", PyTuple_GetItem(attrs, 2));
	if (err == -1) {
		Py_DECREF(v);
		Py_DECREF(attrs);
		return;
	}

	err = PyObject_SetAttrString(v, "errors", PyTuple_GetItem(attrs, 3));
	if (err == -1) {
		Py_DECREF(v);
		Py_DECREF(attrs);
		return;
	}

	PyErr_SetObject((PyObject *) Py_TYPE(v), v);
	Py_DECREF(v);
	Py_DECREF(attrs);
	return;
}

typedef boolean_t (*py_lzc_parse_f)(nvlist_t *list,
				    PyObject *item,
				    PyObject *dsname_set,
				    char *pool_name,
				    size_t pool_name_sz);

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

	Py_BEGIN_ALLOW_THREADS
	fnvlist_add_boolean(list, snap);
	Py_END_ALLOW_THREADS

	return B_TRUE;
}

static
boolean_t py_entry_to_hold_nvpair(nvlist_t *list,
				  PyObject *item,
				  PyObject *snap_set,
				  char *pool_name,
				  size_t pool_name_sz)
{
	const char *snap, *tag;
	PyObject *py_snap = NULL;
	int err;

	if (!PyTuple_Check(item) || PyTuple_Size(item) != 2) {
		PyErr_SetString(PyExc_TypeError,
				"Expected tuple containing the"
				"snapshot name and hold tag.");
		return B_FALSE;
	}

	py_snap = PyTuple_GetItem(item, 0);
	if (py_snap == NULL)
		return B_FALSE;

	snap = PyUnicode_AsUTF8(py_snap);
	if (snap == NULL)
		return B_FALSE;

	tag = PyUnicode_AsUTF8(PyTuple_GET_ITEM(item, 1));
	if (tag == NULL)
		return B_FALSE;

	if (!zfs_name_valid(snap, ZFS_TYPE_SNAPSHOT)) {
		PyErr_Format(PyExc_TypeError,
			     "%s: not a valid snapshot name",
			     snap);
		return B_FALSE;
	}

	if (strlen(tag) > ZFS_MAX_DATASET_NAME_LEN) {
		PyErr_Format(PyExc_ValueError,
			     "%s: hold tag is longer than "
			     "%d bytes.", tag,
			     ZFS_MAX_DATASET_NAME_LEN);
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

	if (PySet_Contains(snap_set, py_snap)) {
		PyErr_Format(PyExc_ValueError,
			     "%s: multiple holds of the same "
			     "snapshot is not permitted in same call.",
			     snap);
		return B_FALSE;
	}

	err = PySet_Add(snap_set, py_snap);
	if (err)
		return B_FALSE;

	Py_BEGIN_ALLOW_THREADS
	fnvlist_add_string(list, snap, tag);
	Py_END_ALLOW_THREADS

	return B_TRUE;
}

static
boolean_t py_entry_to_rel_nvpair(nvlist_t *list,
				 PyObject *item,
				 PyObject *snap_set,
				 char *pool_name,
				 size_t pool_name_sz)
{
	const char *snap, *tag;
	PyObject *py_snap = NULL;
	nvlist_t *target;
	int err;

	if (!PyTuple_Check(item) || PyTuple_Size(item) != 2) {
		PyErr_SetString(PyExc_TypeError,
				"Expected tuple containing the"
				"snapshot name and hold tag.");
		return B_FALSE;
	}

	py_snap = PyTuple_GetItem(item, 0);
	if (py_snap == NULL)
		return B_FALSE;

	snap = PyUnicode_AsUTF8(py_snap);
	if (snap == NULL)
		return B_FALSE;

	tag = PyUnicode_AsUTF8(PyTuple_GET_ITEM(item, 1));
	if (tag == NULL)
		return B_FALSE;

	if (!zfs_name_valid(snap, ZFS_TYPE_SNAPSHOT)) {
		PyErr_Format(PyExc_TypeError,
			     "%s: not a valid snapshot name",
			     snap);
		return B_FALSE;
	}

	if (strlen(tag) > ZFS_MAX_DATASET_NAME_LEN) {
		PyErr_Format(PyExc_ValueError,
			     "%s: hold tag is longer than "
			     "%d bytes.", tag,
			     ZFS_MAX_DATASET_NAME_LEN);
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

	err = PySet_Add(snap_set, py_snap);
	if (err)
		return B_FALSE;

	Py_BEGIN_ALLOW_THREADS
	if (nvlist_lookup_nvlist(list, snap, &target) == 0) {
		fnvlist_add_boolean(target, tag);
	} else {
		nvlist_t *target = fnvlist_alloc();
		fnvlist_add_boolean(target, tag);
		fnvlist_add_nvlist(list, snap, target);
		fnvlist_free(target);
	}

	Py_END_ALLOW_THREADS

	return B_TRUE;
}
static
nvlist_t *py_iter_to_snaps(PyObject *obj,
			   char *pool,
			   size_t pool_size,
			   py_lzc_parse_f fn)
{
	nvlist_t *out = NULL;
	PyObject *item = NULL;
	PyObject *iterator = NULL;
	PyObject *dsname_set = NULL;

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

		ok = fn(out, item, dsname_set, pool, pool_size);
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
		entry = Py_BuildValue("(si)", "Operation failed", error);
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

PyDoc_STRVAR(py_zfs_core_create_holds__doc__,
"create_holds(*, holds, cleanup_fd=False) -> tuple\n"
"-------------------------------------------------\n\n"
"Bulk create \"user holds\" on snapshots. If there is a hold on a snapshot, the\n"
"snapshot can not be destroyed; however, it may be marked for deletion by\n"
"destroy_snapshots(snapshot_names={<snapshot_name>}, defer=True)\n"
"\n"
"Multiple holds in the same operation for the same snapshot are not permitted.\n"
"\n"
"Parameters\n"
"----------\n"
"holds: iterable, required\n"
"    Iterable (set, list, tuple, etc) containing hold tuples to create.\n"
"    Each hold tuple is of the form (<snapshot_name>, <hold_key>) where\n"
"    snapshot_name: str: name of the snapshot to hold\n"
"    hold_key: str: name of the hold\n"
"\n"
"cleanup_fd: int, optional, default=-1\n"
"    The cleanup_fd must be the result of open(ZFS_DEV, O_EXCL). When the\n"
"    cleanup_fd is closed (including on process termination), the holds will be\n"
"    released. If the system is shut down uncleanly, the holds will be released\n"
"    when the pool is next opened or imported.\n"
"\n"
"    If -1 is specifed (the default), then the hold persists until it is\n"
"    explicitly removed.\n"
"\n"
"Returns\n"
"-------\n"
"tuple of snapshots that could not be held because they no longer exist.\n"
"    The libzfs_core library does not consider this to be a failure\n"
"    condition.\n"
"\n"
"Raises\n"
"------\n"
"TypeError:\n"
"- \"holds\" is not iterable.\n"
"- \"holds\" contains an entry that is a tuple.\n"
"- The first item in the hold tuple is not a valid snapshot name.\n"
"\n"
"ValueError:\n"
"- \"holds\" was omitted or is empty\n"
"- \"holds\" contains entries for multiple pools\n"
"\n"
"ZFSCoreException:\n"
"- Failed to create one or more of the specified holds.\n"
"  The snapshot name of failed hold and error numbers are reported by the\n"
"  exception's \"errors\" attribute.\n"
);
static PyObject *py_lzc_create_holds(PyObject *self,
				     PyObject *args_unused,
				     PyObject *kwargs)
{
	PyObject *py_holds = NULL;
	PyObject *py_errors;
	nvlist_t *holds = NULL;
	nvlist_t *errors = NULL;
	char pool[ZFS_MAX_DATASET_NAME_LEN] = { 0 }; // must be zero-initialized
	int err;
	int cleanup_fd = -1;

	char *kwnames [] = { "holds", "cleanup_fd", NULL };

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$Oi",
					 kwnames,
					 &py_holds,
					 &cleanup_fd)) {
		return NULL;
	}

	if (py_holds == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"holds keyword argument is required.");
		return NULL;
	}

	holds = py_iter_to_snaps(py_holds, pool, sizeof(pool),
				 py_entry_to_hold_nvpair);
	if (holds == NULL)
		return NULL;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.create_holds", "O",
			kwargs) < 0) {
		return NULL;
	}

	/* For now we're not exposing nvlist of properties to set */
	Py_BEGIN_ALLOW_THREADS
	err = lzc_hold(holds, cleanup_fd, &errors);
	fnvlist_free(holds);
	Py_END_ALLOW_THREADS

	if (err) {
		py_errors = nvlist_errors_to_err_tuple(errors, err);

		Py_BEGIN_ALLOW_THREADS
		fnvlist_free(errors);
		Py_END_ALLOW_THREADS

		if (py_errors == NULL)
			return NULL;

		set_zfscore_exc(self, "lzc_hold() failed", err, py_errors);
		return NULL;
	}

	if (nvlist_empty(errors)) {
		py_errors = PyTuple_New(0);
	} else {
		py_errors = nvlist_errors_to_err_tuple(errors, err);
	}

	Py_BEGIN_ALLOW_THREADS
	fnvlist_free(errors);
	Py_END_ALLOW_THREADS

	if (!py_zfs_core_log_snap_history("lzc_hold()", pool, py_holds, NULL))
		return NULL;

	return py_errors;
}

PyDoc_STRVAR(py_zfs_core_release_holds__doc__,
"release_holds(*, holds) -> tuple\n"
"--------------------------------\n\n"
"Bulk release \"user holds\" on snapshots. If the snapshot has been marked\n"
"for deferred destroy, it does not have any clones, and all the user holds\n"
"are removed, then the snapshot will be destroyed.\n"
"\n"
"Parameters\n"
"----------\n"
"holds: iterable, required\n"
"    Iterable (set, list, tuple, etc) containing hold tuples to release.\n"
"    Each hold tuple is of the form (<snapshot_name>, <hold_key>) where\n"
"    snapshot_name: str: name of the snapshot to hold\n"
"    hold_key: str: name of the hold\n"
"\n"
"cleanup_fd: int, optional, default=-1\n"
"    The cleanup_fd must be the result of open(ZFS_DEV, O_EXCL). When the\n"
"    cleanup_fd is closed (including on process termination), the holds will be\n"
"    released. If the system is shut down uncleanly, the holds will be released\n"
"    when the pool is next opened or imported.\n"
"\n"
"Returns\n"
"-------\n"
"tuple of holds that could not be released because they no longer exist.\n"
"    The libzfs_core library does not consider this to be a failure\n"
"    condition.\n"
"\n"
"Raises\n"
"------\n"
"TypeError:\n"
"- \"holds\" is not iterable.\n"
"- \"holds\" contains an entry that is a tuple.\n"
"- The first item in the hold tuple is not a valid snapshot name.\n"
"\n"
"ValueError:\n"
"- \"holds\" was omitted or is empty\n"
"- \"holds\" contains entries for multiple pools\n"
"\n"
"ZFSCoreException:\n"
"- Failed to release one or more of the specified holds.\n"
"  The hold name and error numbers are reported by the exception's \"errors\" \n"
"  attribute.\n"
"\n"
"Example\n"
"-------\n"
"release_holds(holds={(\"d/myzv@now\", \"foo4\"), (\"d/myzv@now\", \"foo3\")})\n"
);
static PyObject *py_lzc_release_holds(PyObject *self,
				      PyObject *args_unused,
				      PyObject *kwargs)
{
	PyObject *py_holds = NULL;
	PyObject *py_errors;
	nvlist_t *holds = NULL;
	nvlist_t *errors = NULL;
	char pool[ZFS_MAX_DATASET_NAME_LEN] = { 0 }; // must be zero-initialized
	int err;

	char *kwnames [] = { "holds", NULL };

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$O",
					 kwnames,
					 &py_holds)) {
		return NULL;
	}

	if (py_holds == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"holds keyword argument is required.");
		return NULL;
	}

	holds = py_iter_to_snaps(py_holds, pool, sizeof(pool),
				 py_entry_to_rel_nvpair);
	if (holds == NULL)
		return NULL;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.release_holds", "O",
			kwargs) < 0) {
		return NULL;
	}

	/* For now we're not exposing nvlist of properties to set */
	Py_BEGIN_ALLOW_THREADS
	err = lzc_release(holds, &errors);
	fnvlist_free(holds);
	Py_END_ALLOW_THREADS

	if (err || !nvlist_empty(errors)) {
		py_errors = nvlist_errors_to_err_tuple(errors, err);

		Py_BEGIN_ALLOW_THREADS
		fnvlist_free(errors);
		Py_END_ALLOW_THREADS

		if (py_errors == NULL)
			return NULL;

		set_zfscore_exc(self, "lzc_release() failed", err, py_errors);
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	fnvlist_free(errors);
	Py_END_ALLOW_THREADS

	if (!py_zfs_core_log_snap_history("lzc_release()", pool, py_holds, NULL))
		return NULL;

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_core_create_snaps__doc__,
"create_snapshots(*, snapshot_names, user_properties=None) -> None\n"
"-------------------------------------------\n\n"
"Bulk create ZFS snapshots. Arguments are keyword-only.\n\n"
""
"Parameters\n"
"----------\n"
"snapshot_names: iterable\n"
"    Iterable (set, list, tuple, etc) containing names of snapshots to create.\n\n"
"user_properties: dict, optional\n"
"    Optional user properties to set on the newly-created snapshots.\n"
"    The user properties are specified as key value pairs in a python dictionary.\n"
"    The key specifies the name of the ZFS user property and must meet the \n"
"    requirements for a valid user property name. See man 7 zfsprops for details.\n"
"    The value for the user property must be a string.\n\n"
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
	PyObject *py_props_dict = NULL;
	PyObject *user_props_json = NULL;
	nvlist_t *snaps = NULL;
	nvlist_t *errors = NULL;
	nvlist_t *user_props = NULL;
	char pool[ZFS_MAX_DATASET_NAME_LEN] = { 0 }; // must be zero-initialized
	int rv;

	char *kwnames [] = {
		"snapshot_names",
		"user_properties",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$OO",
					 kwnames,
					 &py_snaps,
					 &py_props_dict)) {
		return NULL;
	}

	if (py_snaps == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"snapshot_names parameter is required");
		return NULL;
	}

	snaps = py_iter_to_snaps(py_snaps, pool, sizeof(pool),
				 py_snapname_to_nvpair);
	if (snaps == NULL)
		return NULL;

	if (py_props_dict && py_props_dict != Py_None) {
		user_props = py_userprops_dict_to_nvlist(py_props_dict);
		if (user_props == NULL) {
			Py_DECREF(snaps);
			return NULL;
		}
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.create_snapshots", "O",
			kwargs) < 0) {
		return NULL;
	}

	/* For now we're not exposing nvlist of properties to set */
	Py_BEGIN_ALLOW_THREADS
	rv = lzc_snapshot(snaps, user_props, &errors);
	fnvlist_free(snaps);
	Py_END_ALLOW_THREADS

	if (rv != 0) {
		/*
		 * lzc_snapshot will create an nvlist of snapshot names
		 * that failed and the error code for each of them.
		 * This converts it into a ZFSCoreException with a tuple
		 * of tuples (snapshot name, error code) so that API
		 * consumer can do something about it.
		 */
		py_errors = nvlist_errors_to_err_tuple(errors, rv);

		Py_BEGIN_ALLOW_THREADS
		fnvlist_free(errors);
		fnvlist_free(user_props);
		Py_END_ALLOW_THREADS

		if (py_errors == NULL)
			return NULL;

		set_zfscore_exc(self, "lzc_snapshot() failed", rv, py_errors);
		return NULL;
	}

	if (user_props)
		user_props_json = py_dump_nvlist(user_props, B_TRUE);

	Py_BEGIN_ALLOW_THREADS
	fnvlist_free(errors);
	fnvlist_free(user_props);
	Py_END_ALLOW_THREADS

	rv = py_zfs_core_log_snap_history("lzc_snapshot()",
					  pool, py_snaps,
					  user_props_json);
	Py_XDECREF(user_props_json);

	if (rv != B_TRUE)
		return NULL;

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
	char pool[ZFS_MAX_DATASET_NAME_LEN] = { 0 }; // must be zero-initialized
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

	snaps = py_iter_to_snaps(py_snaps, pool, sizeof(pool),
				 py_snapname_to_nvpair);
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
		py_errors = nvlist_errors_to_err_tuple(errors, err);

		Py_BEGIN_ALLOW_THREADS
		fnvlist_free(errors);
		Py_END_ALLOW_THREADS

		if (py_errors == NULL)
			return NULL;

		set_zfscore_exc(self, "lzc_destroy_snaps() failed", err, py_errors);
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	fnvlist_free(errors);
	Py_END_ALLOW_THREADS

	if (!py_zfs_core_log_snap_history("lzc_destroy_snaps()", pool, py_snaps, NULL))
		return NULL;

	Py_RETURN_NONE;
}

static int
py_zfs_core_module_clear(PyObject *module)
{
	pylibzfs_core_state_t *state = get_lzc_mod_state(module);
	Py_CLEAR(state->zc_exc);
	Py_CLEAR(state->errorcode);
	Py_CLEAR(state->parent_module);
	return 0;
}

static void
py_zfs_core_module_free(void *module)
{
	if (module)
		py_zfs_core_module_clear((PyObject *)module);
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
	{
		.ml_name = "create_holds",
		.ml_meth = (PyCFunction)py_lzc_create_holds,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_core_create_holds__doc__
	},
	{
		.ml_name = "release_holds",
		.ml_meth = (PyCFunction)py_lzc_release_holds,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_core_release_holds__doc__
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
	.m_name = PYLIBZFS_MODULE_NAME ".lzc",
	.m_doc = py_zfs_core_module__doc__,
	.m_size = sizeof(pylibzfs_core_state_t),
	.m_methods = TruenasPylibzfsCoreMethods,
	.m_clear = py_zfs_core_module_clear,
	.m_free = py_zfs_core_module_free,
};

PyObject *py_setup_lzc_module(PyObject *parent)
{
	pylibzfs_core_state_t *state = NULL;
	PyObject *mlzc = PyModule_Create(&truenas_pylibzfs_core);
	if (mlzc == NULL)
		return NULL;

	setup_zfs_core_exception(mlzc);

	PYZFS_ASSERT((libzfs_core_init() == 0), "Failed to open libzfs_core fd");

	state = get_lzc_mod_state(mlzc);

	state->parent_module = Py_NewRef(parent);

	if (PyModule_AddObjectRef(mlzc, "ZFSCoreException", state->zc_exc) < 0) {
		Py_DECREF(mlzc);
		return NULL;
	}

	return mlzc;
}
