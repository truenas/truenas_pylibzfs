#include "../truenas_pylibzfs.h"
#include <libzutil.h>

#define ZFS_POOL_STR "<" PYLIBZFS_TYPES_MODULE_NAME ".ZFSPool(name=%U)>"


static
PyObject *py_repr_zfs_pool(PyObject *self) {
	py_zfs_pool_t *pool = (py_zfs_pool_t *) self;

	return PyUnicode_FromFormat(ZFS_POOL_STR, pool->name);
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
	char propname[MAXPATHLEN];

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.upgrade", "O",
	    p->name) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);

	ret = zpool_upgrade(p->zhp, SPA_VERSION);
	if (ret) {
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
		goto unlock;
	}

	for (int i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t *feat = &spa_feature_table[i];

		if (!feat->fi_zfs_mod_supported)
			continue;

		(void) snprintf(propname, sizeof (propname),
		    "feature@%s", feat->fi_uname);

		ret = zpool_set_prop(p->zhp, propname,
		    ZFS_FEATURE_ENABLED);
		if (ret) {
			py_get_zfs_error(p->pylibzfsp->lzh, &err);
			goto unlock;
		}
	}

unlock:
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_upgrade() failed");
		return (NULL);
	}

	error = py_log_history_fmt(p->pylibzfsp,
	    "zpool upgrade %s", zpool_get_name(p->zhp));
	if (error)
		return (NULL);

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
"The primary case where an error may occur is a memory allocation failure\n"
"during nvlist-to-dict conversion, which should not happen in practice.\n\n"
);
static
PyObject *py_zfs_pool_config(PyObject *self, PyObject *args)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	PyObject *dict_out = NULL;
	nvlist_t *config, *zpool_config = NULL;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	// We need to take a lock here because in MT case you can have
	// one thread refresh pool stats and free the config while
	// this thread is reading it.
	config = zpool_get_config(p->zhp, NULL);
	PYZFS_ASSERT((config != NULL), "Unexpected NULL zpool config");
	zpool_config = fnvlist_dup(config);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	dict_out = py_nvlist_to_dict(zpool_config);
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
	.name = PYLIBZFS_TYPES_MODULE_NAME ".struct_zpool_feature",
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

PyDoc_STRVAR(py_zfs_pool_scrub__doc__,
"scrub_info() -> struct_zpool_scrub | None\n\n"
"-----------------------------------------\n\n"
"Return current scan/scrub statistics for the pool as a struct_zpool_scrub\n"
"named tuple.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"truenas_pylibzfs.struct_zpool_scrub if scan statistics are available,\n"
"or None if the pool has never been scrubbed (no scan stats in config).\n\n"
"Raises\n"
"------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error that occurred while refreshing pool statistics.\n"
);
static
PyObject *py_zfs_pool_scrub_info(PyObject *self, PyObject *args)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.scrub_info", "O",
	    p->name) < 0)
		return NULL;

	return py_get_pool_scrub_info(p);
}

PyDoc_STRVAR(py_zfs_pool_expand__doc__,
"expand_info() -> struct_zpool_expand | None\n\n"
"-----------------------------------------------\n\n"
"Return current RAIDZ expansion statistics for the pool as a\n"
"struct_zpool_expand named tuple.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"truenas_pylibzfs.struct_zpool_expand if expansion statistics are available,\n"
"or None if the pool has never had a RAIDZ expansion.\n\n"
"Raises\n"
"------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error that occurred while refreshing pool statistics.\n"
);
static
PyObject *py_zfs_pool_expand_info(PyObject *self, PyObject *args)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.expand_info", "O",
	    p->name) < 0)
		return NULL;

	return py_get_pool_expand_info(p);
}

