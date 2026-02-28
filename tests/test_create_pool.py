"""
tests/test_create_pool.py — test suite for create_vdev_spec() and create_pool()

Sections:
  1. create_vdev_spec() validation (no fixtures, no ZFS calls)
  2. create_pool() topology validation (open_handle(), no real disks)
  3. Successful pool creation (needs make_disks fixture)
  4. force=True bypass tests
"""

import os
import pytest
import shutil
import subprocess
import tempfile
import truenas_pylibzfs

ZPOOLStatus = truenas_pylibzfs.ZPOOLStatus
VDevType = truenas_pylibzfs.VDevType
ZPOOLProperty = truenas_pylibzfs.enums.ZPOOLProperty

POOL_NAME = "test_create_pool_pylibzfs"
DISK_SZ = 128 * 1024 * 1024  # 128 MiB


# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def make_disks():
    """
    Factory fixture: call make_disks(n) to get a list of n sparse image paths.
    All files are cleaned up on teardown.
    """
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix="pylibzfs_create_")
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


def _destroy():
    """Destroy the test pool unconditionally."""
    subprocess.run(["zpool", "destroy", "-f", POOL_NAME], check=False)


def _spec(path):
    """Create a file-type leaf vdev spec."""
    return truenas_pylibzfs.create_vdev_spec(vdev_type=VDevType.FILE, name=path)


# ---------------------------------------------------------------------------
# Section 1 — create_vdev_spec() validation (no fixtures)
# ---------------------------------------------------------------------------

def test_create_vdev_spec_file():
    spec = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/x.img")
    assert spec.vdev_type == "file"
    assert spec.name == "/tmp/x.img"
    assert spec.children is None


def test_create_vdev_spec_mirror():
    c1 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/a.img")
    c2 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/b.img")
    spec = truenas_pylibzfs.create_vdev_spec(vdev_type="mirror", children=[c1, c2])
    assert spec.vdev_type == "mirror"
    assert spec.name is None
    assert isinstance(spec.children, tuple)
    assert len(spec.children) == 2


def test_create_vdev_spec_raidz_parities():
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(5)
    ]
    # raidz1: minimum 2 children (parity + 1)
    r1 = truenas_pylibzfs.create_vdev_spec(vdev_type="raidz1", children=disks[:3])
    assert r1.vdev_type == "raidz1"
    assert isinstance(r1.children, tuple) and len(r1.children) == 3
    # raidz2: minimum 3 children (parity + 1)
    r2 = truenas_pylibzfs.create_vdev_spec(vdev_type="raidz2", children=disks[:4])
    assert r2.vdev_type == "raidz2"
    assert isinstance(r2.children, tuple) and len(r2.children) == 4
    # raidz3: minimum 4 children (parity + 1)
    r3 = truenas_pylibzfs.create_vdev_spec(vdev_type="raidz3", children=disks[:5])
    assert r3.vdev_type == "raidz3"
    assert isinstance(r3.children, tuple) and len(r3.children) == 5


def test_create_vdev_spec_children_list_normalised():
    c1 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/a.img")
    c2 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/b.img")
    # Pass children as a list, not a tuple
    spec = truenas_pylibzfs.create_vdev_spec(vdev_type="mirror", children=[c1, c2])
    assert isinstance(spec.children, tuple), \
        f"expected tuple, got {type(spec.children)}"


def test_create_vdev_spec_invalid_type():
    with pytest.raises(ValueError):
        truenas_pylibzfs.create_vdev_spec(vdev_type="bogus", name="/tmp/x.img")


def test_create_vdev_spec_leaf_missing_name():
    with pytest.raises(ValueError):
        truenas_pylibzfs.create_vdev_spec(vdev_type="file")


def test_create_vdev_spec_leaf_with_children():
    child = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/a.img")
    with pytest.raises(ValueError):
        truenas_pylibzfs.create_vdev_spec(
            vdev_type="file", name="/tmp/b.img", children=[child]
        )


def test_create_vdev_spec_virtual_missing_children():
    with pytest.raises(ValueError):
        truenas_pylibzfs.create_vdev_spec(vdev_type="mirror")


def test_create_vdev_spec_mirror_with_name():
    c1 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/a.img")
    c2 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/b.img")
    with pytest.raises(ValueError):
        truenas_pylibzfs.create_vdev_spec(
            vdev_type="mirror", name="should-not-be-here", children=[c1, c2]
        )


def test_create_vdev_spec_draid_valid():
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(5)
    ]
    spec = truenas_pylibzfs.create_vdev_spec(
        vdev_type="draid1", name="3d:1s", children=disks
    )
    assert spec.vdev_type == "draid1"
    assert spec.name == "3d:1s"
    assert len(spec.children) == 5


def test_create_vdev_spec_draid_bad_name():
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(4)
    ]
    with pytest.raises(ValueError):
        truenas_pylibzfs.create_vdev_spec(
            vdev_type="draid1", name="bogus", children=disks
        )


