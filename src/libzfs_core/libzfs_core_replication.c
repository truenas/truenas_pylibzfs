#include "libzfs_core_replication.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

/*
 * Open a temporary libzfs handle for a one-shot libzfs operation that
 * requires one (resume-token decoding, prop validation, etc.).  Returns
 * NULL with a Python RuntimeError set on failure.  Caller must close
 * the handle with libzfs_fini.
 */
static libzfs_handle_t *
lzc_repl_open_temp_handle(void)
{
	libzfs_handle_t *hdl;

	Py_BEGIN_ALLOW_THREADS
	hdl = libzfs_init();
	Py_END_ALLOW_THREADS
	if (hdl == NULL)
		PyErr_SetString(PyExc_RuntimeError,
				"Failed to create temporary libzfs handle.");
	return hdl;
}

/*
 * Convert a Python dict of dataset property overrides into the native-
 * typed nvlist that lzc_receive_with_cmdprops expects.  py_dict_to_nvlist
 * produces a string-valued nvlist; the kernel rejects DATA_TYPE_STRING
 * for non-string properties (compression, readonly, etc.), so we run
 * zfs_valid_proplist against a temporary libzfs handle to do the
 * string -> native conversion that libzfs_sendrecv.c does for
 * `zfs receive -o`.  Returns the converted nvlist (caller frees with
 * fnvlist_free) or NULL with a Python exception set.
 */
static nvlist_t *
local_replicate_build_cmdprops(PyObject *py_props, const char *dest)
{
	nvlist_t *raw_props;
	libzfs_handle_t *vp_hdl;
	nvlist_t *valid_props;
	char vp_errbuf[1024];
	py_zfs_error_t zfs_err;

	raw_props = py_dict_to_nvlist(py_props);
	if (raw_props == NULL)
		return NULL;

	vp_hdl = lzc_repl_open_temp_handle();
	if (vp_hdl == NULL) {
		fnvlist_free(raw_props);
		return NULL;
	}

	snprintf(vp_errbuf, sizeof(vp_errbuf),
		 "cannot validate properties for receive into '%s'", dest);
	valid_props = zfs_valid_proplist(vp_hdl, ZFS_TYPE_DATASET,
					 raw_props, 0, NULL, NULL,
					 B_FALSE, vp_errbuf);
	fnvlist_free(raw_props);
	if (valid_props == NULL) {
		py_get_zfs_error(vp_hdl, &zfs_err);
		libzfs_fini(vp_hdl);
		set_exc_from_libzfs(&zfs_err,
				    "zfs_valid_proplist() failed");
		return NULL;
	}
	libzfs_fini(vp_hdl);
	return valid_props;
}

PyObject *
py_lzc_send_progress(PyObject *self, PyObject *args_unused, PyObject *kwargs)
{
	char *kwnames[] = { "snapshot_name", "fd", NULL };
	const char *snapname = NULL;
	int fd = -1;
	uint64_t written = 0, blocks = 0;
	int err;

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs, "|$zi",
					 kwnames, &snapname, &fd))
		return NULL;

	if (snapname == NULL) {
		PyErr_SetString(PyExc_ValueError, "snapshot_name is required");
		return NULL;
	}

	if (fd == -1) {
		PyErr_SetString(PyExc_ValueError, "fd is required");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.send_progress", "si",
			snapname, fd) < 0)
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	err = lzc_send_progress(snapname, fd, &written, &blocks);
	Py_END_ALLOW_THREADS

	/*
	 * The kernel returns ENOENT for two distinct conditions:
	 *   1. The snapshot does not exist.
	 *   2. No active send stream for this fd exists (the normal case
	 *      when polling and the send has not started yet or just finished).
	 * Because these are indistinguishable by error code, all errors are
	 * treated as (0, 0) — this is a polling function and callers expect
	 * zero progress rather than an exception when no send is in flight.
	 */
	if (err)
		return Py_BuildValue("(KK)", 0ULL, 0ULL);

	return Py_BuildValue("(KK)", (unsigned long long)written,
			     (unsigned long long)blocks);
}

