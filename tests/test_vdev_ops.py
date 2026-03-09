"""
tests/test_vdev_ops.py — test suite for ZFSPool vdev lifecycle methods:
  attach_vdev, replace_vdev, detach_vdev, remove_vdev, cancel_remove_vdev

Sections:
  1. Argument validation — no real disks needed for missing-arg checks
  2. attach_vdev — file-backed images
  3. replace_vdev — file-backed images
  4. detach_vdev — file-backed images
  5. remove_vdev — file-backed images (spare removal)
  6. cancel_remove_vdev — no removal in progress → ZFSError
"""

import os
import pytest
import shutil
import tempfile
import truenas_pylibzfs

VDevType = truenas_pylibzfs.VDevType
ZPOOLStatus = truenas_pylibzfs.ZPOOLStatus

POOL_NAME = "testpool_vdevops_pylibzfs"
DISK_SZ = 128 * 1024 * 1024  # 128 MiB


# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def make_disks():
    """Factory: make_disks(n) → list of n sparse image file paths."""
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix="pylibzfs_vdevops_")
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


def _create_pool_with_spare(lz, disk, spare):
    lz.create_pool(
        name=POOL_NAME,
        storage_vdevs=[_spec(disk)],
        spare_vdevs=[_spec(spare)],
        force=True,
    )
    return lz.open_pool(name=POOL_NAME)


# ---------------------------------------------------------------------------
# Section 1 — argument validation
# ---------------------------------------------------------------------------

class TestVdevOpsArgValidation:
    """Validation errors that fire before any kernel call."""

    def test_attach_missing_device_raises(self, make_disks):
        """attach_vdev() without device must raise ValueError."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(ValueError, match="device"):
                pool.attach_vdev(new_device=_spec(disks[1]))
        finally:
            _destroy(lz)

    def test_attach_missing_new_device_raises(self, make_disks):
        """attach_vdev() without new_device must raise ValueError."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(ValueError, match="new_device"):
                pool.attach_vdev(device=disks[0])
        finally:
            _destroy(lz)

    def test_detach_missing_device_raises(self, make_disks):
        """detach_vdev() without device must raise ValueError."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            with pytest.raises(ValueError, match="device"):
                pool.detach_vdev()
        finally:
            _destroy(lz)

    def test_remove_missing_device_raises(self, make_disks):
        """remove_vdev() without device must raise ValueError."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_pool_with_spare(lz, disks[0], disks[1])
        try:
            with pytest.raises(ValueError, match="device"):
                pool.remove_vdev()
        finally:
            _destroy(lz)

    def test_replace_missing_device_raises(self, make_disks):
        """replace_vdev() without device must raise ValueError."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            with pytest.raises(ValueError, match="device"):
                pool.replace_vdev()
        finally:
            _destroy(lz)

    def test_attach_keyword_only(self, make_disks):
        """attach_vdev() must not accept positional arguments."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(TypeError):
                pool.attach_vdev(disks[0], _spec(disks[1]))
        finally:
            _destroy(lz)


# ---------------------------------------------------------------------------
# Section 2 — attach_vdev
# ---------------------------------------------------------------------------