def test_create_vdev_spec_draid_missing_name():
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(4)
    ]
    with pytest.raises(ValueError):
        truenas_pylibzfs.create_vdev_spec(vdev_type="draid1", children=disks)


# ---------------------------------------------------------------------------
# Section 2 — create_pool() topology validation (no real disks)
# ---------------------------------------------------------------------------

def test_create_pool_name_required():
    lz = truenas_pylibzfs.open_handle()
    disk = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/x.img")
    with pytest.raises(ValueError):
        lz.create_pool(storage_vdevs=[disk])


def test_create_pool_storage_required():
    lz = truenas_pylibzfs.open_handle()
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME)


def test_create_pool_storage_none_rejected():
    lz = truenas_pylibzfs.open_handle()
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME, storage_vdevs=None)


def test_create_pool_mirror_one_child():
    lz = truenas_pylibzfs.open_handle()
    disk = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/a.img")
    mirror = truenas_pylibzfs.create_vdev_spec(vdev_type="mirror", children=[disk])
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME, storage_vdevs=[mirror])


def test_create_pool_raidz1_one_child():
    lz = truenas_pylibzfs.open_handle()
    disk = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/d0.img")
    rz = truenas_pylibzfs.create_vdev_spec(vdev_type="raidz1", children=[disk])
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME, storage_vdevs=[rz])


def test_create_pool_raidz2_two_children():
    lz = truenas_pylibzfs.open_handle()
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(2)
    ]
    rz = truenas_pylibzfs.create_vdev_spec(vdev_type="raidz2", children=disks)
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME, storage_vdevs=[rz])


def test_create_pool_raidz3_three_children():
    lz = truenas_pylibzfs.open_handle()
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(3)
    ]
    rz = truenas_pylibzfs.create_vdev_spec(vdev_type="raidz3", children=disks)
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME, storage_vdevs=[rz])


def test_create_pool_cache_mirror_rejected():
    lz = truenas_pylibzfs.open_handle()
    storage_disk = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/s.img"
    )
    c1 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c1.img")
    c2 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c2.img")
    cache_mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type="mirror", children=[c1, c2]
    )
    with pytest.raises(ValueError):
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage_disk],
            cache_vdevs=[cache_mirror],
        )


def test_create_pool_spare_mirror_rejected():
    lz = truenas_pylibzfs.open_handle()
    storage_disk = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/s.img"
    )
    c1 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c1.img")
    c2 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c2.img")
    spare_mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type="mirror", children=[c1, c2]
    )
    with pytest.raises(ValueError):
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage_disk],
            spare_vdevs=[spare_mirror],
        )


def test_create_pool_log_raidz_rejected():
    lz = truenas_pylibzfs.open_handle()
    storage_disk = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/s.img"
    )
    c1 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c1.img")
    c2 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c2.img")
    c3 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c3.img")
    log_raidz = truenas_pylibzfs.create_vdev_spec(
        vdev_type="raidz1", children=[c1, c2, c3]
    )
    with pytest.raises(ValueError):
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage_disk],
            log_vdevs=[log_raidz],
        )


def test_create_pool_mixed_storage_types():
    lz = truenas_pylibzfs.open_handle()
    c1 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c1.img")
    c2 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c2.img")
    leaf = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/s.img")
    mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type="mirror", children=[c1, c2]
    )
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME, storage_vdevs=[mirror, leaf])


def test_create_pool_mismatched_child_count():
    lz = truenas_pylibzfs.open_handle()
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(7)
    ]
    rz3 = truenas_pylibzfs.create_vdev_spec(
        vdev_type="raidz1", children=disks[:3]
    )
    rz4 = truenas_pylibzfs.create_vdev_spec(
        vdev_type="raidz1", children=disks[3:7]
    )
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME, storage_vdevs=[rz3, rz4])


def test_create_pool_special_draid_rejected():
    # dRAID is never permitted for special vdevs regardless of storage type.
    lz = truenas_pylibzfs.open_handle()
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/s.img"
    )
    draid_children = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(4)
    ]
    special_draid = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.DRAID1, name="3d:0s", children=draid_children
    )
    with pytest.raises(ValueError):
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            special_vdevs=[special_draid],
        )


def test_create_pool_special_insufficient_parity():
    # raidz2 storage (parity 2) + mirror special (parity 1) must be rejected.
    lz = truenas_pylibzfs.open_handle()
    storage_disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/s{i}.img")
        for i in range(4)
    ]
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type="raidz2", children=storage_disks
    )
    c1 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c1.img")
    c2 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c2.img")
    special_mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type="mirror", children=[c1, c2]
    )
    with pytest.raises(ValueError):
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            special_vdevs=[special_mirror],
        )


def test_create_pool_dedup_insufficient_parity():
    # raidz1 storage (parity 1) + leaf dedup (parity 0) must be rejected.
    lz = truenas_pylibzfs.open_handle()
    storage_disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/s{i}.img")
        for i in range(3)
    ]
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type="raidz1", children=storage_disks
    )
    dedup_leaf = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/d.img"
    )
    with pytest.raises(ValueError):
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            dedup_vdevs=[dedup_leaf],
        )


