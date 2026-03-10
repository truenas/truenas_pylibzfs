"""
Tests for snapshot lifecycle operations:
  - lzc.create_snapshots (basic, with user_properties, errors)
  - lzc.destroy_snapshots (basic, defer with hold)
  - ZFSSnapshot.get_holds and get_clones
  - ZFSSnapshot.clone
  - ZFSDataset.promote
"""

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

POOL_NAME = 'testpool_snapops'


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


def _snap_names(root):
    """Return list of snapshot names on a dataset."""
    seen = []

    def cb(snap, state):
        state.append(snap.name)
        return True

    root.iter_snapshots(callback=cb, state=seen)
    return seen


# ---------------------------------------------------------------------------
# lzc.create_snapshots
# ---------------------------------------------------------------------------

class TestCreateSnapshots:
    def test_basic(self, pool):
        # Two snapshots of the SAME dataset must be created in separate calls
        lz, _, root = pool
        snap1 = f'{POOL_NAME}@cs_snap1'
        snap2 = f'{POOL_NAME}@cs_snap2'
        try:
            lzc.create_snapshots(snapshot_names=[snap1])
            lzc.create_snapshots(snapshot_names=[snap2])
            names = _snap_names(root)
            assert snap1 in names
            assert snap2 in names
        finally:
            for s in [snap1, snap2]:
                try:
                    lzc.destroy_snapshots(snapshot_names=[s])
                except Exception:
                    pass

    def test_with_user_properties(self, pool):
        lz, _, root = pool
        snap = f'{POOL_NAME}@cs_userprop'
        try:
            lzc.create_snapshots(
                snapshot_names=[snap],
                user_properties={'org.truenas:test': 'value'}
            )
            assert snap in _snap_names(root)
        finally:
            try:
                lzc.destroy_snapshots(snapshot_names=[snap])
            except Exception:
                pass

    def test_duplicate_raises(self, pool):
        lz, _, root = pool
        snap = f'{POOL_NAME}@cs_dup'
        lzc.create_snapshots(snapshot_names=[snap])
        try:
            with pytest.raises(Exception):
                lzc.create_snapshots(snapshot_names=[snap])
        finally:
            lzc.destroy_snapshots(snapshot_names=[snap])

    def test_keyword_only(self):
        with pytest.raises(TypeError):
            lzc.create_snapshots([f'{POOL_NAME}@kw'])


# ---------------------------------------------------------------------------
# lzc.destroy_snapshots
# ---------------------------------------------------------------------------

class TestDestroySnapshots:
    def test_basic(self, pool):
        lz, _, root = pool
        snap = f'{POOL_NAME}@ds_snap1'
        lzc.create_snapshots(snapshot_names=[snap])
        assert snap in _snap_names(root)

        lzc.destroy_snapshots(snapshot_names=[snap])
        assert snap not in _snap_names(root)

    def test_keyword_only(self):
        with pytest.raises(TypeError):
            lzc.destroy_snapshots([f'{POOL_NAME}@kw'])


# ---------------------------------------------------------------------------
# ZFSSnapshot.get_holds
# ---------------------------------------------------------------------------

class TestSnapshotGetHolds:
    def test_empty_on_fresh_snapshot(self, pool):
        lz, _, root = pool
        snap_name = f'{POOL_NAME}@holds_fresh'
        lzc.create_snapshots(snapshot_names=[snap_name])
        try:
            snap = lz.open_resource(name=snap_name)
            holds = snap.get_holds()
            assert isinstance(holds, tuple)
            assert len(holds) == 0
        finally:
            lzc.destroy_snapshots(snapshot_names=[snap_name])

    def test_holds_present_after_create_hold(self, pool):
        lz, _, root = pool
        snap_name = f'{POOL_NAME}@holds_test'
        hold_tag = 'myhold'
        lzc.create_snapshots(snapshot_names=[snap_name])
        try:
            lzc.create_holds(holds=[(snap_name, hold_tag)])
            snap = lz.open_resource(name=snap_name)
            holds = snap.get_holds()
            assert isinstance(holds, tuple)
            assert hold_tag in holds
        finally:
            try:
                lzc.release_holds(holds=[(snap_name, hold_tag)])
            except Exception:
                pass
            lzc.destroy_snapshots(snapshot_names=[snap_name])


