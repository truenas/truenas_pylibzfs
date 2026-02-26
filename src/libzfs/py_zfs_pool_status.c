#include "../truenas_pylibzfs.h"

/*
 * ZFS pool status implementation for module
 *
 * The pool status is implemented as a struct sequence object in the C API.
 *
 * ---------------------------------------------------------------------------
 * Struct layout — sample configurations
 * ---------------------------------------------------------------------------
 *
 * Example 1: dRAID1 with distributed spare
 *   zpool create pool draid1:3d:5c:1s sda sdb sdc sdd sde
 *
 * struct_zpool_status
 * ├── status:           ZPOOL_STATUS_OK
 * ├── reason:           None
 * ├── action:           None
 * ├── message:          None
 * ├── corrupted_files:  ()
 * ├── storage_vdevs:    (struct_vdev,)
 * │   └── struct_vdev
 * │       ├── name:      "draid1-0"
 * │       ├── vdev_type: "draid1:3d:5c:1s"
 * │       ├── guid:      0xAAAA  ◄──────────────────────────┐
 * │       ├── state:     ONLINE                             │
 * │       ├── stats:     struct_vdev_stats                  │
 * │       ├── children:  (struct_vdev × 5)                  │
 * │       │   └── struct_vdev  [one of five disk vdevs]     │
 * │       │       ├── name:      "sda"                      │
 * │       │       ├── vdev_type: "disk"                     │
 * │       │       ├── guid:      0xBBBB                     │
 * │       │       ├── state:     ONLINE                     │
 * │       │       ├── stats:     struct_vdev_stats          │
 * │       │       ├── children:  None                       │
 * │       │       └── top_guid:  None                       │
 * │       └── top_guid:  None                               │
 * ├── support_vdevs:                                        │
 * │   ├── cache:   ()                                       │
 * │   ├── log:     ()                                       │
 * │   ├── special: ()                                       │
 * │   └── dedup:   ()                                       │
 * └── spares:  (struct_vdev,)                               │
 *     └── struct_vdev                                       │
 *         ├── name:      "draid1-0-0"                       │
 *         ├── vdev_type: "draid_spare"                      │
 *         ├── guid:      0xCCCC                             │
 *         ├── state:     AVAIL                              │
 *         ├── stats:     struct_vdev_stats                  │
 *         ├── children:  None                               │
 *         └── top_guid:  0xAAAA  ────────────────────────────┘
 *
 * ---------------------------------------------------------------------------
 *
 * Example 2: 2× RAIDZ2 with cache and log
 *   zpool create pool \
 *     raidz2 sda sdb sdc sdd  raidz2 sde sdf sdg sdh \
 *     cache sdi  log sdj
 *
 * struct_zpool_status
 * ├── status:           ZPOOL_STATUS_OK
 * ├── reason:           None
 * ├── action:           None
 * ├── message:          None
 * ├── corrupted_files:  ()
 * ├── storage_vdevs:    (struct_vdev, struct_vdev)
 * │   ├── struct_vdev  [first raidz2]
 * │   │   ├── name:      "raidz2-0"
 * │   │   ├── vdev_type: "raidz2"
 * │   │   ├── guid:      0xDDDD
 * │   │   ├── state:     ONLINE
 * │   │   ├── stats:     struct_vdev_stats
 * │   │   ├── children:  (struct_vdev × 4)  [sda..sdd, vdev_type="disk"]
 * │   │   └── top_guid:  None
 * │   └── struct_vdev  [second raidz2]
 * │       ├── name:      "raidz2-1"
 * │       ├── vdev_type: "raidz2"
 * │       ├── guid:      0xEEEE
 * │       ├── state:     ONLINE
 * │       ├── stats:     struct_vdev_stats
 * │       ├── children:  (struct_vdev × 4)  [sde..sdh, vdev_type="disk"]
 * │       └── top_guid:  None
 * ├── support_vdevs:
 * │   ├── cache:   (struct_vdev,)  [sdi, vdev_type="disk", top_guid=None]
 * │   ├── log:     (struct_vdev,)  [sdj, vdev_type="disk", top_guid=None]
 * │   ├── special: ()
 * │   └── dedup:   ()
 * └── spares:  ()
 *
 * ---------------------------------------------------------------------------
 */

PyDoc_STRVAR(py_pool_status_status__doc__,
PYLIBZFS_MODULE_NAME ".ZPOOLStatus enum value for the current zpool status.\n"
);

PyDoc_STRVAR(py_pool_status_reason__doc__,
"Detailed explanation of the current pool status. If the pool status is\n"
PYLIBZFS_MODULE_NAME ".ZPOOLStatus.ZPOOL_STATUS_OK, this field will be None.\n"
);

PyDoc_STRVAR(py_pool_status_action__doc__,
"Possible administrative action(s) that may be taken to resolve the status\n"
"issue. If the pool status is "
PYLIBZFS_MODULE_NAME ".ZPOOLStatus.ZPOOL_STATUS_OK\n"
"then this field will be None.\n\n"
"WARNING: actions described in this field should not be taken without a full\n"
"understanding of the impact of the commands involved.\n"
);

PyDoc_STRVAR(py_pool_status_message__doc__,
"URL to upstream OpenZFS documentation for the issue encountered. If the pool\n"
"status is " PYLIBZFS_MODULE_NAME ".ZPOOLStatus.ZPOOL_STATUS_OK, or if there\n"
"is no associated documentation URL for the status, this field will be None.\n"
);

PyDoc_STRVAR(py_pool_status_files__doc__,
"Tuple containing absolute paths to files located in the pool that contain\n"
"errors.\n"
);

PyDoc_STRVAR(py_pool_status_storage__doc__,
"Tuple of " PYLIBZFS_MODULE_NAME ".struct_vdev objects that comprise the topology\n"
"of the storage pool. Each tuple member represents a top-level vdev.\n"
);

PyDoc_STRVAR(py_pool_status_support__doc__,
PYLIBZFS_MODULE_NAME ".struct_support_vdev object containing information about\n"
"the support vdevs in-use by the pool.\n"
);

PyDoc_STRVAR(py_pool_status_spares__doc__,
"Tuple of " PYLIBZFS_MODULE_NAME ".struct_vdev objects for hot spare vdevs,\n"
"or an empty tuple if no spares are configured.\n"
);

PyStructSequence_Field struct_pool_status_prop [] = {
	{"status", py_pool_status_status__doc__},
	{"reason", py_pool_status_reason__doc__},
	{"action", py_pool_status_action__doc__},
	{"message", py_pool_status_message__doc__},
	{"corrupted_files", py_pool_status_files__doc__},
	{"storage_vdevs", py_pool_status_storage__doc__},
	{"support_vdevs", py_pool_status_support__doc__},
	{"spares", py_pool_status_spares__doc__},
	{0},
};
#define VDEVS_STORAGE_IDX 5
#define VDEVS_SUPPORT_IDX 6
#define VDEVS_SPARES_IDX  7

PyStructSequence_Desc struct_pool_status_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_zpool_status",
	.fields = struct_pool_status_prop,
	.doc = "Python ZFS pool status structure",
	.n_in_sequence = 8
};

