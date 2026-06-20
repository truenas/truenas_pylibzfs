"""
Tests for ZFSResource.get_mountpoint():
  - Mounted dataset with default mountpoint returns str
  - Dataset with legacy mountpoint returns None
  - Unmounted dataset returns None
  - Remount after unmount returns non-None

Tests for recursive ZFSResource.unmount():
  - A cross-tree island (descendant with its own LOCAL mountpoint mounted
    outside the parent's tree, plus its children) is fully unmounted
  - The encrypted variant additionally unloads the encryption key
"""

import os
import shutil
import tempfile

import pytest
import truenas_pylibzfs

POOL_NAME = 'testpool_mntpt'
PASSPHRASE = 'Cats1234'


def _mounted(lz, name):
    """Return True if `name` is currently mounted, read from a fresh handle.

    The recursive unmount operates on descendant handles internally, so a
    handle opened before the call would report stale mount state; re-opening
    reads the live ZFS_PROP_MOUNTED value.
    """
    rsrc = lz.open_resource(name=name)
    d = rsrc.asdict(properties={truenas_pylibzfs.ZFSProperty.MOUNTED})
    return d['properties']['mounted']['raw'] == 'yes'


def _key_loaded(lz, name):
    return lz.open_resource(name=name).crypto().info().key_is_loaded


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


def test_mounted_dataset_returns_str(pool):
    lz, _, root = pool
    ds_name = f'{POOL_NAME}/mnt_ds'
    lz.create_resource(name=ds_name, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
    try:
        ds = lz.open_resource(name=ds_name)
        ds.mount()
        mp = ds.get_mountpoint()
        assert isinstance(mp, str), f'expected str, got {mp!r}'
        assert len(mp) > 0
    finally:
        try:
            lz.destroy_resource(name=ds_name)
        except Exception:
            pass


def test_unmounted_dataset_returns_none(pool):
    lz, _, root = pool
    ds_name = f'{POOL_NAME}/mnt_unmounted'
    lz.create_resource(name=ds_name, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
    try:
        ds = lz.open_resource(name=ds_name)
        ds.mount()
        ds.unmount()
        assert ds.get_mountpoint() is None
    finally:
        try:
            lz.destroy_resource(name=ds_name)
        except Exception:
            pass


def test_legacy_mountpoint_returns_none(pool):
    lz, _, root = pool
    ds_name = f'{POOL_NAME}/mnt_legacy'
    lz.create_resource(
        name=ds_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        properties={truenas_pylibzfs.ZFSProperty.MOUNTPOINT: 'legacy'},
    )
    try:
        assert lz.open_resource(name=ds_name).get_mountpoint() is None
    finally:
        try:
            lz.destroy_resource(name=ds_name)
        except Exception:
            pass


def test_remount_after_unmount(pool):
    lz, _, root = pool
    ds_name = f'{POOL_NAME}/mnt_remount'
    lz.create_resource(name=ds_name, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
    try:
        ds = lz.open_resource(name=ds_name)
        ds.mount()
        ds.unmount()
        assert ds.get_mountpoint() is None

        ds.mount()
        mp = ds.get_mountpoint()
        assert mp is not None
        assert isinstance(mp, str)
    finally:
        try:
            lz.destroy_resource(name=ds_name)
        except Exception:
            pass


def test_recursive_unmount_clears_cross_tree_island(make_pool):
    """Recursive unmount must clear a descendant whose LOCAL mountpoint lives
    outside the parent's tree, along with that descendant's own children."""
    pool_name = 'testpool_island'
    lz, _, root = make_pool(pool_name)
    base = tempfile.mkdtemp(prefix='pylibzfs_island_')
    island = f'{pool_name}/island'
    child = f'{pool_name}/island/child'
    try:
        lz.create_resource(
            name=island,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
            properties={
                truenas_pylibzfs.ZFSProperty.MOUNTPOINT: os.path.join(base, 'island')
            },
        )
        lz.create_resource(
            name=child,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )
        lz.open_resource(name=island).mount()
        lz.open_resource(name=child).mount()
        assert _mounted(lz, island)
        assert _mounted(lz, child)

        root.unmount(force=True, recursive=True)

        assert not _mounted(lz, island)
        assert not _mounted(lz, child)
    finally:
        shutil.rmtree(base, ignore_errors=True)


def test_recursive_unmount_unloads_key_with_cross_tree_island(make_pool):
    """An encrypted dataset hosting a cross-tree island must, on recursive
    unmount with unload_encryption_key, unmount the whole subtree and unload
    the key without the island leaving it busy.

    The island mountpoint sorts lexically before the encryption root's so that
    the (buggy) mountpoint-ordered unmount would unload the key before the
    island is unmounted -- which is exactly the EBUSY case this guards."""
    pool_name = 'testpool_enc_island'
    lz, _, _ = make_pool(pool_name)
    base = tempfile.mkdtemp(prefix='pylibzfs_enc_island_')
    enc = f'{pool_name}/enc'
    island = f'{pool_name}/enc/island'
    child = f'{pool_name}/enc/island/child'
    try:
        crypto = lz.resource_cryptography_config(
            keyformat='passphrase', key=PASSPHRASE
        )
        lz.create_resource(
            name=enc,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
            crypto=crypto,
            properties={
                truenas_pylibzfs.ZFSProperty.MOUNTPOINT: os.path.join(base, 'z_enc')
            },
        )
        lz.create_resource(
            name=island,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
            properties={
                truenas_pylibzfs.ZFSProperty.MOUNTPOINT: os.path.join(base, 'a_island')
            },
        )
        lz.create_resource(
            name=child,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )
        enc_rsrc = lz.open_resource(name=enc)
        enc_rsrc.mount()
        lz.open_resource(name=island).mount()
        lz.open_resource(name=child).mount()
        assert _mounted(lz, enc)
        assert _mounted(lz, island)
        assert _mounted(lz, child)
        assert _key_loaded(lz, enc) is True

        enc_rsrc.unmount(force=True, recursive=True, unload_encryption_key=True)

        assert not _mounted(lz, enc)
        assert not _mounted(lz, island)
        assert not _mounted(lz, child)
        assert _key_loaded(lz, enc) is False
    finally:
        shutil.rmtree(base, ignore_errors=True)
