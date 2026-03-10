"""
tests/test_add_vdevs.py — test suite for ZFSPool.add_vdevs()

Sections:
  1. Argument / spec validation — no real disks, no ZFS calls
  2. Topology-match validation against an existing pool (needs real disks)
  3. Successful add operations (needs real disks)
  4. force=True bypass tests
"""

import os
import pytest
import shutil
import tempfile
import truenas_pylibzfs

VDevType = truenas_pylibzfs.VDevType
ZPOOLStatus = truenas_pylibzfs.libzfs_types.ZPOOLStatus

POOL_NAME = "testpool_add_vdevs_pylibzfs"
DISK_SZ = 128 * 1024 * 1024  # 128 MiB


# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def make_disks():
    """Factory: make_disks(n) → list of n sparse image paths."""
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix="pylibzfs_addvdev_")
        dirs.append(d)
        paths = []
        for i in range(n):
            p = os.path.join(d, f"d{i}.img")
            with open(p, "w") as f:
                os.ftruncate(f.fileno(), DISK_SZ)
            paths.append(p)
        return paths

    yield _make

    for d in dirs:
        shutil.rmtree(d, ignore_errors=True)


def _spec(path):
    """File-type leaf vdev spec."""
    return truenas_pylibzfs.create_vdev_spec(vdev_type=VDevType.FILE, name=path)


def _mirror(d0, d1):
    return truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR, children=[_spec(d0), _spec(d1)]
    )


def _raidz(level, disks):
    vt = {1: VDevType.RAIDZ1, 2: VDevType.RAIDZ2, 3: VDevType.RAIDZ3}[level]
    return truenas_pylibzfs.create_vdev_spec(
        vdev_type=vt, children=[_spec(d) for d in disks]
    )


def _destroy(lz=None):
    try:
        h = lz or truenas_pylibzfs.open_handle()
        h.destroy_pool(name=POOL_NAME, force=True)
    except Exception:
        pass


def _create_stripe_pool(lz, disk):
    lz.create_pool(name=POOL_NAME, storage_vdevs=[_spec(disk)], force=True)
    return lz.open_pool(name=POOL_NAME)


def _create_mirror_pool(lz, d0, d1):
    lz.create_pool(
        name=POOL_NAME,
        storage_vdevs=[_mirror(d0, d1)],
        force=True,
    )
    return lz.open_pool(name=POOL_NAME)


def _create_raidz_pool(lz, level, disks):
    lz.create_pool(
        name=POOL_NAME,
        storage_vdevs=[_raidz(level, disks)],
        force=True,
    )
    return lz.open_pool(name=POOL_NAME)


# ---------------------------------------------------------------------------
# Section 1 — argument / spec validation (no real disks needed)
# ---------------------------------------------------------------------------

