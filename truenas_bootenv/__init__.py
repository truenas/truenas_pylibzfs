"""TrueNAS boot environment management built on truenas_pylibzfs.

Replaces the zectl activate/create/destroy commands consumed by
TrueNAS middleware, which imports the engine directly from
truenas_bootenv.engine.

NOTE: do not import .engine (or anything else that pulls in
truenas_pylibzfs) here. argparse-manpage imports truenas_bootenv.cli
at Debian package build time to generate the manpage, and the build
chroot has no ZFS kernel module -- truenas_pylibzfs calls
libzfs_init() on import and would abort the build. Also keeps
`truenas-bootenv --help` working on hosts without ZFS.
"""

from .errors import (
    BEError,
    BENotFound,
    BEExists,
    BEBusy,
    BEDestroyUnsafe,
    BEGrubError,
)

__all__ = [
    "BEError",
    "BENotFound",
    "BEExists",
    "BEBusy",
    "BEDestroyUnsafe",
    "BEGrubError",
]
