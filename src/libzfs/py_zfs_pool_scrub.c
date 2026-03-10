#include "../truenas_pylibzfs.h"

/*
 * ZFS pool scan/scrub statistics implementation.
 *
 * Provides:
 *   py_get_pool_scrub_info()    — build struct_zpool_scrub from live pool config
 *   init_py_zpool_scrub_state() — create the PyStructSequence type at module init
 *
 * The Python method wrappers (ZFSPool.scrub_info, ZFSPool.scan) live in
 * py_zfs_pool.c and call into this file for the heavy lifting.
 *
 * ---------------------------------------------------------------------------
 * Data source
 * ---------------------------------------------------------------------------
 *
 *   zpool_get_config(zhp)
 *     -> ZPOOL_CONFIG_VDEV_TREE (nvlist)
 *       -> ZPOOL_CONFIG_SCAN_STATS (uint64[] cast to pool_scan_stat_t*)
 *
 * Fields 0–14 are always present once any scan has run.
 * Fields 15–21 (error-scrub) are present only when the kernel nvlist array
 * is large enough to contain pss_error_scrub_func; otherwise set to None.
 * Field 22 is computed (percentage).
 *
 * ---------------------------------------------------------------------------
 * Sample output
 * ---------------------------------------------------------------------------
 *
 * Completed scrub (state=FINISHED):
 *
 *   struct_zpool_scrub(
 *       func=<ScanFunction.SCRUB: 1>,
 *       state=<ScanState.FINISHED: 2>,
 *       start_time=1709386800,
 *       end_time=1709386842,
 *       to_examine=10737418240,
 *       examined=10737418240,
 *       skipped=0,
 *       processed=0,
 *       issued=10737418240,
 *       errors=0,
 *       pass_exam=10737418240,
 *       pass_start=1709386800,
 *       pass_scrub_pause=0,
 *       pass_scrub_spent_paused=0,
 *       pass_issued=10737418240,
 *       error_scrub_func=<ScanFunction.NONE: 0>,
 *       error_scrub_state=<ScanState.NONE: 0>,
 *       error_scrub_start=0,
 *       error_scrub_end=0,
 *       error_scrub_examined=0,
 *       error_scrub_to_be_examined=0,
 *       pass_error_scrub_pause=0,
 *       percentage=None,       # None when not actively scanning
 *   )
 *
 * In-progress scrub (state=SCANNING):
 *
 *   struct_zpool_scrub(
 *       func=<ScanFunction.SCRUB: 1>,
 *       state=<ScanState.SCANNING: 1>,
 *       ...
 *       percentage=47.3,
 *   )
 *
 * Pool never scrubbed: ZFSPool.scrub_info() returns None.
 * Old kernel (no error-scrub support): fields 15–21 are all None.
 */

/* -------------------------------------------------------------------------
 * struct_zpool_scrub — PyStructSequence definition (24 fields)
 * ------------------------------------------------------------------------- */

static PyStructSequence_Field struct_zpool_scrub_fields[] = {
	{"func",     "ScanFunction enum: type of scan in progress (or last run)"},
	{"state",    "ScanState enum: current state of the scan"},
	{"start_time", "Unix timestamp when the scan started (0 if never)"},
	{"end_time",   "Unix timestamp when the scan ended (0 if in progress)"},
	{"to_examine", "Total bytes to examine"},
	{"examined",   "Total bytes examined so far"},
	{"skipped",    "Total bytes skipped"},
	{"processed",  "Total bytes repaired/resilvered"},
	{"issued",     "Total bytes for which I/O has been issued"},
	{"errors",     "Number of scan errors"},
	{"pass_exam",  "Bytes examined during the current pass"},
	{"pass_start", "Unix timestamp of the start of the current pass"},
	{"pass_scrub_pause",
	    "Unix timestamp when the current pass was paused (0 = not paused)"},
	{"pass_scrub_spent_paused",
	    "Cumulative seconds spent paused during the current pass"},
	{"pass_issued", "Bytes issued during the current pass"},
	{"error_scrub_func",
	    "ScanFunction | None: error-scrub function (None if unsupported by kernel)"},
	{"error_scrub_state",
	    "ScanState | None: error-scrub state (None if unsupported by kernel)"},
	{"error_scrub_start",
	    "int | None: error-scrub start timestamp (None if unsupported by kernel)"},
	{"error_scrub_end",
	    "int | None: error-scrub end timestamp (None if unsupported by kernel)"},
	{"error_scrub_examined",
	    "int | None: error blocks examined (None if unsupported by kernel)"},
	{"error_scrub_to_be_examined",
	    "int | None: error blocks to examine (None if unsupported by kernel)"},
	{"pass_error_scrub_pause",
	    "int | None: error-scrub pass pause timestamp (None if unsupported by kernel)"},
	{"percentage",
	    "float | None: scan completion percentage; None if not scanning or "
	    "denominator is zero"},
	{0},
};

