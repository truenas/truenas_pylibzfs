#ifndef _TRUENAS_PYLIBZFS_ENUMS_H
#define _TRUENAS_PYLIBZFS_ENUMS_H

static const struct {
	zfs_error_t error;
	const char *name;
} zfserr_table[] = {
	{ EZFS_SUCCESS, "EZFS_SUCCESS" },
	{ EZFS_NOMEM, "EZFS_NOMEM" },
	{ EZFS_BADPROP, "EZFS_BADPROP" },
	{ EZFS_PROPREADONLY, "EZFS_PROPREADONLY" },
	{ EZFS_PROPTYPE, "EZFS_PROPTYPE" },
	{ EZFS_PROPNONINHERIT, "EZFS_PROPNONINHERIT" },
	{ EZFS_PROPSPACE, "EZFS_PROPSPACE" },
	{ EZFS_BADTYPE, "EZFS_BADTYPE" },
	{ EZFS_BUSY, "EZFS_BUSY" },
	{ EZFS_EXISTS, "EZFS_EXISTS" },
	{ EZFS_NOENT, "EZFS_NOENT" },
	{ EZFS_BADSTREAM, "EZFS_BADSTREAM" },
	{ EZFS_DSREADONLY, "EZFS_DSREADONLY" },
	{ EZFS_VOLTOOBIG, "EZFS_VOLTOOBIG" },
	{ EZFS_INVALIDNAME, "EZFS_INVALIDNAME" },
	{ EZFS_BADRESTORE, "EZFS_BADRESTORE" },
	{ EZFS_BADBACKUP, "EZFS_BADBACKUP" },
	{ EZFS_BADTARGET, "EZFS_BADTARGET" },
	{ EZFS_NODEVICE, "EZFS_NODEVICE" },
	{ EZFS_BADDEV, "EZFS_BADDEV" },
	{ EZFS_NOREPLICAS, "EZFS_NOREPLICAS" },
	{ EZFS_RESILVERING, "EZFS_RESILVERING" },
	{ EZFS_BADVERSION, "EZFS_BADVERSION" },
	{ EZFS_POOLUNAVAIL, "EZFS_POOLUNAVAIL" },
	{ EZFS_DEVOVERFLOW, "EZFS_DEVOVERFLOW" },
	{ EZFS_BADPATH, "EZFS_BADPATH" },
	{ EZFS_CROSSTARGET, "EZFS_CROSSTARGET" },
	{ EZFS_ZONED, "EZFS_ZONED" },
	{ EZFS_MOUNTFAILED, "EZFS_MOUNTFAILED" },
	{ EZFS_UMOUNTFAILED, "EZFS_UMOUNTFAILED" },
	{ EZFS_UNSHARENFSFAILED, "EZFS_UNSHARENFSFAILED" },
	{ EZFS_SHARENFSFAILED, "EZFS_SHARENFSFAILED" },
	{ EZFS_PERM, "EZFS_PERM" },
	{ EZFS_NOSPC, "EZFS_NOSPC" },
	{ EZFS_FAULT, "EZFS_FAULT" },
	{ EZFS_IO, "EZFS_IO" },
	{ EZFS_INTR, "EZFS_INTR" },
	{ EZFS_ISSPARE, "EZFS_ISSPARE" },
	{ EZFS_INVALCONFIG, "EZFS_INVALCONFIG" },
	{ EZFS_RECURSIVE, "EZFS_RECURSIVE" },
	{ EZFS_NOHISTORY, "EZFS_NOHISTORY" },
	{ EZFS_POOLPROPS, "EZFS_POOLPROPS" },
	{ EZFS_POOL_NOTSUP, "EZFS_POOL_NOTSUP" },
	{ EZFS_POOL_INVALARG, "EZFS_POOL_INVALARG" },
	{ EZFS_NAMETOOLONG, "EZFS_NAMETOOLONG" },
	{ EZFS_OPENFAILED, "EZFS_OPENFAILED" },
	{ EZFS_NOCAP, "EZFS_NOCAP" },
	{ EZFS_LABELFAILED, "EZFS_LABELFAILED" },
	{ EZFS_BADWHO, "EZFS_BADWHO" },
	{ EZFS_BADPERM, "EZFS_BADPERM" },
	{ EZFS_BADPERMSET, "EZFS_BADPERMSET" },
	{ EZFS_NODELEGATION, "EZFS_NODELEGATION" },
	{ EZFS_UNSHARESMBFAILED, "EZFS_UNSHARESMBFAILED" },
	{ EZFS_SHARESMBFAILED, "EZFS_SHARESMBFAILED" },
	{ EZFS_BADCACHE, "EZFS_BADCACHE" },
	{ EZFS_ISL2CACHE, "EZFS_ISL2CACHE" },
	{ EZFS_VDEVNOTSUP, "EZFS_VDEVNOTSUP" },
	{ EZFS_NOTSUP, "EZFS_NOTSUP" },
	{ EZFS_ACTIVE_SPARE, "EZFS_ACTIVE_SPARE" },
	{ EZFS_UNPLAYED_LOGS, "EZFS_UNPLAYED_LOGS" },
	{ EZFS_REFTAG_RELE, "EZFS_REFTAG_RELE" },
	{ EZFS_REFTAG_HOLD, "EZFS_REFTAG_HOLD" },
	{ EZFS_TAGTOOLONG, "EZFS_TAGTOOLONG" },
	{ EZFS_PIPEFAILED, "EZFS_PIPEFAILED" },
	{ EZFS_THREADCREATEFAILED, "EZFS_THREADCREATEFAILED" },
	{ EZFS_POSTSPLIT_ONLINE, "EZFS_POSTSPLIT_ONLINE" },
	{ EZFS_SCRUBBING, "EZFS_SCRUBBING" },
	{ EZFS_ERRORSCRUBBING, "EZFS_ERRORSCRUBBING" },
	{ EZFS_ERRORSCRUB_PAUSED, "EZFS_ERRORSCRUB_PAUSED" },
	{ EZFS_NO_SCRUB, "EZFS_NO_SCRUB" },
	{ EZFS_DIFF, "EZFS_DIFF" },
	{ EZFS_DIFFDATA, "EZFS_DIFFDATA" },
	{ EZFS_POOLREADONLY, "EZFS_POOLREADONLY" },
	{ EZFS_SCRUB_PAUSED, "EZFS_SCRUB_PAUSED" },
	{ EZFS_SCRUB_PAUSED_TO_CANCEL, "EZFS_SCRUB_PAUSED_TO_CANCEL" },
	{ EZFS_ACTIVE_POOL, "EZFS_ACTIVE_POOL" },
	{ EZFS_CRYPTOFAILED, "EZFS_CRYPTOFAILED" },
	{ EZFS_NO_PENDING, "EZFS_NO_PENDING" },
	{ EZFS_CHECKPOINT_EXISTS, "EZFS_CHECKPOINT_EXISTS" },
	{ EZFS_DISCARDING_CHECKPOINT, "EZFS_DISCARDING_CHECKPOINT" },
	{ EZFS_NO_CHECKPOINT, "EZFS_NO_CHECKPOINT" },
	{ EZFS_DEVRM_IN_PROGRESS, "EZFS_DEVRM_IN_PROGRESS" },
	{ EZFS_VDEV_TOO_BIG, "EZFS_VDEV_TOO_BIG" },
	{ EZFS_IOC_NOTSUPPORTED, "EZFS_IOC_NOTSUPPORTED" },
	{ EZFS_TOOMANY, "EZFS_TOOMANY" },
	{ EZFS_INITIALIZING, "EZFS_INITIALIZING" },
	{ EZFS_NO_INITIALIZE, "EZFS_NO_INITIALIZE" },
	{ EZFS_WRONG_PARENT, "EZFS_WRONG_PARENT" },
	{ EZFS_TRIMMING, "EZFS_TRIMMING" },
	{ EZFS_NO_TRIM, "EZFS_NO_TRIM" },
	{ EZFS_TRIM_NOTSUP, "EZFS_TRIM_NOTSUP" },
	{ EZFS_NO_RESILVER_DEFER, "EZFS_NO_RESILVER_DEFER" },
	{ EZFS_EXPORT_IN_PROGRESS, "EZFS_EXPORT_IN_PROGRESS" },
	{ EZFS_REBUILDING, "EZFS_REBUILDING" },
	{ EZFS_VDEV_NOTSUP, "EZFS_VDEV_NOTSUP" },
	{ EZFS_NOT_USER_NAMESPACE, "EZFS_NOT_USER_NAMESPACE" },
	{ EZFS_CKSUM, "EZFS_CKSUM" },
	{ EZFS_RESUME_EXISTS, "EZFS_RESUME_EXISTS" },
	{ EZFS_SHAREFAILED, "EZFS_SHAREFAILED" },
	{ EZFS_RAIDZ_EXPAND_IN_PROGRESS, "EZFS_RAIDZ_EXPAND_IN_PROGRESS" },
	{ EZFS_ASHIFT_MISMATCH, "EZFS_ASHIFT_MISMATCH" },
	{ EZFS_UNKNOWN, "EZFS_UNKNOWN" },
};

