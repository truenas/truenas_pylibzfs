#include "libzfs_core_replication.h"

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

#if 0
	/*
	 * ZFS_IOC_SEND and ZFS_IOC_SEND_NEW are registered with
	 * allow_log = B_FALSE in the kernel, so the zfs_allow_log_key TSD
	 * is never set after a send ioctl.  ZFS_IOC_LOG_HISTORY requires
	 * this TSD (returning EPERM otherwise).  Additional design work is
	 * required to support history logging for lzc send operations.
	 */
	err = py_log_history_impl(NULL, NULL,
				  "zfs send%s%s %s",
				  fromsnap ? " -i " : "",
				  fromsnap ? fromsnap : "",
				  snapname);
	if (err)
		return NULL;
#endif

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

#if 0
	/*
	 * Non-resumable receives use the legacy ZFS_IOC_RECV ioctl which is
	 * registered with allow_log = B_FALSE, so ZFS_IOC_LOG_HISTORY fails
	 * with EPERM.  Resumable/raw receives use ZFS_IOC_RECV_NEW
	 * (allow_log = B_TRUE) which already logs history in the kernel.
	 * Additional design work is required to handle the non-resumable case.
	 */
	err = py_log_history_impl(NULL, NULL,
				  "zfs receive%s %s",
				  force ? " -F" : "",
				  snapname);
	if (err)
		return NULL;
#endif

	Py_RETURN_NONE;
}