PyDoc_STRVAR(py_zfs_pool_scan__doc__,
"scan(*, func, cmd=ScanScrubCmd.NORMAL) -> None\n\n"
"-----------------------------------------------\n\n"
"Start, pause, or cancel a pool scan (scrub, resilver, or error-scrub).\n\n"
"Parameters\n"
"----------\n"
"func: ScanFunction\n"
"    The scan function to perform:\n"
"      ScanFunction.SCRUB     — start or resume a data integrity scrub\n"
"      ScanFunction.RESILVER  — start a resilver (mirror/RAIDZ rebuild)\n"
"      ScanFunction.ERRORSCRUB — start an error-scrub\n"
"      ScanFunction.NONE      — cancel the current scan\n\n"
"cmd: ScanScrubCmd, optional, default=ScanScrubCmd.NORMAL\n"
"    Scrub command modifier:\n"
"      ScanScrubCmd.NORMAL        — normal start/resume\n"
"      ScanScrubCmd.PAUSE         — pause an in-progress scrub\n"
"      ScanScrubCmd.FROM_LAST_TXG — resume scrub from last completed TXG\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"ValueError:\n"
"    func or cmd is not a valid enum value.\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error that occurred while issuing the scan command.\n"
);
static
PyObject *py_zfs_pool_scan(PyObject *self, PyObject *args, PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	PyObject *py_func = NULL;
	PyObject *py_cmd = NULL;
	long func_val, cmd_val;
	int ret;
	py_zfs_error_t err;
	int error;
	char *kwnames[] = {"func", "cmd", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$OO", kwnames,
	    &py_func, &py_cmd))
		return NULL;

	if (NULL_OR_NONE(py_func)) {
		PyErr_SetString(PyExc_ValueError,
		    "func is required");
		return NULL;
	}

	func_val = PyLong_AsLong(py_func);
	if (func_val == -1 && PyErr_Occurred())
		return NULL;

	if (func_val < POOL_SCAN_NONE || func_val >= POOL_SCAN_FUNCS) {
		PyErr_Format(PyExc_ValueError,
		    "func value %ld is out of range [%d, %d)",
		    func_val, POOL_SCAN_NONE, POOL_SCAN_FUNCS);
		return NULL;
	}

	/* Default cmd to POOL_SCRUB_NORMAL */
	if (NULL_OR_NONE(py_cmd)) {
		cmd_val = POOL_SCRUB_NORMAL;
	} else {
		cmd_val = PyLong_AsLong(py_cmd);
		if (cmd_val == -1 && PyErr_Occurred())
			return NULL;

		if (cmd_val < POOL_SCRUB_NORMAL ||
		    cmd_val >= POOL_SCRUB_FLAGS_END) {
			PyErr_Format(PyExc_ValueError,
			    "cmd value %ld is out of range [%d, %d)",
			    cmd_val, POOL_SCRUB_NORMAL, POOL_SCRUB_FLAGS_END);
			return NULL;
		}
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.scan", "Oll",
	    p->name, func_val, cmd_val) < 0)
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_scan(p->zhp,
	    (pool_scan_func_t)func_val,
	    (pool_scrub_cmd_t)cmd_val);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&err, "zpool_scan() failed");
		return NULL;
	}

	/* Log the operation to zpool history */
	if (func_val == POOL_SCAN_NONE) {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool scrub -s %s", zpool_get_name(p->zhp));
	} else if (cmd_val == POOL_SCRUB_PAUSE) {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool scrub -p %s", zpool_get_name(p->zhp));
	} else if (func_val == POOL_SCAN_ERRORSCRUB) {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool scrub -e %s", zpool_get_name(p->zhp));
	} else {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool scrub %s", zpool_get_name(p->zhp));
	}
	if (error)
		return NULL;

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_get_properties__doc__,
"get_properties(*, properties) -> struct_zpool_property\n\n"
"------------------------------------------------------\n\n"
"Retrieve pool properties for the given set of ZPOOLProperty members.\n\n"
"Parameters\n"
"----------\n"
"properties: set, required\n"
"    A Python set of ZPOOLProperty enum members identifying which\n"
"    properties to retrieve. Properties not present in the set will\n"
"    have their slot set to None in the returned struct_zpool_property.\n\n"
"Returns\n"
"-------\n"
"truenas_pylibzfs.struct_zpool_property\n\n"
"Raises:\n"
"-------\n"
"RuntimeError:\n"
"    A property could not be retrieved from the pool handle.\n\n"
"Example\n"
"-------\n"
"Retrieve per-allocation-class space counters using the ZPOOL_CLASS_SPACE\n"
"property set from the property_sets submodule::\n\n"
"    import truenas_pylibzfs\n\n"
"    hdl = truenas_pylibzfs.open_handle()\n"
"    pool = hdl.open_pool(name='tank')\n"
"    props = pool.get_properties(\n"
"        properties=truenas_pylibzfs.property_sets.ZPOOL_CLASS_SPACE\n"
"    )\n"
"    # Values are int for numeric properties, str for string/index:\n"
"    print(props.class_normal_size)   # int\n"
"    print(props.class_special_used)  # int\n"
);
static
PyObject *py_zfs_pool_get_properties(PyObject *self,
				      PyObject *args,
				      PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	PyObject *properties = NULL;
	char *kwnames[] = {"properties", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$O", kwnames,
					 &properties))
		return NULL;

	if (properties == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"get_properties() requires 'properties' keyword "
				"argument");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.get_properties",
	    "O", p->name) < 0)
		return NULL;

	return py_zpool_get_properties(p, properties);
}

