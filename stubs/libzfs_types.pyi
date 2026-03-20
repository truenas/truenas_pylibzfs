from collections.abc import Iterable, Iterator, Sequence
import enum
from typing import Any, ClassVar, Literal, Self, final


# ---------------------------------------------------------------------------
# Enums (dual-registered: also available at truenas_pylibzfs.<Name>)
# ---------------------------------------------------------------------------

class PropertySource(enum.IntFlag):
    DEFAULT = 2
    INHERITED = 16
    LOCAL = 8
    NONE = 1
    RECEIVED = 32
    TEMPORARY = 4
    def __format__(self, format_spec: str, /) -> str: ...

class ScanFunction(enum.IntEnum):
    NONE = 0
    SCRUB = 1
    RESILVER = 2
    ERRORSCRUB = 3
    def __format__(self, format_spec: str, /) -> str: ...

class ScanScrubCmd(enum.IntEnum):
    NORMAL = 0
    PAUSE = 1
    FROM_LAST_TXG = 2
    def __format__(self, format_spec: str, /) -> str: ...

class ScanState(enum.IntEnum):
    NONE = 0
    SCANNING = 1
    FINISHED = 2
    CANCELED = 3
    ERRORSCRUBBING = 4
    def __format__(self, format_spec: str, /) -> str: ...

class VDevAuxState(enum.IntEnum):
    VDEV_AUX_ACTIVE = 18
    VDEV_AUX_ASHIFT_TOO_BIG = 20
    VDEV_AUX_BAD_ASHIFT = 16
    VDEV_AUX_BAD_GUID_SUM = 4
    VDEV_AUX_BAD_LABEL = 6
    VDEV_AUX_BAD_LOG = 13
    VDEV_AUX_CHILDREN_OFFLINE = 19
    VDEV_AUX_CORRUPT_DATA = 2
    VDEV_AUX_ERR_EXCEEDED = 11
    VDEV_AUX_EXTERNAL = 14
    VDEV_AUX_EXTERNAL_PERSIST = 17
    VDEV_AUX_IO_FAILURE = 12
    VDEV_AUX_NONE = 0
    VDEV_AUX_NO_REPLICAS = 3
    VDEV_AUX_OPEN_FAILED = 1
    VDEV_AUX_SPARED = 10
    VDEV_AUX_SPLIT_POOL = 15
    VDEV_AUX_TOO_SMALL = 5
    VDEV_AUX_UNSUP_FEAT = 9
    VDEV_AUX_VERSION_NEWER = 7
    VDEV_AUX_VERSION_OLDER = 8
    def __format__(self, format_spec: str, /) -> str: ...

class VDevState(enum.IntEnum):
    CANT_OPEN = 4
    CLOSED = 1
    DEGRADED = 6
    FAULTED = 5
    OFFLINE = 2
    ONLINE = 7
    REMOVED = 3
    UNKNOWN = 0
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
    ZFS_APPENDONLY = 274877906944
    ZFS_ARCHIVE = 34359738368
    ZFS_HIDDEN = 8589934592
    ZFS_IMMUTABLE = 68719476736
    ZFS_NODUMP = 549755813888
    ZFS_NOUNLINK = 137438953472
    ZFS_READONLY = 4294967296
    ZFS_SPARSE = 17592186044416
    ZFS_SYSTEM = 17179869184
    def __format__(self, format_spec: str, /) -> str: ...

