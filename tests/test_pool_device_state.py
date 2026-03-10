"""
tests/test_pool_device_state.py — test suite for ZFSPool.offline_device()
and ZFSPool.online_device()
"""

import os
import pytest
import shutil
import tempfile
import truenas_pylibzfs

VDevType = truenas_pylibzfs.VDevType
ZPOOLStatus = truenas_pylibzfs.libzfs_types.ZPOOLStatus

POOL_NAME = 'testpool_devstate_pylibzfs'
DISK_SZ = 128 * 1024 * 1024  # 128 MiB


# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def make_disks():
    """Factory fixture: call make_disks(n) to get n sparse image paths."""
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix='pylibzfs_devstate_disks_')
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
def mirror_pool(make_disks):
    """
    A 2-disk mirror pool. Yields (lz, pool, disk0, disk1).
    Teardown destroys the pool (and disk images are cleaned up by make_disks).
    """
    disks = make_disks(2)
    lz = truenas_pylibzfs.open_handle()
    mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[0]), _spec(disks[1])],
    )
    lz.create_pool(name=POOL_NAME, storage_vdevs=[mirror])
    pool = lz.open_pool(name=POOL_NAME)
    try:
        yield lz, pool, disks[0], disks[1]
    finally:
        _destroy(lz)


# ---------------------------------------------------------------------------
# offline_device — basic
# ---------------------------------------------------------------------------

class TestOfflineDevice:

    def test_offline_returns_none(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        result = pool.offline_device(device=disk0)
        assert result is None

    def test_offline_device_succeeds(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        # Offlining one leaf of a mirror should succeed without raising
        pool.offline_device(device=disk0)

    def test_offline_temporary(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        # temporary=True should also succeed
        pool.offline_device(device=disk0, temporary=True)

    def test_offline_second_device_raises(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        pool.offline_device(device=disk0)
        # Offlining the only remaining online device would make the pool
        # inaccessible; libzfs must reject this.
        with pytest.raises(truenas_pylibzfs.ZFSException):
            pool.offline_device(device=disk1)

    def test_offline_missing_device_raises(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        with pytest.raises(truenas_pylibzfs.ZFSException):
            pool.offline_device(device='/nonexistent/no_such_device_xyz')

    def test_offline_missing_kwarg_raises(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        with pytest.raises(ValueError):
            pool.offline_device()

    def test_offline_keyword_only_enforcement(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        with pytest.raises(TypeError):
            pool.offline_device(disk0)  # type: ignore[call-arg]


# ---------------------------------------------------------------------------
# online_device — basic
# ---------------------------------------------------------------------------

class TestOnlineDevice:

    def test_online_returns_none(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        pool.offline_device(device=disk0)
        result = pool.online_device(device=disk0)
        assert result is None

    def test_online_missing_device_raises(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        with pytest.raises(truenas_pylibzfs.ZFSException):
            pool.online_device(device='/nonexistent/no_such_device_xyz')

    def test_online_missing_kwarg_raises(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        with pytest.raises(ValueError):
            pool.online_device()

    def test_online_keyword_only_enforcement(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        with pytest.raises(TypeError):
            pool.online_device(disk0)  # type: ignore[call-arg]

    def test_online_expand_accepted(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        pool.offline_device(device=disk0)
        # expand=True is a valid flag (grow disk to fill available space)
        pool.online_device(device=disk0, expand=True)


# ---------------------------------------------------------------------------
# offline → online roundtrip
# ---------------------------------------------------------------------------

class TestOfflineOnlineRoundtrip:

    def test_pool_healthy_after_roundtrip(self, mirror_pool):
        lz, pool, disk0, disk1 = mirror_pool
        pool.offline_device(device=disk0)
        pool.online_device(device=disk0)
        # Re-open to get a fresh status
        pool2 = lz.open_pool(name=POOL_NAME)
        st = pool2.status().status
        assert st in (
            ZPOOLStatus.ZPOOL_STATUS_OK,
            ZPOOLStatus.ZPOOL_STATUS_RESILVERING,
        )