class TestAttachVdev:
    """attach_vdev() — file-backed vdev operations."""

    def test_attach_converts_stripe_to_mirror(self, make_disks):
        """Attaching a device to a single-disk pool creates a 2-way mirror."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            result = pool.attach_vdev(device=disks[0], new_device=_spec(disks[1]))
            assert result is None

            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            # Pool should now have a mirror top-level vdev
            assert len(status.storage_vdevs) == 1
            mirror = status.storage_vdevs[0]
            assert mirror.vdev_type == "mirror"
            assert len(mirror.children) == 2
        finally:
            _destroy(lz)

    def test_attach_expands_2way_mirror_to_3way(self, make_disks):
        """Attaching a device to a 2-way mirror creates a 3-way mirror."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            pool.attach_vdev(device=disks[0], new_device=_spec(disks[2]))

            status = pool.status()
            assert status.status in (
                ZPOOLStatus.ZPOOL_STATUS_OK,
                ZPOOLStatus.ZPOOL_STATUS_RESILVERING,
            )
            mirror = status.storage_vdevs[0]
            assert mirror.vdev_type == "mirror"
            assert len(mirror.children) == 3
        finally:
            _destroy(lz)

    def test_attach_mirror_width_limit_rejected(self, make_disks):
        """Attaching to a 4-wide mirror (would make 5) must raise ValueError."""
        disks = make_disks(5)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            # 2-way → 3-way: fine (resulting width 3 <= 4)
            pool.attach_vdev(device=disks[0], new_device=_spec(disks[2]))
            # 3-way → 4-way: fine (resulting width 4 == limit, still allowed)
            pool.attach_vdev(device=disks[0], new_device=_spec(disks[3]))
            # 4-way → 5-way: rejected (resulting width 5 > 4)
            with pytest.raises(ValueError, match="mirror width"):
                pool.attach_vdev(device=disks[0], new_device=_spec(disks[4]))
        finally:
            _destroy(lz)

    def test_attach_mirror_width_limit_bypassed_by_force(self, make_disks):
        """force=True bypasses the mirror width limit."""
        disks = make_disks(6)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            pool.attach_vdev(device=disks[0], new_device=_spec(disks[2]))
            pool.attach_vdev(device=disks[0], new_device=_spec(disks[3]))
            # 4-way → 5-way with force=True: no ValueError
            pool.attach_vdev(
                device=disks[0], new_device=_spec(disks[4]), force=True
            )
            status = pool.status()
            assert status.storage_vdevs[0].vdev_type == "mirror"
            assert len(status.storage_vdevs[0].children) == 5
        finally:
            _destroy(lz)

    def test_attach_returns_none(self, make_disks):
        """attach_vdev() must return None on success."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            result = pool.attach_vdev(device=disks[0], new_device=_spec(disks[1]))
            assert result is None
        finally:
            _destroy(lz)


# ---------------------------------------------------------------------------
# Section 3 — replace_vdev
# ---------------------------------------------------------------------------

class TestReplaceVdev:
    """replace_vdev() — file-backed vdev operations."""

    def test_replace_device_in_mirror(self, make_disks):
        """Replacing one mirror member with a new device must succeed."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            result = pool.replace_vdev(
                device=disks[0], new_device=_spec(disks[2])
            )
            assert result is None

            status = pool.status()
            assert status.status in (
                ZPOOLStatus.ZPOOL_STATUS_OK,
                ZPOOLStatus.ZPOOL_STATUS_RESILVERING,
            )
        finally:
            _destroy(lz)

    def test_replace_self_file_vdev(self, make_disks):
        """replace_vdev with explicit same spec must not raise ValueError."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            # Pass new_device=same spec; accept ZFSError (libzfs may reject)
            try:
                pool.replace_vdev(device=disks[0], new_device=_spec(disks[0]))
            except truenas_pylibzfs.ZFSException:
                pass  # libzfs rejection is acceptable
            except ValueError:
                raise  # Python-level error is not acceptable
        finally:
            _destroy(lz)

    def test_replace_none_new_device_file_vdev(self, make_disks):
        """replace_vdev(new_device=None) must not raise Python exceptions."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            # new_device=None triggers self-replace; ZFSError is acceptable
            try:
                pool.replace_vdev(device=disks[0])
            except truenas_pylibzfs.ZFSException:
                pass  # libzfs rejection is acceptable
            except ValueError:
                raise
        finally:
            _destroy(lz)

    def test_replace_returns_none(self, make_disks):
        """replace_vdev() must return None on success."""
        disks = make_disks(3)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            result = pool.replace_vdev(
                device=disks[0], new_device=_spec(disks[2])
            )
            assert result is None
        finally:
            _destroy(lz)


# ---------------------------------------------------------------------------
# Section 4 — detach_vdev
# ---------------------------------------------------------------------------