class ZFSError(enum.IntEnum):
    EZFS_ACTIVE_POOL = 2074
    EZFS_ACTIVE_SPARE = 2057
    EZFS_ASHIFT_MISMATCH = 2099
    EZFS_BADBACKUP = 2015
    EZFS_BADCACHE = 2053
    EZFS_BADDEV = 2018
    EZFS_BADPATH = 2024
    EZFS_BADPERM = 2048
    EZFS_BADPERMSET = 2049
    EZFS_BADPROP = 2001
    EZFS_BADRESTORE = 2014
    EZFS_BADSTREAM = 2010
    EZFS_BADTARGET = 2016
    EZFS_BADTYPE = 2006
    EZFS_BADVERSION = 2021
    EZFS_BADWHO = 2047
    EZFS_BUSY = 2007
    EZFS_CHECKPOINT_EXISTS = 2077
    EZFS_CKSUM = 2095
    EZFS_CROSSTARGET = 2025
    EZFS_CRYPTOFAILED = 2075
    EZFS_DEVOVERFLOW = 2023
    EZFS_DEVRM_IN_PROGRESS = 2080
    EZFS_DIFF = 2069
    EZFS_DIFFDATA = 2070
    EZFS_DISCARDING_CHECKPOINT = 2078
    EZFS_DSREADONLY = 2011
    EZFS_ERRORSCRUBBING = 2066
    EZFS_ERRORSCRUB_PAUSED = 2067
    EZFS_EXISTS = 2008
    EZFS_EXPORT_IN_PROGRESS = 2091
    EZFS_FAULT = 2033
    EZFS_INITIALIZING = 2084
    EZFS_INTR = 2035
    EZFS_INVALCONFIG = 2037
    EZFS_INVALIDNAME = 2013
    EZFS_IO = 2034
    EZFS_IOC_NOTSUPPORTED = 2082
    EZFS_ISL2CACHE = 2054
    EZFS_ISSPARE = 2036
    EZFS_LABELFAILED = 2046
    EZFS_MOUNTFAILED = 2027
    EZFS_NAMETOOLONG = 2043
    EZFS_NOCAP = 2045
    EZFS_NODELEGATION = 2050
    EZFS_NODEVICE = 2017
    EZFS_NOENT = 2009
    EZFS_NOHISTORY = 2039
    EZFS_NOMEM = 2000
    EZFS_NOREPLICAS = 2019
    EZFS_NOSPC = 2032
    EZFS_NOTSUP = 2056
    EZFS_NOT_USER_NAMESPACE = 2094
    EZFS_NO_CHECKPOINT = 2079
    EZFS_NO_INITIALIZE = 2085
    EZFS_NO_PENDING = 2076
    EZFS_NO_RESILVER_DEFER = 2090
    EZFS_NO_SCRUB = 2068
    EZFS_NO_TRIM = 2088
    EZFS_OPENFAILED = 2044
    EZFS_PERM = 2031
    EZFS_PIPEFAILED = 2062
    EZFS_POOLPROPS = 2040
    EZFS_POOLREADONLY = 2071
    EZFS_POOLUNAVAIL = 2022
    EZFS_POOL_INVALARG = 2042
    EZFS_POOL_NOTSUP = 2041
    EZFS_POSTSPLIT_ONLINE = 2064
    EZFS_PROPNONINHERIT = 2004
    EZFS_PROPREADONLY = 2002
    EZFS_PROPSPACE = 2005
    EZFS_PROPTYPE = 2003
    EZFS_RAIDZ_EXPAND_IN_PROGRESS = 2098
    EZFS_REBUILDING = 2092
    EZFS_RECURSIVE = 2038
    EZFS_REFTAG_HOLD = 2060
    EZFS_REFTAG_RELE = 2059
    EZFS_RESILVERING = 2020
    EZFS_RESUME_EXISTS = 2096
    EZFS_SCRUBBING = 2065
    EZFS_SCRUB_PAUSED = 2072
    EZFS_SCRUB_PAUSED_TO_CANCEL = 2073
    EZFS_SHAREFAILED = 2097
    EZFS_SHARENFSFAILED = 2030
    EZFS_SHARESMBFAILED = 2052
    EZFS_SUCCESS = 0
    EZFS_TAGTOOLONG = 2061
    EZFS_THREADCREATEFAILED = 2063
    EZFS_TOOMANY = 2083
    EZFS_TRIMMING = 2087
    EZFS_TRIM_NOTSUP = 2089
    EZFS_UMOUNTFAILED = 2028
    EZFS_UNKNOWN = 2100
    EZFS_UNPLAYED_LOGS = 2058
    EZFS_UNSHARENFSFAILED = 2029
    EZFS_UNSHARESMBFAILED = 2051
    EZFS_VDEVNOTSUP = 2055
    EZFS_VDEV_NOTSUP = 2093
    EZFS_VDEV_TOO_BIG = 2081
    EZFS_VOLTOOBIG = 2012
    EZFS_WRONG_PARENT = 2086
    EZFS_ZONED = 2026
    def __format__(self, format_spec: str, /) -> str: ...

