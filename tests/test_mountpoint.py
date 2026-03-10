"""
Tests for ZFSResource.get_mountpoint():
  - Mounted dataset with default mountpoint returns str
  - Dataset with legacy mountpoint returns None
  - Unmounted dataset returns None
  - Remount after unmount returns non-None
"""

import pytest
import truenas_pylibzfs

POOL_NAME = 'testpool_mntpt'


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