static PyStructSequence_Desc struct_zpool_scrub_desc = {
	.name = PYLIBZFS_TYPES_MODULE_NAME ".struct_zpool_scrub",
	.fields = struct_zpool_scrub_fields,
	.doc = "ZFS pool scan/scrub statistics",
	.n_in_sequence = 23,
};

/* Field index constants — must stay in sync with struct_zpool_scrub_fields[] */
#define SCRUB_FUNC_IDX				0
#define SCRUB_STATE_IDX				1
#define SCRUB_START_TIME_IDX			2
#define SCRUB_END_TIME_IDX			3
#define SCRUB_TO_EXAMINE_IDX			4
#define SCRUB_EXAMINED_IDX			5
#define SCRUB_SKIPPED_IDX			6
#define SCRUB_PROCESSED_IDX			7
#define SCRUB_ISSUED_IDX			8
#define SCRUB_ERRORS_IDX			9
#define SCRUB_PASS_EXAM_IDX			10
#define SCRUB_PASS_START_IDX			11
#define SCRUB_PASS_SCRUB_PAUSE_IDX		12
#define SCRUB_PASS_SCRUB_SPENT_PAUSED_IDX	13
#define SCRUB_PASS_ISSUED_IDX			14
#define SCRUB_ERROR_SCRUB_FUNC_IDX		15
#define SCRUB_ERROR_SCRUB_STATE_IDX		16
#define SCRUB_ERROR_SCRUB_START_IDX		17
#define SCRUB_ERROR_SCRUB_END_IDX		18
#define SCRUB_ERROR_SCRUB_EXAMINED_IDX		19
#define SCRUB_ERROR_SCRUB_TO_BE_EXAMINED_IDX	20
#define SCRUB_PASS_ERROR_SCRUB_PAUSE_IDX	21
#define SCRUB_PERCENTAGE_IDX			22

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/*
 * Create a ScanFunction or ScanState enum value from a uint64_t.
 * Returns a new reference, or NULL on error.
 */
static PyObject *
make_scan_enum(PyObject *enum_obj, uint64_t val)
{
	return PyObject_CallFunction(enum_obj, "K", (unsigned long long)val);
}

/*
 * Set a struct sequence item at idx to val (a new reference, stolen on
 * success).  Returns B_TRUE on success, B_FALSE if val is NULL.
 * Lifecycle of `out` is the caller's responsibility.
 */
static boolean_t
scrub_set_item(PyObject *out, int idx, PyObject *val)
{
	if (val == NULL)
		return B_FALSE;
	PyStructSequence_SetItem(out, idx, val);
	return B_TRUE;
}

/* -------------------------------------------------------------------------
 * scrub_fetch_stats — get scan stats from pool config under lock
 * ------------------------------------------------------------------------- */

/*
 * Fetch the pool_scan_stat_t array from the pool config nvlist.
 *
 * The entire function runs with the GIL released (all operations are pure C).
 *
 * Returns B_TRUE on success: *config_out is set to the config copy (caller
 * must free with fnvlist_free), *ps_out points into that copy, *psc_out
 * holds the element count.
 *
 * Returns B_FALSE if no stats are available (pool never scanned or config
 * unavailable). *config_out is NULL in that case.
 */
static boolean_t
scrub_fetch_stats(py_zfs_pool_t *p, nvlist_t **config_out,
    pool_scan_stat_t **ps_out, uint_t *psc_out)
{
	nvlist_t *config_copy = NULL;
	nvlist_t *config = NULL;
	nvlist_t *vdev_tree = NULL;
	boolean_t missing = B_FALSE;
	uint64_t *stats_arr = NULL;
	uint_t psc = 0;
	boolean_t ok = B_FALSE;

	/*
	 * Refresh to get current kernel data.  Ignore the return value —
	 * a missing pool just means stats won't be current, which is fine.
	 */
	(void)zpool_refresh_stats(p->zhp, &missing);
	config = zpool_get_config(p->zhp, NULL);
	if (config != NULL)
		config_copy = fnvlist_dup(config);

	ok = (config_copy != NULL &&
	    nvlist_lookup_nvlist(config_copy, ZPOOL_CONFIG_VDEV_TREE,
	    &vdev_tree) == 0 &&
	    nvlist_lookup_uint64_array(vdev_tree, ZPOOL_CONFIG_SCAN_STATS,
	    &stats_arr, &psc) == 0);

	if (!ok) {
		fnvlist_free(config_copy);
		*config_out = NULL;
		return B_FALSE;
	}

	*config_out = config_copy;
	*ps_out = (pool_scan_stat_t *)stats_arr;
	*psc_out = psc;
	return B_TRUE;
}

