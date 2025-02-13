#include "../truenas_pylibzfs.h"

#define ZFS_VDEV_STR_PATH "<" PYLIBZFS_MODULE_NAME ".ZFSVdev(type=%U, path=%U, pool=%U)>"
#define ZFS_VDEV_STR "<" PYLIBZFS_MODULE_NAME ".ZFSVdev(type=%U, pool=%U)>"

static
PyObject *py_repr_zfs_vdev(PyObject *self) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *) self;
	PyObject *out;
	if (v->path) {
		out = PyUnicode_FromFormat(ZFS_VDEV_STR_PATH, v->type, v->path,
		    v->pool->name);
	} else {
		out = PyUnicode_FromFormat(ZFS_VDEV_STR, v->type,
		    v->pool->name);
	}
	return (out);
}

static
PyObject *py_zfs_vdev_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_vdev_t *self = NULL;
	self = (py_zfs_vdev_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_vdev_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_vdev_dealloc(py_zfs_vdev_t *self) {
	fnvlist_free(self->vdev_tree);
	Py_CLEAR(self->pool);
	Py_CLEAR(self->parent);
	Py_CLEAR(self->path);
	Py_CLEAR(self->type);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_zfs_vdev_asdict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_vdev_type__doc__,
"Returns the type of VDEV in string format.\n\n"
"This can be either of \"root\", \"mirror\", \"replacing\", \"raidz\", \n"
"\"draid\", \"disk\", \"file\", \"missing\",\"hole\", \"spare\", \"log\", \n"
"\"l2cache\", \"indirect\" or \"dspare\".\n"
);
static
PyObject *py_zfs_vdev_get_type(py_zfs_vdev_t *self, void *extra) {
	if (self->type == NULL)
		Py_RETURN_NONE;
	return (Py_NewRef(self->type));
}

PyDoc_STRVAR(py_zfs_vdev_path__doc__,
"Returns the path of VDEV if it exists in nvlist config of VDEV.\n\n"
"If path does not exist, for example in case of root, mirror, raidz VDEVs,\n"
"None is returned. Otherwise path of VDEV is returned in string format."
);
static
PyObject *py_zfs_vdev_get_path(py_zfs_vdev_t *self, void *extra) {
	if (self->path == NULL)
		Py_RETURN_NONE;
	return (Py_NewRef(self->path));
}

PyDoc_STRVAR(py_zfs_vdev_get_name__doc__,
"name(*) -> string\n\n"
"-----------------\n\n"
"Returns the name of the VDEV.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"String\n"
"    Value indicates the name of VDEV used in VDEV nvlist config.\n\n"
"Raises:\n"
"-------\n"
"PyErr_NoMemory:\n"
"    If libzfs fails to allocate memory for returning the path,\n"
"    PyErr_NoMemory will be raised.\n"
);
static
PyObject *py_zfs_vdev_get_name(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	PyObject *out = NULL;
	char *name = NULL;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(v->pool->pylibzfsp);
	name = zpool_vdev_name(v->pool->pylibzfsp->lzh, v->pool->zhp,
	    v->vdev_tree, VDEV_NAME_TYPE_ID);
	PY_ZFS_UNLOCK(v->pool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (name == NULL)
		return PyErr_NoMemory();

	out = PyUnicode_FromString(name);
	free(name);
	return (out);
}

PyDoc_STRVAR(py_zfs_vdev_get_guid__doc__,
"guid(*) -> Int\n\n"
"--------------\n\n"
"Returns current VDEV GUID.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"Int\n"
"    Value indicates the GUID of VDEV.\n\n"
);
static
PyObject *py_zfs_vdev_get_guid(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	uint64_t guid;

	Py_BEGIN_ALLOW_THREADS
	guid = fnvlist_lookup_uint64(v->vdev_tree, ZPOOL_CONFIG_GUID);
	Py_END_ALLOW_THREADS

	return (PyLong_FromUnsignedLongLong(guid));
}

static
const char *vdev_get_status_impl(nvlist_t *tree) {
	vdev_stat_t *vs;
	uint_t c;
	const char *state = NULL;
	Py_BEGIN_ALLOW_THREADS
	if (nvlist_lookup_uint64_array(tree, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0) {
		state = zpool_state_to_name(vs->vs_state, vs->vs_aux);
	}
	Py_END_ALLOW_THREADS
	return (state);
}

PyDoc_STRVAR(py_zfs_vdev_get_status__doc__,
"status(*) -> String\n\n"
"-------------------\n\n"
"Returns the status of the VDEV.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"String\n"
"    Value indicates the state of VDEV used in VDEV nvlist config. If state\n"
"    of VDEV is not found in nvlist config for VDEV, None is returned.\n\n"
);
static
PyObject *py_zfs_vdev_get_status(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	const char *state = vdev_get_status_impl(v->vdev_tree);
	if (state == NULL)
		Py_RETURN_NONE;
	return (PyUnicode_FromString(state));
}

PyDoc_STRVAR(py_zfs_vdev_get_size__doc__,
"size(*) -> Int\n\n"
"--------------\n\n"
"Returns the size of the VDEV.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"Int\n"
"    Value indicates the size of VDEV.\n\n"
);
static
PyObject *py_zfs_vdev_get_size(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	uint64_t asize = 0;
	uint64_t ashift = 0;
	uint64_t size;
	boolean_t found;

	Py_BEGIN_ALLOW_THREADS
	found = (nvlist_lookup_uint64(v->vdev_tree, ZPOOL_CONFIG_ASIZE,
	    &asize) == 0 && nvlist_lookup_uint64(v->vdev_tree,
	    ZPOOL_CONFIG_ASHIFT, &ashift) == 0);
	Py_END_ALLOW_THREADS

	if (!found)
		Py_RETURN_NONE;
	size = asize << ashift;
	return (PyLong_FromUnsignedLongLong(size));
}

PyDoc_STRVAR(py_zfs_vdev_get_children__doc__,
"children(*) -> Tuple\n\n"
"--------------------\n\n"
"Returns ZFSVdev objects for child VDEVs in a Tuple.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"Tuple\n"
"    Tuple contains ZFSVDev objects for child VDEVs under current VDEV.\n\n"
);
static
PyObject *py_zfs_vdev_get_children(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	nvlist_t  **child;
	uint_t children;
	boolean_t found;
	PyObject *list, *obj;

	Py_BEGIN_ALLOW_THREADS
	found = (nvlist_lookup_nvlist_array(v->vdev_tree, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0);
	Py_END_ALLOW_THREADS

	if (!found)
		Py_RETURN_NONE;

	list = PyTuple_New(children);
	PYZFS_ASSERT(list, "Failed to create Python list object.");
	for (uint_t i = 0; i < children; ++i) {
		obj = (PyObject *)init_zfs_vdev(v->pool, child[i], self);
		PYZFS_ASSERT(obj, "Failed to create ZFSVdev child object.");
		PyTuple_SET_ITEM(list, i, obj);
	}

	return (list);
}

static
void vdev_get_disks_impl(PyObject *list, nvlist_t *tree) {
	const char *ctype;
	const char *cstate;
	const char *cpath;
	PyObject *path;
	nvlist_t **child;
	uint_t children;
	boolean_t found;

	cstate = vdev_get_status_impl(tree);
	if (strcmp(cstate, "UNAVAIL") == 0 || strcmp(cstate, "OFFLINE") == 0)
		return;
	Py_BEGIN_ALLOW_THREADS
	ctype = fnvlist_lookup_string(tree, ZPOOL_CONFIG_TYPE);
	Py_END_ALLOW_THREADS
	if (strcmp(ctype, VDEV_TYPE_FILE) == 0)
		return;
	else if (strcmp(ctype, VDEV_TYPE_DISK) == 0) {
		Py_BEGIN_ALLOW_THREADS
		cpath = fnvlist_lookup_string(tree, ZPOOL_CONFIG_PATH);
		Py_END_ALLOW_THREADS
		path = PyUnicode_FromString(cpath);
		PYZFS_ASSERT((PyList_Append(list, path) == 0),
		    "Failed to add disk path to Python list.");
		return;
	} else {
		Py_BEGIN_ALLOW_THREADS
		found = (nvlist_lookup_nvlist_array(tree, ZPOOL_CONFIG_CHILDREN,
		    &child, &children) == 0);
		Py_END_ALLOW_THREADS
		if (found) {
			for (uint_t i = 0; i < children; ++i) {
				vdev_get_disks_impl(list, child[i]);
			}
		}
	}
	return;
}

PyDoc_STRVAR(py_zfs_vdev_get_disks__doc__,
"disks(*) -> Tuple\n\n"
"-----------------\n\n"
"Returns a Tuple containing path for all DISK type VDEVs.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"Tuple\n"
"    Tuple contains the path of all DISK VDEVs in String format.\n\n"
);
static
PyObject *py_zfs_vdev_get_disks(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	PyObject *list;
	PyObject *tuple;

	list = PyList_New(0);
	PYZFS_ASSERT(list, "Failed to create Python list object.");
	vdev_get_disks_impl(list, v->vdev_tree);
	tuple = PyList_AsTuple(list);
	Py_DECREF(list);

	return (tuple);
}

PyDoc_STRVAR(py_zfs_vdev_add_ashift__doc__,
"set_ashift(*, Int) -> None\n\n"
"--------------------------\n\n"
"Add ashift with given value to nvlist config.\n\n"
"Parameters\n"
"----------\n"
"Int: ASHIFT value to set\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    If argument is not a positive Integer, Type Error will be raised.\n\n"
"PyErr_NoMemory:\n"
"    If we fail to allocate memory for adding into nvlist,\n"
"    PyErr_NoMemory will be raised.\n\n"
);
static
PyObject *py_zfs_vdev_add_ashift(PyObject *self, PyObject *arg) {
	int ret = 0;
	uint64_t value = 0;
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;

	if (!PyLong_Check(arg)) {
		PyErr_SetString(PyExc_TypeError, "Argument must be an Integer");
		return (NULL);
	}

	value = PyLong_AsUnsignedLong(arg);
	if (PyErr_Occurred()) {
		PyErr_SetString(PyExc_TypeError,
		    "Failed to convert the argument to unsigned int");
		return (NULL);
	}
	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSVdev.add_ashift", "OO",
	    v->path ? v->path : v->type, arg) < 0) {
		return (NULL);
	}

	Py_BEGIN_ALLOW_THREADS
	ret = nvlist_add_uint64(v->vdev_tree, ZPOOL_CONFIG_ASHIFT, value);
	Py_END_ALLOW_THREADS

	if (ret)
		return (PyErr_NoMemory());

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_vdev_get_vdev_stats__doc__,
"vdev_state(*) -> dict\n\n"
"---------------------\n\n"
"Returns the VDEV Stats in a Python Dictionary.\n\n"
"Parameters\n"
"----------\n"
"None.\n\n"
"Returns\n"
"-------\n"
"Python dictionary containing following keys:\n"
"    timestamp: time since VDEV load\n\n"
"    size: total capacity\n\n"
"    allocated: space allocated\n\n"
"    read_errors: read errors in VDEV\n\n"
"    write_errors: write errors in VDEV\n\n"
"    checksum_errors: checksum error in VDEV\n\n"
"    ops: operation count\n\n"
"    bytes: bytes read/written\n\n"
"    configured_ashift: TLV VDEV ashift\n\n"
"    logical_ashift: VDEV logical ashift\n\n"
"    physical_ashift: VDEV physical ashift\n\n"
"    fragmentation: device fragmentation\n\n"
"    self_healed: self-healed bytes\n\n"
"Raises:\n"
"-------\n"
"None.\n\n"
);
static
PyObject *py_zfs_vdev_get_vdev_stats(PyObject *self, PyObject *args) {
	vdev_stat_t *vs;
	uint_t vsc;
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	PyObject *stats = NULL;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSVdev.vdev_stats", "O",
	    v->path ? v->path : v->type) < 0) {
		return (NULL);
	}

	Py_BEGIN_ALLOW_THREADS
	vs = (vdev_stat_t *)fnvlist_lookup_uint64_array(v->vdev_tree,
	    ZPOOL_CONFIG_VDEV_STATS, &vsc);
	Py_END_ALLOW_THREADS

	// Skip cheking for vs for success since we used fnvlist_* wrapper
	stats = Py_BuildValue(
		"{s:K,s:K,s:K,s:K,s:K,s:K,s:K,s:K,s:K,s:K,s:K,s:K,s:K}",
		"timestamp", vs->vs_timestamp,
		"size", vs->vs_space,
		"allocated", vs->vs_alloc,
		"read_errors", vs->vs_read_errors,
		"write_errors", vs->vs_write_errors,
		"checksum_errors", vs->vs_checksum_errors,
		"ops", vs->vs_ops,
		"bytes", vs->vs_bytes,
		"configured_ashift", vs->vs_configured_ashift,
		"logical_ashift", vs->vs_logical_ashift,
		"physical_ashift", vs->vs_physical_ashift,
		"fragmentation", vs->vs_fragmentation,
		"self_healed", vs->vs_self_healed);

	if (stats == NULL)
		return (NULL);
	return (stats);
}