PyStructSequence_Field struct_pool_support_vdev [] = {
	{"cache", "Tuple of " PYLIBZFS_MODULE_NAME ".struct_vdev objects "
	          "for L2ARC (read cache) vdevs, or None if not present."},
	{"log", "Tuple of " PYLIBZFS_MODULE_NAME ".struct_vdev objects "
	        "for ZFS Intent Log (SLOG) vdevs, or None if not present."},
	{"special", "Tuple of " PYLIBZFS_MODULE_NAME ".struct_vdev objects "
	            "for special (metadata) vdevs, or None if not present."},
	{"dedup", "Tuple of " PYLIBZFS_MODULE_NAME ".struct_vdev objects "
	          "for dedup table vdevs, or None if not present."},
	{0},
};

PyStructSequence_Desc struct_pool_support_vdev_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_support_vdev",
	.fields = struct_pool_support_vdev,
	.doc = "Python ZFS pool support vdev structure",
	.n_in_sequence = 4
};

PyStructSequence_Field struct_vdev_stats [] = {
	{"allocated", "Space allocated, in bytes"},
	{"space", "Total capacity, in bytes"},
	{"dspace", "Deflated capacity, in bytes"},
	{"pspace", "Physical capacity, in bytes"},
	{"rsize", "Replaceable dev size, in bytes"},
	{"esize", "Expandable dev size, in bytes"},
	{"read_errors", "Number of read errors"},
	{"write_errors", "Number of write errors"},
	{"checksum_errors", "Number of checksum errors"},
	{"initialize_errors", "Number of errors initializing vdev"},
	{"dio_verify_errors", "Number of O_DIRECT checksum errors"},
	{"slow_ios", "Number of slow I/Os. None for non-leaf vdevs (those with children)."},
	{"self_healed_bytes", "Number of self-healed bytes"},
	{0},
};

PyStructSequence_Desc struct_vdev_stats_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_vdev_stats",
	.fields = struct_vdev_stats,
	.doc = "Python ZFS vdev stats structure",
	.n_in_sequence = 13
};

/* Index constants for struct_vdev_stats fields — must stay in sync with
 * struct_vdev_stats[] above. */
#define VS_ALLOCATED_IDX 0
#define VS_SPACE_IDX 1
#define VS_DSPACE_IDX 2
#define VS_PSPACE_IDX 3
#define VS_RSIZE_IDX 4
#define VS_ESIZE_IDX 5
#define VS_READ_ERRORS_IDX 6
#define VS_WRITE_ERRORS_IDX 7
#define VS_CHECKSUM_ERRORS_IDX 8
#define VS_INIT_ERRORS_IDX 9
#define VS_DIO_VERIFY_ERRORS_IDX 10
#define VS_SLOW_IOS_IDX 11
#define VS_SELF_HEALED_IDX 12

PyStructSequence_Field struct_vdev_status_prop [] = {
	{"name", "name of the vdev"},
	{"vdev_type", "type of the vdev"},
	{"guid", "GUID for the vdev"},
	{"state", "State of the vdev"},
	{"stats", "Stats counters for vdev."},
	{"children", "Tuple of vdevs that make up this vdev (if applicable)"},
	{"top_guid", "For draid_spare vdevs: GUID of the top-level dRAID vdev "
	             "that owns this distributed spare (matches the guid field "
	             "of the originating draid entry in storage_vdevs). "
	             "None for all other vdev types."},
	{0},
};
#define STATS_IDX    4
#define CHILDREN_IDX 5
#define TOP_GUID_IDX 6

PyStructSequence_Desc struct_vdev_status_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_vdev",
	.fields = struct_vdev_status_prop,
	.doc = "Python pool vdev status structure",
	.n_in_sequence = 7
};

/*
 * Request mask values for getting vdev-related info
 * Minimally, storage class or one of the support classes is required
 */
#define PY_VDEV_CLASS_STORAGE	0x01
#define PY_VDEV_CLASS_LOG	0x02
#define PY_VDEV_CLASS_SPARE	0x04
#define PY_VDEV_CLASS_CACHE	0x08
#define PY_VDEV_CLASS_SPECIAL	0x10
#define PY_VDEV_CLASS_DEDUP	0x20
#define PY_VDEV_CLASS_SUPPORT	(PY_VDEV_CLASS_LOG | PY_VDEV_CLASS_SPARE | \
	PY_VDEV_CLASS_CACHE | PY_VDEV_CLASS_SPECIAL | PY_VDEV_CLASS_DEDUP )
#define PY_VDEV_CLASS_ALL	(PY_VDEV_CLASS_STORAGE | PY_VDEV_CLASS_SUPPORT)
#define PY_VDEV_DATA_WANT_STATS	0x40  // gather stats on vdevs

/*
 * Buffer large enough to hold a fully-qualified draid type name of the form
 * "draid<parity>:<data>d:<children>c:<spares>s\0" with plenty of headroom.
 */
#define VDEV_TYPE_NAME_BUF_SIZE	64

#define PY_VDEV_MASK_ALL	(PY_VDEV_CLASS_ALL | PY_VDEV_DATA_WANT_STATS)

static
boolean_t parse_vdev_stats(vdev_stat_t *vs,
			   boolean_t has_children,
			   PyObject *pyvdev)
{
	PyObject *val = NULL;

	val = PyLong_FromUnsignedLong(vs->vs_alloc);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_ALLOCATED_IDX, val);

	val = PyLong_FromUnsignedLong(vs->vs_space);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_SPACE_IDX, val);

	val = PyLong_FromUnsignedLong(vs->vs_dspace);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_DSPACE_IDX, val);

	val = PyLong_FromUnsignedLong(vs->vs_pspace);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_PSPACE_IDX, val);

	val = PyLong_FromUnsignedLong(vs->vs_rsize);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_RSIZE_IDX, val);

	val = PyLong_FromUnsignedLong(vs->vs_esize);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_ESIZE_IDX, val);

	// Read errors
	val = PyLong_FromUnsignedLong(vs->vs_read_errors);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_READ_ERRORS_IDX, val);

	// Write errors
	val = PyLong_FromUnsignedLong(vs->vs_write_errors);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_WRITE_ERRORS_IDX, val);

	// checksum errors
	val = PyLong_FromUnsignedLong(vs->vs_checksum_errors);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_CHECKSUM_ERRORS_IDX, val);

	val = PyLong_FromUnsignedLong(vs->vs_initialize_errors);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_INIT_ERRORS_IDX, val);

	val = PyLong_FromUnsignedLong(vs->vs_dio_verify_errors);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_DIO_VERIFY_ERRORS_IDX, val);

	if (has_children) {
		// slow ios counter
		PyStructSequence_SetItem(pyvdev, VS_SLOW_IOS_IDX, Py_NewRef(Py_None));
	} else {
		// slow ios counter
		val = PyLong_FromUnsignedLong(vs->vs_slow_ios);
		if (val == NULL)
			return B_FALSE;

		PyStructSequence_SetItem(pyvdev, VS_SLOW_IOS_IDX, val);
	}

	val = PyLong_FromUnsignedLong(vs->vs_self_healed);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, VS_SELF_HEALED_IDX, val);

	return B_TRUE;
}

static
boolean_t add_basic_vdev_props(pylibzfs_state_t *state,
			       vdev_stat_t *vs,
			       const char *name,
			       const char *type,
			       uint64_t guid,
			       PyObject *pyvdev)
{
	PyObject *val;

	val = PyUnicode_FromString(name);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, 0, val);

	val = PyUnicode_FromString(type);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, 1, val);

	val = PyLong_FromUnsignedLong(guid);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, 2, val);

	val = PyObject_CallFunction(state->vdev_state_enum,
				    "i", vs->vs_state);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, 3, val);

	return B_TRUE;
}

