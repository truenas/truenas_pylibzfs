#include "../truenas_pylibzfs.h"

/*
 * ZFS pool RAIDZ expansion statistics implementation.
 *
 * Provides:
 *   py_get_pool_expand_info()    — build struct_zpool_expand from live pool config
 *   init_py_zpool_expand_state() — create the PyStructSequence type at module init
 *
 * The Python method wrapper (ZFSPool.expand_info) lives in py_zfs_pool.c
 * and calls into this file for the heavy lifting.
 *
 * ---------------------------------------------------------------------------
 * Data source
 * ---------------------------------------------------------------------------
 *
 *   zpool_get_config(zhp)
 *     -> ZPOOL_CONFIG_VDEV_TREE (nvlist)
 *       -> ZPOOL_CONFIG_RAIDZ_EXPAND_STATS (uint64[] cast to
 *          pool_raidz_expand_stat_t*)
 *
 * ---------------------------------------------------------------------------
 * Sample output
 * ---------------------------------------------------------------------------
 *
 * Expansion in progress (state=SCANNING):
 *
 *   struct_zpool_expand(
 *       state=<ScanState.SCANNING: 1>,
 *       expanding_vdev=3,
 *       start_time=1709386800,
 *       end_time=0,
 *       to_reflow=10737418240,
 *       reflowed=5368709120,
 *       waiting_for_resilver=0,
 *   )
 *
 * Pool never expanded: ZFSPool.expand_info() returns None.
 */

/* -------------------------------------------------------------------------
 * struct_zpool_expand — PyStructSequence definition (7 fields)
 * ------------------------------------------------------------------------- */

static PyStructSequence_Field struct_zpool_expand_fields[] = {
	{"state",    "ScanState enum: current state of the expansion"},
	{"expanding_vdev", "Index of the vdev being expanded"},
	{"start_time", "Unix timestamp when the expansion started (0 if never)"},
	{"end_time",   "Unix timestamp when the expansion ended (0 if in progress)"},
	{"to_reflow",  "Total bytes that need to be reflowed"},
	{"reflowed",   "Total bytes reflowed so far"},
	{"waiting_for_resilver",
	    "Non-zero if the expansion is waiting for a resilver to complete"},
	{0},
};

static PyStructSequence_Desc struct_zpool_expand_desc = {
	.name = PYLIBZFS_TYPES_MODULE_NAME ".struct_zpool_expand",
	.fields = struct_zpool_expand_fields,
	.doc = "ZFS pool RAIDZ expansion statistics",
	.n_in_sequence = 7,
};

/* Field index constants — must stay in sync with struct_zpool_expand_fields[] */
#define EXPAND_STATE_IDX			0
#define EXPAND_EXPANDING_VDEV_IDX		1
#define EXPAND_START_TIME_IDX			2
#define EXPAND_END_TIME_IDX			3
#define EXPAND_TO_REFLOW_IDX			4
#define EXPAND_REFLOWED_IDX			5
#define EXPAND_WAITING_RESILVER_IDX		6

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static boolean_t
expand_set_item(PyObject *out, int idx, PyObject *val)
{
	if (val == NULL)
		return B_FALSE;
	PyStructSequence_SetItem(out, idx, val);
	return B_TRUE;
}

/* -------------------------------------------------------------------------
 * expand_fetch_stats — get raidz expand stats from pool config under lock
 * ------------------------------------------------------------------------- */

/*
 * Fetch the pool_raidz_expand_stat_t array from the pool config nvlist.
 *
 * The entire function runs with the GIL released (all operations are pure C).
 *
 * Returns B_TRUE on success: *config_out is set to the config copy (caller
 * must free with fnvlist_free), *pres_out points into that copy.
 *
 * Returns B_FALSE if no stats are available (pool has never had an expansion
 * or config unavailable). *config_out is NULL in that case.
 */