# ---------------------------------------------------------------------------
# Section 3 — successful pool creation (needs make_disks)
# ---------------------------------------------------------------------------

def test_create_pool_stripe(make_disks):
    disks = make_disks(1)
    lz = truenas_pylibzfs.open_handle()
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[_spec(disks[0])])
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_mirror(make_disks):
    disks = make_disks(2)
    lz = truenas_pylibzfs.open_handle()
    mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[0]), _spec(disks[1])],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[mirror])
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_raidz1(make_disks):
    disks = make_disks(3)
    lz = truenas_pylibzfs.open_handle()
    rz = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(d) for d in disks],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[rz])
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_raidz2(make_disks):
    disks = make_disks(4)
    lz = truenas_pylibzfs.open_handle()
    rz = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ2,
        children=[_spec(d) for d in disks],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[rz])
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_raidz3(make_disks):
    disks = make_disks(5)
    lz = truenas_pylibzfs.open_handle()
    rz = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ3,
        children=[_spec(d) for d in disks],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[rz])
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_two_mirrors(make_disks):
    disks = make_disks(4)
    lz = truenas_pylibzfs.open_handle()
    m1 = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[0]), _spec(disks[1])],
    )
    m2 = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[2]), _spec(disks[3])],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[m1, m2])
        pool = lz.open_pool(name=POOL_NAME)
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.storage_vdevs) == 2
    finally:
        _destroy()


def test_create_pool_with_cache(make_disks):
    disks = make_disks(3)
    lz = truenas_pylibzfs.open_handle()
    mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[0]), _spec(disks[1])],
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[mirror],
            cache_vdevs=[_spec(disks[2])],
        )
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_with_log(make_disks):
    disks = make_disks(3)
    lz = truenas_pylibzfs.open_handle()
    mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[0]), _spec(disks[1])],
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[mirror],
            log_vdevs=[_spec(disks[2])],
        )
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_with_spare(make_disks):
    disks = make_disks(3)
    lz = truenas_pylibzfs.open_handle()
    mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[0]), _spec(disks[1])],
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[mirror],
            spare_vdevs=[_spec(disks[2])],
        )
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_with_special(make_disks):
    disks = make_disks(4)
    lz = truenas_pylibzfs.open_handle()
    storage_mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[0]), _spec(disks[1])],
    )
    special_mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[2]), _spec(disks[3])],
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage_mirror],
            special_vdevs=[special_mirror],
        )
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_draid1_no_spare(make_disks):
    disks = make_disks(4)
    lz = truenas_pylibzfs.open_handle()
    draid = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.DRAID1,
        name="3d:0s",
        children=[_spec(d) for d in disks],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[draid])
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_draid1_with_spare(make_disks):
    disks = make_disks(5)
    lz = truenas_pylibzfs.open_handle()
    draid = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.DRAID1,
        name="3d:1s",
        children=[_spec(d) for d in disks],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[draid])
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_draid2(make_disks):
    disks = make_disks(4)
    lz = truenas_pylibzfs.open_handle()
    draid = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.DRAID2,
        name="2d:0s",
        children=[_spec(d) for d in disks],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[draid])
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_raidz1_with_raidz1_special(make_disks):
    # raidz1 storage + raidz1 special: same parity (1), must be accepted.
    disks = make_disks(6)
    lz = truenas_pylibzfs.open_handle()
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(disks[0]), _spec(disks[1]), _spec(disks[2])],
    )
    special = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(disks[3]), _spec(disks[4]), _spec(disks[5])],
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            special_vdevs=[special],
        )
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_properties_str_key(make_disks):
    disks = make_disks(1)
    lz = truenas_pylibzfs.open_handle()
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[_spec(disks[0])],
            properties={"autoreplace": "on"},
        )
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


def test_create_pool_properties_enum_key(make_disks):
    disks = make_disks(1)
    lz = truenas_pylibzfs.open_handle()
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[_spec(disks[0])],
            properties={ZPOOLProperty.AUTOREPLACE: "on"},
        )
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


# ---------------------------------------------------------------------------
# Section 4 — force=True bypass
# ---------------------------------------------------------------------------

def test_create_pool_force_bypasses_topology_check(make_disks):
    disks = make_disks(1)
    lz = truenas_pylibzfs.open_handle()
    disk = _spec(disks[0])
    # 1-disk mirror is invalid topology
    mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR, children=[disk]
    )

    # Without force → Python-level ValueError
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME, storage_vdevs=[mirror])

    # With force → topology check bypassed; libzfs may still reject
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[mirror], force=True)
    except truenas_pylibzfs.ZFSException:
        pass  # libzfs rejection is acceptable
    except ValueError:
        raise  # Python-level check must NOT fire with force=True
    finally:
        _destroy()