# ---------------------------------------------------------------------------
# ZFSSnapshot.get_clones
# ---------------------------------------------------------------------------

class TestSnapshotGetClones:
    def test_empty_on_fresh_snapshot(self, pool):
        lz, _, root = pool
        snap_name = f'{POOL_NAME}@clones_fresh'
        lzc.create_snapshots(snapshot_names=[snap_name])
        try:
            snap = lz.open_resource(name=snap_name)
            assert isinstance(snap.get_clones(), tuple)
            assert len(snap.get_clones()) == 0
        finally:
            lzc.destroy_snapshots(snapshot_names=[snap_name])

    def test_clone_name_returned(self, pool):
        lz, _, root = pool
        snap_name = f'{POOL_NAME}@clone_src'
        clone_name = f'{POOL_NAME}/theclone'
        lzc.create_snapshots(snapshot_names=[snap_name])
        try:
            snap = lz.open_resource(name=snap_name)
            snap.clone(name=clone_name)
            # Re-open the snapshot to get a fresh handle with updated clone info
            snap_fresh = lz.open_resource(name=snap_name)
            clones = snap_fresh.get_clones()
            assert isinstance(clones, tuple)
            assert clone_name in clones
        finally:
            try:
                lz.destroy_resource(name=clone_name)
            except Exception:
                pass
            lzc.destroy_snapshots(snapshot_names=[snap_name])


# ---------------------------------------------------------------------------
# ZFSSnapshot.clone
# ---------------------------------------------------------------------------

class TestSnapshotClone:
    def test_basic(self, pool):
        lz, _, root = pool
        snap_name = f'{POOL_NAME}@snap_for_clone'
        clone_name = f'{POOL_NAME}/basic_clone'
        lzc.create_snapshots(snapshot_names=[snap_name])
        try:
            snap = lz.open_resource(name=snap_name)
            snap.clone(name=clone_name)
            assert lz.open_resource(name=clone_name).name == clone_name
        finally:
            try:
                lz.destroy_resource(name=clone_name)
            except Exception:
                pass
            lzc.destroy_snapshots(snapshot_names=[snap_name])

    def test_keyword_only(self, pool):
        lz, _, root = pool
        snap_name = f'{POOL_NAME}@snap_kw'
        lzc.create_snapshots(snapshot_names=[snap_name])
        try:
            snap = lz.open_resource(name=snap_name)
            with pytest.raises(TypeError):
                snap.clone(f'{POOL_NAME}/kw_clone')
        finally:
            lzc.destroy_snapshots(snapshot_names=[snap_name])

    def test_missing_name_raises(self, pool):
        lz, _, root = pool
        snap_name = f'{POOL_NAME}@snap_noname'
        lzc.create_snapshots(snapshot_names=[snap_name])
        try:
            snap = lz.open_resource(name=snap_name)
            with pytest.raises((ValueError, TypeError)):
                snap.clone()
        finally:
            lzc.destroy_snapshots(snapshot_names=[snap_name])


# ---------------------------------------------------------------------------
# ZFSDataset.promote
# ---------------------------------------------------------------------------

class TestPromote:
    def test_dataset_promote_basic(self, pool):
        lz, _, root = pool
        snap_name = f'{POOL_NAME}@snap_promote'
        clone_name = f'{POOL_NAME}/promote_clone'
        lzc.create_snapshots(snapshot_names=[snap_name])
        try:
            snap = lz.open_resource(name=snap_name)
            snap.clone(name=clone_name)
            lz.open_resource(name=clone_name).promote()
        finally:
            try:
                lz.destroy_resource(name=clone_name)
            except Exception:
                pass
            try:
                lzc.destroy_snapshots(snapshot_names=[snap_name])
            except Exception:
                pass

    def test_promote_non_clone_raises(self, pool):
        lz, _, root = pool
        ds_name = f'{POOL_NAME}/notaclone'
        lz.create_resource(name=ds_name, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
        try:
            with pytest.raises(truenas_pylibzfs.ZFSException):
                lz.open_resource(name=ds_name).promote()
        finally:
            lz.destroy_resource(name=ds_name)