PyDoc_STRVAR(py_zfs_pool_set_properties__doc__,
"set_properties(*, properties) -> None\n\n"
"--------------------------------------\n\n"
"Set one or more writable pool properties.\n\n"
"Parameters\n"
"----------\n"
"properties: dict, required\n"
"    A dictionary mapping property keys to new values.\n"
"    Keys may be ZPOOLProperty enum members or plain strings.\n"
"    Values may be str, int, or bool (bool is converted to 'on'/'off').\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises:\n"
"-------\n"
"ValueError:\n"
"    A property is read-only or can only be set at pool creation time,\n"
"    or an unknown property name was supplied.\n"
"TypeError:\n"
"    An unexpected key or value type was encountered.\n"
"truenas_pylibzfs.ZFSException:\n"
"    A libzfs error occurred while setting a property.\n"
);
static
PyObject *py_zfs_pool_set_properties(PyObject *self,
				      PyObject *args,
				      PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	PyObject *properties = NULL;
	char *kwnames[] = {"properties", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$O", kwnames,
					 &properties))
		return NULL;

	if (properties == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"set_properties() requires 'properties' keyword "
				"argument");
		return NULL;
	}

	if (!PyDict_Check(properties)) {
		PyErr_SetString(PyExc_TypeError,
				"'properties' must be a dict");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.set_properties",
	    "OO", p->name, properties) < 0)
		return NULL;

	return py_zpool_set_properties(p, properties);
}

PyDoc_STRVAR(py_zfs_pool_get_user_properties__doc__,
"get_user_properties() -> dict[str, str]\n\n"
"---------------------------------------\n\n"
"Retrieve all user (custom) properties for the pool.\n\n"
"User properties are those whose name contains a colon, such as\n"
"'org.truenas:myprop'.  Only properties that have been explicitly set\n"
"are returned; the pool has no user properties by default.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"dict[str, str]\n"
"    Mapping of property name to value.  An empty dict is returned if no\n"
"    user properties have been set.\n\n"
"Raises\n"
"------\n"
"RuntimeError:\n"
"    The underlying lzc_get_props() call failed.\n"
);
static
PyObject *py_zfs_pool_get_user_properties(PyObject *self, PyObject *args)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.get_user_properties",
	    "O", p->name) < 0)
		return NULL;

	return py_zpool_get_user_properties(p);
}

PyDoc_STRVAR(py_zfs_pool_set_user_properties__doc__,
"set_user_properties(*, user_properties) -> None\n\n"
"------------------------------------------------\n\n"
"Set one or more user (custom) properties on the pool.\n\n"
"User properties must have a colon in their name (e.g. 'org.example:tag').\n"
"Both keys and values must be strings.  Setting a property that already\n"
"exists will overwrite its value.\n\n"
"Parameters\n"
"----------\n"
"user_properties: dict[str, str], required\n"
"    Mapping of property name to value.  Each key must contain a colon\n"
"    and must not exceed the maximum ZFS property name length.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"ValueError:\n"
"    A property name does not contain a colon, or exceeds the maximum\n"
"    allowed length.\n"
"TypeError:\n"
"    user_properties is not a dict, or a key or value is not a string.\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while setting a property.\n"
);
static
PyObject *py_zfs_pool_set_user_properties(PyObject *self,
					  PyObject *args,
					  PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	PyObject *user_properties = NULL;
	char *kwnames[] = {"user_properties", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$O", kwnames,
					 &user_properties))
		return NULL;

	if (user_properties == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"set_user_properties() requires "
				"'user_properties' keyword argument");
		return NULL;
	}

	if (!PyDict_Check(user_properties)) {
		PyErr_SetString(PyExc_TypeError,
				"'user_properties' must be a dict");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.set_user_properties",
	    "OO", p->name, user_properties) < 0)
		return NULL;

	return py_zpool_set_user_properties(p, user_properties);
}

