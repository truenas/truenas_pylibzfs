"""
Tests for ZFSObject.rename():
  - Rename dataset: basic, cross-pool raises, keyword-only
  - Rename snapshot: basic, recursive on child datasets
  - Rename volume: basic
"""

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

POOL_NAME = 'testpool_rename'


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


# ---------------------------------------------------------------------------
# Dataset rename
# ---------------------------------------------------------------------------

class TestRenameDataset:
    def test_basic(self, pool):
        lz, _, root = pool
        old_name = f'{POOL_NAME}/ds_old'
        new_name = f'{POOL_NAME}/ds_new'
        lz.create_resource(name=old_name, type=truenas_pylibzfs.libzfs_types.ZFSType.ZFS_TYPE_FILESYSTEM)
        try:
            lz.open_resource(name=old_name).rename(new_name=new_name)
            assert lz.open_resource(name=new_name).name == new_name
        finally:
            for name in [new_name, old_name]:
                try:
                    lz.destroy_resource(name=name)
                except Exception:
                    pass

    def test_keyword_only(self, pool):
        lz, _, root = pool
        ds_name = f'{POOL_NAME}/ds_kwonly'
        lz.create_resource(name=ds_name, type=truenas_pylibzfs.libzfs_types.ZFSType.ZFS_TYPE_FILESYSTEM)
        try:
            with pytest.raises(TypeError):
                lz.open_resource(name=ds_name).rename(f'{POOL_NAME}/ds_kwonly_new')
        finally:
            try:
                lz.destroy_resource(name=ds_name)
            except Exception:
                pass

    def test_cross_pool_raises(self, pool):
        lz, _, root = pool
        ds_name = f'{POOL_NAME}/ds_crosspool'
        lz.create_resource(name=ds_name, type=truenas_pylibzfs.libzfs_types.ZFSType.ZFS_TYPE_FILESYSTEM)
        try:
            with pytest.raises((ValueError, truenas_pylibzfs.ZFSException)):
                lz.open_resource(name=ds_name).rename(new_name='otherpool/ds_crosspool')
        finally:
            try:
                lz.destroy_resource(name=ds_name)
            except Exception:
                pass


# ---------------------------------------------------------------------------
# Snapshot rename
# ---------------------------------------------------------------------------

class TestRenameSnapshot:
    def test_basic(self, pool):
        lz, _, root = pool
        snap_old = f'{POOL_NAME}@snap_old'
        snap_new = f'{POOL_NAME}@snap_new'
        lzc.create_snapshots(snapshot_names=[snap_old])
        try:
            lz.open_resource(name=snap_old).rename(new_name=snap_new)
            assert lz.open_resource(name=snap_new).name == snap_new
        finally:
            for s in [snap_new, snap_old]:
                try:
                    lzc.destroy_snapshots(snapshot_names=[s])
                except Exception:
                    pass

    def test_recursive_renames_children(self, pool):
        lz, _, root = pool
        parent = f'{POOL_NAME}/recparen'
        child = f'{POOL_NAME}/recparen/recchild'
        lz.create_resource(name=parent, type=truenas_pylibzfs.libzfs_types.ZFSType.ZFS_TYPE_FILESYSTEM)
        lz.create_resource(name=child, type=truenas_pylibzfs.libzfs_types.ZFSType.ZFS_TYPE_FILESYSTEM)
        try:
            snap_parent = f'{parent}@recsnap'
            snap_child = f'{child}@recsnap'
            lzc.create_snapshots(snapshot_names=[snap_parent])
            lzc.create_snapshots(snapshot_names=[snap_child])

            lz.open_resource(name=snap_parent).rename(
                new_name=f'{parent}@recsnap_renamed', recursive=True
            )

            seen = []

            def cb(s, state):
                state.append(s.name)
                return True

            lz.open_resource(name=parent).iter_snapshots(callback=cb, state=seen)
            assert f'{parent}@recsnap_renamed' in seen
        finally:
            for name in [
                f'{child}@recsnap_renamed', f'{child}@recsnap',
                f'{parent}@recsnap_renamed', f'{parent}@recsnap',
            ]:
                try:
                    lzc.destroy_snapshots(snapshot_names=[name])
                except Exception:
                    pass
            for name in [child, parent]:
                try:
                    lz.destroy_resource(name=name)
                except Exception:
                    pass


# ---------------------------------------------------------------------------
# Volume rename
# ---------------------------------------------------------------------------

class TestRenameVolume:
    def test_basic(self, pool):
        lz, _, root = pool
        vol_old = f'{POOL_NAME}/vol_old'
        vol_new = f'{POOL_NAME}/vol_new'
        lz.create_resource(
            name=vol_old,
            type=truenas_pylibzfs.libzfs_types.ZFSType.ZFS_TYPE_VOLUME,
            properties={truenas_pylibzfs.libzfs_types.ZFSProperty.VOLSIZE: 64 * 1024 * 1024},
        )
        try:
            lz.open_resource(name=vol_old).rename(new_name=vol_new)
            assert lz.open_resource(name=vol_new).name == vol_new
        finally:
            for name in [vol_new, vol_old]:
                try:
                    lz.destroy_resource(name=name)
                except Exception:
                    pass
