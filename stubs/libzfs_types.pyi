from collections.abc import Iterable, Iterator, Sequence
import enum
from typing import Any, Literal


# ---------------------------------------------------------------------------
# Enums (dual-registered: also available at truenas_pylibzfs.<Name>)
# ---------------------------------------------------------------------------

class PropertySource(enum.IntFlag):
    DEFAULT: int
    INHERITED: int
    LOCAL: int
    NONE: int
    RECEIVED: int
    TEMPORARY: int
    def __format__(self, format_spec: str, /) -> str: ...

class ScanFunction(enum.IntEnum):
    NONE: int
    SCRUB: int
    RESILVER: int
    ERRORSCRUB: int
    def __format__(self, format_spec: str, /) -> str: ...

class ScanScrubCmd(enum.IntEnum):
    NORMAL: int
    PAUSE: int
    FROM_LAST_TXG: int
    def __format__(self, format_spec: str, /) -> str: ...

class ScanState(enum.IntEnum):
    NONE: int
    SCANNING: int
    FINISHED: int
    CANCELED: int
    ERRORSCRUBBING: int
    def __format__(self, format_spec: str, /) -> str: ...

class VDevAuxState(enum.IntEnum):
    VDEV_AUX_ACTIVE: int
    VDEV_AUX_ASHIFT_TOO_BIG: int
    VDEV_AUX_BAD_ASHIFT: int
    VDEV_AUX_BAD_GUID_SUM: int
    VDEV_AUX_BAD_LABEL: int
    VDEV_AUX_BAD_LOG: int
    VDEV_AUX_CHILDREN_OFFLINE: int
    VDEV_AUX_CORRUPT_DATA: int
    VDEV_AUX_ERR_EXCEEDED: int
    VDEV_AUX_EXTERNAL: int
    VDEV_AUX_EXTERNAL_PERSIST: int
    VDEV_AUX_IO_FAILURE: int
    VDEV_AUX_NONE: int
    VDEV_AUX_NO_REPLICAS: int
    VDEV_AUX_OPEN_FAILED: int
    VDEV_AUX_SPARED: int
    VDEV_AUX_SPLIT_POOL: int
    VDEV_AUX_TOO_SMALL: int
    VDEV_AUX_UNSUP_FEAT: int
    VDEV_AUX_VERSION_NEWER: int
    VDEV_AUX_VERSION_OLDER: int
    def __format__(self, format_spec: str, /) -> str: ...

class VDevState(enum.IntEnum):
    CANT_OPEN: int
    CLOSED: int
    DEGRADED: int
    FAULTED: int
    OFFLINE: int
    ONLINE: int
    REMOVED: int
    UNKNOWN: int
    def __format__(self, format_spec: str, /) -> str: ...

class VDevType(enum.StrEnum):
    """Vdev type for use with create_vdev_spec()."""
    DISK = "disk"
    FILE = "file"
    MIRROR = "mirror"
    RAIDZ1 = "raidz1"
    RAIDZ2 = "raidz2"
    RAIDZ3 = "raidz3"
    DRAID1 = "draid1"
    DRAID2 = "draid2"
    DRAID3 = "draid3"
    def __format__(self, format_spec: str, /) -> str: ...

class ZFSDOSFlag(enum.IntFlag):
    ZFS_APPENDONLY: int
    ZFS_ARCHIVE: int
    ZFS_HIDDEN: int
    ZFS_IMMUTABLE: int
    ZFS_NODUMP: int
    ZFS_NOUNLINK: int
    ZFS_READONLY: int
    ZFS_SPARSE: int
    ZFS_SYSTEM: int
    def __format__(self, format_spec: str, /) -> str: ...