/* Sometimes we get new ZFS errors.
 * If this gets triggered, we need to update lookup table
 */
#define LAST_ZFS_ERR_T EZFS_ASHIFT_MISMATCH
_Static_assert(EZFS_UNKNOWN -1 == LAST_ZFS_ERR_T);

static const struct {
	zpool_status_t status;
	const char *name;
} zpool_status_table[] = {
	{ ZPOOL_STATUS_CORRUPT_CACHE, "ZPOOL_STATUS_CORRUPT_CACHE" },
	{ ZPOOL_STATUS_MISSING_DEV_R, "ZPOOL_STATUS_MISSING_DEV_R" },
	{ ZPOOL_STATUS_MISSING_DEV_NR, "ZPOOL_STATUS_MISSING_DEV_NR" },
	{ ZPOOL_STATUS_CORRUPT_LABEL_R, "ZPOOL_STATUS_CORRUPT_LABEL_R" },
	{ ZPOOL_STATUS_CORRUPT_LABEL_NR, "ZPOOL_STATUS_CORRUPT_LABEL_NR" },
	{ ZPOOL_STATUS_BAD_GUID_SUM, "ZPOOL_STATUS_BAD_GUID_SUM" },
	{ ZPOOL_STATUS_CORRUPT_POOL, "ZPOOL_STATUS_CORRUPT_POOL" },
	{ ZPOOL_STATUS_CORRUPT_DATA, "ZPOOL_STATUS_CORRUPT_DATA" },
	{ ZPOOL_STATUS_FAILING_DEV, "ZPOOL_STATUS_FAILING_DEV" },
	{ ZPOOL_STATUS_VERSION_NEWER, "ZPOOL_STATUS_VERSION_NEWER" },
	{ ZPOOL_STATUS_HOSTID_MISMATCH, "ZPOOL_STATUS_HOSTID_MISMATCH" },
	{ ZPOOL_STATUS_HOSTID_ACTIVE, "ZPOOL_STATUS_HOSTID_ACTIVE" },
	{ ZPOOL_STATUS_HOSTID_REQUIRED, "ZPOOL_STATUS_HOSTID_REQUIRED" },
	{ ZPOOL_STATUS_IO_FAILURE_WAIT, "ZPOOL_STATUS_IO_FAILURE_WAIT" },
	{ ZPOOL_STATUS_IO_FAILURE_CONTINUE, "ZPOOL_STATUS_IO_FAILURE_CONTINUE" },
	{ ZPOOL_STATUS_IO_FAILURE_MMP, "ZPOOL_STATUS_IO_FAILURE_MMP" },
	{ ZPOOL_STATUS_BAD_LOG, "ZPOOL_STATUS_BAD_LOG" },
	{ ZPOOL_STATUS_ERRATA, "ZPOOL_STATUS_ERRATA" },
	{ ZPOOL_STATUS_UNSUP_FEAT_READ, "ZPOOL_STATUS_UNSUP_FEAT_READ" },
	{ ZPOOL_STATUS_UNSUP_FEAT_WRITE, "ZPOOL_STATUS_UNSUP_FEAT_WRITE" },
	{ ZPOOL_STATUS_FAULTED_DEV_R, "ZPOOL_STATUS_FAULTED_DEV_R" },
	{ ZPOOL_STATUS_FAULTED_DEV_NR, "ZPOOL_STATUS_FAULTED_DEV_NR" },
	{ ZPOOL_STATUS_VERSION_OLDER, "ZPOOL_STATUS_VERSION_OLDER" },
	{ ZPOOL_STATUS_FEAT_DISABLED, "ZPOOL_STATUS_FEAT_DISABLED" },
	{ ZPOOL_STATUS_RESILVERING, "ZPOOL_STATUS_RESILVERING" },
	{ ZPOOL_STATUS_OFFLINE_DEV, "ZPOOL_STATUS_OFFLINE_DEV" },
	{ ZPOOL_STATUS_REMOVED_DEV, "ZPOOL_STATUS_REMOVED_DEV" },
	{ ZPOOL_STATUS_REBUILDING, "ZPOOL_STATUS_REBUILDING" },
	{ ZPOOL_STATUS_REBUILD_SCRUB, "ZPOOL_STATUS_REBUILD_SCRUB" },
	{ ZPOOL_STATUS_NON_NATIVE_ASHIFT, "ZPOOL_STATUS_NON_NATIVE_ASHIFT" },
	{ ZPOOL_STATUS_COMPATIBILITY_ERR, "ZPOOL_STATUS_COMPATIBILITY_ERR" },
	{ ZPOOL_STATUS_INCOMPATIBLE_FEAT, "ZPOOL_STATUS_INCOMPATIBLE_FEAT" },
	{ ZPOOL_STATUS_OK, "ZPOOL_STATUS_OK" },
};

