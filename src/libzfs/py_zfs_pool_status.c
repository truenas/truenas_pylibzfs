#include "../truenas_pylibzfs.h"

/*
 * ZFS pool status implementation for module
 *
 * The pool status is implemented as a struct sequence object in the C API.
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
"status is " PYLIBZFS_MODULE_NAME ".ZPOOLStatus.ZPOOL_STATUS_OK\n"
"Possible administrative action(s) that may be taken to resolve the status\n"
"issue. If the pool status is "
PYLIBZFS_MODULE_NAME ".ZPOOLStatus.ZPOOL_STATUS_OK\n"
"then this field will be None.\n"
);

PyDoc_STRVAR(py_pool_status_files__doc__,
"Tuple containing absolute paths to files located in the pool that contain\n"
"errors.\n"
);

PyDoc_STRVAR(py_pool_status_vdevs__doc__,
"Struct sequence object containing information and status of vdevs making up pool\n"
);

PyStructSequence_Field struct_pool_status_prop [] = {
	{"status", py_pool_status_status__doc__},
	{"reason", py_pool_status_reason__doc__},
	{"action", py_pool_status_action__doc__},
	{"message", py_pool_status_message__doc__},
	{"corrupted_files", py_pool_status_files__doc__},
	{"vdevs", py_pool_status_vdevs__doc__},
	{0},
};

PyStructSequence_Desc struct_pool_status_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_zpool_status",
	.fields = struct_pool_status_prop,
	.doc = "Python ZFS pool status structure",
	.n_in_sequence = 6
};

PyStructSequence_Field struct_vdev_status_prop [] = {
	{"name", "name of the vdev"},
	{"vdev_type", "type of the vdev"},
	{"guid", "GUID for the vdev"},
	{"state", "State of the vdev"},
	{"read_errors", "Number of read errors"},
	{"write_errors", "Number of write errors"},
	{"checksum_errors", "Number of checksum errors"},
	{"dio_verify_errors", "Number of O_DIRECT checksum errors"},
	{"slow_ios", "Number of slow I/Os. Leaf-only"},
	{"children", "Tuple of vdevs that make up this vdev (if applicable)"},
	{0},
};

PyStructSequence_Desc struct_vdev_status_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_vdev_status",
	.fields = struct_vdev_status_prop,
	.doc = "Python pool vdev status structure",
	.n_in_sequence = 10
};

static
boolean_t parse_vdev_stats(py_zfs_pool_t *pypool,
			   nvlist_t *nv,
			   vdev_stat_t *vs,
			   boolean_t has_children,
			   uint64_t guid,
			   PyObject *pyvdev)
{
	PyObject *val = NULL;

	val = PyLong_FromUnsignedLong(guid);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, 2, val);

	// Read errors
	val = PyLong_FromUnsignedLong(vs->vs_read_errors);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, 4, val);

	// Write errors
	val = PyLong_FromUnsignedLong(vs->vs_write_errors);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, 5, val);

	// checksum errors
	val = PyLong_FromUnsignedLong(vs->vs_checksum_errors);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, 6, val);

	val = PyLong_FromUnsignedLong(vs->vs_dio_verify_errors);
	if (val == NULL)
		return B_FALSE;

	PyStructSequence_SetItem(pyvdev, 7, val);

	if (has_children) {
		// slow ios counter
		PyStructSequence_SetItem(pyvdev, 8, Py_NewRef(Py_None));
	} else {
		// slow ios counter
		val = PyLong_FromUnsignedLong(vs->vs_slow_ios);
		if (val == NULL)
			return B_FALSE;

		PyStructSequence_SetItem(pyvdev, 8, val);
	}

	return B_TRUE;
}

static
PyObject *gen_vdev_status_nvlist(pylibzfs_state_t *state,
				 py_zfs_pool_t *pypool,
				 nvlist_t *nv,
				 uint depth)
{
	nvlist_t **child;
	uint_t vsc, children;
	const char *type;
	uint64_t guid;
	char *vname = NULL;
	PyObject *out = NULL;
	PyObject *name = NULL;
	PyObject *vdev_type = NULL;
	PyObject *vdev_state = NULL;
	vdev_stat_t *vs;

	Py_BEGIN_ALLOW_THREADS
	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;
	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) == 0);
	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);

	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0);
	PY_ZFS_LOCK(pypool->pylibzfsp);
	vname = zpool_vdev_name(pypool->pylibzfsp->lzh,
				pypool->zhp, nv,
				VDEV_NAME_TYPE_ID);
	PY_ZFS_UNLOCK(pypool->pylibzfsp);

	Py_END_ALLOW_THREADS

	if (strcmp(type, VDEV_TYPE_INDIRECT) == 0)
		return NULL;

	out = PyStructSequence_New(state->struct_vdev_status_type);
	if (out == NULL)
		goto fail;

	name = PyUnicode_FromString(vname);
	if (name == NULL)
		goto fail;

	// libzfs return copy of vdev name and so we need to free it.
	free(name);
	name = NULL;
	PyStructSequence_SetItem(out, 0, name);

	vdev_type = PyUnicode_FromString(type);
	if (vdev_type == NULL)
		goto fail;

	PyStructSequence_SetItem(out, 1, vdev_type);

	vdev_state = PyObject_CallFunction(state->vdev_state_enum,
					   "i", vs->vs_state);
	if (vdev_state == NULL)
		goto fail;

	PyStructSequence_SetItem(out, 3, vdev_state);

	if (!parse_vdev_stats(pypool, nv, vs, children, guid, out))
		goto fail;

	if (children == 0)
		PyStructSequence_SetItem(out, 9, Py_NewRef(Py_None));
	else {
		uint c;
		int err;
		PyObject *cl = PyList_New(0);
		PyObject *child_tuple = NULL;
		if (cl == NULL)
			goto fail;

		for (c = 0; c < children; c++) {
			PyObject *cvdev = NULL;
			uint64_t islog = B_FALSE, ishole = B_FALSE;
			nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
					     &islog);
			nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
					     &ishole);
			if (islog || ishole)
				continue;

			if (nvlist_exists(child[c], ZPOOL_CONFIG_ALLOCATION_BIAS))
				continue;

			cvdev = gen_vdev_status_nvlist(state, pypool,
						       child[c], depth + 1);
			if (cvdev == NULL) {
				Py_CLEAR(cl);
				goto fail;
			}

			err = PyList_Append(cl, cvdev);

			Py_XDECREF(cvdev);
			if (err) {
				Py_CLEAR(cl);
				goto fail;
			}
		}

		child_tuple = PyList_AsTuple(cl);
		Py_CLEAR(cl);
		if (child_tuple == NULL)
			goto fail;

		PyStructSequence_SetItem(out, 9, child_tuple);
	}

	return out;
fail:
	Py_CLEAR(out);
	free(name);  // allocated by libzfs using system malloc
	return NULL;
}

static
PyObject *pypool_vdev_status(py_zfs_pool_t *pypool)
{
	PyObject *out = NULL;
	nvlist_t *config, *nvroot;
	pylibzfs_state_t *state = py_get_module_state(pypool->pylibzfsp);

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

	out = gen_vdev_status_nvlist(state, pypool, nvroot, 0);
	fnvlist_free(nvroot);
	return out;
}

// generate tuple of corrupted files on pool
#define E_PATHMAX MAXPATHLEN * 2
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
	if (pathbuf == NULL)
		goto done;

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

static
PyObject *populate_status_struct(py_zfs_pool_t *pypool,
				 zpool_status_t reason,
				 zpool_errata_t errata,
				 const char *msgid)
{
	PyObject *out = NULL;
	PyObject *pyreason = NULL;
	PyObject *pyaction = NULL;
	PyObject *pytmp = NULL;
	PyObject *pyenum = NULL;
	PyObject *pymsg = NULL;
	PyObject *pyfiles = NULL;
	PyObject *pyvdevs = NULL;
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
		    "Sufficient replicas exist for the pool to continue\n\t"
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
		    "device needs to be replaced, and clear the errors using"
		    "'zpool clear' or replace the device with 'zpool "
		    "replace'.");
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		pyreason = PyUnicode_FromString("One or more devices has "
		    "been taken offline by the administrator.\n\tSufficient "
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
		    "using zpool online' or replace the device with 'zpool "
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
		    "device(s) and run 'zpool online',or ignore the intent log "
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

	pyvdevs = pypool_vdev_status(pypool);
	if (pyvdevs == NULL)
		goto fail;

	PyStructSequence_SET_ITEM(out, 0, pyenum);
	PyStructSequence_SET_ITEM(out, 1, pyreason);
	PyStructSequence_SET_ITEM(out, 2, pyaction);
	PyStructSequence_SET_ITEM(out, 3, pymsg);
	PyStructSequence_SET_ITEM(out, 4, pyfiles);
	PyStructSequence_SET_ITEM(out, 5, pyvdevs);

	return out;

fail:
	Py_CLEAR(pyreason);
	Py_CLEAR(pyaction);
	Py_CLEAR(pyenum);
	Py_CLEAR(out);
	return NULL;
}

PyObject *py_get_pool_status(py_zfs_pool_t *pypool)
{
	zpool_status_t reason;
	zpool_errata_t errata;
	const char *msgid;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(pypool->pylibzfsp);
	reason = zpool_get_status(pypool->zhp, &msgid, &errata);
	PY_ZFS_UNLOCK(pypool->pylibzfsp);
	Py_END_ALLOW_THREADS

	return populate_status_struct(pypool, reason, errata, msgid);
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
}