PyObject *
py_lzc_send_space(PyObject *self, PyObject *args_unused, PyObject *kwargs)
{
	const char *snapname = NULL;
	const char *fromsnap = NULL;
	int flags = 0;
	uint64_t size = 0;
	int err;
	char ctx_msg[ZFS_MAX_DATASET_NAME_LEN + 64];

	char *kwnames[] = { "snapname", "fromsnap", "flags", NULL };

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs, "|$zzi",
					 kwnames,
					 &snapname, &fromsnap, &flags))
		return NULL;

	if (snapname == NULL) {
		PyErr_SetString(PyExc_ValueError, "snapname is required");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.send_space", "ssi",
			snapname,
			fromsnap ? fromsnap : "",
			flags) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	err = lzc_send_space(snapname, fromsnap,
			     (enum lzc_send_flags)flags, &size);
	Py_END_ALLOW_THREADS

	if (err) {
		snprintf(ctx_msg, sizeof(ctx_msg),
			 "lzc_send_space() failed for snapname='%s'",
			 snapname);
		set_zfscore_exc(self, ctx_msg, err, Py_None);
		return NULL;
	}

	return PyLong_FromUnsignedLongLong(size);
}

PyObject *
py_lzc_send(PyObject *self, PyObject *args_unused, PyObject *kwargs)
{
	const char *snapname = NULL;
	const char *fromsnap = NULL;
	const char *resume_token = NULL;
	int fd = -1;
	int flags = 0;
	int err;
	char ctx_msg[ZFS_MAX_DATASET_NAME_LEN + 64];

	char *kwnames[] = {
		"snapname", "fd", "fromsnap", "flags", "resume_token", NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs, "|$ziziz",
					 kwnames,
					 &snapname, &fd, &fromsnap, &flags,
					 &resume_token))
		return NULL;

	if (snapname == NULL) {
		PyErr_SetString(PyExc_ValueError, "snapname is required");
		return NULL;
	}

	if (fd == -1) {
		PyErr_SetString(PyExc_ValueError, "fd is required");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.send", "sisi",
			snapname, fd,
			fromsnap ? fromsnap : "",
			flags) < 0) {
		return NULL;
	}

	if (resume_token != NULL) {
		uint64_t resumeobj, resumeoff;
		libzfs_handle_t *lz;
		nvlist_t *nvl;

		lz = lzc_repl_open_temp_handle();
		if (lz == NULL)
			return NULL;

		nvl = zfs_send_resume_token_to_nvlist(lz, resume_token);
		libzfs_fini(lz);

		if (nvl == NULL) {
			PyErr_SetString(PyExc_ValueError,
					"failed to decode resume_token");
			return NULL;
		}

		if (nvlist_lookup_uint64(nvl, "object", &resumeobj) != 0 ||
		    nvlist_lookup_uint64(nvl, "offset", &resumeoff) != 0) {
			fnvlist_free(nvl);
			PyErr_SetString(PyExc_ValueError,
					"resume_token missing object/offset fields");
			return NULL;
		}

		fnvlist_free(nvl);

		Py_BEGIN_ALLOW_THREADS
		err = lzc_send_resume(snapname, fromsnap, fd,
				      (enum lzc_send_flags)flags,
				      resumeobj, resumeoff);
		Py_END_ALLOW_THREADS

		if (err) {
			snprintf(ctx_msg, sizeof(ctx_msg),
				 "lzc_send_resume() failed for snapname='%s'",
				 snapname);
			set_zfscore_exc(self, ctx_msg, err, Py_None);
			return NULL;
		}
	} else {
		Py_BEGIN_ALLOW_THREADS
		err = lzc_send(snapname, fromsnap, fd,
			       (enum lzc_send_flags)flags);
		Py_END_ALLOW_THREADS

		if (err) {
			snprintf(ctx_msg, sizeof(ctx_msg),
				 "lzc_send() failed for snapname='%s'",
				 snapname);
			set_zfscore_exc(self, ctx_msg, err, Py_None);
			return NULL;
		}
	}

	/*
	 * ZFS_IOC_SEND and ZFS_IOC_SEND_NEW are both registered with
	 * allow_log = B_FALSE, so the zfs_allow_log_key TSD (Thread-Specific
	 * Data) is never set after a send ioctl.  ZFS_IOC_LOG_HISTORY
	 * requires this TSD and returns EINVAL if it is absent.  History
	 * logging for lzc send is therefore not possible.
	 */

	Py_RETURN_NONE;
}