class TestAddVdevsArgValidation:
    """Validation errors that fire before any kernel calls."""

    def test_no_args_raises_valueerror(self):
        lz = truenas_pylibzfs.open_handle()
        # We need an open pool handle — but we can open a non-existent one
        # only if we have a real pool. Instead, use a fake file path to
        # trigger the spec-level check before any libzfs call.
        # The "at least one category non-empty" check fires first.
        # We can't easily get a pool handle without a real pool, so we test
        # the underlying validate_add_topology path via the pool after creation.
        pass  # covered by test_all_none_raises_valueerror below

    def test_all_none_raises_valueerror(self, make_disks):
        """add_vdevs() with no categories at all must raise ValueError."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(ValueError, match="at least one"):
                pool.add_vdevs()
        finally:
            _destroy(lz)

    def test_all_explicit_none_raises_valueerror(self, make_disks):
        """add_vdevs(storage_vdevs=None, ...) must raise ValueError."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(
                    storage_vdevs=None,
                    cache_vdevs=None,
                    log_vdevs=None,
                    special_vdevs=None,
                    dedup_vdevs=None,
                    spare_vdevs=None,
                )
        finally:
            _destroy(lz)

    def test_returns_none_on_success(self, make_disks):
        """add_vdevs() must return None on success."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            result = pool.add_vdevs(
                storage_vdevs=[_spec(disks[1])],
                force=True,
            )
            assert result is None
        finally:
            _destroy(lz)

    def test_keyword_only_args(self, make_disks):
        """add_vdevs does not accept positional arguments."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(TypeError):
                pool.add_vdevs([_spec(disks[0])])
        finally:
            _destroy(lz)

    def test_invalid_spec_type_raises(self, make_disks):
        """Non-spec objects in a vdev list must raise TypeError."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(TypeError):
                pool.add_vdevs(spare_vdevs=["not-a-spec"])
        finally:
            _destroy(lz)

    def test_cache_mirror_rejected(self, make_disks):
        """cache_vdevs must be leaf only — mirror must be rejected."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        cache_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(disks[1]), _spec(disks[2])],
        )
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(cache_vdevs=[cache_mirror])
        finally:
            _destroy(lz)

    def test_cache_raidz_rejected(self, make_disks):
        """cache_vdevs must be leaf only — raidz must be rejected."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        cache_raidz = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.RAIDZ1,
            children=[_spec(disks[1]), _spec(disks[2]), _spec(disks[3])],
        )
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(cache_vdevs=[cache_raidz])
        finally:
            _destroy(lz)

    def test_spare_mirror_rejected(self, make_disks):
        """spare_vdevs must be leaf only — mirror must be rejected."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        spare_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(disks[1]), _spec(disks[2])],
        )
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(spare_vdevs=[spare_mirror])
        finally:
            _destroy(lz)

    def test_log_raidz_rejected(self, make_disks):
        """log_vdevs must be leaf or mirror — raidz must be rejected."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        log_raidz = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.RAIDZ1,
            children=[_spec(disks[1]), _spec(disks[2]), _spec(disks[3])],
        )
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(log_vdevs=[log_raidz])
        finally:
            _destroy(lz)

    def test_special_draid_rejected(self, make_disks):
        """dRAID is never allowed in special_vdevs."""
        disks = make_disks(5)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        special_draid = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.DRAID1,
            name="3d:0s",
            children=[_spec(d) for d in disks[1:]],
        )
        try:
            with pytest.raises(ValueError, match="dRAID"):
                pool.add_vdevs(special_vdevs=[special_draid])
        finally:
            _destroy(lz)

    def test_dedup_draid_rejected(self, make_disks):
        """dRAID is never allowed in dedup_vdevs."""
        disks = make_disks(5)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        dedup_draid = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.DRAID1,
            name="3d:0s",
            children=[_spec(d) for d in disks[1:]],
        )
        try:
            with pytest.raises(ValueError, match="dRAID"):
                pool.add_vdevs(dedup_vdevs=[dedup_draid])
        finally:
            _destroy(lz)


# ---------------------------------------------------------------------------
# Section 2 — topology-match validation against existing pool
# ---------------------------------------------------------------------------

class TestAddVdevsTopologyValidation:
    """Python-level topology checks against the existing pool geometry."""

    def test_add_mirror_to_stripe_pool_rejected(self, make_disks):
        """Adding a mirror to a stripe pool must be rejected (type mismatch)."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        new_mirror = _mirror(disks[1], disks[2])
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(storage_vdevs=[new_mirror])
        finally:
            _destroy(lz)

    def test_add_stripe_to_mirror_pool_rejected(self, make_disks):
        """Adding a stripe (leaf) to a mirror pool must be rejected."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(storage_vdevs=[_spec(disks[2])])
        finally:
            _destroy(lz)

    def test_add_raidz2_to_raidz1_pool_rejected(self, make_disks):
        """Parity mismatch: raidz2 cannot be added to a raidz1 pool."""
        disks = make_disks(7)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 1, disks[:3])
        new_raidz2 = _raidz(2, disks[3:7])
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(storage_vdevs=[new_raidz2])
        finally:
            _destroy(lz)

    def test_add_raidz1_to_raidz2_pool_rejected(self, make_disks):
        """Parity mismatch: raidz1 cannot be added to a raidz2 pool."""
        disks = make_disks(7)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 2, disks[:4])
        new_raidz1 = _raidz(1, disks[4:7])
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(storage_vdevs=[new_raidz1])
        finally:
            _destroy(lz)

    def test_add_wider_raidz1_rejected(self, make_disks):
        """Child count mismatch: raidz1/4 cannot be added to a raidz1/3 pool."""
        disks = make_disks(7)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 1, disks[:3])
        # raidz1 with 4 children (wider)
        wider = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.RAIDZ1,
            children=[_spec(d) for d in disks[3:7]],
        )
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(storage_vdevs=[wider])
        finally:
            _destroy(lz)

    def test_add_narrower_mirror_rejected(self, make_disks):
        """Cannot add a 2-disk mirror to a 3-disk mirror pool."""
        disks = make_disks(6)
        lz = truenas_pylibzfs.open_handle()
        # Create 3-disk mirror pool
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[
                truenas_pylibzfs.create_vdev_spec(
                    vdev_type=VDevType.MIRROR,
                    children=[_spec(disks[0]), _spec(disks[1]), _spec(disks[2])],
                )
            ],
            force=True,
        )
        pool = lz.open_pool(name=POOL_NAME)
        # New mirror only has 2 disks
        narrow_mirror = _mirror(disks[3], disks[4])
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(storage_vdevs=[narrow_mirror])
        finally:
            _destroy(lz)

    def test_add_mirror_to_raidz_pool_rejected(self, make_disks):
        """Type mismatch: mirror cannot be added to a raidz1 pool."""
        disks = make_disks(5)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 1, disks[:3])
        new_mirror = _mirror(disks[3], disks[4])
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(storage_vdevs=[new_mirror])
        finally:
            _destroy(lz)

    def test_special_parity_too_low_for_raidz2_pool_rejected(self, make_disks):
        """special vdev with parity < pool storage parity must be rejected."""
        disks = make_disks(7)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 2, disks[:4])
        # mirror (parity 1) < raidz2 (parity 2)
        special_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(disks[4]), _spec(disks[5])],
        )
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(special_vdevs=[special_mirror])
        finally:
            _destroy(lz)

    def test_dedup_parity_too_low_for_raidz2_pool_rejected(self, make_disks):
        """dedup vdev with parity < pool storage parity must be rejected."""
        disks = make_disks(6)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 2, disks[:4])
        # leaf dedup (parity 0) < raidz2 (parity 2)
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(dedup_vdevs=[_spec(disks[4])])
        finally:
            _destroy(lz)

    def test_special_parity_too_low_for_raidz1_pool_rejected(self, make_disks):
        """Leaf special (parity 0) < raidz1 (parity 1) must be rejected."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 1, disks[:3])
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(special_vdevs=[_spec(disks[3])])
        finally:
            _destroy(lz)

    def test_empty_storage_list_raises(self, make_disks):
        """Passing an empty list for storage_vdevs must still raise ValueError."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(storage_vdevs=[])
        finally:
            _destroy(lz)


# ---------------------------------------------------------------------------
# Section 3 — successful add operations
# ---------------------------------------------------------------------------

class TestAddVdevsSuccess:
    """Real add operations that must succeed and be reflected in pool status."""

    def test_add_stripe_to_stripe_pool(self, make_disks):
        """Adding a second stripe vdev to a stripe pool must succeed."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            pool.add_vdevs(storage_vdevs=[_spec(disks[1])], force=True)
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.storage_vdevs) == 2
        finally:
            _destroy(lz)

    def test_add_matching_mirror_to_mirror_pool(self, make_disks):
        """Adding a 2-disk mirror to a 2-disk mirror pool must succeed."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        new_mirror = _mirror(disks[2], disks[3])
        try:
            pool.add_vdevs(storage_vdevs=[new_mirror])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.storage_vdevs) == 2
        finally:
            _destroy(lz)

    def test_add_matching_raidz1_to_raidz1_pool(self, make_disks):
        """Adding a matching raidz1/3 to a raidz1/3 pool must succeed."""
        disks = make_disks(6)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 1, disks[:3])
        new_raidz1 = _raidz(1, disks[3:6])
        try:
            pool.add_vdevs(storage_vdevs=[new_raidz1])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.storage_vdevs) == 2
        finally:
            _destroy(lz)

    def test_add_matching_raidz2_to_raidz2_pool(self, make_disks):
        """Adding a matching raidz2/4 to a raidz2/4 pool must succeed."""
        disks = make_disks(8)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 2, disks[:4])
        new_raidz2 = _raidz(2, disks[4:8])
        try:
            pool.add_vdevs(storage_vdevs=[new_raidz2])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.storage_vdevs) == 2
        finally:
            _destroy(lz)

    def test_add_cache_to_stripe_pool(self, make_disks):
        """Adding a cache vdev to an existing pool must succeed."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            pool.add_vdevs(cache_vdevs=[_spec(disks[1])])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.cache) == 1
        finally:
            _destroy(lz)

    def test_add_log_leaf_to_pool(self, make_disks):
        """Adding a leaf log vdev to an existing pool must succeed."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            pool.add_vdevs(log_vdevs=[_spec(disks[1])])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.log) == 1
        finally:
            _destroy(lz)

    def test_add_log_mirror_to_pool(self, make_disks):
        """Adding a mirrored log to an existing pool must succeed."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        log_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(disks[1]), _spec(disks[2])],
        )
        try:
            pool.add_vdevs(log_vdevs=[log_mirror])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.log) == 1
        finally:
            _destroy(lz)

    def test_add_spare_to_pool(self, make_disks):
        """Adding a spare vdev to an existing pool must succeed."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            pool.add_vdevs(spare_vdevs=[_spec(disks[1])])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.spares) == 1
        finally:
            _destroy(lz)

    def test_add_special_mirror_to_mirror_pool(self, make_disks):
        """Adding a mirror special vdev to a mirror pool must succeed."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        special_mirror = _mirror(disks[2], disks[3])
        try:
            pool.add_vdevs(special_vdevs=[special_mirror])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.special) == 1
        finally:
            _destroy(lz)

    def test_add_dedup_mirror_to_mirror_pool(self, make_disks):
        """Adding a mirror dedup vdev to a mirror pool must succeed."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        dedup_mirror = _mirror(disks[2], disks[3])
        try:
            pool.add_vdevs(dedup_vdevs=[dedup_mirror])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.dedup) == 1
        finally:
            _destroy(lz)

    def test_add_special_raidz1_to_raidz1_pool(self, make_disks):
        """special raidz1 (parity 1) is valid for raidz1 storage (parity 1)."""
        disks = make_disks(6)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 1, disks[:3])
        special_rz1 = _raidz(1, disks[3:6])
        try:
            pool.add_vdevs(special_vdevs=[special_rz1])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.special) == 1
        finally:
            _destroy(lz)

    def test_add_special_raidz2_to_raidz1_pool(self, make_disks):
        """special raidz2 (parity 2) is valid for raidz1 storage (parity 1)."""
        disks = make_disks(7)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 1, disks[:3])
        special_rz2 = _raidz(2, disks[3:7])
        try:
            pool.add_vdevs(special_vdevs=[special_rz2])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.special) == 1
        finally:
            _destroy(lz)

    def test_add_dedup_leaf_to_stripe_pool(self, make_disks):
        """Leaf dedup (parity 0) is valid for stripe storage (parity 0)."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            pool.add_vdevs(dedup_vdevs=[_spec(disks[1])])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.dedup) == 1
        finally:
            _destroy(lz)

    def test_add_multiple_categories_simultaneously(self, make_disks):
        """Adding cache + log + spare in one call must all appear in status."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            pool.add_vdevs(
                cache_vdevs=[_spec(disks[2])],
                spare_vdevs=[_spec(disks[3])],
            )
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.cache) == 1
            assert len(status.spares) == 1
        finally:
            _destroy(lz)

    def test_add_two_matching_mirrors_at_once(self, make_disks):
        """Adding two matching mirrors in a single call must work."""
        disks = make_disks(6)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        m2 = _mirror(disks[2], disks[3])
        m3 = _mirror(disks[4], disks[5])
        try:
            pool.add_vdevs(storage_vdevs=[m2, m3])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.storage_vdevs) == 3
        finally:
            _destroy(lz)

    def test_add_multiple_caches(self, make_disks):
        """Adding multiple cache vdevs in a single call must work."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            pool.add_vdevs(cache_vdevs=[_spec(disks[1]), _spec(disks[2])])
            status = pool.status()
            assert len(status.support_vdevs.cache) == 2
        finally:
            _destroy(lz)

    def test_add_multiple_spares(self, make_disks):
        """Adding multiple spare vdevs in a single call must work."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            pool.add_vdevs(spare_vdevs=[_spec(disks[1]), _spec(disks[2])])
            status = pool.status()
            assert len(status.spares) == 2
        finally:
            _destroy(lz)

    def test_add_special_leaf_to_stripe_pool(self, make_disks):
        """Leaf special is valid when storage is also stripe (parity 0 >= 0)."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            pool.add_vdevs(special_vdevs=[_spec(disks[1])])
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.support_vdevs.special) == 1
        finally:
            _destroy(lz)


