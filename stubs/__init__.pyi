from collections.abc import Sequence
from typing import Any, ClassVar

from . import libzfs_types
from . import lzc
from . import property_sets

# Enums that are dual-registered (truenas_pylibzfs.X and truenas_pylibzfs.libzfs_types.X).
# Struct types live in libzfs_types only — access them as libzfs_types.struct_*.
from .libzfs_types import (
    PropertySource,
    VDevType,
    ZFSError,
    ZFSProperty,
    ZFSType,
    ZFSUserQuota,
    ZPOOLProperty,
    ZPOOLStatus,
)

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
    children: "Sequence[libzfs_types.struct_vdev_create_spec] | None" = None,
) -> "libzfs_types.struct_vdev_create_spec":
    """Create a vdev specification for use with ZFS.create_pool().

    For dRAID types, ``name`` encodes the configuration as
    ``"<ndata>d:<nspares>s"`` (e.g. ``"3d:1s"``).  The child count is
    derived automatically from ``len(children)``.
    """
    ...

def open_handle(history: Any = ..., history_prefix: Any = ..., mnttab_cache: Any = ...) -> libzfs_types.ZFS: ...
def read_label(*, fd: int) -> dict[str, Any] | None: ...
def clear_label(*, fd: int) -> None: ...
def name_is_valid(*, name: str, type: ZFSType) -> bool: ...
def fzfs_rewrite(fd: int, *, offset: int = ..., length: int = ..., physical: bool = ...) -> None: ...
