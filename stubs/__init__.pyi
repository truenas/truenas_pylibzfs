from collections.abc import Sequence
from typing import Any, ClassVar

from . import libzfs_types
from . import lzc
from . import property_sets

# Re-export everything from libzfs_types so it's accessible at root level too.
# Enums are dual-registered (truenas_pylibzfs.X and truenas_pylibzfs.libzfs_types.X).
# Types and structs are in libzfs_types only but imported here for convenience.
from .libzfs_types import (
    PropertySource,
    VDevType,
    ZFSError,
    ZFSProperty,
    ZFSType,
    ZFSUserQuota,
    ZPOOLProperty,
    ZPOOLStatus,
    struct_vdev_create_spec,
    struct_vdev_stats,
    struct_vdev,
    struct_support_vdev,
    struct_zpool_feature,
    struct_zpool_scrub,
    struct_zpool_expand,
    struct_zpool_status,
    struct_zfs_property_source,
    struct_zfs_property_data,
    struct_zpool_property_data,
    struct_zpool_property,
    struct_zfs_crypto_info,
    ZFSCrypto,
    ZFSPool,
    ZFSSnapshot,
    ZFS,
)

__all__ = ["libzfs_types", "lzc", "property_sets"]

# ZFS handle type alias (returned by open_handle); use ZFS for isinstance checks
_ZFS = ZFS


class ZFSException(RuntimeError):
    action: ClassVar[str] = ...
    code: ClassVar[int] = ...
    description: ClassVar[str] = ...
    err_str: ClassVar[str] = ...
    location: ClassVar[str] = ...
    name: ClassVar[str] = ...


def create_vdev_spec(
    *,
    vdev_type: "VDevType | str",
    name: str | None = None,
    children: Sequence["struct_vdev_create_spec"] | None = None,
) -> struct_vdev_create_spec:
    """Create a vdev specification for use with ZFS.create_pool().

    For dRAID types, ``name`` encodes the configuration as
    ``"<ndata>d:<nspares>s"`` (e.g. ``"3d:1s"``).  The child count is
    derived automatically from ``len(children)``.
    """
    ...

def open_handle(history: Any = ..., history_prefix: Any = ..., mnttab_cache: Any = ...) -> ZFS: ...
def read_label(*, fd: int) -> dict[str, Any] | None: ...
def clear_label(*, fd: int) -> None: ...
def name_is_valid(*, name: str, type: ZFSType) -> bool: ...