class ZFSProperty(enum.IntEnum):
    ACLINHERIT = 25
    ACLMODE = 24
    ACLTYPE = 72
    ATIME = 17
    AVAILABLE = 3
    CANMOUNT = 28
    CASESENSITIVITY = 36
    CHECKSUM = 15
    CLONES = 62
    COMPRESSION = 16
    COMPRESSRATIO = 5
    CONTEXT = 73
    COPIES = 32
    CREATETXG = 26
    CREATION = 1
    DEDUP = 56
    DEFAULTGROUPOBJQUOTA = 104
    DEFAULTGROUPQUOTA = 101
    DEFAULTPROJECTOBJQUOTA = 105
    DEFAULTPROJECTQUOTA = 102
    DEFAULTUSEROBJQUOTA = 103
    DEFAULTUSERQUOTA = 100
    DEFCONTEXT = 75
    DEFER_DESTROY = 51
    DEVICES = 18
    DIRECT = 98
    DNODESIZE = 59
    ENCRYPTION = 82
    ENCRYPTIONROOT = 87
    EXEC = 19
    FILESYSTEM_COUNT = 69
    FILESYSTEM_LIMIT = 67
    FSCONTEXT = 74
    GUID = 42
    KEYFORMAT = 84
    KEYLOCATION = 83
    KEYSTATUS = 89
    LOGBIAS = 53
    LOGICALREFERENCED = 64
    LOGICALUSED = 63
    LONGNAME = 99
    MLSLABEL = 57
    MOUNTED = 6
    MOUNTPOINT = 13
    NBMAND = 38
    NORMALIZATION = 35
    OBJSETID = 55
    ORIGIN = 7
    OVERLAY = 79
    PBKDF2ITERS = 86
    PREFETCH = 96
    PRIMARYCACHE = 43
    QUOTA = 8
    READONLY = 21
    RECEIVE_RESUME_TOKEN = 81
    RECORDSIZE = 12
    REDACT_SNAPS = 94
    REDUNDANT_METADATA = 78
    REFCOMPRESSRATIO = 60
    REFERENCED = 4
    REFQUOTA = 40
    REFRESERVATION = 41
    RELATIME = 77
    RESERVATION = 9
    ROOTCONTEXT = 76
    SECONDARYCACHE = 44
    SETUID = 20
    SHARENFS = 14
    SHARESMB = 39
    SNAPDEV = 71
    SNAPDIR = 23
    SNAPSHOTS_CHANGED = 95
    SNAPSHOT_COUNT = 70
    SNAPSHOT_LIMIT = 68
    SPECIAL_SMALL_BLOCKS = 91
    SYNC = 58
    TYPE = 0
    USED = 2
    USEDBYCHILDREN = 47
    USEDBYDATASET = 46
    USEDBYREFRESERVATION = 48
    USEDBYSNAPSHOTS = 45
    USERREFS = 52
    UTF8ONLY = 34
    VERSION = 33
    VOLBLOCKSIZE = 11
    VOLMODE = 66
    VOLSIZE = 10
    VOLTHREADING = 97
    VSCAN = 37
    WRITTEN = 61
    XATTR = 30
    ZONED = 22
    def __format__(self, format_spec: str, /) -> str: ...

class ZFSType(enum.IntEnum):
    ZFS_TYPE_BOOKMARK = 16
    ZFS_TYPE_FILESYSTEM = 1
    ZFS_TYPE_INVALID = 0
    ZFS_TYPE_POOL = 8
    ZFS_TYPE_SNAPSHOT = 2
    ZFS_TYPE_VDEV = 32
    ZFS_TYPE_VOLUME = 4
    def __format__(self, format_spec: str, /) -> str: ...