PyObject *
py_lzc_receive(PyObject *self, PyObject *args_unused, PyObject *kwargs)
{
	const char *snapname = NULL;
	int fd = -1;
	const char *origin = NULL;
	PyObject *py_props = NULL;
	boolean_t force = B_FALSE;
	boolean_t resumable = B_FALSE;
	boolean_t raw = B_FALSE;
	nvlist_t *props = NULL;
	int err;
	char ctx_msg[ZFS_MAX_DATASET_NAME_LEN + 64];

	char *kwnames[] = {
		"snapname", "fd", "origin", "props",
		"force", "resumable", "raw",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs, "|$zizOppp",
					 kwnames,
					 &snapname, &fd, &origin, &py_props,
					 &force, &resumable, &raw))
		return NULL;

	if (snapname == NULL) {
		PyErr_SetString(PyExc_ValueError, "snapname is required");
		return NULL;
	}

	if (fd == -1) {
		PyErr_SetString(PyExc_ValueError, "fd is required");
		return NULL;
	}

	if (!NULL_OR_NONE(py_props)) {
		props = py_dict_to_nvlist(py_props);
		if (props == NULL)
			return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.receive", "siss",
			snapname, fd,
			origin ? origin : "",
			resumable ? "resumable" : "") < 0) {
		fnvlist_free(props);
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	if (resumable) {
		err = lzc_receive_resumable(snapname, props, origin,
					    force, raw, fd);
	} else {
		err = lzc_receive(snapname, props, origin, force, raw, fd);
	}
	fnvlist_free(props);
	Py_END_ALLOW_THREADS

	if (err) {
		snprintf(ctx_msg, sizeof(ctx_msg),
			 "lzc_receive() failed for snapname='%s'",
			 snapname);
		set_zfscore_exc(self, ctx_msg, err, Py_None);
		return NULL;
	}

	/*
	 * Both ZFS_IOC_RECV and ZFS_IOC_RECV_NEW are registered with
	 * allow_log = B_TRUE, so the zfs_allow_log_key TSD (Thread-Specific
	 * Data) is set after a successful receive and ZFS_IOC_LOG_HISTORY
	 * can be called.
	 */
	err = py_log_history_impl(NULL, NULL,
				  "zfs receive%s%s %s",
				  force ? " -F" : "",
				  resumable ? " -s" : "",
				  snapname);
	if (err)
		return NULL;

	Py_RETURN_NONE;
}

/*
 * Argument struct for the send-side pthread used by py_lzc_local_replicate.
 * Lives on the stack of the main thread for the duration of the transfer.
 */
struct lzc_replicate_send_args {
	const char *snapname;
	const char *fromsnap;
	int fd;
	enum lzc_send_flags flags;
	int err;
};