class ZFSError(enum.IntEnum):
    EZFS_ACTIVE_POOL: int
    EZFS_ACTIVE_SPARE: int
    EZFS_ASHIFT_MISMATCH: int
    EZFS_BADBACKUP: int
    EZFS_BADCACHE: int
    EZFS_BADDEV: int
    EZFS_BADPATH: int
    EZFS_BADPERM: int
    EZFS_BADPERMSET: int
    EZFS_BADPROP: int
    EZFS_BADRESTORE: int
    EZFS_BADSTREAM: int
    EZFS_BADTARGET: int
    EZFS_BADTYPE: int
    EZFS_BADVERSION: int
    EZFS_BADWHO: int
    EZFS_BUSY: int
    EZFS_CHECKPOINT_EXISTS: int
    EZFS_CKSUM: int
    EZFS_CROSSTARGET: int
    EZFS_CRYPTOFAILED: int
    EZFS_DEVOVERFLOW: int
    EZFS_DEVRM_IN_PROGRESS: int
    EZFS_DIFF: int
    EZFS_DIFFDATA: int
    EZFS_DISCARDING_CHECKPOINT: int
    EZFS_DSREADONLY: int
    EZFS_ERRORSCRUBBING: int
    EZFS_ERRORSCRUB_PAUSED: int
    EZFS_EXISTS: int
    EZFS_EXPORT_IN_PROGRESS: int
    EZFS_FAULT: int
    EZFS_INITIALIZING: int
    EZFS_INTR: int
    EZFS_INVALCONFIG: int
    EZFS_INVALIDNAME: int
    EZFS_IO: int
    EZFS_IOC_NOTSUPPORTED: int
    EZFS_ISL2CACHE: int
    EZFS_ISSPARE: int
    EZFS_LABELFAILED: int
    EZFS_MOUNTFAILED: int
    EZFS_NAMETOOLONG: int
    EZFS_NOCAP: int
    EZFS_NODELEGATION: int
    EZFS_NODEVICE: int
    EZFS_NOENT: int
    EZFS_NOHISTORY: int
    EZFS_NOMEM: int
    EZFS_NOREPLICAS: int
    EZFS_NOSPC: int
    EZFS_NOTSUP: int
    EZFS_NOT_USER_NAMESPACE: int
    EZFS_NO_CHECKPOINT: int
    EZFS_NO_INITIALIZE: int
    EZFS_NO_PENDING: int
    EZFS_NO_RESILVER_DEFER: int
    EZFS_NO_SCRUB: int
    EZFS_NO_TRIM: int
    EZFS_OPENFAILED: int
    EZFS_PERM: int
    EZFS_PIPEFAILED: int
    EZFS_POOLPROPS: int
    EZFS_POOLREADONLY: int
    EZFS_POOLUNAVAIL: int
    EZFS_POOL_INVALARG: int
    EZFS_POOL_NOTSUP: int
    EZFS_POSTSPLIT_ONLINE: int
    EZFS_PROPNONINHERIT: int
    EZFS_PROPREADONLY: int
    EZFS_PROPSPACE: int
    EZFS_PROPTYPE: int
    EZFS_RAIDZ_EXPAND_IN_PROGRESS: int
    EZFS_REBUILDING: int
    EZFS_RECURSIVE: int
    EZFS_REFTAG_HOLD: int
    EZFS_REFTAG_RELE: int
    EZFS_RESILVERING: int
    EZFS_RESUME_EXISTS: int
    EZFS_SCRUBBING: int
    EZFS_SCRUB_PAUSED: int
    EZFS_SCRUB_PAUSED_TO_CANCEL: int
    EZFS_SHAREFAILED: int
    EZFS_SHARENFSFAILED: int
    EZFS_SHARESMBFAILED: int
    EZFS_SUCCESS: int
    EZFS_TAGTOOLONG: int
    EZFS_THREADCREATEFAILED: int
    EZFS_TOOMANY: int
    EZFS_TRIMMING: int
    EZFS_TRIM_NOTSUP: int
    EZFS_UMOUNTFAILED: int
    EZFS_UNKNOWN: int
    EZFS_UNPLAYED_LOGS: int
    EZFS_UNSHARENFSFAILED: int
    EZFS_UNSHARESMBFAILED: int
    EZFS_VDEVNOTSUP: int
    EZFS_VDEV_NOTSUP: int
    EZFS_VDEV_TOO_BIG: int
    EZFS_VOLTOOBIG: int
    EZFS_WRONG_PARENT: int
    EZFS_ZONED: int
    def __format__(self, format_spec: str, /) -> str: ...

