#include "../truenas_pylibzfs.h"

#define ZFS_POOL_STR "<" PYLIBZFS_MODULE_NAME ".ZFSPool(name=%U)>"

static
PyObject *py_repr_zfs_pool(PyObject *self) {
	py_zfs_pool_t *pool = (py_zfs_pool_t *) self;

	return PyUnicode_FromFormat(ZFS_POOL_STR, pool->name);
}

static
PyObject *py_zfs_pool_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_pool_t *self = NULL;
	self = (py_zfs_pool_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_pool_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_pool_dealloc(py_zfs_pool_t *self) {
	if (self->zhp != NULL) {
		Py_BEGIN_ALLOW_THREADS
		zpool_close(self->zhp);
		Py_END_ALLOW_THREADS
		self->zhp = NULL;
	}
	Py_CLEAR(self->name);
	Py_CLEAR(self->pylibzfsp);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyDoc_STRVAR(py_zfs_pool_name__doc__,
"Returns the name of ZFS Pool.\n"
);
static
PyObject *py_zfs_pool_get_name(py_zfs_pool_t *self, void *extra) {
	if (self == NULL)
		Py_RETURN_NONE;
	return Py_NewRef(self->name);
}

static
PyObject *py_zfs_pool_asdict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_root_dataset__doc__,
"root_dataset(*) -> ZFSDataset\n\n"
"-----------------\n\n"
"Returns the ZFSDataset type object for root Dataset of the pool.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"None\n\n"
);
static
PyObject *py_zfs_pool_root_dataset(PyObject *self, PyObject *args) {
	PyObject *out = NULL;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	PyObject *open_rsrc;
	PyObject *fargs = NULL;
	PyObject *fkwargs = NULL;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.root_dataset", "O",
	    p->name) < 0) {
		return NULL;
	}

	fargs = PyTuple_New(0);
	if (fargs == NULL)
		return (NULL);

	fkwargs = Py_BuildValue("{s:O}", "name", p->name);
	if (fkwargs == NULL) {
		Py_DECREF(fargs);
		return (NULL);
	}

	open_rsrc = PyObject_GetAttrString((PyObject*)p->pylibzfsp,
					   "open_resource");
	if (open_rsrc != NULL)
		out = PyObject_Call(open_rsrc, fargs, fkwargs);

	Py_XDECREF(open_rsrc);
	Py_DECREF(fargs);
	Py_DECREF(fkwargs);

	return (out);
}

PyDoc_STRVAR(py_zfs_pool_status__doc__,
"status(*, asdict=False, get_stats=True, follow_links=True, full_path=True) -> struct_zpool_status | dict\n"
"-------------------------------------------------------------------------------------------------------\n\n"
"Retrieve health and configuration status of the zpool.\n\n"
"Parameters\n"
"----------\n"
"asdict: boolean, optional, default=False\n"
"    If True, return a plain dictionary instead of a struct_zpool_status\n"
"    struct sequence. The dictionary has the same keys as the struct\n"
"    sequence field names.\n\n"
"get_stats: boolean, optional, default=True\n"
"    If True, include cached per-vdev I/O and error counters in the\n"
"    output. When False, the 'stats' field of each vdev will be None,\n"
"    which reduces overhead when only topology or health state is needed.\n\n"
"follow_links: boolean, optional, default=True\n"
"    If True, resolve symlinks in vdev path names (equivalent to\n"
"    'zpool status -L'). Useful when vdevs are referenced by\n"
"    /dev/disk/by-partuuid/ or similar symlinks and the resolved\n"
"    /dev/<device> path is required.\n\n"
"full_path: boolean, optional, default=True\n"
"    If True, display full path names for vdev devices (equivalent to\n"
"    'zpool status -P'). When True, vdev names are shown as full device\n"
"    paths rather than short names.\n\n"
"Returns\n"
"-------\n"
"truenas_pylibzfs.struct_zpool_status when asdict=False (default),\n"
"or a plain dict with equivalent contents when asdict=True.\n"
);
static
PyObject *py_zfs_pool_status(PyObject *self, PyObject *args, PyObject *kwargs)
{
	boolean_t asdict = B_FALSE;
	boolean_t get_stats = B_TRUE;
	boolean_t follow_links = B_TRUE;
	boolean_t full_path = B_TRUE;
	char *kwnames [] = {"asdict", "get_stats", "follow_links", "full_path",
			     NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs,
					 "|$pppp",
					 kwnames,
					 &asdict,
					 &get_stats,
					 &follow_links,
					 &full_path)) {
		return NULL;
	}

	if (asdict)
		return py_get_pool_status_dict((py_zfs_pool_t *)self,
		    get_stats, follow_links, full_path);

	return py_get_pool_status((py_zfs_pool_t *)self, get_stats,
	    follow_links, full_path);
}

