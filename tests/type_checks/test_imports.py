"""
Verify top-level re-exports from truenas_pylibzfs are accessible and correctly typed.

These functions are never called at runtime; they exist solely to be checked by mypy.
A type error here means a stub re-export or signature is broken.
"""
from truenas_pylibzfs import (
    PropertySource,
    VDevType,
    ZFSError,
    ZFSException,
    ZFSProperty,
    ZFSType,
    ZFSUserQuota,
    ZPOOLProperty,
    ZPOOLStatus,
    clear_label,
    create_vdev_spec,
    name_is_valid,
    open_handle,
    read_label,
)
from truenas_pylibzfs import libzfs_types, lzc, property_sets


def check_enum_members() -> None:
    src: PropertySource = PropertySource.LOCAL
    err: ZFSError = ZFSError.EZFS_SUCCESS
    ztype: ZFSType = ZFSType.ZFS_TYPE_FILESYSTEM
    prop: ZFSProperty = ZFSProperty.COMPRESSION
    pprop: ZPOOLProperty = ZPOOLProperty.HEALTH
    pstatus: ZPOOLStatus = ZPOOLStatus.ZPOOL_STATUS_OK
    vtype: VDevType = VDevType.MIRROR
    quota: ZFSUserQuota = ZFSUserQuota.USER_QUOTA
    _ = (src, err, ztype, prop, pprop, pstatus, vtype, quota)


def check_name_is_valid() -> None:
    result: bool = name_is_valid(name="pool/fs", type=ZFSType.ZFS_TYPE_FILESYSTEM)
    _ = result


def check_create_vdev_spec_disk() -> None:
    spec: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.DISK,
        name="/dev/sda",
    )
    _ = spec


def check_create_vdev_spec_mirror() -> None:
    disk1: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.DISK, name="/dev/sda"
    )
    disk2: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.DISK, name="/dev/sdb"
    )
    mirror: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[disk1, disk2],
    )
    _ = mirror


def check_property_sets() -> None:
    props: frozenset[ZPOOLProperty] = property_sets.ZPOOL_PROPERTIES
    readonly: frozenset[ZPOOLProperty] = property_sets.ZPOOL_READONLY_PROPERTIES
    recoverable: frozenset[ZPOOLStatus] = property_sets.ZPOOL_STATUS_RECOVERABLE
    nonrecoverable: frozenset[ZPOOLStatus] = property_sets.ZPOOL_STATUS_NONRECOVERABLE
    _ = (props, readonly, recoverable, nonrecoverable)


def check_submodules_accessible() -> None:
    # Verify submodules are importable and carry the expected types.
    _lzc = lzc
    _libzfs_types = libzfs_types
    _property_sets = property_sets
    _ = (_lzc, _libzfs_types, _property_sets)


# Suppress unused-import warnings for names imported only to verify they resolve.
__all__ = [
    "ZFSException",
    "open_handle",
    "read_label",
    "clear_label",
]
