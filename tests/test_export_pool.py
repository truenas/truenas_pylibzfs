"""
tests/test_export_pool.py — test suite for ZFS.export_pool()
"""

import os
import pytest
import shutil
import tempfile
import truenas_pylibzfs

VDevType = truenas_pylibzfs.VDevType
ZPOOLStatus = truenas_pylibzfs.ZPOOLStatus

POOL_NAME = 'testpool_export_pylibzfs'
DISK_SZ = 128 * 1024 * 1024  # 128 MiB


# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def make_disks():
    """Factory fixture: call make_disks(n) to get n sparse image paths."""
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix='pylibzfs_export_disks_')
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


def _spec(path):
    return truenas_pylibzfs.create_vdev_spec(vdev_type=VDevType.FILE, name=path)


def _destroy(lz):
    try:
        lz.destroy_pool(name=POOL_NAME, force=True)
    except Exception:
        pass


@pytest.fixture
def imported_pool(make_disks):
    """
    A freshly created single-disk stripe pool, ready to export.
    Yields (lz, disk_dir).
    """
    disks = make_disks(1)
    disk_dir = os.path.dirname(disks[0])
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(name=POOL_NAME, storage_vdevs=[_spec(disks[0])])
    try:
        yield lz, disk_dir
    finally:
        _destroy(lz)


# ---------------------------------------------------------------------------
# Basic export
# ---------------------------------------------------------------------------

class TestExportPoolBasic:

    def test_export_returns_none(self, imported_pool):
        lz, disk_dir = imported_pool
        result = lz.export_pool(name=POOL_NAME)
        assert result is None

    def test_export_pool_discoverable_after_export(self, imported_pool):
        lz, disk_dir = imported_pool
        lz.export_pool(name=POOL_NAME)
        found = lz.import_pool_find(device=disk_dir)
        assert len(found) >= 1

    def test_export_pool_force(self, imported_pool):
        lz, disk_dir = imported_pool
        # force=True should also succeed on a freshly created idle pool
        lz.export_pool(name=POOL_NAME, force=True)
        found = lz.import_pool_find(device=disk_dir)
        assert len(found) >= 1


# ---------------------------------------------------------------------------
# Parameter validation
# ---------------------------------------------------------------------------

class TestExportPoolValidation:

    def test_missing_name_raises_value_error(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(ValueError):
            lz.export_pool()

    def test_unknown_name_raises_zfs_error(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(truenas_pylibzfs.ZFSException):
            lz.export_pool(name='no_such_pool_xyz_export')

    def test_keyword_only_enforcement(self, imported_pool):
        lz, disk_dir = imported_pool
        with pytest.raises(TypeError):
            lz.export_pool(POOL_NAME)  # type: ignore[call-arg]


# ---------------------------------------------------------------------------
# Export + reimport roundtrip
# ---------------------------------------------------------------------------

class TestExportThenReimport:

    def test_export_then_reimport_restores_pool(self, imported_pool):
        lz, disk_dir = imported_pool
        lz.export_pool(name=POOL_NAME)
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir)
        assert pool.name == POOL_NAME

    def test_reimported_pool_is_healthy(self, imported_pool):
        lz, disk_dir = imported_pool
        lz.export_pool(name=POOL_NAME)
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