/* Sometimes we get new zpool status codes.
 * If this gets triggered, we need to update lookup table
 */
#define LAST_ZPOOL_STATUS_T ZPOOL_STATUS_INCOMPATIBLE_FEAT
_Static_assert(ZPOOL_STATUS_OK -1 == LAST_ZPOOL_STATUS_T);

static const struct {
	zfs_type_t type;
	const char *name;
} zfs_type_table[] = {
	{ ZFS_TYPE_FILESYSTEM, "ZFS_TYPE_FILESYSTEM" },
	{ ZFS_TYPE_SNAPSHOT, "ZFS_TYPE_SNAPSHOT" },
	{ ZFS_TYPE_VOLUME, "ZFS_TYPE_VOLUME" },
	{ ZFS_TYPE_POOL, "ZFS_TYPE_POOL" },
	{ ZFS_TYPE_BOOKMARK, "ZFS_TYPE_BOOKMARK" },
	{ ZFS_TYPE_VDEV, "ZFS_TYPE_VDEV" },
	{ ZFS_TYPE_INVALID, "ZFS_TYPE_INVALID" },
};

/* Flags exposed via ZFS_IOC_GETDOSFLAGS and ZFS_IOC_SETDOSFLAGS */
static const struct {
	uint64_t flag;
	const char *name;
} zfs_dosflag_table[] = {
	{ ZFS_READONLY, "ZFS_READONLY" },
	{ ZFS_HIDDEN, "ZFS_HIDDEN" },
	{ ZFS_SYSTEM, "ZFS_SYSTEM" },
	{ ZFS_ARCHIVE, "ZFS_ARCHIVE" },
	{ ZFS_IMMUTABLE, "ZFS_IMMUTABLE" },
	{ ZFS_NOUNLINK, "ZFS_NOUNLINK" },
	{ ZFS_APPENDONLY, "ZFS_APPENDONLY" },
	{ ZFS_NODUMP, "ZFS_NODUMP" },
	{ ZFS_SPARSE, "ZFS_SPARSE" },
	{ ZFS_OFFLINE, "ZFS_SPARSE" },
};

