# This code snippet gives a sample of how to write a context
# manager for taking a temporary hold on a ZFS snapshot

import os
import truenas_pylibzfs

from contextlib import contextmanager


@contextmanager
def get_zfs_fd():
    zfs_fd = os.open(truenas_pylibzfs.constants.ZFS_DEV, os.O_EXCL)
    try:
        yield zfs_fd
    finally:
        os.close(zfs_fd)


@contextmanager
def temporary_holds(holds):
    """
    Context manager to temporarily set the specified holds. When the
    context manager exits, the holds will be released.
    """
    with get_zfs_fd() as z_fd:
        yield truenas_pylibzfs.lzc.create_holds(holds=holds, cleanup_fd=z_fd)



lz = truenas_pylibzfs.open_handle()
assert not lz.open_resource(name='dozer/share@now').get_holds()

with temporary_holds({('dozer/share@now', 'temp_hold')}) as missing:
    assert lz.open_resource(name='dozer/share@now').get_holds()

    os.listdir('/mnt/dozer/share/.zfs/snapshot/now')

assert not lz.open_resource(name='dozer/share@now').get_holds()