PyDoc_STRVAR(py_zfs_pool_root_vdev__doc__,
"root_vdev(*) -> ZFSVdev\n\n"
"-----------------------\n\n"
"Returns the ZFSVdev type object for root VDEV of the pool.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"None\n\n"
);
static
PyObject *py_zfs_pool_root_vdev(PyObject *self, PyObject *args) {
	PyObject *out = NULL;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	nvlist_t *nvroot;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.root_vdev", "O",
	    p->name) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	nvroot = fnvlist_lookup_nvlist(zpool_get_config(p->zhp, NULL),
	    ZPOOL_CONFIG_VDEV_TREE);
	Py_END_ALLOW_THREADS

	out = (PyObject *)init_zfs_vdev(p, nvroot, NULL);
	return (out);
}

PyDoc_STRVAR(py_zfs_pool_clear__doc__,
"clear(*) -> None\n\n"
"----------------\n\n"
"Clear device errors in the pool.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error that occurred while trying to perform the operation.\n"
);
static
PyObject *py_zfs_pool_clear(PyObject *self, PyObject *args) {
	int ret = 0, error;
	nvlist_t *policy = NULL;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_error_t err;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.clear", "O",
	    p->name) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	policy = fnvlist_alloc();
	/* Force nvlist wrapper wraps the nvlist_* functions with assertions
	 * that assume the operation is successful. We can avoid checking for
	 * NULL here since as assert would be hit from libspl in case the
	 * operation failed.
	 */
	fnvlist_add_uint32(policy, ZPOOL_LOAD_REWIND_POLICY, ZPOOL_NO_REWIND);
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_clear(p->zhp, NULL, policy);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	fnvlist_free(policy);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_clear() failed");
		return (NULL);
	} else {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool clear %s", zpool_get_name(p->zhp));
		if (error) {
			// An exception should be set since we failed to log
			// history
			return (NULL);
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_upgrade__doc__,
"upgrade(*) -> None\n\n"
"------------------\n\n"
"Enables all supported features on the given pool.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error that occurred while trying to perform the operation.\n"
);
static
PyObject *py_zfs_pool_upgrade(PyObject *self, PyObject *args) {
	int ret = 0, error;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_error_t err;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.upgrade", "O",
	    p->name) < 0) {
		return NULL;
	}


	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_upgrade(p->zhp, SPA_VERSION);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_upgrade() failed");
		return (NULL);
	} else {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool upgrade %s", zpool_get_name(p->zhp));
		if (error) {
			// An exception should be set since we failed to log
			// history
			return (NULL);
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_prefetch__doc__,
"prefetch(*) -> None\n\n"
"-------------------\n\n"
"Prefetch pool metadata (DDT and BRT) into ARC.\n\n"
"Loads both the Deduplication Table (DDT) and Block Reference Table (BRT)\n"
"into the ARC to reduce latency of subsequent operations. This is equivalent\n"
"to running 'zpool prefetch <pool>' without the -t flag.\n\n"
"The DDT tracks deduplication metadata, while the BRT tracks block cloning\n"
"metadata used for efficient copy-on-write operations.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error that occurred while trying to perform the operation.\n"
);
static
PyObject *py_zfs_pool_prefetch(PyObject *self, PyObject *args) {
	int ret = 0, error;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_error_t err;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.prefetch", "O",
	    p->name) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	// Prefetch DDT first, then BRT if DDT succeeds (matches zpool behavior)
	ret = zpool_prefetch(p->zhp, ZPOOL_PREFETCH_DDT);
	if (ret == 0)
		ret = zpool_prefetch(p->zhp, ZPOOL_PREFETCH_BRT);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_prefetch() failed");
		return (NULL);
	} else {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool prefetch %s", zpool_get_name(p->zhp));
		if (error) {
			// An exception should be set since we failed to log
			// history
			return (NULL);
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_ddt_prune__doc__,
"ddt_prune(*, [days], [percentage]) -> None\n\n"
"------------------------------------------\n\n"
"Prunes the older entries from single reference dedup table(s) to reclaim\n"
"space under the quota. Only one of days or percentage should be passed.\n\n"
"Parameters\n"
"----------\n"
"days: Int, optional\n"
"    Prune the entries based on age, i.e. deletes every entry older than N\n"
"    days. Must be a +ve Integer. -ve values are not allowed.\n\n"
"percentage: Int, optional\n"
"    Target percentage of unique entries to be removed. Value must be between\n"
"    1 to 100. -ve values are not allowed.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error that occurred while trying to perform the operation.\n"
);
static
PyObject *py_zfs_pool_ddt_prune(PyObject *self,
				PyObject *args,
				PyObject *kwargs) {
	int ret, error;
	int days = 0, percentage = 0;
	uint64_t value = 0;
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_error_t err;
	zpool_ddt_prune_unit_t unit = ZPOOL_DDT_PRUNE_NONE;
	char *kwnames[] = {"days", "percentage", NULL};
	ret = days = percentage = 0;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$ii", kwnames, &days,
					 &percentage)) {
		return (NULL);
	}

	if (days < 0 || percentage < 0 || percentage > 100) {
		PyErr_SetString(PyExc_ValueError,
			"days must be >= 1, and percentage must be between 1 "
			"and 100");
		return (NULL);
	} else if (days > 0 && percentage > 0) {
		PyErr_SetString(PyExc_ValueError,
			"Only one of days or percentage should be set");
		return (NULL);
	} else if (days == 0 && percentage == 0) {
		PyErr_SetString(PyExc_ValueError,
			"Either days or percentage must be set");
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.ddt_prune", "OO",
	    p->name, kwargs) < 0) {
		return (NULL);
	}

	if (percentage != 0) {
		unit = ZPOOL_DDT_PRUNE_PERCENTAGE;
		value = percentage;
	} else if (days != 0) {
		unit = ZPOOL_DDT_PRUNE_AGE;
		value = days;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_ddt_prune(p->zhp, unit, value);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_ddt_prune() failed");
		return (NULL);
	} else {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool ddtprune %s%llu %s", days ? "-d ": "-p ", value,
		    zpool_get_name(p->zhp));
		if (error) {
			// exception should be set since we failed to log
			// history
			return (NULL);
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_config__doc__,
"dump_config(*) -> dict\n\n"
"----------------------\n\n"
"Dump the zfs pool configuration for the pool. This information is cached\n"
"inside the zpool handle and contains a wide variety of zpool-related information.\n"
"If the application using the API is holding a zpool handle for a long period of\n"
"time and attempting to gather stats counters, then the stats should be refreshed\n"
"before each config dump.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"dict\n\n"
"Raises:\n"
"-------\n"
"This method instructs libzfs to dump the nvlist contents of the pool config as a\n"
"JSON and then uses the default JSON library in python to load the JSON, and so\n"
"the primary case where error may happen is if there's a memory allocation error or if\n"
"a bug in the libzfs nvlist utilities generats invalid JSON. This, in theory, should not\n"
"happen.\n\n"
);
static
PyObject *py_zfs_pool_config(PyObject *self, PyObject *args)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	pylibzfs_state_t *state = py_get_module_state(p->pylibzfsp);
	PyObject *nvldump = NULL, *dict_out = NULL;
	nvlist_t *config, *zpool_config = NULL;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	// We need to take a lock here because in MT case you can have
	// one thread refresh pool stats and free the config while
	// this thread dumps the nvlist to JSON.
	config = zpool_get_config(p->zhp, NULL);
	PYZFS_ASSERT((config != NULL), "Unexpected NULL zpool config");
	zpool_config = fnvlist_dup(config);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	// convert nvlist to JSON string
	nvldump = py_dump_nvlist(zpool_config, B_TRUE);
	dict_out = PyObject_CallFunction(state->loads_fn, "O", nvldump);
	Py_CLEAR(nvldump);

	fnvlist_free(zpool_config);

	return dict_out;
}