// forward reference because we can have recursion in getting vdev status
static
PyObject *vdev_nvlist_array_to_list(py_zfs_pool_t *pypool,
				    pylibzfs_state_t *state,
				    nvlist_t **child,
				    uint child_cnt,
				    uint depth,
				    uint request_mask);

static
PyObject *gen_vdev_status_nvlist(pylibzfs_state_t *state,
				 py_zfs_pool_t *pypool,
				 nvlist_t *nv,
				 uint depth,
				 uint request_mask)
{
	nvlist_t **child;
	uint_t vsc, children;
	const char *type;
	uint64_t guid, nparity;
	char *vname = NULL;
	char type_buf[VDEV_TYPE_NAME_BUF_SIZE];
	PyObject *out = NULL;
	PyObject *vdev_stats = NULL;
	vdev_stat_t *vs;

	Py_BEGIN_ALLOW_THREADS
	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;
	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) == 0);
	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);

	/*
	 * For raidz vdevs the raw ZPOOL_CONFIG_TYPE is always "raidz"
	 * regardless of parity level.  Append the parity count to match
	 * the display name used by zpool(8) (e.g. "raidz1", "raidz2").
	 *
	 * For draid vdevs the raw ZPOOL_CONFIG_TYPE is always "draid"
	 * regardless of redundancy.  Construct the full display name in the
	 * same format as zpool_draid_name() (e.g. "draid2:4d:6c:0s").
	 */
	if (strcmp(type, VDEV_TYPE_RAIDZ) == 0) {
		verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY,
		    &nparity) == 0);
		(void) snprintf(type_buf, sizeof (type_buf), "%s%llu",
		    type, (u_longlong_t)nparity);
		type = type_buf;
	} else if (strcmp(type, VDEV_TYPE_DRAID) == 0) {
		uint64_t ndata, nspares;
		verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY,
		    &nparity) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DRAID_NDATA,
		    &ndata) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DRAID_NSPARES,
		    &nspares) == 0);
		(void) snprintf(type_buf, sizeof (type_buf),
		    "%s%llu:%llud:%lluc:%llus",
		    type, (u_longlong_t)nparity, (u_longlong_t)ndata,
		    (u_longlong_t)children, (u_longlong_t)nspares);
		type = type_buf;
	}

	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0);
	PY_ZFS_LOCK(pypool->pylibzfsp);
	vname = zpool_vdev_name(pypool->pylibzfsp->lzh,
				pypool->zhp, nv,
				VDEV_NAME_TYPE_ID);
	PY_ZFS_UNLOCK(pypool->pylibzfsp);

	Py_END_ALLOW_THREADS

	if (strcmp(type, VDEV_TYPE_INDIRECT) == 0) {
		free(vname);
		return NULL;
	}

	out = PyStructSequence_New(state->struct_vdev_status_type);
	if (out == NULL)
		goto fail;

	if (!add_basic_vdev_props(state, vs, vname, type, guid, out))
		goto fail;

	free(vname);
	vname = NULL;

	if (request_mask & PY_VDEV_DATA_WANT_STATS) {
		vdev_stats = PyStructSequence_New(state->struct_vdev_stats_type);
		if (vdev_stats == NULL)
			goto fail;

		if (!parse_vdev_stats(vs, children, vdev_stats)) {
			Py_CLEAR(vdev_stats);
			goto fail;
		}
	} else {
		vdev_stats = Py_NewRef(Py_None);
	}

	PyStructSequence_SetItem(out, STATS_IDX, vdev_stats);

	if (children == 0)
		PyStructSequence_SetItem(out, CHILDREN_IDX, Py_NewRef(Py_None));
	else {
		PyObject *cl = NULL;
		PyObject *child_tuple = NULL;

		cl = vdev_nvlist_array_to_list(pypool,
					       state,
					       child,
					       children,
					       depth +1,
					       request_mask);
		if (cl == NULL)
			goto fail;

		child_tuple = PyList_AsTuple(cl);
		Py_CLEAR(cl);
		if (child_tuple == NULL)
			goto fail;

		PyStructSequence_SetItem(out, CHILDREN_IDX, child_tuple);
	}

	/*
	 * top_guid: for draid_spare vdevs, the GUID of the top-level dRAID
	 * vdev that owns this distributed spare (stored in the nvlist as
	 * ZPOOL_CONFIG_TOP_GUID).  None for all other vdev types.
	 */
	if (strcmp(type, VDEV_TYPE_DRAID_SPARE) == 0) {
		uint64_t top_guid;
		PyObject *val;

		verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_TOP_GUID,
		    &top_guid) == 0);
		val = PyLong_FromUnsignedLongLong(top_guid);
		if (val == NULL)
			goto fail;

		PyStructSequence_SetItem(out, TOP_GUID_IDX, val);
	} else {
		PyStructSequence_SetItem(out, TOP_GUID_IDX, Py_NewRef(Py_None));
	}

	return out;
fail:
	Py_CLEAR(out);
	free(vname);  // allocated by libzfs using system malloc
	return NULL;
}

/*
 * Most of the time vdevs are presented in the pool config as an
 * array of nvlists that are retrieved via nvlist_lookup_nvlist_array().
 * This returns an array of nvlists and its size. This function takes
 * the results of this lookup and converts it into a python list of
 * struct sequence objects for the nvlist array.
 */
static
PyObject *vdev_nvlist_array_to_list(py_zfs_pool_t *pypool,
				    pylibzfs_state_t *state,
				    nvlist_t **child,
				    uint child_cnt,
				    uint depth,
				    uint request_mask)
{
	PyObject *vdev_list = NULL;
	uint c;
	uint unknown = (request_mask & ~PY_VDEV_MASK_ALL);
	uint storage_classes = (request_mask & PY_VDEV_CLASS_ALL);

	PYZFS_ASSERT((storage_classes != 0), "No vdev types requested");
	PYZFS_ASSERT((unknown == 0), "Unknown value for request mask");

	vdev_list = PyList_New(0);
	if (vdev_list == NULL)
		return NULL;

	for (c = 0; c < child_cnt; c++) {
		PyObject *vdev = NULL;
		uint64_t is_log = B_FALSE, is_hole = B_FALSE;
		uint vdev_class = 0;
		int err;
		const char *bias = NULL;
		const char *type = NULL;

		// Bail out early if we can
		nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE, &is_hole);
		if (is_hole)
			continue;

		// Identify the vdev class in this item
		nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG, &is_log);
		if (is_log) {
			vdev_class = PY_VDEV_CLASS_LOG;
		} else {
			nvlist_lookup_string(child[c],
				ZPOOL_CONFIG_ALLOCATION_BIAS, &bias);

			nvlist_lookup_string(child[c], ZPOOL_CONFIG_TYPE,
				&type);
		}

		// Don't report on indirect vdevs
		if (!is_log && strcmp(type, VDEV_TYPE_INDIRECT) == 0) {
			continue;
		}

		if (!is_log && (bias == NULL)) {
			// For our purposes here L2ARC and STORAGE
			// vdevs get flagged the same
			vdev_class = PY_VDEV_CLASS_STORAGE | PY_VDEV_CLASS_CACHE;

		} else if (!is_log) {
			if (strcmp(bias, VDEV_ALLOC_BIAS_DEDUP) == 0) {
				vdev_class = PY_VDEV_CLASS_DEDUP;

			} else if (strcmp(bias, VDEV_ALLOC_BIAS_SPECIAL) == 0) {
				vdev_class = PY_VDEV_CLASS_SPECIAL;
			}
		}

		// Array of L2ARC devices is stored in separate nvlist and so we
		// won't see them here. Caller will first request the array, then
		// pass to this function.
		PYZFS_ASSERT((vdev_class != 0), "Unable to determine vdev class");

		if ((request_mask & vdev_class) == 0) {
			// This is not the vdev we're looking for
			continue;
		}

		// If we're here, we want to include this vdev in our list and
		// so we should generate it
		vdev = gen_vdev_status_nvlist(state, pypool,
					      child[c], depth + 1,
					      request_mask);

		if (vdev == NULL) {
			Py_DECREF(vdev_list);
			return NULL;
		}

		err = PyList_Append(vdev_list, vdev);
		Py_CLEAR(vdev);
		if (err) {
			Py_DECREF(vdev_list);
			return NULL;
		}
	}

	return vdev_list;
}