/* vdev_aux_t enum */
static const struct {
	vdev_aux_t aux;
	const char *name;
} zfs_vdev_aux_table[] = {
	{ VDEV_AUX_NONE, "VDEV_AUX_NONE"},
	{ VDEV_AUX_OPEN_FAILED, "VDEV_AUX_OPEN_FAILED"},
	{ VDEV_AUX_CORRUPT_DATA, "VDEV_AUX_CORRUPT_DATA"},
	{ VDEV_AUX_NO_REPLICAS, "VDEV_AUX_NO_REPLICAS"},
	{ VDEV_AUX_BAD_GUID_SUM, "VDEV_AUX_BAD_GUID_SUM"},
	{ VDEV_AUX_TOO_SMALL, "VDEV_AUX_TOO_SMALL"},
	{ VDEV_AUX_BAD_LABEL, "VDEV_AUX_BAD_LABEL"},
	{ VDEV_AUX_VERSION_NEWER, "VDEV_AUX_VERSION_NEWER"},
	{ VDEV_AUX_VERSION_OLDER, "VDEV_AUX_VERSION_OLDER"},
	{ VDEV_AUX_UNSUP_FEAT, "VDEV_AUX_UNSUP_FEAT"},
	{ VDEV_AUX_SPARED, "VDEV_AUX_SPARED"},
	{ VDEV_AUX_ERR_EXCEEDED, "VDEV_AUX_ERR_EXCEEDED"},
	{ VDEV_AUX_IO_FAILURE, "VDEV_AUX_IO_FAILURE"},
	{ VDEV_AUX_BAD_LOG, "VDEV_AUX_BAD_LOG"},
	{ VDEV_AUX_EXTERNAL, "VDEV_AUX_EXTERNAL"},
	{ VDEV_AUX_SPLIT_POOL, "VDEV_AUX_SPLIT_POOL"},
	{ VDEV_AUX_BAD_ASHIFT, "VDEV_AUX_BAD_ASHIFT"},
	{ VDEV_AUX_EXTERNAL_PERSIST, "VDEV_AUX_EXTERNAL_PERSIST"},
	{ VDEV_AUX_ACTIVE, "VDEV_AUX_ACTIVE"},
	{ VDEV_AUX_CHILDREN_OFFLINE, "VDEV_AUX_CHILDREN_OFFLINE"},
	{ VDEV_AUX_ASHIFT_TOO_BIG, "VDEV_AUX_ASHIFT_TOO_BIG"}
};