PyDoc_STRVAR(py_zfs_pool_offline_device__doc__,
"offline_device(*, device, temporary=False) -> None\n\n"
"---------------------------------------------------\n\n"
"Take a pool device offline.\n\n"
"Parameters\n"
"----------\n"
"device: str, required\n"
"    Path or name of the device vdev to offline.\n"
"temporary: bool, optional, default=False\n"
"    If True, the offline state is not persisted across pool exports/imports.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while taking the device offline.\n"
);
static
PyObject *py_zfs_pool_offline_device(PyObject *self,
    PyObject *args,
    PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	char *device = NULL;
	boolean_t temporary = B_FALSE;
	int ret;
	int error;
	py_zfs_error_t zfs_err;
	char *kwnames[] = {"device", "temporary", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$sp", kwnames,
	    &device, &temporary))
		return NULL;

	if (device == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "offline_device() requires 'device' argument");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.offline_device",
	    "Os", p->name, device) < 0)
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_vdev_offline(p->zhp, device, temporary);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &zfs_err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&zfs_err, "zpool_vdev_offline() failed");
		return NULL;
	}

	error = py_log_history_fmt(p->pylibzfsp,
	    "zpool offline%s %s %s",
	    temporary ? " -t" : "", zpool_get_name(p->zhp), device);
	if (error)
		return NULL;

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_online_device__doc__,
"online_device(*, device, expand=False) -> None\n\n"
"------------------------------------------------\n\n"
"Bring a pool device back online.\n\n"
"Parameters\n"
"----------\n"
"device: str, required\n"
"    Path or name of the device vdev to online.\n"
"expand: bool, optional, default=False\n"
"    Expand the device to use all available space (equivalent to\n"
"    'zpool online -e').\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while bringing the device online.\n"
);
static
PyObject *py_zfs_pool_online_device(PyObject *self,
    PyObject *args,
    PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	char *device = NULL;
	boolean_t expand = B_FALSE;
	int flags;
	int ret;
	int error;
	py_zfs_error_t zfs_err;
	vdev_state_t newstate;
	char *kwnames[] = {"device", "expand", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$sp", kwnames,
	    &device, &expand))
		return NULL;

	if (device == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "online_device() requires 'device' argument");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.online_device",
	    "Os", p->name, device) < 0)
		return NULL;

	flags = expand ? ZFS_ONLINE_EXPAND : 0;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_vdev_online(p->zhp, device, flags, &newstate);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &zfs_err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&zfs_err, "zpool_vdev_online() failed");
		return NULL;
	}

	error = py_log_history_fmt(p->pylibzfsp,
	    "zpool online%s %s %s",
	    expand ? " -e" : "", zpool_get_name(p->zhp), device);
	if (error)
		return NULL;

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_add_vdevs__doc__,
"add_vdevs(*, storage_vdevs=None, cache_vdevs=None, log_vdevs=None,\n"
"          special_vdevs=None, dedup_vdevs=None, spare_vdevs=None,\n"
"          force=False) -> None\n\n"
"-----------------------------------------------------------------------\n\n"
"Add vdevs to an existing pool (equivalent to 'zpool add').\n\n"
"At least one vdev category must be non-empty.\n\n"
"Parameters\n"
"----------\n"
"storage_vdevs: iterable of struct_vdev_create_spec, optional\n"
"    Data vdevs to add.  When the pool already has storage vdevs, each\n"
"    new vdev must match the existing pool's type, parity, and child\n"
"    count (unless force=True).\n"
"cache_vdevs: iterable of struct_vdev_create_spec, optional\n"
"    L2ARC cache vdevs (leaf vdevs only).\n"
"log_vdevs: iterable of struct_vdev_create_spec, optional\n"
"    ZIL log vdevs (leaf or mirror).\n"
"special_vdevs: iterable of struct_vdev_create_spec, optional\n"
"    Special allocation class vdevs.  dRAID is not permitted.\n"
"    Parity must be >= existing pool storage parity (unless force=True).\n"
"dedup_vdevs: iterable of struct_vdev_create_spec, optional\n"
"    Dedup allocation class vdevs.  dRAID is not permitted.\n"
"    Parity must be >= existing pool storage parity (unless force=True).\n"
"spare_vdevs: iterable of struct_vdev_create_spec, optional\n"
"    Hot spare vdevs (leaf vdevs only).\n"
"force: bool, optional, default=False\n"
"    Skip pool-match validation (storage type/parity/width against existing\n"
"    pool geometry, special/dedup parity requirements), storage vdev width\n"
"    limits (mirror: max 4 members, raidz: max 15 drives), and the kernel\n"
"    ashift check.  Structural constraints (cache/spare must be leaf, log\n"
"    must be leaf or mirror, dRAID not permitted for special/dedup) always\n"
"    apply.  Equivalent to 'zpool add -f'.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"ValueError:\n"
"    A vdev specification is invalid or topology constraints are violated.\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while adding vdevs.\n"
);
static PyObject *
py_zfs_pool_add_vdevs(PyObject *self, PyObject *args, PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	py_zfs_add_vdevs_args_t ava = {0};
	boolean_t force = B_FALSE;
	char *kwnames[] = {
		"storage_vdevs", "cache_vdevs", "log_vdevs",
		"special_vdevs", "dedup_vdevs", "spare_vdevs",
		"force", NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$OOOOOOp", kwnames,
	    &ava.storage_vdevs, &ava.cache_vdevs, &ava.log_vdevs,
	    &ava.special_vdevs, &ava.dedup_vdevs, &ava.spare_vdevs,
	    &force))
		return (NULL);

	ava.force = force ? B_TRUE : B_FALSE;
	return (py_zfs_do_add_vdevs(p, &ava));
}