class ZFSUserQuota(enum.IntEnum):
    GROUPOBJ_QUOTA = 7
    GROUPOBJ_USED = 6
    GROUP_QUOTA = 3
    GROUP_USED = 2
    PROJECTOBJ_QUOTA = 11
    PROJECTOBJ_USED = 10
    PROJECT_QUOTA = 9
    PROJECT_USED = 8
    USEROBJ_QUOTA = 5
    USEROBJ_USED = 4
    USER_QUOTA = 1
    USER_USED = 0
    def __format__(self, format_spec: str, /) -> str: ...

class ZPOOLProperty(enum.IntEnum):
    ALLOCATED = 17
    ALTROOT = 3
    ASHIFT = 19
    AUTOEXPAND = 13
    AUTOREPLACE = 9
    AUTOTRIM = 31
    AVAILABLE = 42
    BCLONERATIO = 35
    BCLONESAVED = 34
    BCLONEUSED = 33
    BOOTFS = 7
    CACHEFILE = 10
    CAPACITY = 2
    CHECKPOINT = 29
    COMMENT = 20
    COMPATIBILITY = 32
    DEDUPCACHED = 38
    DEDUPDITTO = 14
    DEDUPRATIO = 15
    DEDUP_TABLE_QUOTA = 37
    DEDUP_TABLE_SIZE = 36
    DEDUPSAVED = 41
    DEDUPUSED = 40
    CLASS_DEDUP_ALLOCATED = 66
    CLASS_DEDUP_AVAILABLE = 67
    CLASS_DEDUP_CAPACITY = 64
    CLASS_DEDUP_EXPANDSIZE = 70
    CLASS_DEDUP_FRAGMENTATION = 71
    CLASS_DEDUP_FREE = 65
    CLASS_DEDUP_SIZE = 63
    CLASS_DEDUP_USABLE = 68
    CLASS_DEDUP_USED = 69
    CLASS_ELOG_ALLOCATED = 84
    CLASS_ELOG_AVAILABLE = 85
    CLASS_ELOG_CAPACITY = 82
    CLASS_ELOG_EXPANDSIZE = 88
    CLASS_ELOG_FRAGMENTATION = 89
    CLASS_ELOG_FREE = 83
    CLASS_ELOG_SIZE = 81
    CLASS_ELOG_USABLE = 86
    CLASS_ELOG_USED = 87
    CLASS_LOG_ALLOCATED = 75
    CLASS_LOG_AVAILABLE = 76
    CLASS_LOG_CAPACITY = 73
    CLASS_LOG_EXPANDSIZE = 79
    CLASS_LOG_FRAGMENTATION = 80
    CLASS_LOG_FREE = 74
    CLASS_LOG_SIZE = 72
    CLASS_LOG_USABLE = 77
    CLASS_LOG_USED = 78
    CLASS_NORMAL_ALLOCATED = 48
    CLASS_NORMAL_AVAILABLE = 49
    CLASS_NORMAL_CAPACITY = 46
    CLASS_NORMAL_EXPANDSIZE = 52
    CLASS_NORMAL_FRAGMENTATION = 53
    CLASS_NORMAL_FREE = 47
    CLASS_NORMAL_SIZE = 45
    CLASS_NORMAL_USABLE = 50
    CLASS_NORMAL_USED = 51
    CLASS_SPECIAL_ALLOCATED = 57
    CLASS_SPECIAL_AVAILABLE = 58
    CLASS_SPECIAL_CAPACITY = 55
    CLASS_SPECIAL_ELOG_ALLOCATED = 93
    CLASS_SPECIAL_ELOG_AVAILABLE = 94
    CLASS_SPECIAL_ELOG_CAPACITY = 91
    CLASS_SPECIAL_ELOG_EXPANDSIZE = 97
    CLASS_SPECIAL_ELOG_FRAGMENTATION = 98
    CLASS_SPECIAL_ELOG_FREE = 92
    CLASS_SPECIAL_ELOG_SIZE = 90
    CLASS_SPECIAL_ELOG_USABLE = 95
    CLASS_SPECIAL_ELOG_USED = 96
    CLASS_SPECIAL_EXPANDSIZE = 61
    CLASS_SPECIAL_FRAGMENTATION = 62
    CLASS_SPECIAL_FREE = 56
    CLASS_SPECIAL_SIZE = 54
    CLASS_SPECIAL_USABLE = 59
    CLASS_SPECIAL_USED = 60
    DELEGATION = 8
    EXPANDSIZE = 21
    FAILMODE = 11
    FRAGMENTATION = 23
    FREE = 16
    FREEING = 22
    GUID = 5
    HEALTH = 4
    INVAL = -1
    LAST_SCRUBBED_TXG = 39
    LEAKED = 24
    LISTSNAPSHOTS = 12
    LOAD_GUID = 30
    MAXBLOCKSIZE = 25
    MAXDNODESIZE = 27
    MULTIHOST = 28
    NAME = 0
    READONLY = 18
    SIZE = 1
    TNAME = 26
    USABLE = 43
    USED = 44
    VERSION = 6
    def __format__(self, format_spec: str, /) -> str: ...