static boolean_t
expand_fetch_stats(py_zfs_pool_t *p, nvlist_t **config_out,
    pool_raidz_expand_stat_t **pres_out)
{
	nvlist_t *config_copy = NULL;
	nvlist_t *config = NULL;
	nvlist_t *vdev_tree = NULL;
	boolean_t missing = B_FALSE;
	uint64_t *stats_arr = NULL;
	uint_t cnt = 0;
	boolean_t ok = B_FALSE;

	(void)zpool_refresh_stats(p->zhp, &missing);
	config = zpool_get_config(p->zhp, NULL);
	if (config != NULL)
		config_copy = fnvlist_dup(config);

	ok = (config_copy != NULL &&
	    nvlist_lookup_nvlist(config_copy, ZPOOL_CONFIG_VDEV_TREE,
	    &vdev_tree) == 0 &&
	    nvlist_lookup_uint64_array(vdev_tree,
	    ZPOOL_CONFIG_RAIDZ_EXPAND_STATS, &stats_arr, &cnt) == 0);

	if (!ok) {
		fnvlist_free(config_copy);
		*config_out = NULL;
		return B_FALSE;
	}

	*config_out = config_copy;
	*pres_out = (pool_raidz_expand_stat_t *)stats_arr;
	return B_TRUE;
}

/* -------------------------------------------------------------------------
 * expand_fill_fields — populate all 7 fields
 * ------------------------------------------------------------------------- */

static boolean_t
expand_fill_fields(PyObject *out, const pool_raidz_expand_stat_t *pres,
    pylibzfs_state_t *state)
{
	if (!expand_set_item(out, EXPAND_STATE_IDX,
	    PyObject_CallFunction(state->scan_state_enum, "K",
	    (unsigned long long)pres->pres_state)))
		return B_FALSE;

#define SET_U64(idx, field) \
	if (!expand_set_item(out, idx, \
	    PyLong_FromUnsignedLongLong((unsigned long long)(field)))) \
		return B_FALSE;

	SET_U64(EXPAND_EXPANDING_VDEV_IDX, pres->pres_expanding_vdev)
	SET_U64(EXPAND_START_TIME_IDX,     pres->pres_start_time)
	SET_U64(EXPAND_END_TIME_IDX,       pres->pres_end_time)
	SET_U64(EXPAND_TO_REFLOW_IDX,      pres->pres_to_reflow)
	SET_U64(EXPAND_REFLOWED_IDX,       pres->pres_reflowed)
	SET_U64(EXPAND_WAITING_RESILVER_IDX, pres->pres_waiting_for_resilver)

#undef SET_U64

	return B_TRUE;
}

/* -------------------------------------------------------------------------
 * py_get_pool_expand_info
 * ------------------------------------------------------------------------- */

/*
 * Build and return a struct_zpool_expand for `p`, or Py_None if no expansion
 * stats are present in the pool config (pool has never had a RAIDZ expansion).
 *
 * Caller must hold the GIL; this function drops and re-acquires it
 * internally around the lock + ioctl section.
 */
PyObject *
py_get_pool_expand_info(py_zfs_pool_t *p)
{
	pylibzfs_state_t *state = py_get_module_state(p->pylibzfsp);
	nvlist_t *config_copy = NULL;
	pool_raidz_expand_stat_t *pres = NULL;
	PyObject *out = NULL;
	boolean_t ok;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ok = expand_fetch_stats(p, &config_copy, &pres);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (!ok) {
		Py_RETURN_NONE;
	}

	out = PyStructSequence_New(state->struct_zpool_expand_type);
	if (out == NULL)
		goto cleanup;

	if (!expand_fill_fields(out, pres, state))
		Py_CLEAR(out);

cleanup:
	fnvlist_free(config_copy);
	return out;
}

/* -------------------------------------------------------------------------
 * Module-state initialisation
 * ------------------------------------------------------------------------- */

void
init_py_zpool_expand_state(pylibzfs_state_t *state)
{
	PyTypeObject *obj;

	obj = PyStructSequence_NewType(&struct_zpool_expand_desc);
	PYZFS_ASSERT(obj, "Failed to create struct_zpool_expand type");

	state->struct_zpool_expand_type = obj;
}