PyDoc_STRVAR(py_zfs_pool_iter_history__doc__,
"iter_history(*, skip_internal=True, since=0, until=0) -> Iterator[dict]\n\n"
"------------------------------------------------------------------------\n\n"
"Iterate over the pool's command history log.\n\n"
"Each yielded item is a dict with raw history-record key names, e.g.:\n"
"  'history time'     - Unix timestamp (int)\n"
"  'history command'  - Command string (str) — only for user commands\n"
"  'history who'      - UID that ran the command (int)\n"
"  'history hostname' - Hostname (str)\n\n"
"Parameters\n"
"----------\n"
"skip_internal: bool, optional, default=True\n"
"    When True (default) kernel-internal events (records that contain\n"
"    'history_internal_event') are suppressed, matching the default\n"
"    output of 'zpool history'.\n"
"since: int, optional, default=0\n"
"    Unix timestamp lower bound (inclusive).  Records with\n"
"    'history_time' < since are skipped.  0 means no lower bound.\n"
"    Filtering is applied before any Python object is allocated.\n"
"until: int, optional, default=0\n"
"    Unix timestamp upper bound (inclusive).  Records with\n"
"    'history_time' > until are skipped.  0 means no upper bound.\n\n"
"Yields\n"
"------\n"
"dict\n"
"    One history record.\n\n"
"Raises\n"
"------\n"
"ZFSException\n"
"    A libzfs error occurred while reading history.\n"
);
static PyObject *
py_zfs_pool_iter_history(PyObject *self, PyObject *args, PyObject *kwds)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	boolean_t skip_internal = B_TRUE;
	unsigned long long since = 0, until = 0;
	char *kwlist[] = {"skip_internal", "since", "until", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|pKK", kwlist,
	    &skip_internal, &since, &until)) {
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.iter_history",
	    "O", p->name) < 0) {
		return (NULL);
	}

	return (py_zfs_history_iter_create(p, skip_internal,
	    (uint64_t)since, (uint64_t)until));
}

