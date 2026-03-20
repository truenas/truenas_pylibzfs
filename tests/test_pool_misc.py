"""
Tests for miscellaneous ZFSPool methods:
  - dump_config()
  - root_dataset()
  - prefetch()
  - ddt_prune() argument validation
  - asdict() structure
  - sync_pool() and refresh_stats()
  - upgrade()
"""

import os
import shutil
import tempfile

import pytest
import truenas_pylibzfs

POOL_NAME = 'testpool_misc'
DISK_SZ = 256 * 1048576  # 256 MiB

VDevType = truenas_pylibzfs.VDevType


# ---------------------------------------------------------------------------
# Local fixtures (this branch's conftest lacks make_disks / make_pool)
# ---------------------------------------------------------------------------

@pytest.fixture
def make_disks():
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix='pylibzfs_misc_')
        dirs.append(d)
        paths = []
        for i in range(n):
            p = os.path.join(d, f'd{i}.img')
            with open(p, 'w') as f:
                os.ftruncate(f.fileno(), DISK_SZ)
            paths.append(p)
        return paths

    yield _make

    for d in dirs:
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def pool(make_disks):
    lz = truenas_pylibzfs.open_handle()
    disks = make_disks(1)
    lz.create_pool(
        name=POOL_NAME,
        storage_vdevs=[truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.FILE, name=disks[0],
        )],
        force=True,
    )
    pool_hdl = lz.open_pool(name=POOL_NAME)
    root = lz.open_resource(name=POOL_NAME)

    try:
        yield lz, pool_hdl, root
    finally:
        try:
            lz.destroy_pool(name=POOL_NAME, force=True)
        except Exception:
            pass


# ---------------------------------------------------------------------------
# dump_config
# ---------------------------------------------------------------------------

class TestDumpConfig:
    def test_returns_dict(self, pool):
        _, p, _ = pool
        result = p.dump_config()
        assert isinstance(result, dict)

    def test_contains_pool_name(self, pool):
        _, p, _ = pool
        result = p.dump_config()
        assert POOL_NAME in str(result)

    def test_no_args_required(self, pool):
        _, p, _ = pool
        p.dump_config()  # must not raise


# ---------------------------------------------------------------------------
# root_dataset
# ---------------------------------------------------------------------------

class TestRootDataset:
    def test_returns_object(self, pool):
        _, p, _ = pool
        ds = p.root_dataset()
        assert ds is not None

    def test_name_matches_pool(self, pool):
        _, p, _ = pool
        ds = p.root_dataset()
        assert ds.name == POOL_NAME

    def test_has_name_attribute(self, pool):
        _, p, _ = pool
        ds = p.root_dataset()
        assert hasattr(ds, 'name')


# ---------------------------------------------------------------------------
# prefetch
# ---------------------------------------------------------------------------

class TestPrefetch:
    def test_returns_none(self, pool):
        _, p, _ = pool
        assert p.prefetch() is None

    def test_callable_without_args(self, pool):
        _, p, _ = pool
        p.prefetch()  # must not raise


# ---------------------------------------------------------------------------
# ddt_prune argument validation
# ---------------------------------------------------------------------------

class TestDdtPrune:
    def test_no_args_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError)):
            p.ddt_prune()

    def test_both_args_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError)):
            p.ddt_prune(days=1, percentage=50)

    def test_negative_days_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError, OverflowError)):
            p.ddt_prune(days=-1)

    def test_percentage_zero_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError)):
            p.ddt_prune(percentage=0)

    def test_percentage_over_100_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError)):
            p.ddt_prune(percentage=101)

    def test_keyword_only(self, pool):
        _, p, _ = pool
        with pytest.raises(TypeError):
            p.ddt_prune(1)


# ---------------------------------------------------------------------------
# sync_pool and refresh_stats
# ---------------------------------------------------------------------------

class TestSyncAndRefresh:
    def test_sync_pool_returns_none(self, pool):
        _, p, _ = pool
        assert p.sync_pool() is None

    def test_refresh_stats_callable(self, pool):
        _, p, _ = pool
        result = p.refresh_stats()
        assert result is None or isinstance(result, dict)


# ---------------------------------------------------------------------------
# upgrade
# ---------------------------------------------------------------------------

UPGRADE_POOL_NAME = 'testpool_upgrade'

# Leaf features that have no dependents and can actually stay disabled.
DISABLED_FEATURES = {'head_errlog': False, 'edonr': False}


@pytest.fixture
def degraded_pool(make_disks):
    """Create a pool with selected features disabled via feature_properties."""
    lz = truenas_pylibzfs.open_handle()
    disks = make_disks(1)
    lz.create_pool(
        name=UPGRADE_POOL_NAME,
        storage_vdevs=[truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.FILE, name=disks[0],
        )],
        feature_properties=DISABLED_FEATURES,
        force=True,
    )
    pool_hdl = lz.open_pool(name=UPGRADE_POOL_NAME)

    try:
        yield lz, pool_hdl
    finally:
        try:
            lz.destroy_pool(name=UPGRADE_POOL_NAME, force=True)
        except Exception:
            pass


class TestUpgrade:
    def test_upgrade_enables_features(self, degraded_pool):
        lz, p = degraded_pool

        features_before = p.get_features(asdict=True)
        disabled_before = [
            k for k, v in features_before.items()
            if v['state'] == 'DISABLED'
        ]
        assert len(disabled_before) > 0, \
            "Pool should have disabled features"

        p.upgrade()

        p2 = lz.open_pool(name=UPGRADE_POOL_NAME)
        features_after = p2.get_features(asdict=True)
        disabled_after = [
            k for k, v in features_after.items()
            if v['state'] == 'DISABLED'
        ]
        assert len(disabled_after) == 0, \
            f"Features still disabled after upgrade: {disabled_after}"

    def test_upgrade_returns_none(self, pool):
        _, p, _ = pool
        assert p.upgrade() is None

    def test_upgrade_idempotent(self, pool):
        _, p, _ = pool
        p.upgrade()  # already fully upgraded — must not raise