class ZPOOLStatus(enum.IntEnum):
    ZPOOL_STATUS_BAD_GUID_SUM = 5
    ZPOOL_STATUS_BAD_LOG = 16
    ZPOOL_STATUS_COMPATIBILITY_ERR = 30
    ZPOOL_STATUS_CORRUPT_CACHE = 0
    ZPOOL_STATUS_CORRUPT_DATA = 7
    ZPOOL_STATUS_CORRUPT_LABEL_NR = 4
    ZPOOL_STATUS_CORRUPT_LABEL_R = 3
    ZPOOL_STATUS_CORRUPT_POOL = 6
    ZPOOL_STATUS_ERRATA = 17
    ZPOOL_STATUS_FAILING_DEV = 8
    ZPOOL_STATUS_FAULTED_DEV_NR = 21
    ZPOOL_STATUS_FAULTED_DEV_R = 20
    ZPOOL_STATUS_FEAT_DISABLED = 23
    ZPOOL_STATUS_HOSTID_ACTIVE = 11
    ZPOOL_STATUS_HOSTID_MISMATCH = 10
    ZPOOL_STATUS_HOSTID_REQUIRED = 12
    ZPOOL_STATUS_INCOMPATIBLE_FEAT = 31
    ZPOOL_STATUS_IO_FAILURE_CONTINUE = 14
    ZPOOL_STATUS_IO_FAILURE_MMP = 15
    ZPOOL_STATUS_IO_FAILURE_WAIT = 13
    ZPOOL_STATUS_MISSING_DEV_NR = 2
    ZPOOL_STATUS_MISSING_DEV_R = 1
    ZPOOL_STATUS_NON_NATIVE_ASHIFT = 29
    ZPOOL_STATUS_OFFLINE_DEV = 25
    ZPOOL_STATUS_OK = 32
    ZPOOL_STATUS_REBUILDING = 27
    ZPOOL_STATUS_REBUILD_SCRUB = 28
    ZPOOL_STATUS_REMOVED_DEV = 26
    ZPOOL_STATUS_RESILVERING = 24
    ZPOOL_STATUS_UNSUP_FEAT_READ = 18
    ZPOOL_STATUS_UNSUP_FEAT_WRITE = 19
    ZPOOL_STATUS_VERSION_NEWER = 9
    ZPOOL_STATUS_VERSION_OLDER = 22
    def __format__(self, format_spec: str, /) -> str: ...


# ---------------------------------------------------------------------------
# Struct sequence types
# ---------------------------------------------------------------------------