PyDoc_STRVAR(py_zfs_pool_refresh_stats__doc__,
"refresh_stats(*) -> dict\n\n"
"------------------------\n\n"
"Refresh the vdev statistics stored in the cached zpool config in the zpool handle.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"RuntimeError:\n"
"   An unexpected error occurred when issuing the ZFS ioctl to refresh zpool stats.\n"
"FileNotFoundError:\n"
"   The pool was exported or destroyed. libzfs implementation note: the libzfs call\n"
"   will have also updated the hdl->zpool_state to POOL_STATE_UNAVAIL after erroring\n"
"   out.\n"
"\n\n"
);
static
PyObject *py_zfs_pool_refresh_stats(PyObject *self, PyObject *args)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	boolean_t missing;
	pool_state_t pool_state;
	int err;

	Py_BEGIN_ALLOW_THREADS
	// We need to take a lock here because in MT case you can have
	// one thread refresh pool stats and free the config while
	// this thread dumps the nvlist to JSON.
	PY_ZFS_LOCK(p->pylibzfsp);

	err = zpool_refresh_stats(p->zhp, &missing);
	if (!err)
		// libzfs will set err to zero but change
		// internal pool state on some types of error conditions
		pool_state = zpool_get_state(p->zhp);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	// Err here would indicate zcmd_read_dst_nvlist() failed
	// Many times error is *actually* indicated by the pool state
	// transitioning to unavailable
	if (err) {
		PyErr_Format(PyExc_RuntimeError,
			     "Failed to refresh zpool stats: %s",
			     strerror(errno));
		return NULL;
	} else if (missing) {
		// During the refresh, the ZFS ioctol failed with ENOENT
		// or EINVAL
		PyErr_Format(PyExc_FileNotFoundError,
			     "ZFS ioctl to refresh pool stats failed with "
			     "EINVAL or ENOENT. This may also indicate that the "
			     "pool was exported or destroyed.");

		return NULL;
	} else if (pool_state == POOL_STATE_UNAVAIL) {
		PyErr_Format(PyExc_FileNotFoundError,
			     "Attempt to refresh pool stats. Pool state "
			     "is currently unavailable.");
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_sync__doc__,
"sync_pool(*) -> None\n\n"
"--------------------\n\n"
"Force all in-core dirty data to be written to the primary pool storage and not\n"
"the ZIL.  It will also update administrative information including quota\n"
"reporting.\n\n"
"WARNING: this operation may have a performance impact.\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"ZFSException:\n"
"   The pool sync operation failed.\n\n"
"\n\n"
);
static
PyObject *py_zfs_pool_sync(PyObject *self, PyObject *args)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	/*
	 * libzfs / libzfs_core have support for a `force` argument
	 * that is not exposed in the zpool command. For now we are
	 * keeping same behavior of not exposing this option.
	 */
	boolean_t force = B_FALSE;
	int err;
	py_zfs_error_t zfs_err;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.sync_pool", "O",
	    Py_None) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	err = zpool_sync_one(p->zhp, &force);
	if (err)
		py_get_zfs_error(p->pylibzfsp->lzh, &zfs_err);

	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zpool_sync() failed");
		return (NULL);
	}

	// NOTE: we can't generate a zpool history message for this
	// operation.

	Py_RETURN_NONE;
}