static void *
lzc_local_replicate_send_thread(void *arg)
{
	struct lzc_replicate_send_args *a = arg;
	sigset_t mask;

	/*
	 * Block SIGPIPE on this thread.  When the receiver closes the read
	 * end early (after a recv error), the kernel write inside lzc_send
	 * would otherwise raise SIGPIPE and terminate the process.  With
	 * SIGPIPE blocked the write fails with EPIPE and lzc_send returns
	 * the errno so we can surface it to the caller.
	 */
	sigemptyset(&mask);
	sigaddset(&mask, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	a->err = lzc_send(a->snapname, a->fromsnap, a->fd, a->flags);

	/*
	 * Always close the write end so the receiver wakes up: EOF on the
	 * happy path, or a subsequent EPIPE on the error path.
	 */
	close(a->fd);
	return NULL;
}

/*
 * Internal pipe buffer size in bytes.  Applied best-effort with
 * F_SETPIPE_SZ; failures (EPERM over /proc/sys/fs/pipe-max-size for
 * non-root, EINVAL on rejected sizes) are tolerated and the kernel
 * default takes over.  1 MiB matches the unprivileged default of
 * /proc/sys/fs/pipe-max-size, so the resize typically succeeds without
 * CAP_SYS_RESOURCE.
 */
#define LZC_LOCAL_REPLICATE_PIPE_SIZE	(1 << 20)

/*
 * Emit the synthesized "zfs send | zfs receive" history entry that
 * makes a successful local_replicate grep-able in pool history.  Send
 * and recv flags are rendered as their CLI mnemonics; props are not
 * rendered inline (an arbitrary nested dict does not fit a single log
 * line), only flagged with a fixed marker so a reader knows to inspect
 * the audit hook for the values.  Returns 0 on success, non-zero from
 * py_log_history_impl on failure.
 */
static int
local_replicate_log_history(const char *source, const char *dest,
			    const char *fromsnap,
			    enum lzc_send_flags flags,
			    bool force, bool has_props)
{
	char send_opts[64] = {0};
	char recv_opts[8] = {0};
	const char *props_marker = "";
	int off = 0;

	if (flags & LZC_SEND_FLAG_LARGE_BLOCK)
		off += snprintf(send_opts + off,
				sizeof(send_opts) - off, " -L");
	if (flags & LZC_SEND_FLAG_EMBED_DATA)
		off += snprintf(send_opts + off,
				sizeof(send_opts) - off, " -e");
	if (flags & LZC_SEND_FLAG_COMPRESS)
		off += snprintf(send_opts + off,
				sizeof(send_opts) - off, " -c");
	if (flags & LZC_SEND_FLAG_RAW)
		off += snprintf(send_opts + off,
				sizeof(send_opts) - off, " -w");

	if (force)
		snprintf(recv_opts, sizeof(recv_opts), " -F");

	if (has_props)
		props_marker = " (with -o property overrides)";

	if (fromsnap) {
		return py_log_history_impl(NULL, NULL,
					   "zfs send%s -i %s %s | "
					   "zfs receive%s %s%s",
					   send_opts, fromsnap, source,
					   recv_opts, dest, props_marker);
	}
	return py_log_history_impl(NULL, NULL,
				   "zfs send%s %s | zfs receive%s %s%s",
				   send_opts, source, recv_opts, dest,
				   props_marker);
}

/*
 * Argument struct for the progress-poller pthread used by
 * py_lzc_local_replicate when the caller supplies a progress_callback.
 * Lives on the stack of the main thread for the duration of the
 * transfer.  The callback / state pointers are borrowed from the
 * function's kwargs and remain valid until the call returns.
 */
struct lzc_replicate_progress_args {
	const char *snapname;
	int fd;
	uint64_t total;
	PyObject *callback;
	PyObject *state;
	int interval_seconds;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool stop;
};

/*
 * Poller thread.  Sleeps interval_seconds between polls, queries the
 * kernel for bytes-sent against the internal send fd, reacquires the
 * GIL, and invokes the user callback with (written, total, state).
 *
 * Callback exceptions are routed through sys.unraisablehook (default:
 * traceback to stderr) and the poller exits without firing the
 * callback again.  The transfer itself is unaffected; progress is
 * advisory and a buggy hook should not turn a successful replication
 * into a failure.
 *
 * Coordination with the main thread is via lock + cond + stop flag:
 * the main thread sets stop=true and signals cond once both transfer
 * ioctls return, so this poller exits promptly rather than running
 * out the interval timer.
 */
static void *
lzc_local_replicate_progress_thread(void *arg)
{
	struct lzc_replicate_progress_args *p = arg;

	for (;;) {
		struct timespec ts;
		bool stopped;
		uint64_t written = 0, blocks = 0;
		PyObject *result;
		PyObject *written_obj;
		PyObject *total_obj;
		PyGILState_STATE gs;

		pthread_mutex_lock(&p->lock);
		clock_gettime(CLOCK_MONOTONIC, &ts);
		ts.tv_sec += p->interval_seconds;
		while (!p->stop) {
			int rc = pthread_cond_timedwait(&p->cond, &p->lock,
							&ts);
			if (rc == ETIMEDOUT)
				break;
		}
		stopped = p->stop;
		pthread_mutex_unlock(&p->lock);
		if (stopped)
			break;

		if (lzc_send_progress(p->snapname, p->fd, &written,
				      &blocks) != 0)
			continue;

		gs = PyGILState_Ensure();
		written_obj = PyLong_FromUnsignedLongLong(written);
		total_obj = PyLong_FromUnsignedLongLong(p->total);
		result = NULL;
		if (written_obj != NULL && total_obj != NULL)
			result = PyObject_CallFunctionObjArgs(p->callback,
							      written_obj,
							      total_obj,
							      p->state, NULL);
		Py_XDECREF(written_obj);
		Py_XDECREF(total_obj);
		if (result == NULL) {
			/*
			 * Callback (or building its args) raised.  Route to
			 * sys.unraisablehook the same way signal handlers,
			 * thread targets, and atexit hooks do, then stop
			 * polling so we do not spam the hook each interval.
			 * The transfer itself continues unaffected.
			 */
			PyErr_WriteUnraisable(p->callback);
			PyGILState_Release(gs);
			pthread_mutex_lock(&p->lock);
			p->stop = true;
			pthread_mutex_unlock(&p->lock);
			break;
		}
		Py_DECREF(result);
		PyGILState_Release(gs);
	}
	return NULL;
}

/*
 * One-shot setup of the progress-poller arg struct.  Computes the
 * total-bytes estimate via lzc_send_space and initializes the mutex
 * + cond used to coordinate the poller with the main thread.  The
 * fd is left at -1; the caller must assign it after pipe2() succeeds.
 *
 * Returns 0 on success.  Returns -1 with a Python exception set on
 * lzc_send_space failure; mutex/cond are left uninitialized in that
 * case so the caller must not call local_replicate_progress_fini.
 */
static int
local_replicate_progress_init(struct lzc_replicate_progress_args *p,
			      const char *source, const char *fromsnap,
			      enum lzc_send_flags flags,
			      PyObject *callback, PyObject *state,
			      int interval_seconds, PyObject *exc_owner)
{
	uint64_t total = 0;
	int err;
	pthread_condattr_t cattr;
	char ctx_msg[ZFS_MAX_DATASET_NAME_LEN + 64];

	Py_BEGIN_ALLOW_THREADS
	err = lzc_send_space(source, fromsnap, flags, &total);
	Py_END_ALLOW_THREADS
	if (err) {
		snprintf(ctx_msg, sizeof(ctx_msg),
			 "lzc_send_space() failed for source='%s'",
			 source);
		set_zfscore_exc(exc_owner, ctx_msg, err, Py_None);
		return -1;
	}

	p->snapname = source;
	p->fd = -1;
	p->total = total;
	p->callback = callback;
	p->state = (state != NULL) ? state : Py_None;
	p->interval_seconds = interval_seconds;
	p->stop = false;
	pthread_mutex_init(&p->lock, NULL);
	/*
	 * Use CLOCK_MONOTONIC for cond_timedwait deadlines so wall-clock
	 * adjustments (NTP, manual `date`, leap seconds) cannot push the
	 * poller into a busy loop or stall it past its interval.  The
	 * deadline computed by lzc_local_replicate_progress_thread must
	 * use the same clock, and does.
	 */
	pthread_condattr_init(&cattr);
	pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
	pthread_cond_init(&p->cond, &cattr);
	pthread_condattr_destroy(&cattr);
	return 0;
}

/*
 * Tear down the sync primitives initialized by
 * local_replicate_progress_init.  Always paired one-for-one with that
 * function on the success-of-init path.
 */
static void
local_replicate_progress_fini(struct lzc_replicate_progress_args *p)
{
	pthread_mutex_destroy(&p->lock);
	pthread_cond_destroy(&p->cond);
}

/*
 * Spawn the progress poller thread.  Returns 0 on success, or the
 * errno value from pthread_create on failure.  Must be called inside
 * a Py_BEGIN_ALLOW_THREADS block; the GIL is not held by the spawned
 * thread until it explicitly reacquires it for callback dispatch.
 */
static int
local_replicate_progress_start(pthread_t *tid,
			       struct lzc_replicate_progress_args *p)
{
	return pthread_create(tid, NULL,
			      lzc_local_replicate_progress_thread, p);
}

/*
 * Wake the progress poller out of its cond_timedwait and join it.
 * Must be called once after both transfer ioctls have settled, and
 * only when local_replicate_progress_start returned 0.
 */
static void
local_replicate_progress_stop(pthread_t tid,
			      struct lzc_replicate_progress_args *p)
{
	pthread_mutex_lock(&p->lock);
	p->stop = true;
	pthread_cond_signal(&p->cond);
	pthread_mutex_unlock(&p->lock);
	pthread_join(tid, NULL);
}

PyObject *
py_lzc_local_replicate(PyObject *self, PyObject *args_unused, PyObject *kwargs)
{
	char *kwnames[] = {
		"source", "dest", "fromsnap", "send_flags", "props",
		"force", "raw", "progress_callback", "progress_state",
		"progress_interval_seconds", NULL
	};
	const char *source = NULL;
	const char *dest = NULL;
	const char *fromsnap = NULL;
	int send_flags = 0;
	PyObject *py_props = NULL;
	int force_int = 0;
	int raw_int = 0;
	PyObject *py_progress_cb = NULL;
	PyObject *py_progress_state = NULL;
	int progress_interval = 1;

	nvlist_t *props = NULL;
	int fds[2] = { -1, -1 };
	pthread_t tid;
	int recv_err = 0;
	int pthread_create_err = 0;
	struct lzc_replicate_send_args send_args;
	enum lzc_send_flags effective_flags;
	struct lzc_replicate_progress_args progress_args;
	pthread_t progress_tid;
	bool progress_active = false;
	int progress_create_err = 0;
	char ctx_msg[ZFS_MAX_DATASET_NAME_LEN + 64];

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs, "|$zzziOppOOi",
					 kwnames,
					 &source, &dest, &fromsnap,
					 &send_flags, &py_props,
					 &force_int, &raw_int,
					 &py_progress_cb, &py_progress_state,
					 &progress_interval))
		return NULL;

	if (source == NULL) {
		PyErr_SetString(PyExc_ValueError, "source is required");
		return NULL;
	}

	if (dest == NULL) {
		PyErr_SetString(PyExc_ValueError, "dest is required");
		return NULL;
	}

	/*
	 * RAW must be driven via the `raw` kwarg so the receive side is
	 * paired automatically.  Allowing both knobs would let callers
	 * specify contradictory state.
	 */
	if (send_flags & LZC_SEND_FLAG_RAW) {
		PyErr_SetString(PyExc_ValueError,
				"SendFlags.RAW must be set via the raw=True "
				"keyword argument so both send and receive "
				"sides agree");
		return NULL;
	}

	/*
	 * SAVED is part of the resumable-send flow which this single-call
	 * method does not expose.  Use lzc.send + lzc.receive directly for
	 * resume.
	 */
	if (send_flags & LZC_SEND_FLAG_SAVED) {
		PyErr_SetString(PyExc_ValueError,
				"SendFlags.SAVED is not supported for "
				"local_replicate");
		return NULL;
	}

	if (!NULL_OR_NONE(py_progress_cb) &&
	    !PyCallable_Check(py_progress_cb)) {
		PyErr_SetString(PyExc_TypeError,
				"progress_callback must be callable or None");
		return NULL;
	}

	if (progress_interval <= 0) {
		PyErr_SetString(PyExc_ValueError,
				"progress_interval_seconds must be positive");
		return NULL;
	}

	effective_flags = (enum lzc_send_flags)send_flags;
	if (raw_int)
		effective_flags |= LZC_SEND_FLAG_RAW;

	if (!NULL_OR_NONE(py_props)) {
		props = local_replicate_build_cmdprops(py_props, dest);
		if (props == NULL)
			return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.local_replicate",
			"ssziOOO",
			source, dest, fromsnap,
			(int)effective_flags,
			force_int ? Py_True : Py_False,
			raw_int ? Py_True : Py_False,
			py_props ? py_props : Py_None) < 0) {
		fnvlist_free(props);
		return NULL;
	}

	if (!NULL_OR_NONE(py_progress_cb)) {
		if (local_replicate_progress_init(&progress_args,
						  source, fromsnap,
						  effective_flags,
						  py_progress_cb,
						  py_progress_state,
						  progress_interval,
						  self) < 0) {
			fnvlist_free(props);
			return NULL;
		}
		progress_active = true;
	}

	if (pipe2(fds, O_CLOEXEC) < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		fnvlist_free(props);
		if (progress_active)
			local_replicate_progress_fini(&progress_args);
		return NULL;
	}

	/*
	 * Best-effort pipe enlargement; ignore failures and let the kernel
	 * default stand if the resize is rejected.  The resize is safe
	 * here because the pipe is empty (kernel bug 212295 only deadlocks
	 * on a non-empty pipe).
	 */
	(void)fcntl(fds[1], F_SETPIPE_SZ, LZC_LOCAL_REPLICATE_PIPE_SIZE);

	send_args.snapname = source;
	send_args.fromsnap = fromsnap;
	send_args.fd = fds[1];
	send_args.flags = effective_flags;
	send_args.err = 0;

	if (progress_active)
		progress_args.fd = fds[1];

	Py_BEGIN_ALLOW_THREADS
	pthread_create_err = pthread_create(&tid, NULL,
					    lzc_local_replicate_send_thread,
					    &send_args);
	if (pthread_create_err == 0) {
		if (progress_active)
			progress_create_err = local_replicate_progress_start(
				&progress_tid, &progress_args);
		/*
		 * Pass user props as cmdprops (the `zfs receive -o` slot,
		 * applied with source LOCAL) rather than as the recv-side
		 * props slot.  Recv-side props are overwritten by the
		 * source dataset's local properties carried in the stream,
		 * which makes the override invisible whenever a value
		 * exists on the source - which is the common case.  LOCAL
		 * source on the destination beats the stream's values, so
		 * the override actually sticks.
		 */
		recv_err = lzc_receive_with_cmdprops(
			dest,
			NULL,			/* recv-side props */
			props,			/* cmdprops (-o) */
			NULL, 0,		/* wkeydata, wkeylen */
			NULL,			/* origin */
			(boolean_t)force_int,
			B_FALSE,		/* resumable */
			(boolean_t)raw_int,
			fds[0],
			NULL,			/* begin_record */
			-1,			/* cleanup_fd */
			NULL, NULL, NULL,	/* read_bytes/errflags/action_handle */
			NULL);			/* per-prop errors */
		/*
		 * Closing the read end before joining ensures the sender
		 * exits promptly even on recv error: any further writes on
		 * the write end now return EPIPE (SIGPIPE is masked in the
		 * thread, see lzc_local_replicate_send_thread).
		 */
		close(fds[0]);
		fds[0] = -1;
		pthread_join(tid, NULL);
		/* Sender thread closed fds[1] from inside the thread. */
		fds[1] = -1;

		/* Wake & join the progress poller, if it was started. */
		if (progress_active && progress_create_err == 0)
			local_replicate_progress_stop(progress_tid,
						      &progress_args);
	} else {
		/* No sender thread launched: clean up both fds ourselves. */
		close(fds[0]);
		fds[0] = -1;
		close(fds[1]);
		fds[1] = -1;
	}
	Py_END_ALLOW_THREADS

	fnvlist_free(props);

	/* Tear down progress sync primitives before any error return. */
	if (progress_active)
		local_replicate_progress_fini(&progress_args);

	if (pthread_create_err != 0) {
		errno = pthread_create_err;
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	/*
	 * Error precedence: receive error wins.  A failed receive closes
	 * the read end which causes the sender to fail with EPIPE; that
	 * downstream send error is uninteresting and would mask the real
	 * cause.  Only surface the send error when the receive succeeded.
	 */
	if (recv_err) {
		snprintf(ctx_msg, sizeof(ctx_msg),
			 "lzc_receive() failed for dest='%s'",
			 dest);
		set_zfscore_exc(self, ctx_msg, recv_err, Py_None);
		return NULL;
	}

	if (send_args.err) {
		snprintf(ctx_msg, sizeof(ctx_msg),
			 "lzc_send() failed for source='%s'",
			 source);
		set_zfscore_exc(self, ctx_msg, send_args.err, Py_None);
		return NULL;
	}

	/* Transfer succeeded.  Surface progress-thread spawn failures. */
	if (progress_active && progress_create_err != 0) {
		errno = progress_create_err;
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	/*
	 * History logging: the receive ioctl set the allow-log TSD
	 * (Thread-Specific Data) on the calling thread, so we can log a
	 * synthesized "zfs send | zfs receive" entry.  py_props survives
	 * past the fnvlist_free(props) above, so we can still inspect it
	 * to decide whether to emit the props marker.
	 */
	if (local_replicate_log_history(source, dest, fromsnap,
					effective_flags,
					force_int != 0,
					!NULL_OR_NONE(py_props) &&
					PyDict_Size(py_props) > 0))
		return NULL;

	Py_RETURN_NONE;
}