@final
class struct_vdev_create_spec:
    """Vdev creation specification for use with ZFS.create_pool()."""
    name: str | None
    vdev_type: VDevType | str
    children: tuple[struct_vdev_create_spec, ...] | None
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 3
    n_sequence_fields: ClassVar[int] # = 3
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
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
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 26
    n_sequence_fields: ClassVar[int] # = 26
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_vdev:
    name: str
    vdev_type: str
    guid: int
    state: VDevState
    stats: struct_vdev_stats | None
    children: tuple[struct_vdev, ...] | None
    top_guid: int | None
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 7
    n_sequence_fields: ClassVar[int] # = 7
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_support_vdev:
    cache: tuple[struct_vdev, ...]
    log: tuple[struct_vdev, ...]
    special: tuple[struct_vdev, ...]
    dedup: tuple[struct_vdev, ...]
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 4
    n_sequence_fields: ClassVar[int] # = 4
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_zpool_feature:
    guid: str
    description: str
    state: Literal["DISABLED", "ENABLED", "ACTIVE"]
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 3
    n_sequence_fields: ClassVar[int] # = 3
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
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
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 23
    n_sequence_fields: ClassVar[int] # = 23
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_zpool_expand:
    state: ScanState
    expanding_vdev: int
    start_time: int
    end_time: int
    to_reflow: int
    reflowed: int
    waiting_for_resilver: int
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 7
    n_sequence_fields: ClassVar[int] # = 7
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
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
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 10
    n_sequence_fields: ClassVar[int] # = 10
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_zfs_property_source:
    """Source information for a ZFS or pool property."""
    type: PropertySource
    value: str | None
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 2
    n_sequence_fields: ClassVar[int] # = 2
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_zfs_property_data:
    """Individual property value and source information."""
    value: int | str | None
    raw: str
    source: struct_zfs_property_source | None
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 3
    n_sequence_fields: ClassVar[int] # = 3
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_zpool_property_data:
    """Per-property data returned by ZFSPool.get_properties()."""
    prop: ZPOOLProperty
    value: int | str | None
    raw: str
    source: PropertySource | None
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 4
    n_sequence_fields: ClassVar[int] # = 4
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
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
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 99
    n_sequence_fields: ClassVar[int] # = 99
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
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
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 4
    n_sequence_fields: ClassVar[int] # = 4
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_zfs_crypto_config:
    """Crypto configuration spec for ZFS.resource_cryptography_config()."""
    keyformat: str | None
    keylocation: str | None
    key: str | bytes | None
    pbkdf2iters: int | None
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 4
    n_sequence_fields: ClassVar[int] # = 4
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_zfs_userquota:
    """User/group quota entry returned by ZFSDataset.iter_userspace()."""
    quota_type: ZFSUserQuota
    xid: int
    value: int
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]          # = 3
    n_sequence_fields: ClassVar[int] # = 3
    n_unnamed_fields: ClassVar[int]  # = 0
    def __replace__(self, **changes: Any) -> Self: ...