/* ZFS property enum. Does not expose ones marked as obsolete or
 * not exposed to the user in the ZFS header file */
static const struct {
	zfs_prop_t prop;
	const char *name;
} zfs_prop_table[] = {
	// { ZPROP_CONT, "ZPROP_CONT" },
	{ ZFS_PROP_TYPE, "TYPE" },
	{ ZFS_PROP_CREATION, "CREATION" },
	{ ZFS_PROP_USED, "USED" },
	{ ZFS_PROP_AVAILABLE, "AVAILABLE" },
	{ ZFS_PROP_REFERENCED, "REFERENCED" },
	{ ZFS_PROP_COMPRESSRATIO, "COMPRESSRATIO" },
	{ ZFS_PROP_MOUNTED, "MOUNTED" },
	{ ZFS_PROP_ORIGIN, "ORIGIN" },
	{ ZFS_PROP_QUOTA, "QUOTA" },
	{ ZFS_PROP_RESERVATION, "RESERVATION" },
	{ ZFS_PROP_VOLSIZE, "VOLSIZE" },
	{ ZFS_PROP_VOLBLOCKSIZE, "VOLBLOCKSIZE" },
	{ ZFS_PROP_RECORDSIZE, "RECORDSIZE" },
	{ ZFS_PROP_MOUNTPOINT, "MOUNTPOINT" },
	{ ZFS_PROP_SHARENFS, "SHARENFS" },
	{ ZFS_PROP_CHECKSUM, "CHECKSUM" },
	{ ZFS_PROP_COMPRESSION, "COMPRESSION" },
	{ ZFS_PROP_ATIME, "ATIME" },
	{ ZFS_PROP_DEVICES, "DEVICES" },
	{ ZFS_PROP_EXEC, "EXEC" },
	{ ZFS_PROP_SETUID, "SETUID" },
	{ ZFS_PROP_READONLY, "READONLY" },
	{ ZFS_PROP_ZONED, "ZONED" },
	{ ZFS_PROP_SNAPDIR, "SNAPDIR" },
	{ ZFS_PROP_ACLMODE, "ACLMODE" },
	{ ZFS_PROP_ACLINHERIT, "ACLINHERIT" },
	{ ZFS_PROP_CREATETXG, "CREATETXG" },
	{ ZFS_PROP_CANMOUNT, "CANMOUNT" },
	{ ZFS_PROP_XATTR, "XATTR" },
	{ ZFS_PROP_COPIES, "COPIES" },
	{ ZFS_PROP_VERSION, "VERSION" },
	{ ZFS_PROP_UTF8ONLY, "UTF8ONLY" },
	{ ZFS_PROP_NORMALIZE, "NORMALIZE" },
	{ ZFS_PROP_CASE, "CASESENSITIVE" },
	{ ZFS_PROP_VSCAN, "VSCAN" },
	{ ZFS_PROP_NBMAND, "NBMAND" },
	{ ZFS_PROP_SHARESMB, "SHARESMB" },
	{ ZFS_PROP_REFQUOTA, "REFQUOTA" },
	{ ZFS_PROP_REFRESERVATION, "REFRESERVATION" },
	{ ZFS_PROP_GUID, "GUID" },
	{ ZFS_PROP_PRIMARYCACHE, "PRIMARYCACHE" },
	{ ZFS_PROP_SECONDARYCACHE, "SECONDARYCACHE" },
	{ ZFS_PROP_USEDSNAP, "USEDSNAP" },
	{ ZFS_PROP_USEDDS, "USEDDS" },
	{ ZFS_PROP_USEDCHILD, "USEDCHILD" },
	{ ZFS_PROP_USEDREFRESERV, "USEDREFRESERV" },
	{ ZFS_PROP_DEFER_DESTROY, "DEFER_DESTROY" },
	{ ZFS_PROP_USERREFS, "USERREFS" },
	{ ZFS_PROP_LOGBIAS, "LOGBIAS" },
	{ ZFS_PROP_OBJSETID, "OBJSETID" },
	{ ZFS_PROP_DEDUP, "DEDUP" },
	{ ZFS_PROP_MLSLABEL, "MLSLABEL" },
	{ ZFS_PROP_SYNC, "SYNC" },
	{ ZFS_PROP_DNODESIZE, "DNODESIZE" },
	{ ZFS_PROP_REFRATIO, "REFRATIO" },
	{ ZFS_PROP_WRITTEN, "WRITTEN" },
	{ ZFS_PROP_CLONES, "CLONES" },
	{ ZFS_PROP_LOGICALUSED, "LOGICALUSED" },
	{ ZFS_PROP_LOGICALREFERENCED, "LOGICALREFERENCED" },
	{ ZFS_PROP_VOLMODE, "VOLMODE" },
	{ ZFS_PROP_FILESYSTEM_LIMIT, "FILESYSTEM_LIMIT" },
	{ ZFS_PROP_SNAPSHOT_LIMIT, "SNAPSHOT_LIMIT" },
	{ ZFS_PROP_FILESYSTEM_COUNT, "FILESYSTEM_COUNT" },
	{ ZFS_PROP_SNAPSHOT_COUNT, "SNAPSHOT_COUNT" },
	{ ZFS_PROP_SNAPDEV, "SNAPDEV" },
	{ ZFS_PROP_ACLTYPE, "ACLTYPE" },
	{ ZFS_PROP_SELINUX_CONTEXT, "SELINUX_CONTEXT" },
	{ ZFS_PROP_SELINUX_FSCONTEXT, "SELINUX_FSCONTEXT" },
	{ ZFS_PROP_SELINUX_DEFCONTEXT, "SELINUX_DEFCONTEXT" },
	{ ZFS_PROP_SELINUX_ROOTCONTEXT, "SELINUX_ROOTCONTEXT" },
	{ ZFS_PROP_RELATIME, "RELATIME" },
	{ ZFS_PROP_REDUNDANT_METADATA, "REDUNDANT_METADATA" },
	{ ZFS_PROP_OVERLAY, "OVERLAY" },
	{ ZFS_PROP_PREV_SNAP, "PREV_SNAP" },
	{ ZFS_PROP_RECEIVE_RESUME_TOKEN, "RECEIVE_RESUME_TOKEN" },
	{ ZFS_PROP_ENCRYPTION, "ENCRYPTION" },
	{ ZFS_PROP_KEYLOCATION, "KEYLOCATION" },
	{ ZFS_PROP_KEYFORMAT, "KEYFORMAT" },
	{ ZFS_PROP_PBKDF2_SALT, "PBKDF2_SALT" },
	{ ZFS_PROP_PBKDF2_ITERS, "PBKDF2_ITERS" },
	{ ZFS_PROP_ENCRYPTION_ROOT, "ENCRYPTION_ROOT" },
	{ ZFS_PROP_KEY_GUID, "KEY_GUID" },
	{ ZFS_PROP_KEYSTATUS, "KEYSTATUS" },
	{ ZFS_PROP_SPECIAL_SMALL_BLOCKS, "SPECIAL_SMALL_BLOCKS" },
	{ ZFS_PROP_REDACTED, "REDACTED" },
	{ ZFS_PROP_REDACT_SNAPS, "REDACT_SNAPS" },
	{ ZFS_PROP_SNAPSHOTS_CHANGED, "SNAPSHOTS_CHANGED" },
	{ ZFS_PROP_PREFETCH, "PREFETCH" },
	{ ZFS_PROP_VOLTHREADING, "VOLTHREADING" },
	{ ZFS_PROP_DIRECT, "DIRECT" },
	{ ZFS_PROP_LONGNAME, "LONGNAME" },
	{ ZFS_PROP_DEFAULTUSERQUOTA, "DEFAULTUSERQUOTA" },
	{ ZFS_PROP_DEFAULTGROUPQUOTA, "DEFAULTGROUPQUOTA" },
	{ ZFS_PROP_DEFAULTPROJECTQUOTA, "DEFAULTPROJECTQUOTA" },
	{ ZFS_PROP_DEFAULTUSEROBJQUOTA, "DEFAULTUSEROBJQUOTA" },
	{ ZFS_PROP_DEFAULTGROUPOBJQUOTA, "DEFAULTGROUPOBJQUOTA" },
	{ ZFS_PROP_DEFAULTPROJECTOBJQUOTA, "DEFAULTPROJECTOBJQUOTA" },
};
_Static_assert(ZFS_NUM_PROPS -1 == ZFS_PROP_DEFAULTPROJECTOBJQUOTA);