class ZFSProperty(enum.IntEnum):
    ACLINHERIT: int
    ACLMODE: int
    ACLTYPE: int
    ATIME: int
    AVAILABLE: int
    CANMOUNT: int
    CASESENSITIVITY: int
    CHECKSUM: int
    CLONES: int
    COMPRESSION: int
    COMPRESSRATIO: int
    CONTEXT: int
    COPIES: int
    CREATETXG: int
    CREATION: int
    DEDUP: int
    DEFAULTGROUPOBJQUOTA: int
    DEFAULTGROUPQUOTA: int
    DEFAULTPROJECTOBJQUOTA: int
    DEFAULTPROJECTQUOTA: int
    DEFAULTUSEROBJQUOTA: int
    DEFAULTUSERQUOTA: int
    DEFCONTEXT: int
    DEFER_DESTROY: int
    DEVICES: int
    DIRECT: int
    DNODESIZE: int
    ENCRYPTION: int
    ENCRYPTIONROOT: int
    EXEC: int
    FILESYSTEM_COUNT: int
    FILESYSTEM_LIMIT: int
    FSCONTEXT: int
    GUID: int
    KEYFORMAT: int
    KEYLOCATION: int
    KEYSTATUS: int
    LOGBIAS: int
    LOGICALREFERENCED: int
    LOGICALUSED: int
    LONGNAME: int
    MLSLABEL: int
    MOUNTED: int
    MOUNTPOINT: int
    NBMAND: int
    NORMALIZATION: int
    OBJSETID: int
    ORIGIN: int
    OVERLAY: int
    PBKDF2ITERS: int
    PREFETCH: int
    PRIMARYCACHE: int
    QUOTA: int
    READONLY: int
    RECEIVE_RESUME_TOKEN: int
    RECORDSIZE: int
    REDACT_SNAPS: int
    REDUNDANT_METADATA: int
    REFCOMPRESSRATIO: int
    REFERENCED: int
    REFQUOTA: int
    REFRESERVATION: int
    RELATIME: int
    RESERVATION: int
    ROOTCONTEXT: int
    SECONDARYCACHE: int
    SETUID: int
    SHARENFS: int
    SHARESMB: int
    SNAPDEV: int
    SNAPDIR: int
    SNAPSHOTS_CHANGED: int
    SNAPSHOT_COUNT: int
    SNAPSHOT_LIMIT: int
    SPECIAL_SMALL_BLOCKS: int
    SYNC: int
    TYPE: int
    USED: int
    USEDBYCHILDREN: int
    USEDBYDATASET: int
    USEDBYREFRESERVATION: int
    USEDBYSNAPSHOTS: int
    USERREFS: int
    UTF8ONLY: int
    VERSION: int
    VOLBLOCKSIZE: int
    VOLMODE: int
    VOLSIZE: int
    VOLTHREADING: int
    VSCAN: int
    WRITTEN: int
    XATTR: int
    ZONED: int
    def __format__(self, format_spec: str, /) -> str: ...

class ZFSType(enum.IntEnum):
    ZFS_TYPE_BOOKMARK: int
    ZFS_TYPE_FILESYSTEM: int
    ZFS_TYPE_INVALID: int
    ZFS_TYPE_POOL: int
    ZFS_TYPE_SNAPSHOT: int
    ZFS_TYPE_VDEV: int
    ZFS_TYPE_VOLUME: int
    def __format__(self, format_spec: str, /) -> str: ...

class ZFSUserQuota(enum.IntEnum):
    GROUPOBJ_QUOTA: int
    GROUPOBJ_USED: int
    GROUP_QUOTA: int
    GROUP_USED: int
    PROJECTOBJ_QUOTA: int
    PROJECTOBJ_USED: int
    PROJECT_QUOTA: int
    PROJECT_USED: int
    USEROBJ_QUOTA: int
    USEROBJ_USED: int
    USER_QUOTA: int
    USER_USED: int
    def __format__(self, format_spec: str, /) -> str: ...