static
boolean_t populate_support_vdevs(py_zfs_pool_t *pypool,
				 pylibzfs_state_t *state,
				 nvlist_t *nvl,
				 PyObject *vdev_struct,
				 boolean_t get_stats)
{
	uint_t children, cache_cnt;
	nvlist_t **child, **cache;
	PyObject *l_vdevs = NULL;
	PyObject *s_vdevs = NULL;
	PyObject *d_vdevs = NULL;
	PyObject *c_vdevs = NULL;
	PyObject *val = NULL;
	uint mask = get_stats ? PY_VDEV_DATA_WANT_STATS : 0;

	Py_BEGIN_ALLOW_THREADS
	if (nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;
	if (nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_L2CACHE,
	    &cache, &cache_cnt) != 0)
		cache_cnt = 0;
	Py_END_ALLOW_THREADS

	if (!children) {
		// This shouldn't happen, but let's not philosphize deeply
		// The pool has no vdevs and so it definitely doesn't have
		// any "support" vdevs.
		l_vdevs = PyList_New(0);
		s_vdevs = PyList_New(0);
		d_vdevs = PyList_New(0);
	} else {
		l_vdevs = vdev_nvlist_array_to_list(pypool,
						    state,
						    child,
						    children,
						    0,
						    PY_VDEV_CLASS_LOG | mask);
		s_vdevs = vdev_nvlist_array_to_list(pypool,
						    state,
						    child,
						    children,
						    0,
						    PY_VDEV_CLASS_SPECIAL | mask);
		d_vdevs = vdev_nvlist_array_to_list(pypool,
						    state,
						    child,
						    children,
						    0,
						    PY_VDEV_CLASS_DEDUP | mask);
	}

	if (!cache_cnt)
		c_vdevs = PyList_New(0);
	else {
		c_vdevs = vdev_nvlist_array_to_list(pypool,
						    state,
						    cache,
						    cache_cnt,
						    0,
						    PY_VDEV_CLASS_CACHE | mask);
	}

	if (!l_vdevs || !s_vdevs || !d_vdevs || !c_vdevs)
		goto fail;

	val = PyList_AsTuple(c_vdevs);
	Py_CLEAR(c_vdevs);
	if (val == NULL)
		goto fail;

	PyStructSequence_SetItem(vdev_struct, 0, val);

	val = PyList_AsTuple(l_vdevs);
	Py_CLEAR(l_vdevs);
	if (val == NULL)
		goto fail;

	PyStructSequence_SetItem(vdev_struct, 1, val);

	val = PyList_AsTuple(s_vdevs);
	Py_CLEAR(s_vdevs);
	if (val == NULL)
		goto fail;

	PyStructSequence_SetItem(vdev_struct, 2, val);

	val = PyList_AsTuple(d_vdevs);
	Py_CLEAR(d_vdevs);
	if (val == NULL)
		goto fail;

	PyStructSequence_SetItem(vdev_struct, 3, val);

	return B_TRUE;
fail:
	Py_CLEAR(l_vdevs);
	Py_CLEAR(s_vdevs);
	Py_CLEAR(d_vdevs);
	Py_CLEAR(c_vdevs);
	return B_FALSE;
}

static
PyObject *pypool_status_get_support_vdevs(py_zfs_pool_t *pypool,
					  nvlist_t *nvl,
					  boolean_t get_stats)
{
	pylibzfs_state_t *state = py_get_module_state(pypool->pylibzfsp);
	PyObject *vdev_struct;

	vdev_struct = PyStructSequence_New(state->struct_support_vdev_type);
	if (vdev_struct == NULL)
		return NULL;

	if (!populate_support_vdevs(pypool, state, nvl, vdev_struct, get_stats))
		Py_CLEAR(vdev_struct);

	return vdev_struct;
}

static
PyObject *pypool_status_get_spare_vdevs(py_zfs_pool_t *pypool,
					nvlist_t *nvl,
					boolean_t get_stats)
{
	nvlist_t **spares;
	uint_t nspares;
	PyObject *vdev_list = NULL;
	pylibzfs_state_t *state = py_get_module_state(pypool->pylibzfsp);
	uint request_mask = PY_VDEV_CLASS_STORAGE;

	if (get_stats)
		request_mask |= PY_VDEV_DATA_WANT_STATS;

	Py_BEGIN_ALLOW_THREADS
	if (nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) != 0)
		nspares = 0;
	Py_END_ALLOW_THREADS

	if (nspares == 0)
		vdev_list = PyList_New(0);
	else
		vdev_list = vdev_nvlist_array_to_list(pypool,
						      state,
						      spares,
						      nspares,
						      0,
						      request_mask);
	if (vdev_list == NULL)
		return NULL;

	PyObject *out = PyList_AsTuple(vdev_list);
	Py_DECREF(vdev_list);
	return out;
}