/* -------------------------------------------------------------------------
 * scrub_fill_base_fields — populate fields 0–14
 * ------------------------------------------------------------------------- */

/*
 * Populate fields 0–14 of the struct_zpool_scrub sequence:
 *   0–1:  func and state as ScanFunction/ScanState enum values
 *   2–14: plain uint64 statistics always present after any scan
 * Returns B_TRUE on success, B_FALSE on any allocation failure.
 * Lifecycle of `out` is the caller's responsibility.
 */
static boolean_t
scrub_fill_base_fields(PyObject *out, const pool_scan_stat_t *ps,
    pylibzfs_state_t *state)
{
	if (!scrub_set_item(out, SCRUB_FUNC_IDX,
	    make_scan_enum(state->scan_function_enum, ps->pss_func)))
		return B_FALSE;

	if (!scrub_set_item(out, SCRUB_STATE_IDX,
	    make_scan_enum(state->scan_state_enum, ps->pss_state)))
		return B_FALSE;

#define SET_U64(idx, field) \
	if (!scrub_set_item(out, idx, \
	    PyLong_FromUnsignedLongLong((unsigned long long)(field)))) \
		return B_FALSE;

	SET_U64(SCRUB_START_TIME_IDX,              ps->pss_start_time)
	SET_U64(SCRUB_END_TIME_IDX,                ps->pss_end_time)
	SET_U64(SCRUB_TO_EXAMINE_IDX,              ps->pss_to_examine)
	SET_U64(SCRUB_EXAMINED_IDX,                ps->pss_examined)
	SET_U64(SCRUB_SKIPPED_IDX,                 ps->pss_skipped)
	SET_U64(SCRUB_PROCESSED_IDX,               ps->pss_processed)
	SET_U64(SCRUB_ISSUED_IDX,                  ps->pss_issued)
	SET_U64(SCRUB_ERRORS_IDX,                  ps->pss_errors)
	SET_U64(SCRUB_PASS_EXAM_IDX,               ps->pss_pass_exam)
	SET_U64(SCRUB_PASS_START_IDX,              ps->pss_pass_start)
	SET_U64(SCRUB_PASS_SCRUB_PAUSE_IDX,        ps->pss_pass_scrub_pause)
	SET_U64(SCRUB_PASS_SCRUB_SPENT_PAUSED_IDX, ps->pss_pass_scrub_spent_paused)
	SET_U64(SCRUB_PASS_ISSUED_IDX,             ps->pss_pass_issued)

#undef SET_U64

	return B_TRUE;
}

/* -------------------------------------------------------------------------
 * scrub_fill_error_scrub_fields — populate fields 15–21
 * ------------------------------------------------------------------------- */

/*
 * Populate fields 15–21 of the struct_zpool_scrub sequence.
 *
 * These error-scrub fields are only present on kernels new enough to include
 * pss_error_scrub_func in the SCAN_STATS array.  When the array is too short,
 * all seven fields are set to None.
 *
 * Returns B_TRUE on success, B_FALSE on any allocation failure.
 * Lifecycle of `out` is the caller's responsibility.
 */
