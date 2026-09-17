"""Integer and string constants exported from the ZFS headers and the binding's own policy."""

ZPL_VERSION: int
L2ARC_PERSISTENT_VERSION: int
ZFS_MAX_DATASET_NAME_LEN: int
ZFS_IOC_GETDOSFLAGS: int
ZFS_IOC_SETDOSFLAGS: int
VDEV_DRAID_MAX_CHILDREN: int
"""Most children a dRAID vdev may have."""
VDEV_DRAID_MAXPARITY: int
"""Highest dRAID parity level."""
VDEV_DRAID_MAX_SPARES: int
"""Most distributed spares a dRAID vdev may have."""
MAX_MIRROR_WIDTH: int
"""Widest mirror vdev create_pool(), add_vdevs() and attach_vdev() accept without force=True."""
MAX_RAIDZ_WIDTH: int
"""Widest raidz vdev create_pool(), add_vdevs() and attach_vdev() accept without force=True."""

ZPOOL_CACHE_BOOT: str
ZPOOL_CACHE: str
ZFS_DEV: str
MNTOPT_ATIME: str
MNTOPT_NOATIME: str
MNTOPT_EXEC: str
MNTOPT_NOEXEC: str
MNTOPT_SUID: str
MNTOPT_NOSUID: str
MNTOPT_DEVICES: str
MNTOPT_NODEVICES: str
MNTOPT_RO: str
MNTOPT_RW: str
MNTOPT_RELATIME: str
MNTOPT_NORELATIME: str
MNTOPT_XATTR: str
MNTOPT_NOXATTR: str
LIBZFS_NONE_VALUE: str
LIBZFS_INCONSISTENT_VALUE: str
LIBZFS_IOERROR_VALUE: str