@final
class struct_zfs_property:
    """ZFS dataset property bundle returned by ZFSResource.get_properties().

    Each attribute corresponds to a visible ZFSProperty member, named using
    the lowercase libzfs property name (e.g. ``atime``, ``compression``).
    Requested properties contain a struct_zfs_property_data instance;
    unrequested or non-applicable properties are None.
    """
    type: struct_zfs_property_data | None
    creation: struct_zfs_property_data | None
    used: struct_zfs_property_data | None
    available: struct_zfs_property_data | None
    referenced: struct_zfs_property_data | None
    compressratio: struct_zfs_property_data | None
    mounted: struct_zfs_property_data | None
    origin: struct_zfs_property_data | None
    quota: struct_zfs_property_data | None
    reservation: struct_zfs_property_data | None
    volsize: struct_zfs_property_data | None
    volblocksize: struct_zfs_property_data | None
    recordsize: struct_zfs_property_data | None
    mountpoint: struct_zfs_property_data | None
    sharenfs: struct_zfs_property_data | None
    checksum: struct_zfs_property_data | None
    compression: struct_zfs_property_data | None
    atime: struct_zfs_property_data | None
    devices: struct_zfs_property_data | None
    exec: struct_zfs_property_data | None
    setuid: struct_zfs_property_data | None
    readonly: struct_zfs_property_data | None
    zoned: struct_zfs_property_data | None
    snapdir: struct_zfs_property_data | None
    aclmode: struct_zfs_property_data | None
    aclinherit: struct_zfs_property_data | None
    createtxg: struct_zfs_property_data | None
    canmount: struct_zfs_property_data | None
    xattr: struct_zfs_property_data | None
    copies: struct_zfs_property_data | None
    version: struct_zfs_property_data | None
    utf8only: struct_zfs_property_data | None
    normalization: struct_zfs_property_data | None
    casesensitivity: struct_zfs_property_data | None
    vscan: struct_zfs_property_data | None
    nbmand: struct_zfs_property_data | None
    sharesmb: struct_zfs_property_data | None
    refquota: struct_zfs_property_data | None
    refreservation: struct_zfs_property_data | None
    guid: struct_zfs_property_data | None
    primarycache: struct_zfs_property_data | None
    secondarycache: struct_zfs_property_data | None
    usedbysnapshots: struct_zfs_property_data | None
    usedbydataset: struct_zfs_property_data | None
    usedbychildren: struct_zfs_property_data | None
    usedbyrefreservation: struct_zfs_property_data | None
    defer_destroy: struct_zfs_property_data | None
    userrefs: struct_zfs_property_data | None
    logbias: struct_zfs_property_data | None
    objsetid: struct_zfs_property_data | None
    dedup: struct_zfs_property_data | None
    mlslabel: struct_zfs_property_data | None
    sync: struct_zfs_property_data | None
    dnodesize: struct_zfs_property_data | None
    refcompressratio: struct_zfs_property_data | None
    written: struct_zfs_property_data | None
    clones: struct_zfs_property_data | None
    logicalused: struct_zfs_property_data | None
    logicalreferenced: struct_zfs_property_data | None
    volmode: struct_zfs_property_data | None
    filesystem_limit: struct_zfs_property_data | None
    snapshot_limit: struct_zfs_property_data | None
    filesystem_count: struct_zfs_property_data | None
    snapshot_count: struct_zfs_property_data | None
    snapdev: struct_zfs_property_data | None
    acltype: struct_zfs_property_data | None
    context: struct_zfs_property_data | None
    fscontext: struct_zfs_property_data | None
    defcontext: struct_zfs_property_data | None
    rootcontext: struct_zfs_property_data | None
    relatime: struct_zfs_property_data | None
    redundant_metadata: struct_zfs_property_data | None
    overlay: struct_zfs_property_data | None
    receive_resume_token: struct_zfs_property_data | None
    encryption: struct_zfs_property_data | None
    keylocation: struct_zfs_property_data | None
    keyformat: struct_zfs_property_data | None
    pbkdf2iters: struct_zfs_property_data | None
    encryptionroot: struct_zfs_property_data | None
    keystatus: struct_zfs_property_data | None
    special_small_blocks: struct_zfs_property_data | None
    redact_snaps: struct_zfs_property_data | None
    snapshots_changed: struct_zfs_property_data | None
    prefetch: struct_zfs_property_data | None
    volthreading: struct_zfs_property_data | None
    direct: struct_zfs_property_data | None
    longname: struct_zfs_property_data | None
    defaultuserquota: struct_zfs_property_data | None
    defaultgroupquota: struct_zfs_property_data | None
    defaultprojectquota: struct_zfs_property_data | None
    defaultuserobjquota: struct_zfs_property_data | None
    defaultgroupobjquota: struct_zfs_property_data | None
    defaultprojectobjquota: struct_zfs_property_data | None
    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]
    n_sequence_fields: ClassVar[int]
    n_unnamed_fields: ClassVar[int]
    def __replace__(self, **changes: Any) -> Self: ...


# ---------------------------------------------------------------------------
# C extension types (not directly instantiable; use factories)
# ---------------------------------------------------------------------------