class ZPOOLProperty(enum.IntEnum):
    ALLOCATED: int
    ALTROOT: int
    ASHIFT: int
    AUTOEXPAND: int
    AUTOREPLACE: int
    AUTOTRIM: int
    AVAILABLE: int
    BCLONERATIO: int
    BCLONESAVED: int
    BCLONEUSED: int
    BOOTFS: int
    CACHEFILE: int
    CAPACITY: int
    CHECKPOINT: int
    COMMENT: int
    COMPATIBILITY: int
    DEDUPCACHED: int
    DEDUPDITTO: int
    DEDUPRATIO: int
    DEDUP_TABLE_QUOTA: int
    DEDUP_TABLE_SIZE: int
    DEDUPSAVED: int
    DEDUPUSED: int
    CLASS_DEDUP_ALLOCATED: int
    CLASS_DEDUP_AVAILABLE: int
    CLASS_DEDUP_CAPACITY: int
    CLASS_DEDUP_EXPANDSIZE: int
    CLASS_DEDUP_FRAGMENTATION: int
    CLASS_DEDUP_FREE: int
    CLASS_DEDUP_SIZE: int
    CLASS_DEDUP_USABLE: int
    CLASS_DEDUP_USED: int
    CLASS_ELOG_ALLOCATED: int
    CLASS_ELOG_AVAILABLE: int
    CLASS_ELOG_CAPACITY: int
    CLASS_ELOG_EXPANDSIZE: int
    CLASS_ELOG_FRAGMENTATION: int
    CLASS_ELOG_FREE: int
    CLASS_ELOG_SIZE: int
    CLASS_ELOG_USABLE: int
    CLASS_ELOG_USED: int
    CLASS_LOG_ALLOCATED: int
    CLASS_LOG_AVAILABLE: int
    CLASS_LOG_CAPACITY: int
    CLASS_LOG_EXPANDSIZE: int
    CLASS_LOG_FRAGMENTATION: int
    CLASS_LOG_FREE: int
    CLASS_LOG_SIZE: int
    CLASS_LOG_USABLE: int
    CLASS_LOG_USED: int
    CLASS_NORMAL_ALLOCATED: int
    CLASS_NORMAL_AVAILABLE: int
    CLASS_NORMAL_CAPACITY: int
    CLASS_NORMAL_EXPANDSIZE: int
    CLASS_NORMAL_FRAGMENTATION: int
    CLASS_NORMAL_FREE: int
    CLASS_NORMAL_SIZE: int
    CLASS_NORMAL_USABLE: int
    CLASS_NORMAL_USED: int
    CLASS_SPECIAL_ALLOCATED: int
    CLASS_SPECIAL_AVAILABLE: int
    CLASS_SPECIAL_CAPACITY: int
    CLASS_SPECIAL_ELOG_ALLOCATED: int
    CLASS_SPECIAL_ELOG_AVAILABLE: int
    CLASS_SPECIAL_ELOG_CAPACITY: int
    CLASS_SPECIAL_ELOG_EXPANDSIZE: int
    CLASS_SPECIAL_ELOG_FRAGMENTATION: int
    CLASS_SPECIAL_ELOG_FREE: int
    CLASS_SPECIAL_ELOG_SIZE: int
    CLASS_SPECIAL_ELOG_USABLE: int
    CLASS_SPECIAL_ELOG_USED: int
    CLASS_SPECIAL_EXPANDSIZE: int
    CLASS_SPECIAL_FRAGMENTATION: int
    CLASS_SPECIAL_FREE: int
    CLASS_SPECIAL_SIZE: int
    CLASS_SPECIAL_USABLE: int
    CLASS_SPECIAL_USED: int
    DELEGATION: int
    EXPANDSIZE: int
    FAILMODE: int
    FRAGMENTATION: int
    FREE: int
    FREEING: int
    GUID: int
    HEALTH: int
    INVAL: int
    LAST_SCRUBBED_TXG: int
    LEAKED: int
    LISTSNAPSHOTS: int
    LOAD_GUID: int
    MAXBLOCKSIZE: int
    MAXDNODESIZE: int
    MULTIHOST: int
    NAME: int
    READONLY: int
    SIZE: int
    TNAME: int
    USABLE: int
    USED: int
    VERSION: int
    def __format__(self, format_spec: str, /) -> str: ...

