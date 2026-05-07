#include "py_local_replicate.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * Exported via libzfs_core_replication.h.  Re-declared here to avoid
 * a layering inversion (common/ should not include libzfs_core/).
 */
extern void set_zfscore_exc(PyObject *module, const char *msg, int code,
			    PyObject *errors_tuple);

/*
 * 1 MiB - matches the unprivileged default of
 * /proc/sys/fs/pipe-max-size; the resize is best-effort either way
 * (see fcntl(F_SETPIPE_SZ) comment in py_local_replicate).
 */
#define REPLICATE_PIPE_SIZE	(1024 * 1024)

libzfs_handle_t *
py_replicate_open_temp_handle(void)
{
	libzfs_handle_t *hdl = NULL;

	Py_BEGIN_ALLOW_THREADS
	hdl = libzfs_init();
	Py_END_ALLOW_THREADS
	if (hdl == NULL)
		PyErr_SetString(PyExc_RuntimeError,
				"Failed to create temporary libzfs handle.");
	return hdl;
}

/*
 * Iterate the user-supplied exclude iterable and add each entry as a
 * DATA_TYPE_BOOLEAN nvpair to `nvl`.  The kernel's
 * zfs_setup_cmdline_props treats boolean entries in cmdprops as
 * exclusions (the `zfs receive -x prop` semantic).  Returns 0 on
 * success, -1 with a Python exception set on failure.
 */
static int
add_exclude_props(nvlist_t *nvl, PyObject *py_exclude)
{
	PyObject *iter = NULL;
	PyObject *item = NULL;

	iter = PyObject_GetIter(py_exclude);
	if (iter == NULL)
		return -1;

	while ((item = PyIter_Next(iter)) != NULL) {
		const char *name = PyUnicode_AsUTF8(item);
		if (name == NULL) {
			Py_DECREF(item);
			Py_DECREF(iter);
			return -1;
		}
		fnvlist_add_boolean(nvl, name);
		Py_DECREF(item);
	}
	Py_DECREF(iter);
	if (PyErr_Occurred())
		return -1;
	return 0;
}

/*
 * Convert a Python dict of dataset property overrides plus an
 * optional iterable of property names to exclude into the cmdprops
 * nvlist that the receive side expects.  py_dict_to_nvlist produces
 * a string-valued nvlist; excluded names are added as
 * DATA_TYPE_BOOLEAN entries.
 *
 * lzc mode: lzc_receive_with_cmdprops takes a native-typed nvlist
 * directly; the kernel rejects DATA_TYPE_STRING for non-string
 * properties (compression, readonly, etc.) via
 * zfs_setup_cmdline_props's own validation step.  Run
 * zfs_valid_proplist here against a temporary libzfs handle to do
 * the string -> native conversion before handing the nvlist off.
 *
 * zfs mode: zfs_receive runs zfs_setup_cmdline_props internally,
 * which iterates the cmdprops nvlist and rejects anything other
 * than DATA_TYPE_STRING (-o) or DATA_TYPE_BOOLEAN (-x), then runs
 * its own zfs_valid_proplist.  Pass the string-valued nvlist
 * straight through; doing the conversion ourselves would land us
 * with native-typed entries that zfs_setup_cmdline_props rejects.
 */
