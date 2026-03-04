"""
Tests for ZFSResource.iter_filesystems(), ZFSResource.iter_snapshots(),
ZFS.iter_pools(), and ZFS.iter_root_filesystems().

Covers:
  - iter_filesystems collects child datasets
  - iter_filesystems callback returning False stops iteration
  - iter_filesystems state object passed through to callback
  - iter_filesystems fast=True yields objects with accessible .name
  - iter_snapshots collects snapshots
  - iter_snapshots TXG ordering and min_transaction_group filtering
  - iter_snapshots callback returning False stops iteration
  - iter_pools sees the test pool
  - iter_pools state passed through
  - iter_root_filesystems sees the root FS of the test pool
"""

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

POOL_NAME = 'testpool_iter'


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


@pytest.fixture
def pool_with_children(pool):
    """Pool with 2 child filesystems."""
    lz, p, root = pool
    child1 = f'{POOL_NAME}/child1'
    child2 = f'{POOL_NAME}/child2'
    lz.create_resource(name=child1, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
    lz.create_resource(name=child2, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
    try:
        yield lz, p, root
    finally:
        for name in [child2, child1]:
            try:
                lz.destroy_resource(name=name)
            except Exception:
                pass


@pytest.fixture
def pool_with_snapshots(pool):
    """Pool root with 2 snapshots."""
    lz, p, root = pool
    snap1 = f'{POOL_NAME}@snap1'
    snap2 = f'{POOL_NAME}@snap2'
    lzc.create_snapshots(snapshot_names=[snap1])
    lzc.create_snapshots(snapshot_names=[snap2])
    try:
        yield lz, p, root
    finally:
        for snap in [snap2, snap1]:
            try:
                lzc.destroy_snapshots(snapshot_names=[snap])
            except Exception:
                pass


# ---------------------------------------------------------------------------
# iter_filesystems
# ---------------------------------------------------------------------------

class TestIterFilesystems:
    def test_collects_children(self, pool_with_children):
        lz, p, root = pool_with_children
        seen = []

        def cb(ds, state):
            state.append(ds.name)
            return True

        root.iter_filesystems(callback=cb, state=seen)
        assert f'{POOL_NAME}/child1' in seen
        assert f'{POOL_NAME}/child2' in seen

    def test_callback_stop(self, pool_with_children):
        lz, p, root = pool_with_children
        seen = []

        def cb(ds, state):
            state.append(ds.name)
            return False  # stop after first

        result = root.iter_filesystems(callback=cb, state=seen)
        assert result is False
        assert len(seen) == 1

    def test_state_passed_to_callback(self, pool_with_children):
        lz, p, root = pool_with_children
        collector = []

        def cb(ds, state):
            state.append(ds.name)
            return True

        root.iter_filesystems(callback=cb, state=collector)
        assert len(collector) >= 2

    def test_fast_mode(self, pool_with_children):
        lz, p, root = pool_with_children
        seen = []

        def cb(ds, state):
            state.append(ds.name)
            return True

        root.iter_filesystems(callback=cb, state=seen, fast=True)
        assert f'{POOL_NAME}/child1' in seen
        assert f'{POOL_NAME}/child2' in seen

    def test_keyword_only(self, pool):
        lz, p, root = pool

        def cb(ds, state):
            return True

        with pytest.raises(TypeError):
            root.iter_filesystems(cb, None)

    def test_no_children_returns_true(self, pool):
        lz, p, root = pool
        seen = []

        def cb(ds, state):
            state.append(ds.name)
            return True

        result = root.iter_filesystems(callback=cb, state=seen)
        assert result is True
        assert seen == []


# ---------------------------------------------------------------------------
# iter_snapshots
# ---------------------------------------------------------------------------

class TestIterSnapshots:
    def test_collects_snapshots(self, pool_with_snapshots):
        lz, p, root = pool_with_snapshots
        seen = []

        def cb(snap, state):
            state.append(snap.name)
            return True

        root.iter_snapshots(callback=cb, state=seen)
        assert f'{POOL_NAME}@snap1' in seen
        assert f'{POOL_NAME}@snap2' in seen

    def test_callback_stop(self, pool_with_snapshots):
        lz, p, root = pool_with_snapshots
        seen = []

        def cb(snap, state):
            state.append(snap.name)
            return False

        result = root.iter_snapshots(callback=cb, state=seen)
        assert result is False
        assert len(seen) == 1

    def test_state_passed_to_callback(self, pool_with_snapshots):
        lz, p, root = pool_with_snapshots
        collector = []

        def cb(snap, state):
            state.append(snap.name)
            return True

        root.iter_snapshots(callback=cb, state=collector)
        assert len(collector) >= 2

    def test_order_by_txg(self, pool_with_snapshots):
        lz, p, root = pool_with_snapshots
        txgs = []

        def cb(snap, state):
            txg = snap.createtxg
            if txg is not None:
                state.append(int(txg))
            return True

        root.iter_snapshots(callback=cb, state=txgs, order_by_transaction_group=True)
        if len(txgs) >= 2:
            assert txgs == sorted(txgs), 'TXG order not ascending'

    def test_min_txg_filter(self, pool_with_snapshots):
        lz, p, root = pool_with_snapshots
        all_snaps = []

        def cb_all(snap, state):
            txg = snap.createtxg
            if txg is not None:
                state.append((snap.name, int(txg)))
            return True

        root.iter_snapshots(callback=cb_all, state=all_snaps, order_by_transaction_group=True)
        if len(all_snaps) < 2:
            pytest.skip('need at least 2 snapshots with TXG info')

        cutoff_txg = all_snaps[1][1]
        filtered = []

        def cb_filtered(snap, state):
            state.append(snap.name)
            return True

        root.iter_snapshots(
            callback=cb_filtered,
            state=filtered,
            min_transaction_group=cutoff_txg,
            order_by_transaction_group=True,
        )
        assert all_snaps[0][0] not in filtered

    def test_keyword_only(self, pool):
        lz, p, root = pool

        def cb(snap, state):
            return True

        with pytest.raises(TypeError):
            root.iter_snapshots(cb, None)

    def test_no_snapshots_returns_true(self, pool):
        lz, p, root = pool
        seen = []

        def cb(snap, state):
            state.append(snap.name)
            return True

        result = root.iter_snapshots(callback=cb, state=seen)
        assert result is True
        assert seen == []


# ---------------------------------------------------------------------------
# iter_pools
# ---------------------------------------------------------------------------

class TestIterPools:
    def test_sees_test_pool(self, pool):
        lz, p, root = pool
        seen = []

        def cb(pool_obj, state):
            state.append(pool_obj.name)
            return True

        lz.iter_pools(callback=cb, state=seen)
        assert POOL_NAME in seen

    def test_state_passed(self, pool):
        lz, p, root = pool
        counter = [0]

        def cb(pool_obj, state):
            state[0] += 1
            return True

        lz.iter_pools(callback=cb, state=counter)
        assert counter[0] >= 1

    def test_callback_stop(self, pool):
        lz, p, root = pool
        seen = []

        def cb(pool_obj, state):
            state.append(pool_obj.name)
            return False

        result = lz.iter_pools(callback=cb, state=seen)
        assert result is False
        assert len(seen) == 1

    def test_keyword_only(self, pool):
        lz, p, root = pool

        def cb(pool_obj, state):
            return True

        with pytest.raises(TypeError):
            lz.iter_pools(cb, None)


# ---------------------------------------------------------------------------
# iter_root_filesystems
# ---------------------------------------------------------------------------

class TestIterRootFilesystems:
    def test_sees_test_root(self, pool):
        lz, p, root = pool
        seen = []

        def cb(ds, state):
            state.append(ds.name)
            return True

        lz.iter_root_filesystems(callback=cb, state=seen)
        assert POOL_NAME in seen

    def test_state_passed(self, pool):
        lz, p, root = pool
        counter = [0]

        def cb(ds, state):
            state[0] += 1
            return True

        lz.iter_root_filesystems(callback=cb, state=counter)
        assert counter[0] >= 1

    def test_keyword_only(self, pool):
        lz, p, root = pool

        def cb(ds, state):
            return True

        with pytest.raises(TypeError):
            lz.iter_root_filesystems(cb, None)