static const struct {
	zpool_prop_t prop;
	const char *name;
} zpool_prop_table[] = {
	{ ZPOOL_PROP_INVAL, "INVAL" },
	{ ZPOOL_PROP_NAME, "NAME" },
	{ ZPOOL_PROP_SIZE, "SIZE" },
	{ ZPOOL_PROP_CAPACITY, "CAPACITY" },
	{ ZPOOL_PROP_ALTROOT, "ALTROOT" },
	{ ZPOOL_PROP_HEALTH, "HEALTH" },
	{ ZPOOL_PROP_GUID, "GUID" },
	{ ZPOOL_PROP_VERSION, "VERSION" },
	{ ZPOOL_PROP_BOOTFS, "BOOTFS" },
	{ ZPOOL_PROP_DELEGATION, "DELEGATION" },
	{ ZPOOL_PROP_AUTOREPLACE, "AUTOREPLACE" },
	{ ZPOOL_PROP_CACHEFILE, "CACHEFILE" },
	{ ZPOOL_PROP_FAILUREMODE, "FAILUREMODE" },
	{ ZPOOL_PROP_LISTSNAPS, "LISTSNAPS" },
	{ ZPOOL_PROP_AUTOEXPAND, "AUTOEXPAND" },
	{ ZPOOL_PROP_DEDUPDITTO, "DEDUPDITTO" },
	{ ZPOOL_PROP_DEDUPRATIO, "DEDUPRATIO" },
	{ ZPOOL_PROP_FREE, "FREE" },
	{ ZPOOL_PROP_ALLOCATED, "ALLOCATED" },
	{ ZPOOL_PROP_READONLY, "READONLY" },
	{ ZPOOL_PROP_ASHIFT, "ASHIFT" },
	{ ZPOOL_PROP_COMMENT, "COMMENT" },
	{ ZPOOL_PROP_EXPANDSZ, "EXPANDSZ" },
	{ ZPOOL_PROP_FREEING, "FREEING" },
	{ ZPOOL_PROP_FRAGMENTATION, "FRAGMENTATION" },
	{ ZPOOL_PROP_LEAKED, "LEAKED" },
	{ ZPOOL_PROP_MAXBLOCKSIZE, "MAXBLOCKSIZE" },
	{ ZPOOL_PROP_TNAME, "TNAME" },
	{ ZPOOL_PROP_MAXDNODESIZE, "MAXDNODESIZE" },
	{ ZPOOL_PROP_MULTIHOST, "MULTIHOST" },
	{ ZPOOL_PROP_CHECKPOINT, "CHECKPOINT" },
	{ ZPOOL_PROP_LOAD_GUID, "LOAD_GUID" },
	{ ZPOOL_PROP_AUTOTRIM, "AUTOTRIM" },
	{ ZPOOL_PROP_COMPATIBILITY, "COMPATIBILITY" },
	{ ZPOOL_PROP_BCLONEUSED, "BCLONEUSED" },
	{ ZPOOL_PROP_BCLONESAVED, "BCLONESAVED" },
	{ ZPOOL_PROP_BCLONERATIO, "BCLONERATIO" },
	{ ZPOOL_PROP_DEDUP_TABLE_SIZE, "DEDUP_TABLE_SIZE" },
	{ ZPOOL_PROP_DEDUP_TABLE_QUOTA, "DEDUP_TABLE_QUOTA" },
	{ ZPOOL_PROP_DEDUPCACHED, "DEDUPCACHED" },
	{ ZPOOL_PROP_LAST_SCRUBBED_TXG, "LAST_SCRUBBED_TXG" },
};
_Static_assert(ZPOOL_NUM_PROPS -1 == ZPOOL_PROP_LAST_SCRUBBED_TXG);