class ZPOOLStatus(enum.IntEnum):
    ZPOOL_STATUS_BAD_GUID_SUM: int
    ZPOOL_STATUS_BAD_LOG: int
    ZPOOL_STATUS_COMPATIBILITY_ERR: int
    ZPOOL_STATUS_CORRUPT_CACHE: int
    ZPOOL_STATUS_CORRUPT_DATA: int
    ZPOOL_STATUS_CORRUPT_LABEL_NR: int
    ZPOOL_STATUS_CORRUPT_LABEL_R: int
    ZPOOL_STATUS_CORRUPT_POOL: int
    ZPOOL_STATUS_ERRATA: int
    ZPOOL_STATUS_FAILING_DEV: int
    ZPOOL_STATUS_FAULTED_DEV_NR: int
    ZPOOL_STATUS_FAULTED_DEV_R: int
    ZPOOL_STATUS_FEAT_DISABLED: int
    ZPOOL_STATUS_HOSTID_ACTIVE: int
    ZPOOL_STATUS_HOSTID_MISMATCH: int
    ZPOOL_STATUS_HOSTID_REQUIRED: int
    ZPOOL_STATUS_INCOMPATIBLE_FEAT: int
    ZPOOL_STATUS_IO_FAILURE_CONTINUE: int
    ZPOOL_STATUS_IO_FAILURE_MMP: int
    ZPOOL_STATUS_IO_FAILURE_WAIT: int
    ZPOOL_STATUS_MISSING_DEV_NR: int
    ZPOOL_STATUS_MISSING_DEV_R: int
    ZPOOL_STATUS_NON_NATIVE_ASHIFT: int
    ZPOOL_STATUS_OFFLINE_DEV: int
    ZPOOL_STATUS_OK: int
    ZPOOL_STATUS_REBUILDING: int
    ZPOOL_STATUS_REBUILD_SCRUB: int
    ZPOOL_STATUS_REMOVED_DEV: int
    ZPOOL_STATUS_RESILVERING: int
    ZPOOL_STATUS_UNSUP_FEAT_READ: int
    ZPOOL_STATUS_UNSUP_FEAT_WRITE: int
    ZPOOL_STATUS_VERSION_NEWER: int
    ZPOOL_STATUS_VERSION_OLDER: int
    def __format__(self, format_spec: str, /) -> str: ...


# ---------------------------------------------------------------------------
# Struct sequence types
# ---------------------------------------------------------------------------

class struct_vdev_create_spec:
    """Vdev creation specification for use with ZFS.create_pool()."""
    name: str | None
    vdev_type: VDevType | str
    children: tuple[struct_vdev_create_spec, ...] | None

class struct_vdev_stats:
    timestamp: int
    allocated: int
    space: int
    dspace: int
    pspace: int
    rsize: int
    esize: int
    read_errors: int
    write_errors: int
    checksum_errors: int
    initialize_errors: int
    dio_verify_errors: int
    slow_ios: int | None
    self_healed_bytes: int
    fragmentation: int | None
    scan_processed: int
    scan_removing: int
    rebuild_processed: int
    noalloc: int | None
    ops_read: int
    ops_write: int
    bytes_read: int
    bytes_write: int
    configured_ashift: int | None
    logical_ashift: int | None
    physical_ashift: int | None

class struct_vdev:
    name: str
    vdev_type: str
    guid: int
    state: VDevState
    stats: struct_vdev_stats | None
    children: tuple[struct_vdev, ...] | None
    top_guid: int | None

class struct_support_vdev:
    cache: tuple[struct_vdev, ...]
    log: tuple[struct_vdev, ...]
    special: tuple[struct_vdev, ...]
    dedup: tuple[struct_vdev, ...]

class struct_zpool_feature:
    guid: str
    description: str
    state: Literal["DISABLED", "ENABLED", "ACTIVE"]

class struct_zpool_scrub:
    func: ScanFunction
    state: ScanState
    start_time: int
    end_time: int
    to_examine: int
    examined: int
    skipped: int
    processed: int
    issued: int
    errors: int
    pass_exam: int
    pass_start: int
    pass_scrub_pause: int
    pass_scrub_spent_paused: int
    pass_issued: int
    error_scrub_func: ScanFunction | None
    error_scrub_state: ScanState | None
    error_scrub_start: int | None
    error_scrub_end: int | None
    error_scrub_examined: int | None
    error_scrub_to_be_examined: int | None
    pass_error_scrub_pause: int | None
    percentage: float | None