static
PyObject *pypool_status_get_storage_vdevs(py_zfs_pool_t *pypool,
					  nvlist_t *nvl,
					  boolean_t get_stats)
{
	uint_t children;
	nvlist_t **child;
	pylibzfs_state_t *state = py_get_module_state(pypool->pylibzfsp);
	uint request_mask = PY_VDEV_CLASS_STORAGE;

	if (get_stats)
		request_mask |= PY_VDEV_DATA_WANT_STATS;

	Py_BEGIN_ALLOW_THREADS
	if (nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;
	Py_END_ALLOW_THREADS

	PYZFS_ASSERT((children != 0), "No vdevs in pool!");

	return vdev_nvlist_array_to_list(pypool,
					 state,
					 child,
					 children,
					 0,
					 request_mask);
}

/*
 * @brief populate the storage_vdevs, support_vdevs, and spares fields of a
 * struct_zpool_status struct sequence.
 *
 * Takes a snapshot of the pool's vdev tree (under lock to guard against
 * concurrent config updates), then builds the storage, support, and spare vdev
 * tuples and inserts them at VDEVS_STORAGE_IDX, VDEVS_SUPPORT_IDX, and
 * VDEVS_SPARES_IDX.
 *
 * @param[in]  pypool        - open pool handle
 * @param[in]  status_struct - partially-populated struct_zpool_status object
 *                             to write vdev fields into
 * @param[in]  get_stats     - include per-vdev I/O and error counters
 *
 * @return  B_TRUE on success, B_FALSE with a Python exception set on failure.
 *
 * @note GIL must be held when calling this function.
 */
static
boolean_t pypool_status_add_vdevs(py_zfs_pool_t *pypool,
				  PyObject *status_struct,
				  boolean_t get_stats)
{
	PyObject *storage_vdevs = NULL;
	PyObject *support_vdevs = NULL;
	PyObject *spare_vdevs = NULL;
	nvlist_t *config, *nvroot;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(pypool->pylibzfsp);
	config = zpool_get_config(pypool->zhp, NULL);
	PYZFS_ASSERT((config != NULL), "Unexpected NULL zpool config");

	// create a copy of the vdev tree. Otherwise there's a risk that
	// another thread doing something like updating the nvlist will change
	// it out or free it from under us.
	nvroot = fnvlist_dup(fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE));

	PY_ZFS_UNLOCK(pypool->pylibzfsp);
	Py_END_ALLOW_THREADS

	storage_vdevs = pypool_status_get_storage_vdevs(pypool, nvroot, get_stats);
	if (storage_vdevs == NULL)
		goto fail;

	PyStructSequence_SetItem(status_struct, VDEVS_STORAGE_IDX, storage_vdevs);

	support_vdevs = pypool_status_get_support_vdevs(pypool, nvroot, get_stats);
	if (support_vdevs == NULL)
		goto fail;

	PyStructSequence_SetItem(status_struct, VDEVS_SUPPORT_IDX, support_vdevs);

	spare_vdevs = pypool_status_get_spare_vdevs(pypool, nvroot, get_stats);
	if (spare_vdevs == NULL)
		goto fail;

	PyStructSequence_SetItem(status_struct, VDEVS_SPARES_IDX, spare_vdevs);

	fnvlist_free(nvroot);
	return B_TRUE;
fail:
	fnvlist_free(nvroot);
	return B_FALSE;
}

// generate tuple of corrupted files on pool
#define E_PATHMAX (MAXPATHLEN * 2)
static
PyObject *pypool_error_log(py_zfs_pool_t *pypool)
{
	PyObject *out = NULL, *tmplist = NULL;
	char *pathbuf = NULL;
	py_zfs_error_t zfs_err;
	nvlist_t *nverrlist = NULL;
	nvpair_t *elem = NULL;
	int err;

	tmplist = PyList_New(0);
	if (tmplist == NULL)
		return NULL;

	pathbuf = PyMem_RawCalloc(1, E_PATHMAX);
	if (pathbuf == NULL) {
		PyErr_NoMemory();
		goto done;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(pypool->pylibzfsp);
	err = zpool_get_errlog(pypool->zhp, &nverrlist);
	if (err) {
		py_get_zfs_error(pypool->pylibzfsp->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(pypool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "Failed to get zpool error log");
		goto done;
	}

	while ((elem = nvlist_next_nvpair(nverrlist, elem)) != NULL) {
		nvlist_t *nv;
		uint64_t dsobj, obj;
		PyObject *errpath;

		verify(nvpair_value_nvlist(elem, &nv) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_DATASET,
		    &dsobj) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_OBJECT,
		    &obj) == 0);

		Py_BEGIN_ALLOW_THREADS
		PY_ZFS_LOCK(pypool->pylibzfsp);
		zpool_obj_to_path(pypool->zhp, dsobj, obj, pathbuf, E_PATHMAX);
		PY_ZFS_UNLOCK(pypool->pylibzfsp);
		Py_END_ALLOW_THREADS

		errpath = PyUnicode_FromString(pathbuf);
		if (errpath == NULL)
			goto done;

		err = PyList_Append(tmplist, errpath);
		Py_XDECREF(errpath);
		if (err)
			goto done;
	}

	// Convert the list to a tuple
	out = PyList_AsTuple(tmplist);

done:
	Py_CLEAR(tmplist);
	PyMem_RawFree(pathbuf);
	fnvlist_free(nverrlist);
	return out;
}

static
PyObject *py_explain_recover(py_zfs_pool_t *pypool,
			     zpool_status_t reason)
{
	char acbuf[2048] = {0};

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(pypool->pylibzfsp);
	zpool_explain_recover(zpool_get_handle(pypool->zhp),
			      zpool_get_name(pypool->zhp),
			      reason,
			      zpool_get_config(pypool->zhp, NULL),
			      acbuf, sizeof(acbuf));
	PY_ZFS_UNLOCK(pypool->pylibzfsp);
	Py_END_ALLOW_THREADS

	return PyUnicode_FromString(acbuf);
}