PyStructSequence_Field struct_zpool_feature_prop[] = {
	{"guid", "Feature GUID (e.g. 'com.delphix:async_destroy')"},
	{"description", "Human-readable description of the feature"},
	{"state", "One of 'DISABLED', 'ENABLED', or 'ACTIVE'"},
	{0},
};

PyStructSequence_Desc struct_zpool_feature_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_zpool_feature",
	.fields = struct_zpool_feature_prop,
	.doc = "ZFS pool feature with guid, description, and state",
	.n_in_sequence = 3
};

#define	ZPOOL_FEAT_GUID_IDX	0
#define	ZPOOL_FEAT_DESC_IDX	1
#define	ZPOOL_FEAT_STATE_IDX	2

PyDoc_STRVAR(py_zfs_pool_get_features__doc__,
"get_features(*, asdict=False) -> dict\n\n"
"-------------------------------------\n\n"
"Return a dictionary of all known ZFS pool features with their current state\n"
"and metadata.\n\n"
"Each key is the user-facing feature name (e.g. 'async_destroy'). Each value\n"
"is a struct_zpool_feature with attributes:\n"
"  - guid: the feature GUID (e.g. 'com.delphix:async_destroy')\n"
"  - description: human-readable description of the feature\n"
"  - state: one of 'DISABLED', 'ENABLED', or 'ACTIVE'\n\n"
"State semantics:\n"
"  - DISABLED: feature is not enabled on this pool\n"
"  - ENABLED: feature is enabled but has zero active references\n"
"  - ACTIVE: feature is enabled and has one or more active references\n\n"
"Parameters\n"
"----------\n"
"asdict: boolean, optional, default=False\n"
"    If True, each feature entry is a plain dict instead of a\n"
"    struct_zpool_feature struct sequence.\n\n"
"Returns\n"
"-------\n"
"dict[str, struct_zpool_feature] when asdict=False (default),\n"
"or dict[str, dict[str, str]] when asdict=True.\n\n"
);
static
PyObject *py_zfs_pool_get_features(PyObject *self,
				   PyObject *args,
				   PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	pylibzfs_state_t *state = py_get_module_state(p->pylibzfsp);
	nvlist_t *features = NULL;
	PyObject *dict_out = NULL;
	boolean_t asdict = B_FALSE;
	char *kwnames[] = {"asdict", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$p", kwnames,
	    &asdict)) {
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.get_features",
	    "O", p->name) < 0)
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	nvlist_t *raw_features = zpool_get_features(p->zhp);
	if (raw_features != NULL)
		features = fnvlist_dup(raw_features);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	dict_out = PyDict_New();
	if (dict_out == NULL)
		goto out;

	for (int i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t *feat = &spa_feature_table[i];

		const char *feat_state;
		if (features == NULL ||
		    !nvlist_exists(features, feat->fi_guid)) {
			feat_state = "DISABLED";
		} else {
			uint64_t refcount;
			if (nvlist_lookup_uint64(features, feat->fi_guid,
			    &refcount) == 0 && refcount > 0) {
				feat_state = "ACTIVE";
			} else {
				feat_state = "ENABLED";
			}
		}

		PyObject *entry;
		if (asdict) {
			entry = Py_BuildValue("{s:s, s:s, s:s}",
			    "guid", feat->fi_guid,
			    "description", feat->fi_desc,
			    "state", feat_state);
		} else {
			entry = PyStructSequence_New(
			    state->struct_zpool_feature_type);
			if (entry != NULL) {
				PyObject *guid_str =
				    PyUnicode_FromString(feat->fi_guid);
				PyObject *desc_str =
				    PyUnicode_FromString(feat->fi_desc);
				PyObject *state_str =
				    PyUnicode_FromString(feat_state);
				if (guid_str == NULL || desc_str == NULL ||
				    state_str == NULL) {
					Py_XDECREF(guid_str);
					Py_XDECREF(desc_str);
					Py_XDECREF(state_str);
					Py_DECREF(entry);
					entry = NULL;
				} else {
					PyStructSequence_SetItem(entry,
					    ZPOOL_FEAT_GUID_IDX, guid_str);
					PyStructSequence_SetItem(entry,
					    ZPOOL_FEAT_DESC_IDX, desc_str);
					PyStructSequence_SetItem(entry,
					    ZPOOL_FEAT_STATE_IDX, state_str);
				}
			}
		}

		if (entry == NULL) {
			Py_CLEAR(dict_out);
			goto out;
		}

		if (PyDict_SetItemString(dict_out, feat->fi_uname,
		    entry) < 0) {
			Py_DECREF(entry);
			Py_CLEAR(dict_out);
			goto out;
		}
		Py_DECREF(entry);
	}