PyDoc_STRVAR(py_zfs_vdev_degrade__doc__,
"degrade(*, truenas_pylibzfs.VDevAuxState) -> None\n\n"
"-------------------------------------------------\n\n"
"Mark the VDEV degraded with given AUX state.\n\n"
"Parameters\n"
"----------\n"
"truenas_pylibzfs.VdevAuxState: Enum that desribes VDEV AUX state.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    If argument is not truenas_pylibzfs.VDevAuxState, Type Error will be\n"
"    raised.\n\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while trying to set the state to degraded.\n\n"
);
static
PyObject *py_zfs_vdev_degrade(PyObject *self, PyObject *arg) {
	int ret;
	PyObject *value;
	vdev_aux_t state;
	py_zfs_error_t err;
	uint64_t guid;
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	PyObject *mod = (PyObject *)v->pool->pylibzfsp->module;

	PyObject *etype = PyObject_GetAttrString(mod, "VDevAuxState");
	if (etype == NULL)
		return (NULL);

	if (!PyObject_IsInstance(arg, etype)) {
		Py_DECREF(etype);
		PyErr_SetString(PyExc_TypeError,
		    "Expected VDevAuxState Enum type");
		return (NULL);
	}
	Py_DECREF(etype);

	value = PyObject_GetAttrString(arg, "value");
	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError,
		    "Not able to retrieve value from Enum");
		return (NULL);
	}
	state = PyLong_AsUnsignedLong(value);
	Py_DECREF(value);
	if (PyErr_Occurred()) {
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSVdev.degrade", "OO",
	    v->path ? v->path : v->type, arg) < 0) {
		return (NULL);
	}

	Py_BEGIN_ALLOW_THREADS
	guid = fnvlist_lookup_uint64(v->vdev_tree, ZPOOL_CONFIG_GUID);
	PY_ZFS_LOCK(v->pool->pylibzfsp);
	ret = zpool_vdev_degrade(v->pool->zhp, guid, state);
	if (ret)
		py_get_zfs_error(v->pool->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(v->pool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_vdev_degrade failed");
		return (NULL);
	} else {
		const char *vdev;
		if (v->path)
			vdev = PyUnicode_AsUTF8(v->path);
		else
			vdev = PyUnicode_AsUTF8(v->type);
		if (vdev == NULL)
			return (NULL);
		ret = py_log_history_fmt(v->pool->pylibzfsp,
		    "zpool_vdev_degrade %s %s", zpool_get_name(v->pool->zhp),
		    vdev);
		if (ret) {
			// An exception should be set since we failed to log
			// history
			return (NULL);
		}
	}
	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_vdev_fault__doc__,
"fault(*, truenas_pylibzfs.VDevAuxState) -> None\n\n"
"-----------------------------------------------\n\n"
"Mark the VDEV faulted with given AUX state.\n\n"
"Parameters\n"
"----------\n"
"truenas_pylibzfs.VdevAuxState: Enum that desribes VDEV AUX state.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    If argument is not truenas_pylibzfs.VDevAuxState, Type Error will be\n"
"    raised.\n\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while trying to set the state to faulted.\n\n"
);
static
PyObject *py_zfs_vdev_fault(PyObject *self, PyObject *arg) {
	int ret;
	PyObject *value;
	vdev_aux_t state;
	py_zfs_error_t err;
	uint64_t guid;
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	PyObject *mod = (PyObject *)v->pool->pylibzfsp->module;

	PyObject *etype = PyObject_GetAttrString(mod, "VDevAuxState");
	if (etype == NULL)
		return (NULL);

	if (!PyObject_IsInstance(arg, etype)) {
		Py_DECREF(etype);
		PyErr_SetString(PyExc_TypeError,
		    "Expected VDevAuxState Enum type");
		return (NULL);
	}
	Py_DECREF(etype);

	value = PyObject_GetAttrString(arg, "value");
	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError,
		    "Not able to retrieve value from Enum");
		return (NULL);
	}
	state = PyLong_AsUnsignedLong(value);
	Py_DECREF(value);
	if (PyErr_Occurred()) {
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSVdev.fault", "OO",
	    v->path ? v->path : v->type, arg) < 0) {
		return (NULL);
	}

	Py_BEGIN_ALLOW_THREADS
	guid = fnvlist_lookup_uint64(v->vdev_tree, ZPOOL_CONFIG_GUID);
	PY_ZFS_LOCK(v->pool->pylibzfsp);
	ret = zpool_vdev_fault(v->pool->zhp, guid, state);
	if (ret)
		py_get_zfs_error(v->pool->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(v->pool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_vdev_fault failed");
		return (NULL);
	} else {
		const char *vdev;
		if (v->path)
			vdev = PyUnicode_AsUTF8(v->path);
		else
			vdev = PyUnicode_AsUTF8(v->type);
		if (vdev == NULL)
			return (NULL);
	
		ret = py_log_history_fmt(v->pool->pylibzfsp,
		    "zpool_vdev_fault %s %s", zpool_get_name(v->pool->zhp),
		    vdev);
		if (ret) {
			// An exception should be set since we failed to log
			// history
			return (NULL);
		}
	}
	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_vdev_online__doc__,
"online(*, expand=False) -> None\n\n"
"-------------------------------\n\n"
"Set the VDEV stats to ONLINE.\n\n"
"Parameters\n"
"----------\n"
"expand: bool, optional, default=False\n"
"    Expand the VDEV to use all available space.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    If the operation is invoked on VDEV that is not of type DISK or FILE,\n"
"    TypeError will be raised.\n\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while trying to set the state to faulted.\n\n"
);
static
PyObject *py_zfs_vdev_online(PyObject *self, PyObject *args, PyObject *kwargs) {
	int flags = 0, expand = 0, ret;
	vdev_state_t vstate;
	py_zfs_error_t err;
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	const char *cpath;
	const char *ctype = PyUnicode_AsUTF8(v->type);

	if (ctype == NULL)
		return (NULL);
	if (strcmp(ctype, VDEV_TYPE_DISK) != 0 &&
	    strcmp(ctype, VDEV_TYPE_FILE) != 0) {
		PyErr_SetString(PyExc_TypeError,
		    "Only disk/file vdev can be set to online");
		return (NULL);
	}

	char *kwnames[] = {"expand", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|p", kwnames,
	    &expand)) {
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSVdev.online", "OO",
	    v->path ? v->path : v->type, kwargs ? kwargs : args) < 0) {
		return (NULL);
	}
	if (expand)
		flags |= ZFS_ONLINE_EXPAND;

	cpath = PyUnicode_AsUTF8(v->path);
	if (cpath == NULL)
		return (NULL);
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(v->pool->pylibzfsp);
	ret = zpool_vdev_online(v->pool->zhp, cpath, flags, &vstate);
	if (ret)
		py_get_zfs_error(v->pool->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(v->pool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool online failed");
		return (NULL);
	} else {
		ret = py_log_history_fmt(v->pool->pylibzfsp,
		    "zpool online %s%s %s", expand ? "-e " : "",
		    zpool_get_name(v->pool->zhp), cpath);
		if (ret) {
			// An exception should be set since we failed to log
			// history
			return (NULL);
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_vdev_offline__doc__,
"offline(*, temporary=False) -> None\n\n"
"-----------------------------------\n\n"
"Set the VDEV stats to OFFLINE.\n\n"
"Parameters\n"
"----------\n"
"temporary: bool, optional, default=False\n"
"    If set, the VDEV will revert to it's previous state on reboot.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    If the operation is invoked on VDEV that is not of type DISK or FILE,\n"
"    TypeError will be raised.\n\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while trying to set the state to faulted.\n\n"
);
static
PyObject *py_zfs_vdev_offline(PyObject *self, PyObject *args,
    PyObject *kwargs) {
	int temp = 0, ret;
	py_zfs_error_t err;
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	const char *cpath;
	const char *ctype = PyUnicode_AsUTF8(v->type);

	if (ctype == NULL)
		return (NULL);
	if (strcmp(ctype, VDEV_TYPE_DISK) != 0 &&
	    strcmp(ctype, VDEV_TYPE_FILE) != 0) {
		PyErr_SetString(PyExc_TypeError,
		    "Only disk/file vdev can be set to offline");
		return (NULL);
	}

	char *kwnames[] = {"temporary", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|p", kwnames,
	    &temp)) {
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSVdev.offline", "OO",
	    v->path ? v->path : v->type, kwargs ? kwargs : args) < 0) {
		return (NULL);
	}

	cpath = PyUnicode_AsUTF8(v->path);
	if (cpath == NULL)
		return (NULL);
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(v->pool->pylibzfsp);
	ret = zpool_vdev_offline(v->pool->zhp, cpath, temp);
	if (ret)
		py_get_zfs_error(v->pool->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(v->pool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool offline failed");
		return (NULL);
	} else {
		ret = py_log_history_fmt(v->pool->pylibzfsp,
		    "zpool offline %s%s %s", temp ? "-t " : "",
		    zpool_get_name(v->pool->zhp), cpath);
		if (ret) {
			// An exception should be set since we failed to log
			// history
			return (NULL);
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_vdev_remove__doc__,
"remove(*) -> None\n\n"
"-----------------\n\n"
"Remove the VDEV from pool.\n\n"
"Parameters\n"
"----------\n"
"None.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while trying to set the state to faulted.\n\n"
);
static
PyObject *py_zfs_vdev_remove(PyObject *self, PyObject *arg) {
	int ret;
	uint64_t guid;
	char sguid[22];
	py_zfs_error_t err;
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;

	Py_BEGIN_ALLOW_THREADS
	guid = fnvlist_lookup_uint64(v->vdev_tree, ZPOOL_CONFIG_GUID);
	Py_END_ALLOW_THREADS
	snprintf(sguid, sizeof(sguid), "%" PRIu64, guid);

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSVdev.remove", "s",
	    sguid) < 0) {
		return (NULL);
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(v->pool->pylibzfsp);
	ret = zpool_vdev_remove(v->pool->zhp, sguid);
	if (ret)
		py_get_zfs_error(v->pool->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(v->pool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool remove failed");
		return (NULL);
	} else {
		ret = py_log_history_fmt(v->pool->pylibzfsp,
		    "zpool remove %s %s", zpool_get_name(v->pool->zhp), sguid);
		if (ret) {
			// An exception should be set since we failed to log
			// history
			return (NULL);
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_vdev_detach__doc__,
"detach(*) -> None\n\n"
"-----------------\n\n"
"Detach the VDEV (from mirror).\n\n"
"Parameters\n"
"----------\n"
"None.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    If the operation is invoked on VDEV that is not of type DISK or FILE,\n"
"    TypeError will be raised.\n\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while trying to set the state to faulted.\n\n"
);
static
PyObject *py_zfs_vdev_detach(PyObject *self, PyObject *arg) {
	int ret;
	py_zfs_error_t err;
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	const char *cpath = NULL;
	const char *ctype = PyUnicode_AsUTF8(v->type);

	if (ctype == NULL)
		return (NULL);
	if (strcmp(ctype, VDEV_TYPE_DISK) != 0 &&
	    strcmp(ctype, VDEV_TYPE_FILE) != 0) {
		PyErr_SetString(PyExc_TypeError,
		    "Only disk/file vdev can be detached");
		return (NULL);
	}
	if (v->path)
		cpath = PyUnicode_AsUTF8(v->path);
	else {
		PyErr_SetString(PyExc_TypeError,
		    "Cannot find vdev path");
		return (NULL);
	}
	if (cpath == NULL)
		return (NULL);

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSVdev.detach", "O",
	    v->path) < 0) {
		return (NULL);
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(v->pool->pylibzfsp);
	ret = zpool_vdev_detach(v->pool->zhp, cpath);
	if (ret)
		py_get_zfs_error(v->pool->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(v->pool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool detach failed");
		return (NULL);
	} else {
		ret = py_log_history_fmt(v->pool->pylibzfsp,
		    "zpool detach %s %s", zpool_get_name(v->pool->zhp), cpath);
		if (ret) {
			// An exception should be set since we failed to log
			// history
			return (NULL);
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_vdev_attach__doc__,
"attach(*, path) -> None\n\n"
"-----------------\n\n"
"Attaches a new device with path given as argument.\n\n"
"Parameters\n"
"----------\n"
"path: str\n"
"    Valid path for VDEV\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"TypeError:\n"
"    If the operation is invoked on VDEV that is not of type DISK, FILE, or\n"
"    Mirror, TypeError will be raised.\n"
"RuntimeError:\n"
"    If no child vdevs or leaf vdevs are found, RuntimeError will be raised.\n\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error can occur, if libzfs fails to attach the device.\n\n"
);
static
PyObject *py_zfs_vdev_attach(PyObject *self, PyObject *args, PyObject *kwargs) {
	int ret = 0;
	py_zfs_error_t err;
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	const char *ctype = PyUnicode_AsUTF8(v->type);
	char *path = NULL;
	const char *fpath = NULL;
	PyObject *topology;

	if (ctype == NULL)
		return (NULL);

	char *kwnames[] = {"path", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$s", kwnames,
	    &path)) {
		return (NULL);
	}

	if (path == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "path keyword argument is required.");
		return (NULL);
	}

	if (strcmp(ctype, VDEV_TYPE_MIRROR) == 0) {
		nvlist_t **child;
		uint_t children;
		if (nvlist_lookup_nvlist_array(v->vdev_tree, ZPOOL_CONFIG_CHILDREN,
		    &child, &children) == 0) {
			for (uint_t i = 0; i < children; ++i) {
				if (nvlist_lookup_string(child[i],
				    ZPOOL_CONFIG_PATH, &fpath) == 0) {
					break;
				}
			}
			if (fpath == NULL) {
				PyErr_SetString(PyExc_RuntimeError,
				    "No leaf vdev found to attach the VDEV");
				return (NULL);
			}
		} else {
			PyErr_SetString(PyExc_RuntimeError,
			    "Cannot find child vdevs in vdev tree");
			    return (NULL);
		}
	} else if (strcmp(ctype, VDEV_TYPE_DISK) == 0 || strcmp(ctype,
	    VDEV_TYPE_FILE) == 0) {
		fpath = PyUnicode_AsUTF8(v->path);
	} else {
		PyErr_SetString(PyExc_TypeError,
		    "Can only attach DISK or FILE type VDEVs to MIRROR or STRIPE devices.");
		    return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSVdev.attach", "s",
		path) < 0) {
		    return (NULL);
	}

	topology = Py_BuildValue("[{s:s,s:s,s:[s]}]", "root", "data", "type",
	    "stripe", "devices", path);
	nvlist_t *tree = make_vdev_tree(topology, NULL);
	nvlist_print_json(stdout, tree);
	putchar('\n');

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(v->pool->pylibzfsp);
	ret = zpool_vdev_attach(v->pool->zhp, fpath, path, tree, 0, B_FALSE);
	if (ret)
		py_get_zfs_error(v->pool->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(v->pool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool attach failed");
		fnvlist_free(tree);
		Py_DECREF(topology);
		return (NULL);
	} else {
		ret = py_log_history_fmt(v->pool->pylibzfsp,
		    "zpool attach %s %s %s", zpool_get_name(v->pool->zhp),
		    fpath, path);
		if (ret) {
			// An exception should be set since we failed to log
			// history
			fnvlist_free(tree);
			Py_DECREF(topology);
			return (NULL);
		}
	}

	fnvlist_free(tree);
	Py_DECREF(topology);
	Py_RETURN_NONE;
}

static
PyGetSetDef zfs_vdev_getsetters[] = {
	{
		.name	= "type",
		.get	= (getter)py_zfs_vdev_get_type,
		.doc	= py_zfs_vdev_type__doc__
	},
	{
		.name	= "path",
		.get	= (getter)py_zfs_vdev_get_path,
		.doc	= py_zfs_vdev_path__doc__
	},
	{ .name = NULL }
};

static
PyMethodDef zfs_vdev_methods[] = {
	{
		.ml_name = "asdict",
		.ml_meth = py_zfs_vdev_asdict,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{
		.ml_name = "name",
		.ml_meth = py_zfs_vdev_get_name,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_name__doc__
	},
	{
		.ml_name = "guid",
		.ml_meth = py_zfs_vdev_get_guid,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_guid__doc__
	},
	{
		.ml_name = "status",
		.ml_meth = py_zfs_vdev_get_status,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_status__doc__
	},
	{
		.ml_name = "size",
		.ml_meth = py_zfs_vdev_get_size,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_size__doc__
	},
	{
		.ml_name = "children",
		.ml_meth = py_zfs_vdev_get_children,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_children__doc__
	},
	{
		.ml_name = "disks",
		.ml_meth = py_zfs_vdev_get_disks,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_disks__doc__
	},
	{
		.ml_name = "add_ashift",
		.ml_meth = py_zfs_vdev_add_ashift,
		.ml_flags = METH_O,
		.ml_doc = py_zfs_vdev_add_ashift__doc__
	},
	{
		.ml_name = "vdev_stats",
		.ml_meth = py_zfs_vdev_get_vdev_stats,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_vdev_stats__doc__
	},
	{
		.ml_name = "degrade",
		.ml_meth = py_zfs_vdev_degrade,
		.ml_flags = METH_O,
		.ml_doc = py_zfs_vdev_degrade__doc__
	},
	{
		.ml_name = "fault",
		.ml_meth = py_zfs_vdev_fault,
		.ml_flags = METH_O,
		.ml_doc = py_zfs_vdev_fault__doc__
	},
	{
		.ml_name = "online",
		.ml_meth = (PyCFunction)py_zfs_vdev_online,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_vdev_online__doc__
	},
	{
		.ml_name = "offline",
		.ml_meth = (PyCFunction)py_zfs_vdev_offline,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_vdev_offline__doc__
	},
	{
		.ml_name = "remove",
		.ml_meth = py_zfs_vdev_remove,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_remove__doc__
	},
	{
		.ml_name = "detach",
		.ml_meth = py_zfs_vdev_detach,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_detach__doc__
	},
	{
		.ml_name = "attach",
		.ml_meth = (PyCFunction)py_zfs_vdev_attach,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_vdev_attach__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSVdev = {
	.tp_name = "ZFSVdev",
	.tp_basicsize = sizeof (py_zfs_vdev_t),
	.tp_methods = zfs_vdev_methods,
	.tp_getset = zfs_vdev_getsetters,
	.tp_new = py_zfs_vdev_new,
	.tp_init = py_zfs_vdev_init,
	.tp_doc = "ZFSVdev",
	.tp_dealloc = (destructor)py_zfs_vdev_dealloc,
	.tp_repr = py_repr_zfs_vdev,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE
};

py_zfs_vdev_t *init_zfs_vdev(py_zfs_pool_t *pool, nvlist_t *tree,
    PyObject *parent)
{
	py_zfs_vdev_t *out = NULL;
	const char *type;
	const char *path;
	boolean_t path_found;

	out = (py_zfs_vdev_t *)PyObject_CallFunction((PyObject *)&ZFSVdev, NULL);
	if (out == NULL) {
		return (NULL);
	}

	out->pool = pool;
	Py_INCREF(pool);
	out->parent = parent;
	Py_XINCREF(parent);

	Py_BEGIN_ALLOW_THREADS
	/*
	 * force nvlist wrappers wrap nvlist_* functions with asserions that
	 * operation was successful. We can avoid checking for success with
	 * these functions, since an assert would be hit in case of failure.
	 */
	out->vdev_tree = fnvlist_dup(tree);
	type = fnvlist_lookup_string(out->vdev_tree, ZPOOL_CONFIG_TYPE);
	path_found = nvlist_lookup_string(out->vdev_tree, ZPOOL_CONFIG_PATH, &path) == 0;
	Py_END_ALLOW_THREADS

	if (strncmp(type, VDEV_TYPE_RAIDZ, strlen(VDEV_TYPE_RAIDZ)) == 0) {
		int parity;
		Py_BEGIN_ALLOW_THREADS
		parity = fnvlist_lookup_uint64(out->vdev_tree, ZPOOL_CONFIG_NPARITY);
		Py_END_ALLOW_THREADS
		out->type = PyUnicode_FromFormat("%s%d", type, parity);
	} else
		out->type = PyUnicode_FromString(type);
	if (out->type == NULL) {
		Py_DECREF(out);
		return (NULL);
	}

	if (path_found)
		out->path = PyUnicode_FromString(path);
	else
		out->path = NULL;

	return (out);
}