static boolean_t
scrub_fill_error_scrub_fields(PyObject *out, const pool_scan_stat_t *ps,
    uint_t psc, pylibzfs_state_t *state)
{
	boolean_t has_error_scrub =
	    (psc * sizeof (uint64_t) >=
	    offsetof(pool_scan_stat_t, pss_error_scrub_func) + sizeof (uint64_t));

	if (!has_error_scrub) {
		for (int i = SCRUB_ERROR_SCRUB_FUNC_IDX;
		    i <= SCRUB_PASS_ERROR_SCRUB_PAUSE_IDX; i++) {
			PyStructSequence_SetItem(out, i, Py_NewRef(Py_None));
		}
		return B_TRUE;
	}

	if (!scrub_set_item(out, SCRUB_ERROR_SCRUB_FUNC_IDX,
	    make_scan_enum(state->scan_function_enum, ps->pss_error_scrub_func)))
		return B_FALSE;

	if (!scrub_set_item(out, SCRUB_ERROR_SCRUB_STATE_IDX,
	    make_scan_enum(state->scan_state_enum, ps->pss_error_scrub_state)))
		return B_FALSE;

#define SET_ERR_U64(idx, field) \
	if (!scrub_set_item(out, idx, \
	    PyLong_FromUnsignedLongLong((unsigned long long)(field)))) \
		return B_FALSE;

	SET_ERR_U64(SCRUB_ERROR_SCRUB_START_IDX,          ps->pss_error_scrub_start)
	SET_ERR_U64(SCRUB_ERROR_SCRUB_END_IDX,            ps->pss_error_scrub_end)
	SET_ERR_U64(SCRUB_ERROR_SCRUB_EXAMINED_IDX,       ps->pss_error_scrub_examined)
	SET_ERR_U64(SCRUB_ERROR_SCRUB_TO_BE_EXAMINED_IDX, ps->pss_error_scrub_to_be_examined)
	SET_ERR_U64(SCRUB_PASS_ERROR_SCRUB_PAUSE_IDX,     ps->pss_pass_error_scrub_pause)

#undef SET_ERR_U64

	return B_TRUE;
}

/* -------------------------------------------------------------------------
 * scrub_fill_computed_fields — populate field 22
 * ------------------------------------------------------------------------- */

/*
 * Populate field 22 of the struct_zpool_scrub sequence:
 *   22: percentage — (issued / (to_examine - skipped)) * 100, float | None
 *
 * None when the pool is not actively scanning or the denominator is zero.
 *
 * Returns B_TRUE on success, B_FALSE on any allocation failure.
 * Lifecycle of `out` is the caller's responsibility.
 */
static boolean_t
scrub_fill_computed_fields(PyObject *out, const pool_scan_stat_t *ps)
{
	PyObject *pct;
	uint64_t denom = ps->pss_to_examine - ps->pss_skipped;

	if (ps->pss_state != DSS_SCANNING || denom == 0) {
		pct = Py_NewRef(Py_None);
	} else {
		pct = PyFloat_FromDouble(
		    100.0 * (double)ps->pss_issued / (double)denom);
		if (pct == NULL)
			return B_FALSE;
	}
	PyStructSequence_SetItem(out, SCRUB_PERCENTAGE_IDX, pct);
	return B_TRUE;
}

/* -------------------------------------------------------------------------
 * py_get_pool_scrub_info
 * ------------------------------------------------------------------------- */

/*
 * Build and return a struct_zpool_scrub for `p`, or Py_None if no scan stats
 * are present in the pool config (pool has never been scanned).
 *
 * Caller must hold the GIL; this function drops and re-acquires it
 * internally around the lock + ioctl section.
 */
PyObject *
py_get_pool_scrub_info(py_zfs_pool_t *p)
{
	pylibzfs_state_t *state = py_get_module_state(p->pylibzfsp);
	nvlist_t *config_copy = NULL;
	pool_scan_stat_t *ps = NULL;
	uint_t psc = 0;
	PyObject *out = NULL;
	boolean_t ok;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(p->pylibzfsp);
	ok = scrub_fetch_stats(p, &config_copy, &ps, &psc);
	PY_ZFS_UNLOCK(p->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (!ok) {
		/* this will happen if pool has never benen scrubbed */
		Py_RETURN_NONE;
	}

	out = PyStructSequence_New(state->struct_zpool_scrub_type);
	if (out == NULL)
		goto cleanup;

	if (!scrub_fill_base_fields(out, ps, state) ||
	    !scrub_fill_error_scrub_fields(out, ps, psc, state) ||
	    !scrub_fill_computed_fields(out, ps))
		Py_CLEAR(out);

cleanup:
	fnvlist_free(config_copy);
	return out;
}

/* -------------------------------------------------------------------------
 * Module-state initialisation
 * ------------------------------------------------------------------------- */

void
init_py_zpool_scrub_state(pylibzfs_state_t *state)
{
	PyTypeObject *obj;

	obj = PyStructSequence_NewType(&struct_zpool_scrub_desc);
	PYZFS_ASSERT(obj, "Failed to create struct_zpool_scrub type");

	state->struct_zpool_scrub_type = obj;
}