static nvlist_t *
build_cmdprops(PyObject *py_props, PyObject *py_exclude,
	       const char *dest_label, enum local_replicate_mode mode)
{
	nvlist_t *raw_props = NULL;
	libzfs_handle_t *vp_hdl = NULL;
	nvlist_t *valid_props = NULL;
	char vp_errbuf[1024];
	py_zfs_error_t zfs_err;

	if (NULL_OR_NONE(py_props))
		raw_props = fnvlist_alloc();
	else
		raw_props = py_dict_to_nvlist(py_props);
	if (raw_props == NULL)
		return NULL;

	if (!NULL_OR_NONE(py_exclude)) {
		if (add_exclude_props(raw_props, py_exclude) < 0) {
			fnvlist_free(raw_props);
			return NULL;
		}
	}

	if (mode == LOCAL_REPLICATE_ZFS)
		return raw_props;

	vp_hdl = py_replicate_open_temp_handle();
	if (vp_hdl == NULL) {
		fnvlist_free(raw_props);
		return NULL;
	}

	snprintf(vp_errbuf, sizeof(vp_errbuf),
		 "cannot validate properties for receive into '%s'",
		 dest_label);
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

/*
 * zfs-mode send: opens a private libzfs handle + zfs_handle_t, runs
 * zfs_send, captures any libzfs error info via the private handle
 * before closing it, and returns the libzfs error code.  Worker is
 * isolated from the caller's mutex-protected libzfs handle.
 *
 * args->source arrives as "dataset@snap"; this op splits on '@' so
 * it can zfs_open the dataset and pass the bare snapshot suffix to
 * zfs_send.  Same for args->fromsnap.
 */
static int
zfs_send_op(const struct local_replicate_args *a, int outfd,
	    py_zfs_error_t *err_out, bool *err_set)
{
	libzfs_handle_t *hdl = NULL;
	zfs_handle_t *zhp = NULL;
	sendflags_t flags;
	char dataset_name[ZFS_MAX_DATASET_NAME_LEN];
	const char *at;
	const char *tosnap_suffix;
	const char *fromsnap_suffix = NULL;
	size_t namelen;
	int err;

	*err_set = false;

	at = strchr(a->source, '@');
	if (at == NULL || at == a->source)
		return EINVAL;
	namelen = (size_t)(at - a->source);
	if (namelen >= sizeof(dataset_name))
		return ENAMETOOLONG;
	(void) strlcpy(dataset_name, a->source, namelen + 1);
	tosnap_suffix = at + 1;

	if (a->fromsnap != NULL) {
		const char *fat = strchr(a->fromsnap, '@');
		fromsnap_suffix = fat ? (fat + 1) : a->fromsnap;
	}

	hdl = libzfs_init();
	if (hdl == NULL)
		return ENOMEM;

	zhp = zfs_open(hdl, dataset_name,
		       ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL) {
		py_get_zfs_error(hdl, err_out);
		*err_set = true;
		libzfs_fini(hdl);
		return ENOENT;
	}

	/*
	 * -R already triggers prop embedding (see send_iterate_fs in
	 * libzfs: `if (sd->props || sd->backup || sd->recursive)`),
	 * so flags.props is only load-bearing on the non-recursive
	 * (volume) path.  Setting it explicitly there - and leaving
	 * it false under -R - keeps the wire output identical and
	 * makes it obvious which line does the work for which mode.
	 */
	flags = (sendflags_t){
		.replicate = a->recursive,
		.props = !a->recursive,
		.largeblock = (a->send_flags_int & LZC_SEND_FLAG_LARGE_BLOCK) != 0,
		.embed_data = (a->send_flags_int & LZC_SEND_FLAG_EMBED_DATA) != 0,
		.compress = (a->send_flags_int & LZC_SEND_FLAG_COMPRESS) != 0,
		.raw = a->raw,
	};

	err = zfs_send(zhp, fromsnap_suffix, tosnap_suffix,
		       &flags, outfd, NULL, NULL, NULL);
	if (err) {
		py_get_zfs_error(hdl, err_out);
		*err_set = true;
	}

	zfs_close(zhp);
	libzfs_fini(hdl);
	return err;
}

/*
 * lzc-mode send: a thin wrapper that combines send_flags_int with the
 * raw bit and dispatches to lzc_send.  No libzfs handle needed.  When
 * args->resume_token is set, dispatches to lzc_send_resume with the
 * (object, offset) pair already extracted from the token by the
 * calling thread (lzc_decode_resume_token, which needs the GIL for
 * its error path).
 */
static int
lzc_send_op(const struct local_replicate_args *a, int outfd,
	    uint64_t resume_obj, uint64_t resume_off)
{
	enum lzc_send_flags eff = (enum lzc_send_flags)a->send_flags_int;

	if (a->raw)
		eff |= LZC_SEND_FLAG_RAW;
	if (a->resume_token != NULL) {
		return lzc_send_resume(a->source, a->fromsnap, outfd, eff,
				       resume_obj, resume_off);
	}
	return lzc_send(a->source, a->fromsnap, outfd, eff);
}

/*
 * Decode a resume token into the (object, offset) pair that
 * lzc_send_resume needs.  Runs on the calling thread under the GIL
 * because it may set ValueError.  Returns 0 on success or -1 with a
 * Python exception set.
 */
static int
lzc_decode_resume_token(const char *token, uint64_t *obj_out,
			uint64_t *off_out)
{
	libzfs_handle_t *hdl = NULL;
	nvlist_t *nvl = NULL;

	hdl = py_replicate_open_temp_handle();
	if (hdl == NULL)
		return -1;

	Py_BEGIN_ALLOW_THREADS
	nvl = zfs_send_resume_token_to_nvlist(hdl, token);
	Py_END_ALLOW_THREADS
	libzfs_fini(hdl);

	if (nvl == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"failed to decode resume_token");
		return -1;
	}
	if (nvlist_lookup_uint64(nvl, "object", obj_out) != 0 ||
	    nvlist_lookup_uint64(nvl, "offset", off_out) != 0) {
		fnvlist_free(nvl);
		PyErr_SetString(PyExc_ValueError,
				"resume_token missing object/offset fields");
		return -1;
	}
	fnvlist_free(nvl);
	return 0;
}

