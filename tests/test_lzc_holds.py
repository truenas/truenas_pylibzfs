"""
Tests for lzc.create_holds() and lzc.release_holds().

The `holds` parameter is an iterable of (snapshot_name, tag) tuples for both
create_holds and release_holds.

Covers:
  - create_holds basic
  - release_holds basic
  - hold prevents direct destroy
  - defer destroy + hold lifecycle
  - nonexistent snapshot returned in result tuple
  - keyword-only argument enforcement
"""

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

POOL_NAME = 'testpool_holds'


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


@pytest.fixture
def snapshot(pool):
    lz, _, root = pool
    snap_name = f'{POOL_NAME}@hold_snap'
    lzc.create_snapshots(snapshot_names=[snap_name])
    snap = lz.open_resource(name=snap_name)
    try:
        yield lz, root, snap_name, snap
    finally:
        try:
            lzc.destroy_snapshots(snapshot_names=[snap_name])
        except Exception:
            pass


# ---------------------------------------------------------------------------
# create_holds
# ---------------------------------------------------------------------------

def test_create_holds_basic(snapshot):
    lz, root, snap_name, snap = snapshot
    tag = 'testhold'
    try:
        lzc.create_holds(holds=[(snap_name, tag)])
        assert tag in snap.get_holds()
    finally:
        try:
            lzc.release_holds(holds=[(snap_name, tag)])
        except Exception:
            pass


def test_create_holds_returns_tuple(snapshot):
    lz, root, snap_name, snap = snapshot
    tag = 'testhold_tuple'
    try:
        result = lzc.create_holds(holds=[(snap_name, tag)])
        assert isinstance(result, tuple)
    finally:
        try:
            lzc.release_holds(holds=[(snap_name, tag)])
        except Exception:
            pass


def test_create_holds_nonexistent_snap_in_result(pool):
    lz, _, root = pool
    bogus = f'{POOL_NAME}@doesnotexist_hold'
    # Result is a tuple of (snap_name, errno) pairs for snaps that couldn't be held
    result = lzc.create_holds(holds=[(bogus, 'tag')])
    assert isinstance(result, tuple)
    assert any(entry[0] == bogus for entry in result)


def test_create_holds_keyword_only():
    with pytest.raises(TypeError):
        lzc.create_holds([(f'{POOL_NAME}@snap', 'tag')])


# ---------------------------------------------------------------------------
# release_holds
# ---------------------------------------------------------------------------

def test_release_holds_basic(snapshot):
    lz, root, snap_name, snap = snapshot
    tag = 'reltesthold'
    lzc.create_holds(holds=[(snap_name, tag)])
    assert tag in snap.get_holds()

    lzc.release_holds(holds=[(snap_name, tag)])
    assert tag not in snap.get_holds()


def test_release_holds_returns_none(snapshot):
    lz, root, snap_name, snap = snapshot
    tag = 'reltag_none'
    lzc.create_holds(holds=[(snap_name, tag)])
    result = lzc.release_holds(holds=[(snap_name, tag)])
    assert result is None


def test_release_nonexistent_hold_raises(snapshot):
    lz, root, snap_name, snap = snapshot
    # Releasing a hold that doesn't exist raises ZFSCoreException
    with pytest.raises(lzc.ZFSCoreException):
        lzc.release_holds(holds=[(snap_name, 'nosuchold')])


def test_release_holds_keyword_only():
    with pytest.raises(TypeError):
        lzc.release_holds([(f'{POOL_NAME}@snap', 'tag')])


# ---------------------------------------------------------------------------
# hold prevents direct destroy
# ---------------------------------------------------------------------------

def test_hold_prevents_direct_destroy(snapshot):
    lz, root, snap_name, snap = snapshot
    tag = 'destroyhold'
    lzc.create_holds(holds=[(snap_name, tag)])
    try:
        with pytest.raises(lzc.ZFSCoreException):
            lzc.destroy_snapshots(snapshot_names=[snap_name])
    finally:
        lzc.release_holds(holds=[(snap_name, tag)])


# ---------------------------------------------------------------------------
# defer destroy + hold lifecycle
# ---------------------------------------------------------------------------

def test_defer_destroy_hold_lifecycle(pool):
    lz, _, root = pool
    snap_name = f'{POOL_NAME}@defer_snap'
    tag = 'deferhold'

    lzc.create_snapshots(snapshot_names=[snap_name])
    lzc.create_holds(holds=[(snap_name, tag)])

    # defer_destroy=True succeeds even with an active hold
    lzc.destroy_snapshots(snapshot_names=[snap_name], defer_destroy=True)

    # Snapshot must still exist while hold is active
    seen = []

    def cb(s, state):
        state.append(s.name)
        return True

    root.iter_snapshots(callback=cb, state=seen)
    assert snap_name in seen, 'expected deferred snap to persist while held'

    # Release the hold — snap should now be gone
    lzc.release_holds(holds=[(snap_name, tag)])

    seen2 = []
    root.iter_snapshots(callback=cb, state=seen2)
    assert snap_name not in seen2, 'expected snap gone after hold released'