# ---------------------------------------------------------------------------
# Section 4 — force=True bypass
# ---------------------------------------------------------------------------

class TestAddVdevsForce:
    """force=True must bypass Python-level topology checks."""

    def test_force_bypasses_type_mismatch(self, make_disks):
        """force=True bypasses the storage type mismatch check."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        new_mirror = _mirror(disks[1], disks[2])

        # Without force → ValueError
        with pytest.raises(ValueError):
            pool.add_vdevs(storage_vdevs=[new_mirror])

        # With force → no Python ValueError (libzfs may or may not accept it)
        try:
            pool.add_vdevs(storage_vdevs=[new_mirror], force=True)
        except truenas_pylibzfs.ZFSException:
            pass  # libzfs rejection is acceptable
        except ValueError:
            raise  # Python topology check must NOT fire
        finally:
            _destroy(lz)

    def test_force_bypasses_parity_mismatch(self, make_disks):
        """force=True bypasses storage parity mismatch check."""
        disks = make_disks(7)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 1, disks[:3])
        new_raidz2 = _raidz(2, disks[3:7])

        # Without force → ValueError
        with pytest.raises(ValueError):
            pool.add_vdevs(storage_vdevs=[new_raidz2])

        # With force → no Python ValueError
        try:
            pool.add_vdevs(storage_vdevs=[new_raidz2], force=True)
        except truenas_pylibzfs.ZFSException:
            pass
        except ValueError:
            raise
        finally:
            _destroy(lz)

    def test_force_bypasses_special_parity_check(self, make_disks):
        """force=True bypasses the special vdev parity check."""
        disks = make_disks(5)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 2, disks[:4])
        # mirror (parity 1) < raidz2 (parity 2)
        special_mirror = _mirror(disks[3], disks[4])

        with pytest.raises(ValueError):
            pool.add_vdevs(special_vdevs=[special_mirror])

        try:
            pool.add_vdevs(special_vdevs=[special_mirror], force=True)
        except truenas_pylibzfs.ZFSException:
            pass
        except ValueError:
            raise
        finally:
            _destroy(lz)

    def test_force_bypasses_dedup_parity_check(self, make_disks):
        """force=True bypasses the dedup vdev parity check."""
        disks = make_disks(5)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_raidz_pool(lz, 2, disks[:4])
        # leaf dedup (parity 0) < raidz2 (parity 2)
        with pytest.raises(ValueError):
            pool.add_vdevs(dedup_vdevs=[_spec(disks[3])])

        try:
            pool.add_vdevs(dedup_vdevs=[_spec(disks[3])], force=True)
        except truenas_pylibzfs.ZFSException:
            pass
        except ValueError:
            raise
        finally:
            _destroy(lz)

    def test_force_spec_validation_still_runs(self, make_disks):
        """Even with force=True, per-spec validation is not skipped."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(TypeError):
                pool.add_vdevs(cache_vdevs=["not-a-spec"], force=True)
        finally:
            _destroy(lz)

    def test_force_cache_leaf_check_still_runs(self, make_disks):
        """force=True does NOT bypass the cache-must-be-leaf check."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        cache_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(disks[1]), _spec(disks[2])],
        )
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(cache_vdevs=[cache_mirror], force=True)
        finally:
            _destroy(lz)

    def test_force_spare_leaf_check_still_runs(self, make_disks):
        """force=True does NOT bypass the spare-must-be-leaf check."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        spare_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(disks[1]), _spec(disks[2])],
        )
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(spare_vdevs=[spare_mirror], force=True)
        finally:
            _destroy(lz)

    def test_force_log_raidz_check_still_runs(self, make_disks):
        """force=True does NOT bypass the log-must-be-leaf-or-mirror check."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        log_raidz = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.RAIDZ1,
            children=[_spec(disks[1]), _spec(disks[2]), _spec(disks[3])],
        )
        try:
            with pytest.raises(ValueError):
                pool.add_vdevs(log_vdevs=[log_raidz], force=True)
        finally:
            _destroy(lz)

    def test_force_valid_topology_actually_works(self, make_disks):
        """force=True with valid topology: pool gets the new vdev."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        new_mirror = _mirror(disks[2], disks[3])
        try:
            pool.add_vdevs(storage_vdevs=[new_mirror], force=True)
            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.storage_vdevs) == 2
        finally:
            _destroy(lz)