PyDoc_STRVAR(py_zfs_pool_attach_vdev__doc__,
"attach_vdev(*, device, new_device, rebuild=False, force=False) -> None\n\n"
"----------------------------------------------------------------------\n\n"
"Attach a new device to an existing vdev.\n\n"
"Converts a single-device vdev into a mirror, or expands a raidz when\n"
"the raidz_expansion feature is enabled.\n\n"
"By default an error is raised if the resulting mirror would exceed\n"
"4 members or the resulting raidz would exceed 15 drives.  Pass\n"
"force=True to bypass these width limits.\n\n"
"Parameters\n"
"----------\n"
"device: str, required\n"
"    Path or name of the existing vdev to attach to.\n"
"new_device: struct_vdev_create_spec, required\n"
"    Specification for the new leaf device (type DISK or FILE).\n"
"rebuild: bool, optional, default=False\n"
"    If True, use sequential reconstruction instead of healing resilver.\n"
"    Requires the device_rebuild feature on mirrors and dRAID.\n"
"force: bool, optional, default=False\n"
"    If True, bypass the mirror/raidz width policy limits.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"ValueError:\n"
"    A required argument is missing, or a width limit would be exceeded\n"
"    without force=True.\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while attaching the device.\n"
);
static PyObject *
py_zfs_pool_attach_vdev(PyObject *self, PyObject *args, PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	char *device = NULL;
	PyObject *new_device = NULL;
	boolean_t rebuild = B_FALSE;
	boolean_t force = B_FALSE;
	nvlist_t *nvroot = NULL;
	PyObject *py_name = NULL;
	const char *new_path = NULL;
	int vdev_width = -1;
	boolean_t is_mirror = B_FALSE;
	boolean_t is_raidz = B_FALSE;
	int ret;
	int error;
	py_zfs_error_t zfs_err;
	char *kwnames[] = {"device", "new_device", "rebuild", "force", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$sOpp", kwnames,
	    &device, &new_device, &rebuild, &force))
		return (NULL);

	if (device == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "attach_vdev() requires 'device' argument");
		return (NULL);
	}
	if (new_device == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "attach_vdev() requires 'new_device' argument");
		return (NULL);
	}

	nvroot = py_zfs_build_single_vdev_nvroot(new_device);
	if (nvroot == NULL)
		return (NULL);

	py_name = PyStructSequence_GET_ITEM(new_device, 0);
	new_path = PyUnicode_AsUTF8(py_name);
	if (new_path == NULL) {
		fnvlist_free(nvroot);
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.attach_vdev",
	    "Os", p->name, device) < 0) {
		fnvlist_free(nvroot);
		return (NULL);
	}

	/*
	 * Phase 1: look up the current vdev width so we can apply policy
	 * limits before handing off to libzfs.
	 */
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	{
		boolean_t avail_spare = B_FALSE;
		boolean_t l2cache = B_FALSE;
		boolean_t log_dev = B_FALSE;
		nvlist_t **children;
		uint_t nchildren;
		nvlist_t *vdev_nvl;
		const char *type_ptr;

		/*
		 * We need the parent vdev (mirror/raidz), not the leaf
		 * device itself.  zpool_find_parent_vdev() returns the
		 * parent of the vdev identified by 'device', which is the
		 * mirror or raidz vdev when 'device' is a member disk.
		 * For a single-disk pool the parent is the root vdev
		 * (type="root"), so the mirror/raidz type checks below
		 * will not fire.
		 */
		vdev_nvl = zpool_find_parent_vdev(p->zhp, device,
		    &avail_spare, &l2cache, &log_dev);
		if (vdev_nvl != NULL) {
			if (nvlist_lookup_string(vdev_nvl, ZPOOL_CONFIG_TYPE,
			    &type_ptr) == 0) {
				is_mirror =
				    (strcmp(type_ptr, VDEV_TYPE_MIRROR) == 0);
				is_raidz =
				    (strcmp(type_ptr, VDEV_TYPE_RAIDZ) == 0);
			}
			if (nvlist_lookup_nvlist_array(vdev_nvl,
			    ZPOOL_CONFIG_CHILDREN, &children,
			    &nchildren) == 0)
				vdev_width = (int)nchildren;
			else
				vdev_width = 0;
		}
	}
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	/* Apply width policy (GIL held, no lock needed). */
	if (!force && vdev_width >= 0) {
		if (is_mirror &&
		    vdev_width >= PYLIBZFS_MAX_MIRROR_WIDTH) {
			PyErr_Format(PyExc_ValueError,
			    "attach_vdev: resulting mirror width (%d) would "
			    "exceed limit of %d; use force=True to override",
			    vdev_width + 1, PYLIBZFS_MAX_MIRROR_WIDTH);
			fnvlist_free(nvroot);
			return (NULL);
		}
		if (is_raidz &&
		    vdev_width >= PYLIBZFS_MAX_RAIDZ_WIDTH) {
			PyErr_Format(PyExc_ValueError,
			    "attach_vdev: resulting raidz width (%d) would "
			    "exceed limit of %d; use force=True to override",
			    vdev_width + 1, PYLIBZFS_MAX_RAIDZ_WIDTH);
			fnvlist_free(nvroot);
			return (NULL);
		}
	}

	/* Phase 2: perform the attach. */
	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_vdev_attach(p->zhp, device, new_path, nvroot,
	    B_FALSE, rebuild);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &zfs_err);
	else {
		boolean_t missing;
		(void) zpool_refresh_stats(p->zhp, &missing);
	}
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	fnvlist_free(nvroot);

	if (ret) {
		set_exc_from_libzfs(&zfs_err, "zpool_vdev_attach() failed");
		return (NULL);
	}

	error = py_log_history_fmt(p->pylibzfsp,
	    "zpool attach%s %s %s %s",
	    rebuild ? " -s" : "", zpool_get_name(p->zhp), device, new_path);
	if (error)
		return (NULL);

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_replace_vdev__doc__,
"replace_vdev(*, device, new_device=None, rebuild=False) -> None\n\n"
"---------------------------------------------------------------\n\n"
"Replace an existing pool device with a new one.\n\n"
"When new_device is None, replaces the device with itself, which is used\n"
"to clear a faulted state after physical replacement with the same path.\n\n"
"Parameters\n"
"----------\n"
"device: str, required\n"
"    Path or name of the existing vdev to replace.\n"
"new_device: struct_vdev_create_spec or None, optional, default=None\n"
"    Specification for the replacement device.  If None, the device is\n"
"    replaced with itself.\n"
"rebuild: bool, optional, default=False\n"
"    If True, use sequential reconstruction instead of healing resilver.\n"
"    Requires the device_rebuild feature on mirrors and dRAID.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"ValueError:\n"
"    A required argument is missing.\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while replacing the device.\n"
);
static PyObject *
py_zfs_pool_replace_vdev(PyObject *self, PyObject *args, PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	char *device = NULL;
	PyObject *new_device = NULL;
	boolean_t rebuild = B_FALSE;
	nvlist_t *nvroot = NULL;
	nvlist_t *child_nvl = NULL;
	nvlist_t *self_children[1];
	PyObject *py_name = NULL;
	const char *new_path = NULL;
	boolean_t whole_disk = B_FALSE;
	boolean_t self_replace;
	int ret;
	int error;
	py_zfs_error_t zfs_err;
	char *kwnames[] = {"device", "new_device", "rebuild", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$sOp", kwnames,
	    &device, &new_device, &rebuild))
		return (NULL);

	if (device == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "replace_vdev() requires 'device' argument");
		return (NULL);
	}

	self_replace = (new_device == NULL || new_device == Py_None);

	if (!self_replace) {
		nvroot = py_zfs_build_single_vdev_nvroot(new_device);
		if (nvroot == NULL)
			return (NULL);
		py_name = PyStructSequence_GET_ITEM(new_device, 0);
		new_path = PyUnicode_AsUTF8(py_name);
		if (new_path == NULL) {
			fnvlist_free(nvroot);
			return (NULL);
		}
	} else {
		/*
		 * Replace-with-self: build a root nvlist with a single child
		 * pointing at the same device path.  Use zfs_dev_is_whole_disk()
		 * to select the correct vdev type (DISK vs FILE).
		 */
		whole_disk = zfs_dev_is_whole_disk(device);
		child_nvl = fnvlist_alloc();
		fnvlist_add_string(child_nvl, ZPOOL_CONFIG_TYPE,
		    whole_disk ? VDEV_TYPE_DISK : VDEV_TYPE_FILE);
		fnvlist_add_string(child_nvl, ZPOOL_CONFIG_PATH, device);
		if (whole_disk)
			fnvlist_add_uint64(child_nvl, ZPOOL_CONFIG_WHOLE_DISK,
			    1ULL);
		nvroot = fnvlist_alloc();
		fnvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
		self_children[0] = child_nvl;
		fnvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
		    (const nvlist_t * const *)self_children, 1);
		fnvlist_free(child_nvl);
		new_path = device;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.replace_vdev",
	    "Os", p->name, device) < 0) {
		fnvlist_free(nvroot);
		return (NULL);
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_vdev_attach(p->zhp, device, new_path, nvroot,
	    B_TRUE, rebuild);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &zfs_err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	fnvlist_free(nvroot);

	if (ret) {
		set_exc_from_libzfs(&zfs_err, "zpool_vdev_attach() failed");
		return (NULL);
	}

	if (self_replace) {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool replace%s %s %s",
		    rebuild ? " -s" : "", zpool_get_name(p->zhp), device);
	} else {
		error = py_log_history_fmt(p->pylibzfsp,
		    "zpool replace%s %s %s %s",
		    rebuild ? " -s" : "", zpool_get_name(p->zhp), device,
		    new_path);
	}
	if (error)
		return (NULL);

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_detach_vdev__doc__,
"detach_vdev(*, device) -> None\n\n"
"------------------------------\n\n"
"Detach a device from a mirror or replacing vdev.\n\n"
"The device must be part of a mirror or replacing vdev.  The operation\n"
"is rejected by the kernel if it would leave fewer than the required\n"
"number of replicas.\n\n"
"Parameters\n"
"----------\n"
"device: str, required\n"
"    Path or name of the vdev to detach.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"ValueError:\n"
"    A required argument is missing.\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while detaching the device.\n"
);
static PyObject *
py_zfs_pool_detach_vdev(PyObject *self, PyObject *args, PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	char *device = NULL;
	int ret;
	int error;
	py_zfs_error_t zfs_err;
	char *kwnames[] = {"device", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$s", kwnames,
	    &device))
		return (NULL);

	if (device == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "detach_vdev() requires 'device' argument");
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.detach_vdev",
	    "Os", p->name, device) < 0)
		return (NULL);

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_vdev_detach(p->zhp, device);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &zfs_err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&zfs_err, "zpool_vdev_detach() failed");
		return (NULL);
	}

	error = py_log_history_fmt(p->pylibzfsp,
	    "zpool detach %s %s", zpool_get_name(p->zhp), device);
	if (error)
		return (NULL);

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_remove_vdev__doc__,
"remove_vdev(*, device) -> None\n\n"
"------------------------------\n\n"
"Remove a top-level vdev from the pool (asynchronous).\n\n"
"Supported for top-level data vdevs, log devices, and redundant vdevs\n"
"with uniform sector size.  Spares, L2ARC, and dRAID spares are not\n"
"removable.  The operation is asynchronous; use cancel_remove_vdev()\n"
"to abort an in-progress removal.\n\n"
"Parameters\n"
"----------\n"
"device: str, required\n"
"    Path or name of the top-level vdev to remove.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"ValueError:\n"
"    A required argument is missing.\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while removing the device.\n"
);
static PyObject *
py_zfs_pool_remove_vdev(PyObject *self, PyObject *args, PyObject *kwargs)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	char *device = NULL;
	int ret;
	int error;
	py_zfs_error_t zfs_err;
	char *kwnames[] = {"device", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$s", kwnames,
	    &device))
		return (NULL);

	if (device == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "remove_vdev() requires 'device' argument");
		return (NULL);
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.remove_vdev",
	    "Os", p->name, device) < 0)
		return (NULL);

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_vdev_remove(p->zhp, device);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &zfs_err);
	else {
		boolean_t missing;
		(void) zpool_refresh_stats(p->zhp, &missing);
	}
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&zfs_err, "zpool_vdev_remove() failed");
		return (NULL);
	}

	error = py_log_history_fmt(p->pylibzfsp,
	    "zpool remove %s %s", zpool_get_name(p->zhp), device);
	if (error)
		return (NULL);

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_pool_cancel_remove_vdev__doc__,
"cancel_remove_vdev(*) -> None\n\n"
"-----------------------------\n\n"
"Cancel an in-progress asynchronous vdev removal.\n\n"
"Equivalent to 'zpool remove -s <pool>'.  Raises ZFSError if no removal\n"
"is currently in progress.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred, or no removal is in progress.\n"
);
static PyObject *
py_zfs_pool_cancel_remove_vdev(PyObject *self, PyObject *args)
{
	py_zfs_pool_t *p = (py_zfs_pool_t *)self;
	int ret;
	int error;
	py_zfs_error_t zfs_err;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSPool.cancel_remove_vdev",
	    "O", p->name) < 0)
		return (NULL);

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ret = zpool_vdev_remove_cancel(p->zhp);
	if (ret)
		py_get_zfs_error(p->pylibzfsp->lzh, &zfs_err);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (ret) {
		set_exc_from_libzfs(&zfs_err,
		    "zpool_vdev_remove_cancel() failed");
		return (NULL);
	}

	error = py_log_history_fmt(p->pylibzfsp,
	    "zpool remove -s %s", zpool_get_name(p->zhp));
	if (error)
		return (NULL);

	Py_RETURN_NONE;
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
	{
		.ml_name = "scrub_info",
		.ml_meth = py_zfs_pool_scrub_info,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_scrub__doc__
	},
	{
		.ml_name = "expand_info",
		.ml_meth = py_zfs_pool_expand_info,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_expand__doc__
	},
	{
		.ml_name = "scan",
		.ml_meth = (PyCFunction)py_zfs_pool_scan,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_scan__doc__
	},
	{
		.ml_name = "get_properties",
		.ml_meth = (PyCFunction)py_zfs_pool_get_properties,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_get_properties__doc__
	},
	{
		.ml_name = "set_properties",
		.ml_meth = (PyCFunction)py_zfs_pool_set_properties,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_set_properties__doc__
	},
	{
		.ml_name = "get_user_properties",
		.ml_meth = py_zfs_pool_get_user_properties,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_get_user_properties__doc__
	},
	{
		.ml_name = "set_user_properties",
		.ml_meth = (PyCFunction)py_zfs_pool_set_user_properties,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_set_user_properties__doc__
	},
	{
		.ml_name = "offline_device",
		.ml_meth = (PyCFunction)py_zfs_pool_offline_device,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_offline_device__doc__
	},
	{
		.ml_name = "online_device",
		.ml_meth = (PyCFunction)py_zfs_pool_online_device,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_online_device__doc__
	},
	{
		.ml_name = "add_vdevs",
		.ml_meth = (PyCFunction)py_zfs_pool_add_vdevs,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_add_vdevs__doc__
	},
	{
		.ml_name = "attach_vdev",
		.ml_meth = (PyCFunction)py_zfs_pool_attach_vdev,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_attach_vdev__doc__
	},
	{
		.ml_name = "replace_vdev",
		.ml_meth = (PyCFunction)py_zfs_pool_replace_vdev,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_replace_vdev__doc__
	},
	{
		.ml_name = "detach_vdev",
		.ml_meth = (PyCFunction)py_zfs_pool_detach_vdev,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_detach_vdev__doc__
	},
	{
		.ml_name = "remove_vdev",
		.ml_meth = (PyCFunction)py_zfs_pool_remove_vdev,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_remove_vdev__doc__
	},
	{
		.ml_name = "cancel_remove_vdev",
		.ml_meth = py_zfs_pool_cancel_remove_vdev,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_pool_cancel_remove_vdev__doc__
	},
	{
		.ml_name = "iter_history",
		.ml_meth = (PyCFunction)py_zfs_pool_iter_history,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_pool_iter_history__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSPool = {
	.tp_name = PYLIBZFS_TYPES_MODULE_NAME ".ZFSPool",
	.tp_basicsize = sizeof (py_zfs_pool_t),
	.tp_methods = zfs_pool_methods,
	.tp_getset = zfs_pool_getsetters,
	.tp_new = py_no_new_impl,
	.tp_doc = "ZFSPool",
	.tp_dealloc = (destructor)py_zfs_pool_dealloc,
	.tp_repr = py_repr_zfs_pool,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};

py_zfs_pool_t *init_zfs_pool(py_zfs_t *lzp, zpool_handle_t *zhp)
{
	py_zfs_pool_t *out = NULL;
	const char *name;

	out = (py_zfs_pool_t *)ZFSPool.tp_alloc(&ZFSPool, 0);
	if (out == NULL)
		return (NULL);

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