static const struct {
	zprop_source_t sourcetype;
	const char *name;
} zprop_source_table[] = {
	{ ZPROP_SRC_NONE, "NONE" },
	{ ZPROP_SRC_DEFAULT, "DEFAULT" },
	{ ZPROP_SRC_TEMPORARY, "TEMPORARY" },
	{ ZPROP_SRC_LOCAL, "LOCAL" },
	{ ZPROP_SRC_INHERITED, "INHERITED" },
	{ ZPROP_SRC_RECEIVED, "RECEIVED" },
};
/* Verify that potential sources haven't changed */
_Static_assert(ZPROP_SRC_ALL == 0x3f);


static const struct {
	zfs_userquota_prop_t uquota_type;
	const char *name;
} zfs_uquota_table[] = {
	{ ZFS_PROP_USERUSED, "USER_USED" },
	{ ZFS_PROP_USERQUOTA, "USER_QUOTA" },
	{ ZFS_PROP_GROUPUSED, "GROUP_USED" },
	{ ZFS_PROP_GROUPQUOTA, "GROUP_QUOTA" },
	{ ZFS_PROP_USEROBJUSED, "USEROBJ_USED" },
	{ ZFS_PROP_USEROBJQUOTA, "USEROBJ_QUOTA" },
	{ ZFS_PROP_GROUPOBJUSED, "GROUPOBJ_USED" },
	{ ZFS_PROP_GROUPOBJQUOTA, "GROUPOBJ_QUOTA" },
	{ ZFS_PROP_PROJECTUSED, "PROJECT_USED" },
	{ ZFS_PROP_PROJECTQUOTA, "PROJECT_QUOTA" },
	{ ZFS_PROP_PROJECTOBJUSED, "PROJECTOBJ_USED" },
	{ ZFS_PROP_PROJECTOBJQUOTA, "PROJECTOBJ_QUOTA" },
};
_Static_assert(ZFS_NUM_USERQUOTA_PROPS -1 == ZFS_PROP_PROJECTOBJQUOTA);


#endif /* _TRUENAS_PYLIBZFS_ENUMS_H */