/*
 * zfs-mode receive: opens a private libzfs handle, runs zfs_receive,
 * captures error info via the private handle, returns the libzfs
 * error code.
 */
static int
zfs_recv_op(const struct local_replicate_args *a, int infd,
	    nvlist_t *cmdprops, py_zfs_error_t *err_out, bool *err_set)
{
	libzfs_handle_t *hdl = NULL;
	recvflags_t rflags;
	int err;

	*err_set = false;

	hdl = libzfs_init();
	if (hdl == NULL)
		return ENOMEM;

	rflags = (recvflags_t){
		.force = a->force,
		.nomount = a->nomount,
	};

	err = zfs_receive(hdl, a->dest, cmdprops, &rflags, infd, NULL);
	if (err) {
		py_get_zfs_error(hdl, err_out);
		*err_set = true;
	}
	libzfs_fini(hdl);
	return err;
}

/*
 * lzc-mode receive: lzc_receive_with_cmdprops with no libzfs handle.
 *
 * cmdprops are passed in the `zfs receive -o` slot (LOCAL source on
 * the destination) rather than the recv-side slot, so they win over
 * any source dataset properties carried in the stream.  Recv-side
 * props would be overwritten by the source's local properties.
 */
static int
lzc_recv_op(const struct local_replicate_args *a, int infd,
	    nvlist_t *cmdprops)
{
	return lzc_receive_with_cmdprops(
		a->dest,
		NULL,				/* recv-side props */
		cmdprops,			/* cmdprops (-o) */
		NULL, 0,			/* wkeydata, wkeylen */
		NULL,				/* origin */
		a->force,
		a->resumable,
		a->raw,
		infd,
		NULL,				/* begin_record */
		-1,				/* cleanup_fd */
		NULL, NULL, NULL,		/* read_bytes/errflags/action_handle */
		NULL);				/* per-prop errors */
}

/*
 * zfs-mode pre-flight: verify the source snapshot (and the
 * incremental base, if any) actually exist before any pipe or worker
 * is spun up.  Uses a temp libzfs handle so it never touches the
 * caller's mutex-protected handle.  Returns 0 on success, -1 with a
 * Python exception set on failure.
 */
static int
zfs_preflight(const struct local_replicate_args *a)
{
	libzfs_handle_t *hdl = NULL;
	zfs_handle_t *probe = NULL;
	py_zfs_error_t zfs_err;
	bool err_set = false;
	bool init_failed = false;

	Py_BEGIN_ALLOW_THREADS
	hdl = libzfs_init();
	if (hdl == NULL) {
		init_failed = true;
	} else {
		probe = zfs_open(hdl, a->source, ZFS_TYPE_SNAPSHOT);
		if (probe == NULL) {
			py_get_zfs_error(hdl, &zfs_err);
			err_set = true;
		} else {
			zfs_close(probe);
			probe = NULL;
		}
		if (!err_set && a->fromsnap != NULL) {
			probe = zfs_open(hdl, a->fromsnap, ZFS_TYPE_SNAPSHOT);
			if (probe == NULL) {
				py_get_zfs_error(hdl, &zfs_err);
				err_set = true;
			} else {
				zfs_close(probe);
			}
		}
		libzfs_fini(hdl);
	}
	Py_END_ALLOW_THREADS

	if (init_failed) {
		PyErr_SetString(PyExc_RuntimeError,
				"Failed to create temporary libzfs handle.");
		return -1;
	}
	if (err_set) {
		set_exc_from_libzfs(&zfs_err,
				    "local_replicate: snapshot lookup failed");
		return -1;
	}
	return 0;
}

