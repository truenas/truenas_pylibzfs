#include "../truenas_pylibzfs.h"
#include "../common/py_local_replicate.h"

#include <stdio.h>

/*
 * Resource-object local_replicate: replicates a snapshot of `self`
 * (a filesystem or volume) plus its descendants to a destination
 * dataset on the same host.  This is the libzfs-pure path -
 * zfs_send + zfs_receive, equivalent to `zfs send -Rp` piped into
 * `zfs recv`.  See PyDoc_STRVAR(py_zfs_local_replicate__doc__) in
 * truenas_pylibzfs.h for the user-facing description.
 *
 * All replication life cycle (cmdprops, pre-flight, pipe, send +
 * recv workers, progress poller, error capture, history) lives in
 * src/common/py_local_replicate.c.  This translation unit just
 * parses kwargs, audits, and dispatches.
 */

PyObject *
py_zfs_local_replicate(py_zfs_resource_t *res, PyObject *args_unused,
		       PyObject *kwargs)
{
	char *kwnames[] = {
		"tosnap", "dest", "fromsnap", "send_flags", "props",
		"exclude_props", "force", "raw", "nomount",
		"progress_callback", "progress_state",
		"progress_interval_seconds", NULL
	};
	const char *tosnap = NULL;
	const char *dest = NULL;
	const char *fromsnap = NULL;
	int send_flags_int = 0;
	PyObject *py_props = NULL;
	PyObject *py_exclude = NULL;
	int force_int = 0;
	int raw_int = 0;
	int nomount_int = 0;
	PyObject *py_progress_cb = NULL;
	PyObject *py_progress_state = NULL;
	int progress_interval = 1;

	py_zfs_obj_t *obj = &res->obj;
	const char *src_name;
	char source_buf[ZFS_MAX_DATASET_NAME_LEN];
	char fromsnap_buf[ZFS_MAX_DATASET_NAME_LEN];
	struct local_replicate_args la;

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$zzziOOpppOOi",
					 kwnames,
					 &tosnap, &dest, &fromsnap,
					 &send_flags_int, &py_props,
					 &py_exclude,
					 &force_int, &raw_int, &nomount_int,
					 &py_progress_cb, &py_progress_state,
					 &progress_interval))
		return NULL;

	if (tosnap == NULL) {
		PyErr_SetString(PyExc_ValueError, "tosnap is required");
		return NULL;
	}
	if (dest == NULL) {
		PyErr_SetString(PyExc_ValueError, "dest is required");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSResource.local_replicate",
			"OssziOOOOO", obj->name,
			tosnap, dest,
			fromsnap ? fromsnap : "",
			send_flags_int,
			force_int ? Py_True : Py_False,
			raw_int ? Py_True : Py_False,
			py_props ? py_props : Py_None,
			py_exclude ? py_exclude : Py_None,
			nomount_int ? Py_True : Py_False) < 0)
		return NULL;

	/*
	 * Build full snapshot names from the resource's dataset name +
	 * the suffix-only kwargs.  source_buf / fromsnap_buf live on
	 * this stack frame for the duration of py_local_replicate.
	 */
	src_name = zfs_get_name(obj->zhp);
	snprintf(source_buf, sizeof(source_buf), "%s@%s", src_name, tosnap);
	if (fromsnap != NULL)
		snprintf(fromsnap_buf, sizeof(fromsnap_buf), "%s@%s",
			 src_name, fromsnap);

	la = (struct local_replicate_args){
		.mode = LOCAL_REPLICATE_ZFS,
		.source = source_buf,
		.fromsnap = fromsnap ? fromsnap_buf : NULL,
		.dest = dest,
		.send_flags_int = send_flags_int,
		.raw = raw_int != 0,
		.force = force_int != 0,
		.recursive = obj->ctype == ZFS_TYPE_FILESYSTEM,
		.nomount = nomount_int != 0,
		.py_exclude = py_exclude,
		.skip_history = !obj->pylibzfsp->history,
		.history_prefix = obj->pylibzfsp->history_prefix,
		.py_props = py_props,
		.py_progress_cb = py_progress_cb,
		.py_progress_state = py_progress_state,
		.progress_interval_seconds = progress_interval,
	};

	return py_local_replicate(&la);
}