class struct_zpool_expand:
    state: ScanState
    expanding_vdev: int
    start_time: int
    end_time: int
    to_reflow: int
    reflowed: int
    waiting_for_resilver: int

class struct_zpool_status:
    status: ZPOOLStatus
    reason: str | None
    action: str | None
    message: str | None
    corrupted_files: tuple[str, ...]
    storage_vdevs: tuple[struct_vdev, ...]
    support_vdevs: struct_support_vdev
    spares: tuple[struct_vdev, ...]
    name: str
    guid: int

class struct_zfs_property_source:
    """Source information for a ZFS or pool property."""
    type: PropertySource
    value: str | None

class struct_zfs_property_data:
    """Individual property value and source information."""
    value: int | str | None
    raw: str
    source: struct_zfs_property_source | None

class struct_zpool_property_data:
    """Per-property data returned by ZFSPool.get_properties()."""
    prop: ZPOOLProperty
    value: int | str | None
    raw: str
    source: PropertySource | None

class struct_zpool_property:
    """Pool property bundle returned by ZFSPool.get_properties().

    Each attribute corresponds to a ZPOOLProperty member.
    Requested properties contain the parsed value directly (int for numeric
    properties, str for string/index properties); unrequested properties
    are None.

    Attribute names are the lowercase libzfs property names as returned by
    zpool_prop_to_name() (e.g. ``failmode``, ``listsnapshots``,
    ``expandsize``).  These differ from the ZPOOLProperty enum member names.
    """
    name:                         struct_zpool_property_data | None
    size:                         struct_zpool_property_data | None
    capacity:                     struct_zpool_property_data | None
    altroot:                      struct_zpool_property_data | None
    health:                       struct_zpool_property_data | None
    guid:                         struct_zpool_property_data | None
    version:                      struct_zpool_property_data | None
    bootfs:                       struct_zpool_property_data | None
    delegation:                   struct_zpool_property_data | None
    autoreplace:                  struct_zpool_property_data | None
    cachefile:                    struct_zpool_property_data | None
    failmode:                     struct_zpool_property_data | None
    listsnapshots:                struct_zpool_property_data | None
    autoexpand:                   struct_zpool_property_data | None
    dedupditto:                   struct_zpool_property_data | None
    dedupratio:                   struct_zpool_property_data | None
    free:                         struct_zpool_property_data | None
    allocated:                    struct_zpool_property_data | None
    readonly:                     struct_zpool_property_data | None
    ashift:                       struct_zpool_property_data | None
    comment:                      struct_zpool_property_data | None
    expandsize:                   struct_zpool_property_data | None
    freeing:                      struct_zpool_property_data | None
    fragmentation:                struct_zpool_property_data | None
    leaked:                       struct_zpool_property_data | None
    maxblocksize:                 struct_zpool_property_data | None
    tname:                        struct_zpool_property_data | None
    maxdnodesize:                 struct_zpool_property_data | None
    multihost:                    struct_zpool_property_data | None
    checkpoint:                   struct_zpool_property_data | None
    load_guid:                    struct_zpool_property_data | None
    autotrim:                     struct_zpool_property_data | None
    compatibility:                struct_zpool_property_data | None
    bcloneused:                   struct_zpool_property_data | None
    bclonesaved:                  struct_zpool_property_data | None
    bcloneratio:                  struct_zpool_property_data | None
    dedup_table_size:             struct_zpool_property_data | None
    dedup_table_quota:            struct_zpool_property_data | None
    dedupcached:                  struct_zpool_property_data | None
    last_scrubbed_txg:            struct_zpool_property_data | None
    dedupused:                    struct_zpool_property_data | None
    dedupsaved:                   struct_zpool_property_data | None
    available:                    struct_zpool_property_data | None
    usable:                       struct_zpool_property_data | None
    used:                         struct_zpool_property_data | None
    class_normal_size:            struct_zpool_property_data | None
    class_normal_capacity:        struct_zpool_property_data | None
    class_normal_free:            struct_zpool_property_data | None
    class_normal_allocated:       struct_zpool_property_data | None
    class_normal_available:       struct_zpool_property_data | None
    class_normal_usable:          struct_zpool_property_data | None
    class_normal_used:            struct_zpool_property_data | None
    class_normal_expandsize:      struct_zpool_property_data | None
    class_normal_fragmentation:   struct_zpool_property_data | None
    class_special_size:           struct_zpool_property_data | None
    class_special_capacity:       struct_zpool_property_data | None
    class_special_free:           struct_zpool_property_data | None
    class_special_allocated:      struct_zpool_property_data | None
    class_special_available:      struct_zpool_property_data | None
    class_special_usable:         struct_zpool_property_data | None
    class_special_used:           struct_zpool_property_data | None
    class_special_expandsize:     struct_zpool_property_data | None
    class_special_fragmentation:  struct_zpool_property_data | None
    class_dedup_size:             struct_zpool_property_data | None
    class_dedup_capacity:         struct_zpool_property_data | None
    class_dedup_free:             struct_zpool_property_data | None
    class_dedup_allocated:        struct_zpool_property_data | None
    class_dedup_available:        struct_zpool_property_data | None
    class_dedup_usable:           struct_zpool_property_data | None
    class_dedup_used:             struct_zpool_property_data | None
    class_dedup_expandsize:       struct_zpool_property_data | None
    class_dedup_fragmentation:    struct_zpool_property_data | None
    class_log_size:               struct_zpool_property_data | None
    class_log_capacity:           struct_zpool_property_data | None
    class_log_free:               struct_zpool_property_data | None
    class_log_allocated:          struct_zpool_property_data | None
    class_log_available:          struct_zpool_property_data | None
    class_log_usable:             struct_zpool_property_data | None
    class_log_used:               struct_zpool_property_data | None
    class_log_expandsize:         struct_zpool_property_data | None
    class_log_fragmentation:      struct_zpool_property_data | None
    class_elog_size:              struct_zpool_property_data | None
    class_elog_capacity:          struct_zpool_property_data | None
    class_elog_free:              struct_zpool_property_data | None
    class_elog_allocated:         struct_zpool_property_data | None
    class_elog_available:         struct_zpool_property_data | None
    class_elog_usable:            struct_zpool_property_data | None
    class_elog_used:              struct_zpool_property_data | None
    class_elog_expandsize:        struct_zpool_property_data | None
    class_elog_fragmentation:     struct_zpool_property_data | None
    class_special_elog_size:          struct_zpool_property_data | None
    class_special_elog_capacity:      struct_zpool_property_data | None
    class_special_elog_free:          struct_zpool_property_data | None
    class_special_elog_allocated:     struct_zpool_property_data | None
    class_special_elog_available:     struct_zpool_property_data | None
    class_special_elog_usable:        struct_zpool_property_data | None
    class_special_elog_used:          struct_zpool_property_data | None
    class_special_elog_expandsize:    struct_zpool_property_data | None
    class_special_elog_fragmentation: struct_zpool_property_data | None