/*
 * lzc-mode total-bytes estimate, used as the second positional
 * argument the user-visible progress callback receives.  Only called
 * when the user supplied a progress_callback.  Returns 0 on success,
 * -1 with a Python exception set on lzc_send_space failure.
 */
static int
lzc_total_estimate(const struct local_replicate_args *a, uint64_t *out)
{
	enum lzc_send_flags eff = (enum lzc_send_flags)a->send_flags_int;
	int err;
	char ctx_msg[ZFS_MAX_DATASET_NAME_LEN + 64];

	if (a->raw)
		eff |= LZC_SEND_FLAG_RAW;

	Py_BEGIN_ALLOW_THREADS
	err = lzc_send_space(a->source, a->fromsnap, eff, out);
	Py_END_ALLOW_THREADS

	if (err) {
		snprintf(ctx_msg, sizeof(ctx_msg),
			 "lzc_send_space() failed for source='%s'",
			 a->source);
		set_zfscore_exc(a->exc_owner, ctx_msg, err, Py_None);
		return -1;
	}
	return 0;
}

/*
 * Render the synthesized "zfs send | zfs receive" pool history entry.
 * Send and recv flags are rendered as their CLI mnemonics; props are
 * flagged with a fixed marker (the raw values would not fit a single
 * log line - see the audit hook for them).  -R tracks args->recursive
 * (zfs mode only); -p is unconditional in zfs mode.  Logging goes through a
 * temp libzfs handle either way (NULL hdl_in to py_log_history_impl);
 * the caller never has to expose its own.
 */
static int
log_history(const struct local_replicate_args *a, bool has_props)
{
	char send_opts[64] = {0};
	char recv_opts[8] = {0};
	const char *props_marker = "";
	int off = 0;

	if (a->skip_history)
		return 0;

	if (a->mode == LOCAL_REPLICATE_ZFS) {
		if (a->recursive)
			off += snprintf(send_opts + off,
					sizeof(send_opts) - off, " -R");
		off += snprintf(send_opts + off,
				sizeof(send_opts) - off, " -p");
	}
	if (a->send_flags_int & LZC_SEND_FLAG_LARGE_BLOCK)
		off += snprintf(send_opts + off,
				sizeof(send_opts) - off, " -L");
	if (a->send_flags_int & LZC_SEND_FLAG_EMBED_DATA)
		off += snprintf(send_opts + off,
				sizeof(send_opts) - off, " -e");
	if (a->send_flags_int & LZC_SEND_FLAG_COMPRESS)
		off += snprintf(send_opts + off,
				sizeof(send_opts) - off, " -c");
	if (a->raw)
		off += snprintf(send_opts + off,
				sizeof(send_opts) - off, " -w");
	if (a->force)
		snprintf(recv_opts, sizeof(recv_opts), " -F");
	if (has_props)
		props_marker = " (with -o property overrides)";

	if (a->fromsnap) {
		return py_log_history_impl(NULL, a->history_prefix,
					   "zfs send%s -i %s %s | "
					   "zfs receive%s %s%s",
					   send_opts, a->fromsnap, a->source,
					   recv_opts, a->dest, props_marker);
	}
	return py_log_history_impl(NULL, a->history_prefix,
				   "zfs send%s %s | zfs receive%s %s%s",
				   send_opts, a->source,
				   recv_opts, a->dest, props_marker);
}

/*
 * Argument struct for the send-side worker pthread.  Lives on the
 * stack of the main thread for the duration of the transfer.  The
 * mode is read from a->mode to dispatch between lzc_send_op and
 * zfs_send_op.
 */
struct send_thread_args {
	const struct local_replicate_args *a;
	int fd;
	int err;
	py_zfs_error_t zfs_err;		/* zfs mode only */
	bool err_set;			/* zfs mode only */
	uint64_t resume_obj;		/* lzc resume only; populated by
					 * the calling thread via
					 * lzc_decode_resume_token */
	uint64_t resume_off;		/* lzc resume only */
};