out:
	nvlist_free(features);
	return dict_out;
}

PyGetSetDef zfs_pool_getsetters[] = {
	{
		.name	= "name",
		.get	= (getter)py_zfs_pool_get_name,
		.doc	= py_zfs_pool_name__doc__
	},
	{ .name = NULL }
};

PyMethodDef zfs_pool_methods[] = {
	{
		.ml_name = "asdict",
		.ml_meth = py_zfs_pool_asdict,
		.ml_flags = METH_NOARGS
	},
	{
		.ml_name = "status",
		.ml_meth = (PyCFunction)py_zfs_pool_status,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_status__doc__
	},
	{
		.ml_name = "root_dataset",
		.ml_meth = py_zfs_pool_root_dataset,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_root_dataset__doc__
	},
	{
		.ml_name = "dump_config",
		.ml_meth = py_zfs_pool_config,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_config__doc__
	},
	{
		.ml_name = "refresh_stats",
		.ml_meth = py_zfs_pool_refresh_stats,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_refresh_stats__doc__
	},
	{
		.ml_name = "sync_pool",
		.ml_meth = py_zfs_pool_sync,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_sync__doc__
	},
	{
		.ml_name = "root_vdev",
		.ml_meth = py_zfs_pool_root_vdev,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_root_vdev__doc__
	},
	{
		.ml_name = "clear",
		.ml_meth = py_zfs_pool_clear,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_clear__doc__
	},
	{
		.ml_name = "upgrade",
		.ml_meth = py_zfs_pool_upgrade,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_upgrade__doc__
	},
	{
		.ml_name = "prefetch",
		.ml_meth = py_zfs_pool_prefetch,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_prefetch__doc__
	},
	{
		.ml_name = "ddt_prune",
		.ml_meth = (PyCFunction)py_zfs_pool_ddt_prune,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_ddt_prune__doc__
	},
	{
		.ml_name = "get_features",
		.ml_meth = (PyCFunction)py_zfs_pool_get_features,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_get_features__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSPool = {
	.tp_name = "ZFSPool",
	.tp_basicsize = sizeof (py_zfs_pool_t),
	.tp_methods = zfs_pool_methods,
	.tp_getset = zfs_pool_getsetters,
	.tp_new = py_zfs_pool_new,
	.tp_init = py_zfs_pool_init,
	.tp_doc = "ZFSPool",
	.tp_dealloc = (destructor)py_zfs_pool_dealloc,
	.tp_repr = py_repr_zfs_pool,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};

py_zfs_pool_t *init_zfs_pool(py_zfs_t *lzp, zpool_handle_t *zhp)
{
	py_zfs_pool_t *out = NULL;
	const char *name;

	out = (py_zfs_pool_t *)PyObject_CallFunction((PyObject *)&ZFSPool, NULL);
	if (out == NULL) {
		return (NULL);
	}

	out->pylibzfsp = lzp;
	Py_INCREF(lzp);

	Py_BEGIN_ALLOW_THREADS
	name = zpool_get_name(zhp);
	Py_END_ALLOW_THREADS

	out->name = PyUnicode_FromString(name);
	if (out->name == NULL) {
		Py_DECREF(out);
		return (NULL);
	}

	out->zhp = zhp;
	return (out);
}

void init_py_pool_feature_state(pylibzfs_state_t *state)
{
	PyTypeObject *obj;

	obj = PyStructSequence_NewType(&struct_zpool_feature_desc);
	PYZFS_ASSERT(obj, "Failed to create zpool feature struct type");

	state->struct_zpool_feature_type = obj;
}