class ZFSObject:
    """Base ZFS resource object (datasets, volumes, snapshots, bookmarks)."""
    @property
    def name(self) -> str: ...
    @property
    def type(self) -> ZFSType: ...
    @property
    def guid(self) -> int: ...
    @property
    def createtxg(self) -> int: ...
    @property
    def pool_name(self) -> str: ...
    @property
    def encrypted(self) -> bool: ...
    def rename(
        self,
        *,
        new_name: str,
        recursive: bool = ...,
        no_unmount: bool = ...,
        force_unmount: bool = ...,
    ) -> None: ...

@final
class ZFSResource(ZFSObject):
    """ZFS resource (filesystem or volume) with property and mount operations."""
    def iter_filesystems(self, *, callback: Any, state: Any, fast: bool = ...) -> bool: ...
    def iter_snapshots(
        self,
        *,
        callback: Any,
        state: Any,
        fast: bool = ...,
        min_transaction_group: int = ...,
        max_transaction_group: int = ...,
        order_by_transaction_group: bool = ...,
    ) -> bool: ...
    def get_properties(self, *, properties: Any, get_source: bool = ...) -> struct_zfs_property: ...
    def set_properties(self, *, properties: dict[str, Any], remount: bool = ...) -> None: ...
    def inherit_property(self, *, property: str, received: bool = ...) -> None: ...
    def asdict(
        self,
        *,
        properties: Any,
        get_source: bool = ...,
        get_user_properties: bool = ...,
        get_crypto: bool = ...,
    ) -> dict[str, Any]: ...
    def mount(
        self,
        *,
        mountpoint: str,
        mount_options: str | None = ...,
        force: bool = ...,
        load_encryption_key: bool = ...,
    ) -> None: ...
    def unmount(
        self,
        *,
        mountpoint: str,
        force: bool = ...,
        lazy: bool = ...,
        unload_encryption_key: bool = ...,
        follow_symlinks: bool = ...,
        recursive: bool = ...,
    ) -> None: ...
    def get_user_properties(self) -> dict[str, str]: ...
    def refresh_properties(self) -> None: ...
    def get_mountpoint(self) -> str | None: ...
    def set_user_properties(self, *, user_properties: dict[str, str]) -> None: ...
    def open_pool(self) -> ZFSPool: ...

@final
class ZFSDataset(ZFSResource):  # type: ignore[misc]
    """ZFS filesystem dataset."""
    def iter_userspace(self, *, callback: Any, state: Any, quota_type: Any) -> bool: ...
    def set_userquotas(self, *, quotas: Any) -> None: ...
    def crypto(self) -> ZFSCrypto | None: ...
    def promote(self) -> None: ...

@final
class ZFSVolume(ZFSResource):  # type: ignore[misc]
    """ZFS volume (zvol) dataset."""
    def crypto(self) -> ZFSCrypto | None: ...
    def promote(self) -> None: ...

@final
class ZFSEventIterator(Iterator[dict[str, Any]]):
    """Iterator over ZFS kernel events."""
    def __iter__(self) -> ZFSEventIterator: ...
    def __next__(self) -> dict[str, Any]: ...

@final
class ZFSHistoryIterator(Iterator[dict[str, Any]]):
    """Iterator over ZFS pool command history."""
    def __iter__(self) -> ZFSHistoryIterator: ...
    def __next__(self) -> dict[str, Any]: ...


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


@final
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

    def asdict(self) -> dict[str, Any]: ...
    def clear(self) -> None: ...
    def ddt_prune(self, *, days: int = ..., percentage: int = ...) -> None: ...
    def dump_config(self) -> dict[str, Any]: ...
    def refresh_stats(self) -> None: ...
    def root_dataset(self) -> ZFSDataset: ...
    def sync_pool(self) -> None: ...
    def upgrade(self) -> None: ...


class ZFS:
    """ZFS library handle. Obtain via truenas_pylibzfs.open_handle()."""
    def __init__(self, *args: Any, **kwargs: Any) -> None: ...
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
        feature_properties: dict[str, bool] | None = None,
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