/*
 * Send worker.  Blocks SIGPIPE so a write into a half-closed pipe
 * returns EPIPE through the libzfs/lzc call rather than killing the
 * process, then dispatches to the mode-appropriate send op.  Closes
 * the write end so the receiver wakes up: EOF on the happy path,
 * EPIPE on the error path.
 */
static void *
send_thread(void *arg)
{
	struct send_thread_args *s = arg;
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	if (s->a->mode == LOCAL_REPLICATE_LZC)
		s->err = lzc_send_op(s->a, s->fd, s->resume_obj, s->resume_off);
	else
		s->err = zfs_send_op(s->a, s->fd, &s->zfs_err, &s->err_set);

	close(s->fd);
	return NULL;
}

/*
 * Argument struct for the optional progress poller pthread.  Lives
 * on the stack of the main thread.  callback / state are borrowed -
 * they must remain alive until py_local_replicate returns.
 */
struct progress_thread_args {
	const char *snapname;	/* "name@snap" form for lzc_send_progress */
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
 * Progress poller.  Sleeps interval_seconds between polls (using
 * CLOCK_MONOTONIC so wall-clock skew cannot starve or busy-loop the
 * timer), queries lzc_send_progress (which works for both modes -
 * zfs_send_progress is just lzc_send_progress in disguise),
 * reacquires the GIL, and invokes the user callback with
 * (written, total, state).
 *
 * Callback exceptions route through sys.unraisablehook (default:
 * traceback to stderr) and the poller stops after the first such
 * failure to avoid spamming the hook.  The transfer itself is
 * unaffected; progress is advisory.
 *
 * Coordination with the calling thread is via lock + cond + stop:
 * the calling thread sets stop=true and signals cond once both
 * transfer callbacks have returned, so this poller exits promptly
 * rather than running out the interval timer.
 */
static void *
progress_thread(void *arg)
{
	struct progress_thread_args *p = arg;

	for (;;) {
		struct timespec ts;
		bool stopped;
		uint64_t written = 0;
		uint64_t blocks = 0;
		PyObject *result = NULL;
		PyObject *written_obj = NULL;
		PyObject *total_obj = NULL;
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
		if (written_obj != NULL && total_obj != NULL)
			result = PyObject_CallFunctionObjArgs(p->callback,
							      written_obj,
							      total_obj,
							      p->state, NULL);
		Py_XDECREF(written_obj);
		Py_XDECREF(total_obj);
		if (result == NULL) {
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
 * Initialize the synchronization primitives used by the progress
 * poller.  Mirrored by progress_fini.  CLOCK_MONOTONIC on the cond
 * matches the deadline computation in progress_thread.
 */
static void
progress_init(struct progress_thread_args *p)
{
	pthread_condattr_t cattr;

	p->stop = false;
	pthread_mutex_init(&p->lock, NULL);
	pthread_condattr_init(&cattr);
	pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
	pthread_cond_init(&p->cond, &cattr);
	pthread_condattr_destroy(&cattr);
}

static void
progress_fini(struct progress_thread_args *p)
{
	pthread_mutex_destroy(&p->lock);
	pthread_cond_destroy(&p->cond);
}

/*
 * Wake the poller out of its cond_timedwait and join it.  Must only
 * be called when pthread_create for the poller succeeded.
 */
static void
progress_stop_and_join(pthread_t tid, struct progress_thread_args *p)
{
	pthread_mutex_lock(&p->lock);
	p->stop = true;
	pthread_cond_signal(&p->cond);
	pthread_mutex_unlock(&p->lock);
	pthread_join(tid, NULL);
}

/*
 * Validate args (source/dest non-NULL, source has '@', RAW/SAVED
 * rejection, progress_interval, callable check).  Returns 0 on
 * success or -1 with a Python exception set.
 */
static int
validate_args(const struct local_replicate_args *a)
{
	if (a == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"py_local_replicate: args is NULL");
		return -1;
	}
	if (a->source == NULL || strchr(a->source, '@') == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"source must be a full snapshot name "
				"of the form 'dataset@snapshot'");
		return -1;
	}
	if (a->dest == NULL) {
		PyErr_SetString(PyExc_ValueError, "dest is required");
		return -1;
	}
	if (a->send_flags_int & LZC_SEND_FLAG_RAW) {
		PyErr_SetString(PyExc_ValueError,
				"SendFlags.RAW must be set via the raw=True "
				"keyword argument so both send and receive "
				"sides agree");
		return -1;
	}
	if (a->send_flags_int & LZC_SEND_FLAG_SAVED) {
		PyErr_SetString(PyExc_ValueError,
				"SendFlags.SAVED is not supported for "
				"local_replicate");
		return -1;
	}
	if (a->mode == LOCAL_REPLICATE_ZFS &&
	    (a->resumable || a->resume_token != NULL)) {
		PyErr_SetString(PyExc_ValueError,
				"resumable / resume_token are only supported "
				"on lzc.local_replicate; recursive sends and "
				"resume tokens do not compose");
		return -1;
	}
	if (a->progress_interval_seconds <= 0) {
		PyErr_SetString(PyExc_ValueError,
				"progress_interval_seconds must be positive");
		return -1;
	}
	if (!NULL_OR_NONE(a->py_progress_cb) &&
	    !PyCallable_Check(a->py_progress_cb)) {
		PyErr_SetString(PyExc_TypeError,
				"progress_callback must be callable or None");
		return -1;
	}
	if (a->mode == LOCAL_REPLICATE_LZC &&
	    (a->nomount || !NULL_OR_NONE(a->py_exclude))) {
		PyErr_SetString(PyExc_ValueError,
				"nomount / exclude_props are only supported "
				"on the resource-object local_replicate methods");
		return -1;
	}
	if (!NULL_OR_NONE(a->py_props) && !NULL_OR_NONE(a->py_exclude)) {
		/*
		 * Reject overlapping keys: the kernel processes cmdprops as
		 * a single iteration where each entry is either an override
		 * (DATA_TYPE_STRING) or an exclusion (DATA_TYPE_BOOLEAN);
		 * collisions on the same name produce undefined ordering.
		 */
		PyObject *iter = PyObject_GetIter(a->py_exclude);
		PyObject *item;
		if (iter == NULL)
			return -1;
		while ((item = PyIter_Next(iter)) != NULL) {
			int contains = PyDict_Contains(a->py_props, item);
			if (contains < 0) {
				Py_DECREF(item);
				Py_DECREF(iter);
				return -1;
			}
			if (contains) {
				const char *name = PyUnicode_AsUTF8(item);
				PyErr_Format(PyExc_ValueError,
				    "property '%s' appears in both props "
				    "and exclude_props",
				    name ? name : "<?>");
				Py_DECREF(item);
				Py_DECREF(iter);
				return -1;
			}
			Py_DECREF(item);
		}
		Py_DECREF(iter);
		if (PyErr_Occurred())
			return -1;
	}
	return 0;
}

/*
 * Map a captured send/recv error to a Python exception.  err carries
 * the libzfs/lzc error code; err_set / zfs_err carry libzfs-side
 * description/action when the caller is in zfs mode.  The caller
 * builds ctx_msg already in its own snprintf buffer.
 */
static void
raise_op_exception(const struct local_replicate_args *a, int err,
		   bool err_set, py_zfs_error_t *zfs_err,
		   const char *ctx_msg)
{
	if (a->mode == LOCAL_REPLICATE_LZC) {
		set_zfscore_exc(a->exc_owner, ctx_msg, err, Py_None);
	} else if (err_set) {
		set_exc_from_libzfs(zfs_err, ctx_msg);
	} else {
		errno = err;
		PyErr_SetFromErrno(PyExc_OSError);
	}
}

/*
 * Build cmdprops from py_props + py_exclude when either is supplied,
 * and decide whether the synthesized history line should carry the
 * "with -o property overrides" marker.  Sets *cmdprops_out to a fresh
 * nvlist (caller fnvlist_frees) or NULL when neither input was
 * supplied.  Returns 0 on success or -1 with a Python exception set.
 */
static int
prepare_cmdprops(const struct local_replicate_args *args,
		 nvlist_t **cmdprops_out, bool *has_props_out)
{
	*cmdprops_out = NULL;
	*has_props_out = false;

	if (NULL_OR_NONE(args->py_props) && NULL_OR_NONE(args->py_exclude))
		return 0;

	*cmdprops_out = build_cmdprops(args->py_props, args->py_exclude,
				       args->dest, args->mode);
	if (*cmdprops_out == NULL)
		return -1;

	*has_props_out = (!NULL_OR_NONE(args->py_props) &&
			  PyDict_Size(args->py_props) > 0) ||
			 !NULL_OR_NONE(args->py_exclude);
	return 0;
}

/*
 * Allocate the bounded in-process pipe used by the send worker to
 * push stream bytes to the recv side.  Returns 0 on success with
 * fds[0]/fds[1] populated, or -1 with a Python OSError set.
 */
static int
setup_replicate_pipe(int fds[2])
{
	if (pipe2(fds, O_CLOEXEC) < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		return -1;
	}
	/*
	 * Best-effort pipe enlarge.  Failures are tolerated and the
	 * kernel default takes over: EPERM over the cap in
	 * /proc/sys/fs/pipe-max-size for non-root, EINVAL on rejected
	 * sizes.  Safe here because the pipe is empty - kernel bug
	 * 212295 only deadlocks F_SETPIPE_SZ on a non-empty pipe.
	 */
	(void)fcntl(fds[1], F_SETPIPE_SZ, REPLICATE_PIPE_SIZE);
	return 0;
}

/*
 * Outputs from run_transfer.  recv_err / recv_err_set / recv_zfs_err
 * carry the receive-side outcome (the receive runs on the calling
 * thread).  send_create_err / progress_create_err are the
 * pthread_create failure codes for the two workers; non-zero means
 * that worker did not run.  Send-side errors land in the caller's
 * struct send_thread_args, not here.
 */
struct transfer_result {
	int recv_err;
	bool recv_err_set;
	py_zfs_error_t recv_zfs_err;
	int send_create_err;
	int progress_create_err;
};

/*
 * GIL-released orchestration: spin up the send worker, optionally
 * the progress poller, and run the receive on the calling thread.
 * Closes the read end before joining so the sender exits promptly
 * even on a recv error (any further write returns EPIPE; SIGPIPE
 * is masked in the worker, see send_thread).  Joins both workers
 * before returning.
 *
 * fds[0]/fds[1] are taken over and reset to -1 in place; the caller
 * must not close them after the call returns.
 */
static void
run_transfer(const struct local_replicate_args *args, int fds[2],
	     struct send_thread_args *sargs,
	     struct progress_thread_args *pargs, bool progress_enabled,
	     nvlist_t *cmdprops, struct transfer_result *r)
{
	pthread_t send_tid;
	pthread_t progress_tid;

	*r = (struct transfer_result){0};

	Py_BEGIN_ALLOW_THREADS
	r->send_create_err = pthread_create(&send_tid, NULL, send_thread,
					    sargs);
	if (r->send_create_err == 0) {
		if (progress_enabled)
			r->progress_create_err =
				pthread_create(&progress_tid, NULL,
					       progress_thread, pargs);
		if (args->mode == LOCAL_REPLICATE_LZC)
			r->recv_err = lzc_recv_op(args, fds[0], cmdprops);
		else
			r->recv_err = zfs_recv_op(args, fds[0], cmdprops,
						  &r->recv_zfs_err,
						  &r->recv_err_set);
		close(fds[0]);
		fds[0] = -1;
		pthread_join(send_tid, NULL);
		/* worker closed the write end from inside send_thread */
		fds[1] = -1;

		if (progress_enabled && r->progress_create_err == 0)
			progress_stop_and_join(progress_tid, pargs);
	} else {
		close(fds[0]);
		fds[0] = -1;
		close(fds[1]);
		fds[1] = -1;
	}
	Py_END_ALLOW_THREADS
}

/*
 * Build the "<op>() failed for <role>='<name>'" context message for
 * a send- or recv-side failure and dispatch through
 * raise_op_exception.  is_recv selects role/name: true means the
 * receive side ("dest", a->dest); false means the send side
 * ("source", a->source).
 */
static void
raise_replicate_error(const struct local_replicate_args *a, bool is_recv,
		      int err, bool err_set, py_zfs_error_t *zfs_err)
{
	char ctx_msg[ZFS_MAX_DATASET_NAME_LEN + 64];
	const char *fn;

	if (a->mode == LOCAL_REPLICATE_LZC)
		fn = is_recv ? "lzc_receive" : "lzc_send";
	else
		fn = is_recv ? "zfs_receive" : "zfs_send";
	snprintf(ctx_msg, sizeof(ctx_msg), "%s() failed for %s='%s'",
		 fn, is_recv ? "dest" : "source",
		 is_recv ? a->dest : a->source);
	raise_op_exception(a, err, err_set, zfs_err, ctx_msg);
}

PyObject *
py_local_replicate(const struct local_replicate_args *args)
{
	nvlist_t *cmdprops = NULL;
	int fds[2] = { -1, -1 };
	struct send_thread_args sargs;
	struct progress_thread_args pargs;
	struct transfer_result tr;
	bool progress_enabled = false;
	uint64_t total_estimate = 0;
	bool has_props = false;

	if (validate_args(args) < 0)
		return NULL;

	if (prepare_cmdprops(args, &cmdprops, &has_props) < 0)
		return NULL;

	if (args->mode == LOCAL_REPLICATE_ZFS) {
		if (zfs_preflight(args) < 0) {
			fnvlist_free(cmdprops);
			return NULL;
		}
	}

	/*
	 * lzc_send_space cannot estimate a resumed transfer (it expects
	 * a fresh send), so when resuming the progress callback's total
	 * stays at 0.  The "bytes" field on the resume token nvlist
	 * gives a remaining-bytes hint we could plumb through, but it
	 * is not required for correctness; skip for simplicity.
	 */
	if (args->mode == LOCAL_REPLICATE_LZC &&
	    !NULL_OR_NONE(args->py_progress_cb) &&
	    args->resume_token == NULL) {
		if (lzc_total_estimate(args, &total_estimate) < 0) {
			fnvlist_free(cmdprops);
			return NULL;
		}
	}

	sargs = (struct send_thread_args){ .a = args };

	if (args->mode == LOCAL_REPLICATE_LZC && args->resume_token != NULL) {
		if (lzc_decode_resume_token(args->resume_token,
					    &sargs.resume_obj,
					    &sargs.resume_off) < 0) {
			fnvlist_free(cmdprops);
			return NULL;
		}
	}

	if (!NULL_OR_NONE(args->py_progress_cb)) {
		pargs = (struct progress_thread_args){
			.snapname = args->source,
			.fd = -1,
			.total = total_estimate,
			.callback = args->py_progress_cb,
			.state = NULL_OR_NONE(args->py_progress_state)
					? Py_None : args->py_progress_state,
			.interval_seconds = args->progress_interval_seconds,
		};
		progress_init(&pargs);
		progress_enabled = true;
	}

	if (setup_replicate_pipe(fds) < 0) {
		if (progress_enabled)
			progress_fini(&pargs);
		fnvlist_free(cmdprops);
		return NULL;
	}

	sargs.fd = fds[1];
	if (progress_enabled)
		pargs.fd = fds[1];

	run_transfer(args, fds, &sargs, &pargs, progress_enabled, cmdprops,
		     &tr);

	if (progress_enabled)
		progress_fini(&pargs);

	if (tr.send_create_err != 0) {
		errno = tr.send_create_err;
		PyErr_SetFromErrno(PyExc_OSError);
		fnvlist_free(cmdprops);
		return NULL;
	}

	/*
	 * Error precedence: receive error wins.  A failed receive
	 * closes the read end, which causes the sender to fail with
	 * EPIPE/EINTR; reporting the EPIPE would mask the real cause.
	 */
	if (tr.recv_err) {
		raise_replicate_error(args, true, tr.recv_err,
				      tr.recv_err_set, &tr.recv_zfs_err);
		fnvlist_free(cmdprops);
		return NULL;
	}

	if (sargs.err) {
		raise_replicate_error(args, false, sargs.err, sargs.err_set,
				      &sargs.zfs_err);
		fnvlist_free(cmdprops);
		return NULL;
	}

	if (progress_enabled && tr.progress_create_err != 0) {
		errno = tr.progress_create_err;
		PyErr_SetFromErrno(PyExc_OSError);
		fnvlist_free(cmdprops);
		return NULL;
	}

	fnvlist_free(cmdprops);

	if (log_history(args, has_props))
		return NULL;

	Py_RETURN_NONE;
}
