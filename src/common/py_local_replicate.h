#ifndef _PY_LOCAL_REPLICATE_H
#define _PY_LOCAL_REPLICATE_H
#include "../truenas_pylibzfs.h"

/*
 * Single entry point for "send a stream to a co-resident receive on
 * the same host through an internal pipe" methods.  Backs both
 * lzc.local_replicate (lzc_send + lzc_receive_with_cmdprops) and the
 * resource-object local_replicate (zfs_send + zfs_receive).  The
 * caller's job is to parse kwargs, run PySys_Audit, populate a
 * `local_replicate_args` struct, and call py_local_replicate.  All
 * of the replication life cycle (cmdprops validation, snapshot
 * pre-flight, pipe setup, send worker thread, progress poller,
 * recv-on-calling-thread, error precedence, history logging, and
 * Python exception raising) lives on the other side.
 *
 * Worker threads each open their own libzfs handles where they need
 * one; no caller's libzfs handle is borrowed across threads.  The
 * pool history line is written through a temporary libzfs handle
 * too (see history_prefix below), so the caller never needs to hand
 * over its py_zfs_t.
 */

enum local_replicate_mode {
	LOCAL_REPLICATE_LZC,
	LOCAL_REPLICATE_ZFS,
};

struct local_replicate_args {
	enum local_replicate_mode mode;

	/*
	 * Source / destination.  Full snapshot names ("dataset@snap")
	 * regardless of mode; the zfs send op splits source on '@'
	 * internally to satisfy zfs_open + zfs_send.  The lzc wrapper
	 * forwards the user-supplied full name; the zfs wrapper
	 * builds it from the resource object's name and tosnap kwarg.
	 */
	const char *source;
	const char *fromsnap;	/* "dataset@base" or NULL */
	const char *dest;

	/*
	 * Bit-set of LZC_SEND_FLAG_LARGE_BLOCK / EMBED_DATA / COMPRESS.
	 * RAW must come via the `raw` field below; SAVED is rejected
	 * by the validator.
	 */
	int send_flags_int;
	boolean_t raw;
	boolean_t force;

	/*
	 * zfs-mode only: when true, sendflags_t.replicate is set
	 * (`zfs send -R`).  Filesystem callers set this so descendants
	 * and clones are included in the stream; volume callers leave
	 * it false because volumes have no non-snapshot descendants.
	 *
	 * Note: source properties are always sent in zfs mode.  -p
	 * (sendflags_t.props) is set unconditionally; it is also
	 * implied by -R, so the kwarg pair we used to expose was
	 * meaningless on the recursive path.  Callers who want the
	 * destination to inherit from its parent should pass
	 * `props={...}` to override specific values rather than try
	 * to suppress the embedding.
	 */
	boolean_t recursive;

	/*
	 * zfs-mode only: when true, recvflags_t.nomount is set so the
	 * destination is not auto-mounted after receive.  The `zfs receive
	 * -u` flag.  Inapplicable to lzc mode - lzc_receive_with_cmdprops
	 * does not auto-mount.
	 */
	boolean_t nomount;

	/*
	 * zfs-mode only: iterable of property names to exclude from being
	 * applied on the destination (the `zfs receive -x prop` flag).
	 * Each name becomes a DATA_TYPE_BOOLEAN nvpair in cmdprops, which
	 * the kernel reads as an exclusion.  Borrowed reference; may be
	 * None.  Inapplicable to lzc mode - lzc_send produces a
	 * data-only stream with no embedded properties, so there is
	 * nothing to suppress.
	 */
	PyObject *py_exclude;

	/*
	 * lzc-mode only: target module object for set_zfscore_exc on
	 * send/recv/preflight failure.  Required in lzc mode; ignored
	 * in zfs mode (which raises ZFSException via set_exc_from_libzfs).
	 */
	PyObject *exc_owner;

	/*
	 * lzc-mode only.  When true, the receive side is run in
	 * resumable mode - a partial transfer leaves a
	 * RECEIVE_RESUME_TOKEN property on the destination so a
	 * subsequent call with resume_token can pick up where this one
	 * stopped.  Resume only addresses one (dataset, snapshot)
	 * pair; the resource-object paths are recursive (datasets) or
	 * have lzc as a single-snapshot alternative (volumes), so
	 * resume is not exposed there.  validate_args rejects
	 * resumable / resume_token in zfs mode.
	 */
	boolean_t resumable;

	/*
	 * lzc-mode only.  When set, the send side resumes a previously
	 * interrupted transfer instead of starting fresh; source /
	 * fromsnap / send_flags must match the snapshot encoded in the
	 * token, the kernel rejects mismatches.
	 */
	const char *resume_token;

	/*
	 * Pool-history controls.  When skip_history is true, no entry
	 * is logged.  history_prefix overrides libzfs's default
	 * ("truenas-pylibzfs: ") - NULL means use the default.  The
	 * history call goes through a fresh temp libzfs handle either
	 * way, so the caller does not need to expose its own handle.
	 *
	 * The zfs-mode wrapper forwards its parent py_zfs_t's history
	 * flag (negated) and history_prefix here; the lzc-mode wrapper
	 * leaves both at zero so history is logged with the default
	 * prefix.
	 */
	boolean_t skip_history;
	const char *history_prefix;

	/*
	 * Borrowed references; must stay valid until py_local_replicate
	 * returns.  py_props may be a dict or None; py_progress_cb
	 * may be a callable or None; py_progress_state is opaque and
	 * defaults to None when NULL.
	 */
	PyObject *py_props;
	PyObject *py_progress_cb;
	PyObject *py_progress_state;
	int progress_interval_seconds;	/* must be > 0 */
};

/*
 * Run a local replication to completion.  Returns a new reference to
 * Py_None on success, or NULL with a Python exception set on any
 * failure: ValueError for argument validation, OSError for transport
 * setup failures (pipe2, pthread_create), TypeError for non-dict
 * props, ZFSException via set_exc_from_libzfs (zfs mode) or the
 * lzc-side exception via set_zfscore_exc (lzc mode) for
 * send/recv/preflight failures, RuntimeError for history-logging
 * failures.
 */
extern PyObject *py_local_replicate(const struct local_replicate_args *args);

/*
 * Open a temporary libzfs handle for a one-shot libzfs operation.
 * Returns NULL with a Python RuntimeError set on failure.  Caller
 * must close the handle with libzfs_fini.  Releases the GIL across
 * the libzfs_init call.  Exported for callers outside this module
 * (e.g. resume-token decoding in py_lzc_send).
 */
extern libzfs_handle_t *py_replicate_open_temp_handle(void);

#endif /* _PY_LOCAL_REPLICATE_H */