class struct_zfs_crypto_info:
    """Encryption status information for a ZFS dataset or volume."""
    is_root: bool
    """Whether this dataset is an encryption root."""
    encryption_root: str | None
    """Name of the encryption root dataset, or None if not encrypted."""
    key_location: str
    """The keylocation property value (e.g. 'prompt', 'file://...')."""
    key_is_loaded: bool
    """Whether the wrapping key is currently loaded."""


# ---------------------------------------------------------------------------
# C extension types (not directly instantiable; use factories)
# ---------------------------------------------------------------------------

class ZFSCrypto:
    """Encryption operations for a ZFS dataset or volume."""

    def info(self) -> struct_zfs_crypto_info: ...

    def load_key(
        self,
        *,
        key: str | bytes | None = None,
        key_location: str | None = None,
    ) -> None: ...

    def unload_key(self) -> None: ...

    def check_key(
        self,
        *,
        key: str | bytes | None = None,
        key_location: str | None = None,
    ) -> bool: ...

    def change_key(self, *, info: Any) -> None: ...

    def inherit_key(self) -> None: ...


class ZFSSnapshot:
    def get_holds(self) -> tuple[str, ...]: ...
    def get_clones(self) -> tuple[str, ...]: ...
    def clone(self, *, name: str, properties: dict[str, Any] | None = None) -> None: ...


