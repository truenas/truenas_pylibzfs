"""
Tests for lzc.rollback():
  - rollback to most recent snapshot returns snap name (str)
  - rollback with explicit snapshot_name
  - rollback to non-most-recent raises MoreRecentSnapshotsExist
    (a subclass of ZFSException) carrying the conflicting snapshots
  - rollback on nonexistent resource raises
  - keyword-only enforcement
"""

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

POOL_NAME = 'testpool_rollback'


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


@pytest.fixture
def dataset_with_snap(pool):
    lz, _, root = pool
    ds_name = f'{POOL_NAME}/rb_ds'
    snap_name = f'{ds_name}@rb_snap1'
    lz.create_resource(name=ds_name, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
    lzc.create_snapshots(snapshot_names=[snap_name])
    try:
        yield lz, ds_name, snap_name
    finally:
        try:
            lzc.destroy_snapshots(snapshot_names=[snap_name])
        except Exception:
            pass
        try:
            lz.destroy_resource(name=ds_name)
        except Exception:
            pass


@pytest.fixture
def dataset_with_two_snaps(pool):
    lz, _, root = pool
    ds_name = f'{POOL_NAME}/rb_ds2'
    snap1 = f'{ds_name}@rb_snap_a'
    snap2 = f'{ds_name}@rb_snap_b'
    lz.create_resource(name=ds_name, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
    lzc.create_snapshots(snapshot_names=[snap1])
    lzc.create_snapshots(snapshot_names=[snap2])
    try:
        yield lz, ds_name, snap1, snap2
    finally:
        for snap in [snap2, snap1]:
            try:
                lzc.destroy_snapshots(snapshot_names=[snap])
            except Exception:
                pass
        try:
            lz.destroy_resource(name=ds_name)
        except Exception:
            pass


# ---------------------------------------------------------------------------
# rollback tests
# ---------------------------------------------------------------------------

def test_rollback_returns_str(dataset_with_snap):
    lz, ds_name, snap_name = dataset_with_snap
    result = lzc.rollback(resource_name=ds_name)
    assert isinstance(result, str)


def test_rollback_to_most_recent_returns_snap_name(dataset_with_snap):
    lz, ds_name, snap_name = dataset_with_snap
    result = lzc.rollback(resource_name=ds_name)
    assert snap_name in result or result == snap_name


def test_rollback_with_named_snapshot(dataset_with_two_snaps):
    lz, ds_name, snap1, snap2 = dataset_with_two_snaps
    # snapshot_name is the snap component only — the function builds
    # the full name as "{resource_name}@{snapshot_name}" internally
    snap2_component = snap2.split('@')[1]
    result = lzc.rollback(resource_name=ds_name, snapshot_name=snap2_component)
    assert isinstance(result, str)


def test_rollback_to_non_recent_raises(dataset_with_two_snaps):
    lz, ds_name, snap1, snap2 = dataset_with_two_snaps
    # snap1 is not the most recent — must raise MoreRecentSnapshotsExist
    snap1_component = snap1.split('@')[1]
    with pytest.raises(truenas_pylibzfs.MoreRecentSnapshotsExist) as exc_info:
        lzc.rollback(resource_name=ds_name, snapshot_name=snap1_component)

    exc = exc_info.value
    # snap2 is more recent than snap1 and therefore blocks the rollback
    assert snap2 in exc.snapshots
    # it is a ZFSException carrying the EZFS_EXISTS code
    assert isinstance(exc, truenas_pylibzfs.ZFSException)
    assert exc.code == truenas_pylibzfs.ZFSError.EZFS_EXISTS


def test_rollback_conflict_list_is_ordered(dataset_with_two_snaps):
    lz, ds_name, snap1, snap2 = dataset_with_two_snaps
    # add a third snapshot, more recent than snap2 (and snap1)
    snap3 = f'{ds_name}@rb_snap_c'
    lzc.create_snapshots(snapshot_names=[snap3])
    try:
        snap1_component = snap1.split('@')[1]
        with pytest.raises(truenas_pylibzfs.MoreRecentSnapshotsExist) as exc_info:
            lzc.rollback(resource_name=ds_name, snapshot_name=snap1_component)

        # conflicting snapshots are returned in ascending creation order
        assert exc_info.value.snapshots == [snap2, snap3]
    finally:
        try:
            lzc.destroy_snapshots(snapshot_names=[snap3])
        except Exception:
            pass


def test_more_recent_snapshots_exist_is_zfs_exception(dataset_with_two_snaps):
    # MoreRecentSnapshotsExist belongs to the ZFS exception hierarchy, so it
    # must be catchable as ZFSException
    assert issubclass(truenas_pylibzfs.MoreRecentSnapshotsExist, truenas_pylibzfs.ZFSException)

    lz, ds_name, snap1, snap2 = dataset_with_two_snaps
    snap1_component = snap1.split('@')[1]
    with pytest.raises(truenas_pylibzfs.ZFSException):
        lzc.rollback(resource_name=ds_name, snapshot_name=snap1_component)


def test_rollback_nonexistent_resource_raises(pool):
    lz, _, root = pool
    with pytest.raises((FileNotFoundError, ValueError, lzc.ZFSCoreException)):
        lzc.rollback(resource_name=f'{POOL_NAME}/nosuchds')


def test_rollback_keyword_only(dataset_with_snap):
    lz, ds_name, snap_name = dataset_with_snap
    with pytest.raises(TypeError):
        lzc.rollback(ds_name)