class TestDetachVdev:
    """detach_vdev() — file-backed vdev operations."""

    def test_detach_from_2way_mirror_succeeds(self, make_disks):
        """Detaching one device from a 2-way mirror leaves a single-disk pool."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            result = pool.detach_vdev(device=disks[0])
            assert result is None

            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            # After detach the top-level vdev is a single disk (not a mirror)
            assert len(status.storage_vdevs) == 1
        finally:
            _destroy(lz)

    def test_detach_returns_none(self, make_disks):
        """detach_vdev() must return None on success."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        try:
            result = pool.detach_vdev(device=disks[0])
            assert result is None
        finally:
            _destroy(lz)

    def test_detach_from_single_disk_fails(self, make_disks):
        """Detaching from a non-mirror pool must raise ZFSError."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(truenas_pylibzfs.ZFSException):
                pool.detach_vdev(device=disks[0])
        finally:
            _destroy(lz)


# ---------------------------------------------------------------------------
# Section 5 — remove_vdev
# ---------------------------------------------------------------------------

class TestRemoveVdev:
    """remove_vdev() — file-backed vdev operations."""

    def test_remove_spare_succeeds(self, make_disks):
        """Removing a spare vdev must succeed."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_pool_with_spare(lz, disks[0], disks[1])
        try:
            result = pool.remove_vdev(device=disks[1])
            assert result is None

            status = pool.status()
            assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
            assert len(status.spares) == 0
        finally:
            _destroy(lz)

    def test_remove_returns_none(self, make_disks):
        """remove_vdev() must return None on success."""
        disks = make_disks(2)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_pool_with_spare(lz, disks[0], disks[1])
        try:
            result = pool.remove_vdev(device=disks[1])
            assert result is None
        finally:
            _destroy(lz)

    def test_remove_nonexistent_vdev_fails(self, make_disks):
        """Removing a device not in the pool must raise ZFSError."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(truenas_pylibzfs.ZFSException):
                pool.remove_vdev(device="/nonexistent/device")
        finally:
            _destroy(lz)


# ---------------------------------------------------------------------------
# Section 6 — cancel_remove_vdev
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Section 7 — width limits enforced at create_pool and add_vdevs
# ---------------------------------------------------------------------------

class TestWidthLimitsCreateAdd:
    """Width policy limits apply during pool creation and add_vdevs too."""

    def test_create_pool_wide_mirror_rejected(self, make_disks):
        """Creating a pool with a 5-way mirror must raise ValueError."""
        disks = make_disks(5)
        lz = truenas_pylibzfs.open_handle()
        wide_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(d) for d in disks],
        )
        try:
            with pytest.raises(ValueError, match="mirror width"):
                lz.create_pool(name=POOL_NAME, storage_vdevs=[wide_mirror])
        finally:
            _destroy(lz)

    def test_create_pool_wide_mirror_force_bypasses(self, make_disks):
        """force=True bypasses the mirror width limit during create_pool."""
        disks = make_disks(5)
        lz = truenas_pylibzfs.open_handle()
        wide_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(d) for d in disks],
        )
        try:
            lz.create_pool(
                name=POOL_NAME, storage_vdevs=[wide_mirror], force=True
            )
            pool = lz.open_pool(name=POOL_NAME)
            status = pool.status()
            assert status.storage_vdevs[0].vdev_type == "mirror"
            assert len(status.storage_vdevs[0].children) == 5
        finally:
            _destroy(lz)

    def test_create_pool_4way_mirror_allowed(self, make_disks):
        """Creating a pool with a 4-way mirror (at the limit) must succeed."""
        disks = make_disks(4)
        lz = truenas_pylibzfs.open_handle()
        mirror4 = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(d) for d in disks],
        )
        try:
            lz.create_pool(name=POOL_NAME, storage_vdevs=[mirror4])
            pool = lz.open_pool(name=POOL_NAME)
            assert len(pool.status().storage_vdevs[0].children) == 4
        finally:
            _destroy(lz)

    def test_add_vdevs_wide_mirror_rejected(self, make_disks):
        """add_vdevs with a 5-way mirror must raise ValueError."""
        disks = make_disks(7)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        wide_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(d) for d in disks[2:7]],
        )
        try:
            with pytest.raises(ValueError, match="mirror width"):
                pool.add_vdevs(storage_vdevs=[wide_mirror])
        finally:
            _destroy(lz)

    def test_add_vdevs_wide_mirror_force_bypasses(self, make_disks):
        """force=True bypasses the mirror width limit during add_vdevs."""
        disks = make_disks(7)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_mirror_pool(lz, disks[0], disks[1])
        wide_mirror = truenas_pylibzfs.create_vdev_spec(
            vdev_type=VDevType.MIRROR,
            children=[_spec(d) for d in disks[2:7]],
        )
        try:
            pool.add_vdevs(storage_vdevs=[wide_mirror], force=True)
            status = pool.status()
            assert len(status.storage_vdevs) == 2
        finally:
            _destroy(lz)


class TestCancelRemoveVdev:
    """cancel_remove_vdev() — cancellation behavior."""

    def test_cancel_with_no_removal_raises(self, make_disks):
        """cancel_remove_vdev() with no in-progress removal must raise ZFSError."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(truenas_pylibzfs.ZFSException):
                pool.cancel_remove_vdev()
        finally:
            _destroy(lz)

    def test_cancel_takes_no_arguments(self, make_disks):
        """cancel_remove_vdev() must not accept any arguments."""
        disks = make_disks(1)
        lz = truenas_pylibzfs.open_handle()
        pool = _create_stripe_pool(lz, disks[0])
        try:
            with pytest.raises(TypeError):
                pool.cancel_remove_vdev("unexpected_arg")
        finally:
            _destroy(lz)