class ZFSPool:
    """Represents a ZFS pool."""
    @property
    def name(self) -> str: ...

    def prefetch(self) -> None: ...

    def status(
        self,
        *,
        asdict: bool = ...,
        get_stats: bool = ...,
        follow_links: bool = ...,
        full_path: bool = ...,
    ) -> struct_zpool_status | dict[str, Any]: ...

    def get_features(self, *, asdict: bool = ...) -> dict[str, struct_zpool_feature] | dict[str, dict[str, str]]: ...

    def scrub_info(self) -> struct_zpool_scrub | None: ...

    def expand_info(self) -> struct_zpool_expand | None: ...

    def scan(self, *, func: ScanFunction, cmd: ScanScrubCmd = ...) -> None: ...

    def get_properties(self, *, properties: set[ZPOOLProperty]) -> struct_zpool_property: ...

    def set_properties(self, *, properties: dict[str, Any]) -> None: ...

    def get_user_properties(self) -> dict[str, str]: ...

    def set_user_properties(self, *, user_properties: dict[str, str]) -> None: ...

    def offline_device(self, *, device: str, temporary: bool = False) -> None: ...

    def online_device(self, *, device: str, expand: bool = False) -> None: ...

    def add_vdevs(
        self,
        *,
        storage_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        cache_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        log_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        special_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        dedup_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        spare_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        force: bool = False,
    ) -> None: ...

    def attach_vdev(
        self,
        *,
        device: str,
        new_device: struct_vdev_create_spec,
        rebuild: bool = False,
        force: bool = False,
    ) -> None: ...

    def replace_vdev(
        self,
        *,
        device: str,
        new_device: struct_vdev_create_spec | None = None,
        rebuild: bool = False,
    ) -> None: ...

    def detach_vdev(self, *, device: str) -> None: ...

    def remove_vdev(self, *, device: str) -> None: ...

    def cancel_remove_vdev(self) -> None: ...

    def iter_history(
        self,
        *,
        skip_internal: bool = True,
        since: int = 0,
        until: int = 0,
    ) -> Iterator[dict[str, Any]]: ...


class ZFS:
    """ZFS library handle. Obtain via truenas_pylibzfs.open_handle()."""
    def open_pool(self, *, name: str) -> ZFSPool: ...
    def open_resource(self, **kwargs: Any) -> Any: ...
    def create_resource(self, *, name: str, type: ZFSType, properties: dict[str, Any] | None = None, crypto: Any | None = None) -> None: ...
    def create_pool(
        self,
        *,
        name: str,
        storage_vdevs: Iterable[struct_vdev_create_spec],
        cache_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        log_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        special_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        dedup_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        spare_vdevs: Iterable[struct_vdev_create_spec] | None = None,
        properties: dict[ZPOOLProperty, str] | None = None,
        filesystem_properties: dict[ZFSProperty, str] | None = None,
        force: bool = False,
    ) -> None: ...
    def destroy_pool(self, *, name: str, force: bool = False) -> None: ...
    def export_pool(self, *, name: str, force: bool = False) -> None: ...
    def destroy_resource(self, *, name: str) -> bool: ...
    def iter_pools(self, *, callback: Any, state: Any) -> bool: ...
    def iter_root_filesystems(self, *, callback: Any, state: Any) -> bool: ...
    def resource_cryptography_config(self, *, keyformat: str | None = None, keylocation: str | None = None, pbkdf2iters: int | None = None, key: str | bytes | None = None) -> Any: ...
    def zpool_events(self, *, blocking: bool = False, skip_existing_events: bool = True) -> Iterator[dict[str, Any]]: ...
    def import_pool_find(self, *, cache_file: str | None = None, device: str | None = None) -> list[struct_zpool_status]: ...
    def import_pool(self, *, name: str | None = None, guid: int | None = None,
                    allow_missing_log: bool = False, altroot: str | None = None,
                    force: bool = False, properties: dict[ZPOOLProperty | str, str] | None = None,
                    temporary_name: str | None = None, device: str | None = None) -> ZFSPool: ...
