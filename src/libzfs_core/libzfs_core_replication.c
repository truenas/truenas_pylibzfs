#include "libzfs_core_replication.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

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
		set_zfscore_exc(self, "lzc_send_space() failed", err, Py_None);
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

		lz = libzfs_init();
		if (lz == NULL) {
			PyErr_SetString(PyExc_RuntimeError,
					"libzfs_init() failed");
			return NULL;
		}

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
			set_zfscore_exc(self, "lzc_send_resume() failed",
					err, Py_None);
			return NULL;
		}
	} else {
		Py_BEGIN_ALLOW_THREADS
		err = lzc_send(snapname, fromsnap, fd,
			       (enum lzc_send_flags)flags);
		Py_END_ALLOW_THREADS

		if (err) {
			set_zfscore_exc(self, "lzc_send() failed",
					err, Py_None);
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

	if (py_props != NULL && py_props != Py_None) {
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
		set_zfscore_exc(self, "lzc_receive() failed", err, Py_None);
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

PyObject *
py_lzc_local_replicate(PyObject *self, PyObject *args_unused, PyObject *kwargs)
{
	char *kwnames[] = {
		"source", "dest", "fromsnap", "send_flags", "props",
		"force", "raw", "pipe_size", NULL
	};
	const char *source = NULL;
	const char *dest = NULL;
	const char *fromsnap = NULL;
	int send_flags = 0;
	PyObject *py_props = NULL;
	int force_int = 0;
	int raw_int = 0;
	int pipe_size = 1 << 20;

	nvlist_t *props = NULL;
	int fds[2] = { -1, -1 };
	pthread_t tid;
	int recv_err = 0;
	int pthread_create_err = 0;
	struct lzc_replicate_send_args send_args;
	enum lzc_send_flags effective_flags;

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs, "|$zzziOppi",
					 kwnames,
					 &source, &dest, &fromsnap,
					 &send_flags, &py_props,
					 &force_int, &raw_int, &pipe_size))
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

	if (pipe_size <= 0) {
		PyErr_SetString(PyExc_ValueError,
				"pipe_size must be positive");
		return NULL;
	}

	effective_flags = (enum lzc_send_flags)send_flags;
	if (raw_int)
		effective_flags |= LZC_SEND_FLAG_RAW;

	if (py_props != NULL && py_props != Py_None) {
		props = py_dict_to_nvlist(py_props);
		if (props == NULL)
			return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.local_replicate",
			"sszii",
			source, dest,
			fromsnap ? fromsnap : "",
			send_flags, raw_int) < 0) {
		fnvlist_free(props);
		return NULL;
	}

	if (pipe2(fds, O_CLOEXEC) < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		fnvlist_free(props);
		return NULL;
	}

	/*
	 * Best-effort pipe enlargement.  Larger than the kernel default
	 * (64 KiB) reduces context-switch overhead between the threads.
	 * EPERM (over /proc/sys/fs/pipe-max-size for non-root) and EINVAL
	 * are tolerated; the resize is safe because the pipe is empty
	 * (kernel bug 212295 only deadlocks on a non-empty pipe).
	 */
	(void)fcntl(fds[1], F_SETPIPE_SZ, pipe_size);

	send_args.snapname = source;
	send_args.fromsnap = fromsnap;
	send_args.fd = fds[1];
	send_args.flags = effective_flags;
	send_args.err = 0;

	Py_BEGIN_ALLOW_THREADS
	pthread_create_err = pthread_create(&tid, NULL,
					    lzc_local_replicate_send_thread,
					    &send_args);
	if (pthread_create_err == 0) {
		recv_err = lzc_receive(dest, props, NULL,
				       (boolean_t)force_int,
				       (boolean_t)raw_int,
				       fds[0]);
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
	} else {
		/* No sender thread launched: clean up both fds ourselves. */
		close(fds[0]);
		fds[0] = -1;
		close(fds[1]);
		fds[1] = -1;
	}
	Py_END_ALLOW_THREADS

	fnvlist_free(props);

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
		set_zfscore_exc(self, "lzc_receive() failed",
				recv_err, Py_None);
		return NULL;
	}

	if (send_args.err) {
		set_zfscore_exc(self, "lzc_send() failed",
				send_args.err, Py_None);
		return NULL;
	}

	/*
	 * History logging: the receive ioctl set the allow-log TSD
	 * (Thread-Specific Data) on the calling thread, so we can log a
	 * synthesized "zfs send | zfs receive" entry.  Two formats keeps
	 * the message readable.
	 */
	if (fromsnap) {
		if (py_log_history_impl(NULL, NULL,
					"zfs send%s -i %s %s | "
					"zfs receive%s %s",
					raw_int ? " -w" : "",
					fromsnap, source,
					force_int ? " -F" : "",
					dest))
			return NULL;
	} else {
		if (py_log_history_impl(NULL, NULL,
					"zfs send%s %s | zfs receive%s %s",
					raw_int ? " -w" : "",
					source,
					force_int ? " -F" : "",
					dest))
			return NULL;
	}

	Py_RETURN_NONE;
}