static
PyObject *py_collect_unsupported_feat(py_zfs_pool_t *pypool,
				      PyObject *reason)
{
	char buf[2048] = {0};
	PyObject *pyfeat;
	nvlist_t *config;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(pypool->pylibzfsp);

	// WARNING: do not free this nvlist
	config = zpool_get_config(pypool->zhp, NULL);
	zpool_collect_unsup_feat(config, buf, sizeof(buf));

	PY_ZFS_UNLOCK(pypool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (reason) {
		PyObject *tmp = PyUnicode_FromFormat(" %s", buf);
		if (tmp == NULL)
			return NULL;

		pyfeat = PyUnicode_Concat(reason, tmp);
		Py_DECREF(tmp);
	} else {
		pyfeat = PyUnicode_FromString(buf);
	}

	return pyfeat;
}

/*
 * @brief build and return a struct_zpool_status struct sequence object
 *
 * Converts the raw output of zpool_get_status() into a Python
 * struct_zpool_status struct sequence.  The reason / errata / msgid
 * arguments are the values returned by zpool_get_status(); get_stats
 * controls whether per-vdev I/O and error counters are included.
 *
 * @param[in]  pypool    - open pool handle
 * @param[in]  reason    - pool status code from zpool_get_status()
 * @param[in]  errata    - errata code (only meaningful when
 *                         reason == ZPOOL_STATUS_ERRATA)
 * @param[in]  msgid     - openzfs.github.io message ID string, or NULL
 * @param[in]  get_stats - include vdev I/O and error counters when B_TRUE
 *
 * @return  new reference to a struct_zpool_status object, or NULL with
 *          a Python exception set on failure.
 *
 * @note GIL must be held when calling this function.
 */
static
PyObject *populate_status_struct(py_zfs_pool_t *pypool,
				 zpool_status_t reason,
				 zpool_errata_t errata,
				 const char *msgid,
				 boolean_t get_stats)
{
	PyObject *out = NULL;
	PyObject *pyreason = NULL;
	PyObject *pyaction = NULL;
	PyObject *pytmp = NULL;
	PyObject *pyenum = NULL;
	PyObject *pymsg = NULL;
	PyObject *pyfiles = NULL;
	pylibzfs_state_t *state = py_get_module_state(pypool->pylibzfsp);

	pyenum = PyObject_CallFunction(state->zpool_status_enum, "i", reason);
	if (pyenum == NULL)
		return NULL;

	if (msgid) {
		pymsg = PyUnicode_FromFormat(
			"https://openzfs.github.io/openzfs-docs/msg/%s",
			msgid);
		if (pymsg == NULL)
			goto fail;
	} else {
		pymsg = Py_NewRef(Py_None);
	}

	out = PyStructSequence_New(state->struct_zpool_status_type);
	if (out == NULL)
		goto fail;

	// This needs to be maintained to match cmd/zpool/zpool_main.c
	switch(reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
		pyreason = PyUnicode_FromString("One or more devices could "
		    "not be opened.  Sufficient replicas exist for the pool "
		    "to continue functioning in a degraded state.");

		pyaction = PyUnicode_FromString("Attach the missing device "
		    "and online it using 'zpool online'");
		break;

	case ZPOOL_STATUS_MISSING_DEV_NR:
		pyreason = PyUnicode_FromString("One or more devices could "
		    "not be opened. There are insufficient replicas for the "
		    "pool to continue functioning.");

		pyaction = PyUnicode_FromString("Attach the missing device "
		    "and online it using 'zpool online'");
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
		pyreason = PyUnicode_FromString("One or more devices could "
		    "not be used because the label is missing or invalid. "
		    "Sufficient replicas exist for the pool to continue "
		    "functioning in a degraded state.");
		pyaction = PyUnicode_FromString("Replace the device using "
		    "'zpool replace'.");
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		pyreason = PyUnicode_FromString("One or more devices could "
		    "not be used because the label is missing or invalid. "
		    "There are insufficient replicas for the pool to "
		    "continue functioning.");

		pyaction = py_explain_recover(pypool, reason);
		break;

	case ZPOOL_STATUS_FAILING_DEV:
		pyreason = PyUnicode_FromString("One or more devices has "
		    "experienced an unrecoverable error.  An attempt was "
		    "made to correct the error.  Applications are "
		    "unaffected.");
		pyaction = PyUnicode_FromString("Determine if the "
		    "device needs to be replaced, and clear the errors using "
		    "'zpool clear' or replace the device with 'zpool "
		    "replace'.");
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		pyreason = PyUnicode_FromString("One or more devices has "
		    "been taken offline by the administrator. Sufficient "
		    "replicas exist for the pool to continue functioning in "
		    "a degraded state.");
		pyaction = PyUnicode_FromString("Online the device "
		    "using 'zpool online' or replace the device with 'zpool "
		    "replace'.");
		break;

	case ZPOOL_STATUS_REMOVED_DEV:
		pyreason = PyUnicode_FromString("One or more devices have "
		    "been removed. Sufficient replicas exist for the pool "
		    "to continue functioning in a degraded state.");
		pyaction = PyUnicode_FromString("Online the device "
		    "using 'zpool online' or replace the device with 'zpool "
		    "replace'.");
		break;

	case ZPOOL_STATUS_RESILVERING:
	case ZPOOL_STATUS_REBUILDING:
		pyreason = PyUnicode_FromString("One or more devices is "
		    "currently being resilvered.  The pool will continue "
		    "to function, possibly in a degraded state.");
		pyaction = PyUnicode_FromString("Wait for the resilver to "
		    "complete.");
		break;

	case ZPOOL_STATUS_REBUILD_SCRUB:
		pyreason = PyUnicode_FromString("One or more devices have "
		    "been sequentially resilvered, scrubbing the pool "
		    "is recommended.");
		pyaction = PyUnicode_FromString("Use 'zpool scrub' to "
		    "verify all data checksums.");
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		pyreason = PyUnicode_FromString("One or more devices has "
		    "experienced an error resulting in data corruption. "
		    "Applications may be affected.");
		pyaction = PyUnicode_FromString("Restore the file in question"
		    " if possible.  Otherwise restore the entire pool from "
		    "backup.");
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		pyreason = PyUnicode_FromString("The pool metadata is "
		    "corrupted and the pool cannot be opened.");
		pyaction = py_explain_recover(pypool, reason);
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		pyreason = PyUnicode_FromString("The pool is formatted using "
		    "a legacy on-disk format.  The pool can still be used, "
		    "but some features are unavailable.");
		pyaction = PyUnicode_FromString("Upgrade the pool using "
		    "'zpool upgrade'.  Once this is done, the pool will no "
		    "longer be accessible on software that does not support "
		    "feature flags.");
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		pyreason = PyUnicode_FromString("The pool has been upgraded "
		    "to a newer, incompatible on-disk version. The pool "
		    "cannot be accessed on this system.");
		pyaction = PyUnicode_FromString("Access the pool from a "
		    "system running more recent software, or restore the "
		    "pool from backup.");
		break;

	case ZPOOL_STATUS_FEAT_DISABLED:
		pyreason = PyUnicode_FromString("Some supported and "
		    "requested features are not enabled on the pool. "
		    "The pool can still be used, but some features are "
		    "unavailable.");
		pyaction = PyUnicode_FromString("Enable all features using "
		    "'zpool upgrade'. Once this is done, the pool may no "
		    "longer be accessible by software that does not support "
		    "the features. See zpool-features(7) for details.");
		break;

	case ZPOOL_STATUS_COMPATIBILITY_ERR:
		pyreason = PyUnicode_FromString("This pool has a "
		    "compatibility list specified, but it could not be "
		    "read/parsed at this time. The pool can still be used, "
		    "but this should be investigated.");
		pyaction = PyUnicode_FromString("Check the value of the "
		    "'compatibility' property against the "
		    "appropriate file in /etc/zfs/compatibility.d or "
		    "/usr/share/zfs/compatibility.d.");
		break;

	case ZPOOL_STATUS_INCOMPATIBLE_FEAT:
		pyreason = PyUnicode_FromString("One or more features "
		    "are enabled on the pool despite not being "
		    "requested by the 'compatibility' property.");
		pyaction = PyUnicode_FromString("Consider setting "
		    "'compatibility' to an appropriate value, or "
		    "adding needed features to the relevant file in "
		    "/etc/zfs/compatibility.d or "
		    "/usr/share/zfs/compatibility.d.");
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		pyreason = PyUnicode_FromString("The pool cannot be accessed "
		    "on this system because it uses the following feature(s)"
		    " not supported on this system:\n");
		pytmp = py_collect_unsupported_feat(pypool, pyreason);
		Py_CLEAR(pyreason);
		pyreason = pytmp;

		pyaction = PyUnicode_FromString("Access the pool from a "
		    "system that supports the required feature(s), or "
		    "restore the pool from backup.");
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		pyreason = PyUnicode_FromString("The pool can only be "
		    "accessed in read-only mode on this system. It cannot be"
		    " accessed in read-write mode because it uses the "
		    "following feature(s) not supported on this system:\n");
		pytmp = py_collect_unsupported_feat(pypool, pyreason);
		Py_CLEAR(pyreason);
		pyreason = pytmp;

		pyaction = PyUnicode_FromString("The pool cannot be accessed "
		    "in read-write mode. Import the pool with "
		    "\"-o readonly=on\", access the pool from a system that "
		    "supports the required feature(s), or restore the "
		    "pool from backup.");
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
		pyreason = PyUnicode_FromString("One or more devices are "
		    "faulted in response to persistent errors. Sufficient "
		    "replicas exist for the pool to continue functioning "
		    "in a degraded state.");
		pyaction = PyUnicode_FromString("Replace the faulted device, "
		    "or use 'zpool clear' to mark the device repaired.");
		break;

	case ZPOOL_STATUS_FAULTED_DEV_NR:
		pyreason = PyUnicode_FromString("One or more devices are "
		    "faulted in response to persistent errors.  There are "
		    "insufficient replicas for the pool to continue "
		    "functioning.");
		pyaction = PyUnicode_FromString("Destroy and re-create the "
		    "pool from a backup source.  Manually marking the device "
		    "repaired using 'zpool clear' may allow some data "
		    "to be recovered.");
		break;

	case ZPOOL_STATUS_IO_FAILURE_MMP:
		pyreason = PyUnicode_FromString("The pool is suspended "
		    "because multihost writes failed or were delayed; "
		    "another system could import the pool undetected.");
		pyaction = PyUnicode_FromString("Make sure the pool's devices"
		    " are connected, then reboot your system and import the "
		    "pool or run 'zpool clear' to resume the pool.");
		break;

	case ZPOOL_STATUS_IO_FAILURE_WAIT:
	case ZPOOL_STATUS_IO_FAILURE_CONTINUE:
		pyreason = PyUnicode_FromString("One or more devices are "
		    "faulted in response to IO failures.");
		pyaction = PyUnicode_FromString("Make sure the affected "
		    "devices are connected, then run 'zpool clear'.");
		break;

	case ZPOOL_STATUS_BAD_LOG:
		pyreason = PyUnicode_FromString("An intent log record "
		    "could not be read. Waiting for administrator intervention "
		    "to fix the faulted pool.");
		pyaction = PyUnicode_FromString("Either restore the affected "
		    "device(s) and run 'zpool online', or ignore the intent log "
		    "records by running 'zpool clear'.");
		break;

	case ZPOOL_STATUS_NON_NATIVE_ASHIFT:
		pyreason = PyUnicode_FromString("One or more devices are "
		    "configured to use a non-native block size. Expect reduced "
		    "performance.");
		pyaction = PyUnicode_FromString("Replace affected devices "
		    "with devices that support the configured block size, "
		    "or migrate data to a properly configured pool.");
		break;

	case ZPOOL_STATUS_HOSTID_ACTIVE:
		pyreason = PyUnicode_FromString("The pool is currently "
		    "imported by another system.");
		pyaction = PyUnicode_FromString("The pool must be exported "
		    "from the other system before it can be safely imported.");
		break;

	case ZPOOL_STATUS_HOSTID_REQUIRED:
		pyreason = PyUnicode_FromString("The pool has the multihost "
		    "property on. It cannot be safely imported when the "
		    "system hostid is not set.");
		pyaction = PyUnicode_FromString("Set a unique system hostid "
		    "with the zgenhostid(8) command.");
		break;

	case ZPOOL_STATUS_HOSTID_MISMATCH:
		pyreason = PyUnicode_FromString("Mismatch between pool hostid"
		    " and system hostid on imported pool. This pool was "
		    "previously imported into a system with a different "
		    "hostid, and then was verbatim imported into this "
		    "system.");
		pyaction = PyUnicode_FromString("Export this pool on all "
		    "systems on which it is imported. Then import it to correct "
		    "the mismatch.");
		break;

	case ZPOOL_STATUS_ERRATA:
		const char *reason_str = NULL;
		pyreason = PyUnicode_FromFormat("Errata #%d detected.", errata);
		switch (errata) {
		case ZPOOL_ERRATA_NONE:
			pyaction = Py_NewRef(Py_None);
			break;

		case ZPOOL_ERRATA_ZOL_2094_SCRUB:
			pyaction = PyUnicode_FromString("To correct the issue"
			    " run 'zpool scrub'.");
			break;

		case ZPOOL_ERRATA_ZOL_6845_ENCRYPTION:
			reason_str = PyUnicode_AsUTF8(pyreason);
			if (reason_str == NULL)
				goto fail;

			pytmp = PyUnicode_FromFormat("%s Existing encrypted "
			    "datasets contain an on-disk incompatibility "
			    "which needs to be corrected.", reason_str);
			if (pytmp == NULL)
				goto fail;

			Py_CLEAR(pyreason);
			pyreason = pytmp;

			pyaction = PyUnicode_FromString("To correct the issue"
			    " backup existing encrypted datasets to new "
			    "encrypted datasets and destroy the old ones. "
			    "'zfs mount -o ro' can be used to temporarily "
			    "mount existing encrypted datasets readonly.");
			break;

		case ZPOOL_ERRATA_ZOL_8308_ENCRYPTION:
			reason_str = PyUnicode_AsUTF8(pyreason);
			if (reason_str == NULL)
				goto fail;

			pytmp = PyUnicode_FromFormat("%s Existing encrypted "
			    "snapshots and bookmarks contain an on-disk "
			    "incompatibility. This may cause on-disk "
			    "corruption if they are used with "
			    "'zfs recv'.", reason_str);
			if (pytmp == NULL)
				goto fail;

			Py_CLEAR(pyreason);
			pyreason = pytmp;

			pyaction = PyUnicode_FromString("To correct the"
			    "issue, enable the bookmark_v2 feature. No "
			    "additional action is needed if there are no "
			    "encrypted snapshots or bookmarks. If preserving"
			    "the encrypted snapshots and bookmarks is required,"
			    " use a non-raw send to backup and restore them."
			    " Alternately, they may be removed to resolve "
			    "the incompatibility.");
			break;

		default:
			/*
			 * All errata which allow the pool to be imported
			 * must contain an action message.
			 *
			 * TODO: enforce this with compile-time assertion.
			 */
			PYZFS_ASSERT(0, "Unhandled zpool errata message.");
		}
		break;

	case ZPOOL_STATUS_CORRUPT_CACHE:
		/*
		 * ZPOOL_STATUS_CORRUPT_CACHE is defined in the enum and has a
		 * msgid (ZFS-8000-14) but zpool_get_status() never actually
		 * returns it -- included here for completeness only.
		 */
	case ZPOOL_STATUS_BAD_GUID_SUM:
		/*
		 * ZPOOL_STATUS_BAD_GUID_SUM is only surfaced during pool
		 * import, not for an active imported pool -- included here
		 * for completeness only.
		 */
	default:
		/*
		 * The remaining errors can't actually be generated, yet.
		 */
		PYZFS_ASSERT((reason == ZPOOL_STATUS_OK), "Unhandle zpool status.");
		// Set the reason and action to None rather than NULL
		pyreason = Py_NewRef(Py_None);
		pyaction = Py_NewRef(Py_None);
	}

	if (!pyreason || !pyaction)
		// most likely malloc failure. let's raise exception and
		// pretend the world isn't on fire
		goto fail;

	pyfiles = pypool_error_log(pypool);
	if (pyfiles == NULL)
		goto fail;

	if (!pypool_status_add_vdevs(pypool, out, get_stats))
		// this needs to occur before setting references
		// in the struct sequence otherwise we risk UAF
		goto fail;

	PyStructSequence_SET_ITEM(out, 0, pyenum);
	PyStructSequence_SET_ITEM(out, 1, pyreason);
	PyStructSequence_SET_ITEM(out, 2, pyaction);
	PyStructSequence_SET_ITEM(out, 3, pymsg);
	PyStructSequence_SET_ITEM(out, 4, pyfiles);

	return out;

fail:
	Py_CLEAR(pyreason);
	Py_CLEAR(pyaction);
	Py_CLEAR(pyenum);
	Py_CLEAR(pymsg);
	Py_CLEAR(pyfiles);
	Py_CLEAR(out);
	return NULL;
}

PyObject *py_get_pool_status(py_zfs_pool_t *pypool, boolean_t get_stats)
{
	zpool_status_t reason;
	zpool_errata_t errata;
	const char *msgid;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(pypool->pylibzfsp);
	reason = zpool_get_status(pypool->zhp, &msgid, &errata);
	PY_ZFS_UNLOCK(pypool->pylibzfsp);
	Py_END_ALLOW_THREADS

	return populate_status_struct(pypool, reason, errata, msgid, get_stats);
}

/* create new dictionary containing references to info from struct sequence */
static
boolean_t py_vdev_add_stats(PyObject *vdev_dict,
			    const char *key,
			    PyObject *pystats)
{
	PyObject *stats_dict = NULL;
	int idx, err;

	stats_dict = PyDict_New();
	if (stats_dict == NULL)
		return B_FALSE;

	for (idx = 0; idx < struct_vdev_stats_desc.n_in_sequence; idx++) {
		const char *name = struct_vdev_stats[idx].name;
		PyObject *val = PyStructSequence_GET_ITEM(pystats, idx);
		PYZFS_ASSERT((val != NULL), "Unexpected NULL");

		err = PyDict_SetItemString(stats_dict, name, val);
		if (err) {
			Py_CLEAR(stats_dict);
			return B_FALSE;
		}
	}

	err = PyDict_SetItemString(vdev_dict, key, stats_dict);
	Py_DECREF(stats_dict);
	return err ? B_FALSE : B_TRUE;
}

static
boolean_t py_vdevs_dict(PyObject *vdevs, const char *key, PyObject *dict_out);

/* convert individual vdev to a dict and return it */
static
PyObject *py_vdev_to_dict(PyObject *vdev)

{
	PyObject *vdev_dict = NULL;
	int idx, err;

	vdev_dict = PyDict_New();
	if (vdev_dict == NULL)
		return NULL;

	for (idx = 0; idx < struct_vdev_status_desc.n_in_sequence;
	     idx++) {
		PyObject *val = PyStructSequence_GET_ITEM(vdev, idx);
		const char *name = struct_vdev_status_prop[idx].name;
		PYZFS_ASSERT((val != NULL), "Unexpected NULL");

		switch(idx) {
		case STATS_IDX:
			if (val == Py_None) {
				err = PyDict_SetItemString(vdev_dict, name, val);
				if (err)
					goto fail;

			} else if (!py_vdev_add_stats(vdev_dict, name, val)) {
				goto fail;
			}
			break;
		case CHILDREN_IDX:
			if (val == Py_None) {
				err = PyDict_SetItemString(vdev_dict, name, val);
				if (err)
					goto fail;

			} else if (!py_vdevs_dict(val, name, vdev_dict)) {
				goto fail;
			}
			break;
		default:
			err = PyDict_SetItemString(vdev_dict, name, val);
			if (err)
				goto fail;
		}
	}

	return vdev_dict;

fail:
	Py_CLEAR(vdev_dict);
	return NULL;
}

/* convert vdevs tuple (of vdev structs) to tuple of dictionaries */
static
boolean_t py_vdevs_dict(PyObject *vdevs, const char *key, PyObject *dict_out)
{
	PyObject *vdev = NULL;
	PyObject *iterator = NULL;
	PyObject *vdev_list = NULL;
	PyObject *vdev_tuple = NULL;
	int err;

	// vdevs should be a tuple
	iterator = PyObject_GetIter(vdevs);
	if (!iterator)
		return B_FALSE;

	vdev_list = PyList_New(0);
	if (vdev_list == NULL)
		goto fail;

	while ((vdev = PyIter_Next(iterator))) {
		PyObject *vdev_dict = NULL;

		vdev_dict = py_vdev_to_dict(vdev);
		Py_CLEAR(vdev);
		if (vdev_dict == NULL)
			goto fail;

		err = PyList_Append(vdev_list, vdev_dict);
		Py_CLEAR(vdev_dict);
		if (err)
			goto fail;
	}

	Py_CLEAR(iterator);
	vdev_tuple = PyList_AsTuple(vdev_list);
	if (vdev_tuple == NULL)
		goto fail;

	Py_CLEAR(vdev_list);
	err = PyDict_SetItemString(dict_out, key, vdev_tuple);
	Py_CLEAR(vdev_tuple);
	if (err)
		goto fail;

	return B_TRUE;
fail:
	Py_CLEAR(vdev);
	Py_CLEAR(iterator);
	Py_CLEAR(vdev_list);
	Py_CLEAR(vdev_tuple);
	return B_FALSE;
}

static
boolean_t py_support_vdevs_dict(PyObject *py_support_vdevs,
				const char *support_vdevs_key,
				PyObject *dict_out)
{
	PyObject *vdevs_dict = NULL;
	int idx, err;

	vdevs_dict = PyDict_New();
	if (vdevs_dict == NULL)
		return B_FALSE;

	for (idx = 0; idx < struct_pool_support_vdev_desc.n_in_sequence; idx++) {
		const char *key = struct_pool_support_vdev[idx].name;
		PyObject *val = PyStructSequence_GET_ITEM(py_support_vdevs, idx);

		if (!py_vdevs_dict(val, key, vdevs_dict))
			goto fail;

	}

	err = PyDict_SetItemString(dict_out, support_vdevs_key, vdevs_dict);
	Py_CLEAR(vdevs_dict);
	return err ? B_FALSE : B_TRUE;

fail:
	Py_CLEAR(vdevs_dict);
	return B_FALSE;
}

PyObject *py_get_pool_status_dict(py_zfs_pool_t *pypool, boolean_t get_stats)
{
	int idx, err;
	PyObject *out = NULL;

	PyObject *status_obj = py_get_pool_status(pypool, get_stats);
	if (status_obj == NULL)
		return NULL;

	out = PyDict_New();
	if (out == NULL)
		goto fail;

	for (idx = 0; idx < struct_pool_status_desc.n_in_sequence; idx++) {
		const char *name = struct_pool_status_prop[idx].name;
		PyObject *val = PyStructSequence_GET_ITEM(status_obj, idx);
		PYZFS_ASSERT((val != NULL), "Unexpected NULL");

		// vdevs are a tuple that may require recursion
		// so this means special handling
		switch(idx) {
		case VDEVS_STORAGE_IDX:
			if (!py_vdevs_dict(val, name, out))
				goto fail;
			break;
		case VDEVS_SUPPORT_IDX:
			if (!py_support_vdevs_dict(val, name, out))
				goto fail;
			break;
		default:
			err = PyDict_SetItemString(out, name, val);
			if (err)
				goto fail;
		}
	}

	Py_DECREF(status_obj);
	return out;

fail:
	Py_CLEAR(out);
	Py_CLEAR(status_obj);
	return NULL;
}

void init_py_pool_status_state(pylibzfs_state_t *state)
{
	PyTypeObject *obj;

	obj = PyStructSequence_NewType(&struct_pool_status_desc);
	PYZFS_ASSERT(obj, "Failed to create zpool status struct type");

	state->struct_zpool_status_type = obj;

	obj = PyStructSequence_NewType(&struct_vdev_status_desc);
	PYZFS_ASSERT(obj, "Failed to create vdev status struct type");

	state->struct_vdev_status_type = obj;

	obj = PyStructSequence_NewType(&struct_vdev_stats_desc);
	PYZFS_ASSERT(obj, "Failed to create vdev stats struct type");

	state->struct_vdev_stats_type = obj;

	obj = PyStructSequence_NewType(&struct_pool_support_vdev_desc);
	PYZFS_ASSERT(obj, "Failed to create support vdev struct type");

	state->struct_support_vdev_type = obj;
}
