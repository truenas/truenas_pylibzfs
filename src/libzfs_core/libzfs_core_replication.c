#include "libzfs_core_replication.h"
#include "../common/py_local_replicate.h"

#include <stdio.h>

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
	 * treated as (0, 0) - this is a polling function and callers expect
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

		lz = py_replicate_open_temp_handle();
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

		/*
		 * The token records which feature flags the interrupted send
		 * was using. Resuming without them yields a stream that does
		 * not match what the receiver already has, so fold them in
		 * alongside the caller's flags, as libzfs does in
		 * zfs_send_resume_impl_cb_impl().
		 */
		if (nvlist_exists(nvl, "largeblockok"))
			flags |= LZC_SEND_FLAG_LARGE_BLOCK;
		if (nvlist_exists(nvl, "embedok"))
			flags |= LZC_SEND_FLAG_EMBED_DATA;
		if (nvlist_exists(nvl, "compressok"))
			flags |= LZC_SEND_FLAG_COMPRESS;
		if (nvlist_exists(nvl, "rawok"))
			flags |= LZC_SEND_FLAG_RAW;
		if (nvlist_exists(nvl, "savedok"))
			flags |= LZC_SEND_FLAG_SAVED;

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

PyObject *
py_lzc_local_replicate(PyObject *self, PyObject *args_unused, PyObject *kwargs)
{
	char *kwnames[] = {
		"source", "dest", "fromsnap", "send_flags", "props",
		"force", "raw", "resumable", "resume_token",
		"progress_callback", "progress_state",
		"progress_interval_seconds", NULL
	};
	const char *source = NULL;
	const char *dest = NULL;
	const char *fromsnap = NULL;
	int send_flags = 0;
	PyObject *py_props = NULL;
	int force_int = 0;
	int raw_int = 0;
	int resumable_int = 0;
	const char *resume_token = NULL;
	PyObject *py_progress_cb = NULL;
	PyObject *py_progress_state = NULL;
	int progress_interval = 1;
	struct local_replicate_args la;

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$zzziOpppzOOi",
					 kwnames,
					 &source, &dest, &fromsnap,
					 &send_flags, &py_props,
					 &force_int, &raw_int,
					 &resumable_int, &resume_token,
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

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.local_replicate",
			"ssziOOOOs",
			source, dest, fromsnap,
			send_flags | (raw_int ? LZC_SEND_FLAG_RAW : 0),
			force_int ? Py_True : Py_False,
			raw_int ? Py_True : Py_False,
			py_props ? py_props : Py_None,
			resumable_int ? Py_True : Py_False,
			resume_token ? resume_token : "") < 0)
		return NULL;

	la = (struct local_replicate_args){
		.mode = LOCAL_REPLICATE_LZC,
		.source = source,
		.fromsnap = fromsnap,
		.dest = dest,
		.exc_owner = self,
		.send_flags_int = send_flags,
		.raw = raw_int != 0,
		.force = force_int != 0,
		.resumable = resumable_int != 0,
		.resume_token = resume_token,
		.py_props = py_props,
		.py_progress_cb = py_progress_cb,
		.py_progress_state = py_progress_state,
		.progress_interval_seconds = progress_interval,
	};

	return py_local_replicate(&la);
}
