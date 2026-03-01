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
ZFSProperty = truenas_pylibzfs.ZFSProperty

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


# S1-1: vdev_type=None → ValueError ("vdev_type keyword argument is required")
def test_create_vdev_spec_vdev_type_none():
    with pytest.raises(ValueError):
        truenas_pylibzfs.create_vdev_spec(vdev_type=None)


# S1-2: vdev_type=42 (not str/VDevType) → TypeError ("vdev_type must be a string")
def test_create_vdev_spec_vdev_type_not_string():
    with pytest.raises(TypeError):
        truenas_pylibzfs.create_vdev_spec(vdev_type=42)


# S1-3: name=42 (non-str) → TypeError ("name must be a string or None")
def test_create_vdev_spec_name_not_string():
    with pytest.raises(TypeError):
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=42)


# S1-4: children=42 (non-sequence) → TypeError ("children must be a sequence")
def test_create_vdev_spec_children_not_sequence():
    with pytest.raises(TypeError):
        truenas_pylibzfs.create_vdev_spec(vdev_type="mirror", children=42)


# S1-5: raidz1 with name="x" set → ValueError ("must have name=None")
def test_create_vdev_spec_raidz_with_name():
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(3)
    ]
    with pytest.raises(ValueError):
        truenas_pylibzfs.create_vdev_spec(
            vdev_type="raidz1", name="x", children=disks
        )


# S1-6: draid1 name "0d:1s" (ndata=0) → ValueError ("dRAID ndata must be > 0")
def test_create_vdev_spec_draid_ndata_zero():
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(4)
    ]
    with pytest.raises(ValueError, match="ndata must be > 0"):
        truenas_pylibzfs.create_vdev_spec(
            vdev_type="draid1", name="0d:1s", children=disks
        )


# S1-7: draid1 "3d:2s" with 4 children (needs 3+1+2=6) → ValueError
def test_create_vdev_spec_draid_too_few_children():
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(4)
    ]
    with pytest.raises(ValueError, match="requires at least"):
        truenas_pylibzfs.create_vdev_spec(
            vdev_type="draid1", name="3d:2s", children=disks
        )


# S1-8: draid3 happy path ("2d:0s" needs 2+3+0=5 children)
def test_create_vdev_spec_draid3():
    disks = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(5)
    ]
    spec = truenas_pylibzfs.create_vdev_spec(
        vdev_type="draid3", name="2d:0s", children=disks
    )
    assert spec.vdev_type == "draid3"
    assert spec.name == "2d:0s"
    assert len(spec.children) == 5


# S1-9: disk type leaf spec happy path (WHOLE_DISK branch runs only at create time)
def test_create_vdev_spec_disk():
    spec = truenas_pylibzfs.create_vdev_spec(vdev_type="disk", name="/dev/sda")
    assert spec.vdev_type == "disk"
    assert spec.name == "/dev/sda"
    assert spec.children is None


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


# S2-1: storage_vdevs=[] → ValueError ("storage_vdevs must be non-empty")
def test_create_pool_storage_empty():
    lz = truenas_pylibzfs.open_handle()
    with pytest.raises(ValueError, match="non-empty"):
        lz.create_pool(name=POOL_NAME, storage_vdevs=[])


# S2-2: log vdev = mirror → valid (no ValueError); libzfs may fail with ZFSException
def test_create_pool_log_mirror_accepted():
    lz = truenas_pylibzfs.open_handle()
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/s.img"
    )
    c1 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c1.img")
    c2 = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/c2.img")
    log_mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type="mirror", children=[c1, c2]
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            log_vdevs=[log_mirror],
        )
    except (truenas_pylibzfs.ZFSException, OSError):
        pass  # libzfs rejection with fake paths is acceptable
    except ValueError:
        raise  # Python-level topology check must NOT fire
    finally:
        _destroy()


# S2-3: special leaf + leaf storage (both parity 0) → valid topology
def test_create_pool_special_leaf_accepted():
    lz = truenas_pylibzfs.open_handle()
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/s.img"
    )
    special = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/sp.img"
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            special_vdevs=[special],
        )
    except (truenas_pylibzfs.ZFSException, OSError):
        pass
    except ValueError:
        raise
    finally:
        _destroy()


# S2-4: dedup leaf + leaf storage (both parity 0) → valid topology
def test_create_pool_dedup_leaf_accepted():
    lz = truenas_pylibzfs.open_handle()
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/s.img"
    )
    dedup = truenas_pylibzfs.create_vdev_spec(
        vdev_type="file", name="/tmp/dd.img"
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            dedup_vdevs=[dedup],
        )
    except (truenas_pylibzfs.ZFSException, OSError):
        pass
    except ValueError:
        raise
    finally:
        _destroy()


# S2-5: properties={b"key": "on"} (bytes key) → TypeError
def test_create_pool_properties_bytes_key():
    lz = truenas_pylibzfs.open_handle()
    disk = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/x.img")
    with pytest.raises(TypeError, match="Pool property keys must be str or ZPoolProperty"):
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[disk],
            properties={b"key": "on"},
        )


# S2-6: properties={"bogus_prop": "on"} (invalid name) → ValueError
def test_create_pool_properties_invalid_name():
    lz = truenas_pylibzfs.open_handle()
    disk = truenas_pylibzfs.create_vdev_spec(vdev_type="file", name="/tmp/x.img")
    with pytest.raises(ValueError, match="not a valid zpool property"):
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[disk],
            properties={"bogus_prop": "on"},
        )


# S2-7: mixed dRAID + raidz storage → ValueError ("all vdevs must share the same type")
def test_create_pool_mixed_draid_raidz_storage():
    lz = truenas_pylibzfs.open_handle()
    draid_children = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/d{i}.img")
        for i in range(4)
    ]
    draid = truenas_pylibzfs.create_vdev_spec(
        vdev_type="draid1", name="3d:0s", children=draid_children
    )
    raidz_children = [
        truenas_pylibzfs.create_vdev_spec(vdev_type="file", name=f"/tmp/r{i}.img")
        for i in range(3)
    ]
    raidz = truenas_pylibzfs.create_vdev_spec(
        vdev_type="raidz1", children=raidz_children
    )
    with pytest.raises(ValueError):
        lz.create_pool(name=POOL_NAME, storage_vdevs=[draid, raidz])


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
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.support_vdevs.cache) == 1
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
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.support_vdevs.log) == 1
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
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.spares) == 1
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
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.support_vdevs.special) == 1
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


# S3-1: dedup_vdevs success path (raidz1 storage + raidz1 dedup, 6 disks)
def test_create_pool_with_dedup(make_disks):
    disks = make_disks(6)
    lz = truenas_pylibzfs.open_handle()
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(disks[0]), _spec(disks[1]), _spec(disks[2])],
    )
    dedup = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(disks[3]), _spec(disks[4]), _spec(disks[5])],
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            dedup_vdevs=[dedup],
        )
        pool = lz.open_pool(name=POOL_NAME)
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.support_vdevs.dedup) == 1
    finally:
        _destroy()


# S3-2: dRAID3 pool creation ("2d:0s" needs 2+3+0=5 children)
def test_create_pool_draid3(make_disks):
    disks = make_disks(5)
    lz = truenas_pylibzfs.open_handle()
    draid = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.DRAID3,
        name="2d:0s",
        children=[_spec(d) for d in disks],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[draid])
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


# S3-3: log mirror pool (mirror storage + mirror log)
def test_create_pool_with_log_mirror(make_disks):
    disks = make_disks(4)
    lz = truenas_pylibzfs.open_handle()
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[0]), _spec(disks[1])],
    )
    log_mirror = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[2]), _spec(disks[3])],
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            log_vdevs=[log_mirror],
        )
        pool = lz.open_pool(name=POOL_NAME)
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.support_vdevs.log) == 1
    finally:
        _destroy()


# S3-4: pool already exists → ZFSException from zpool_create()
def test_create_pool_already_exists(make_disks):
    disks = make_disks(2)
    lz = truenas_pylibzfs.open_handle()
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[_spec(disks[0])])
        with pytest.raises(truenas_pylibzfs.ZFSException):
            lz.create_pool(name=POOL_NAME, storage_vdevs=[_spec(disks[1])])
    finally:
        _destroy()


# S3-5: filesystem_properties parameter
def test_create_pool_filesystem_properties(make_disks):
    disks = make_disks(1)
    lz = truenas_pylibzfs.open_handle()
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[_spec(disks[0])],
            filesystem_properties={ZFSProperty.ACLMODE: "restricted"},
        )
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()


# S3-6: two raidz1 vdevs (same type, same child count — 2× raidz1/3)
def test_create_pool_two_raidz1(make_disks):
    disks = make_disks(6)
    lz = truenas_pylibzfs.open_handle()
    rz1 = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(disks[0]), _spec(disks[1]), _spec(disks[2])],
    )
    rz2 = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(disks[3]), _spec(disks[4]), _spec(disks[5])],
    )
    try:
        lz.create_pool(name=POOL_NAME, storage_vdevs=[rz1, rz2])
        pool = lz.open_pool(name=POOL_NAME)
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.storage_vdevs) == 2
    finally:
        _destroy()


# S3-7: cache + log simultaneously (mirror storage)
def test_create_pool_with_cache_and_log(make_disks):
    disks = make_disks(4)
    lz = truenas_pylibzfs.open_handle()
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_spec(disks[0]), _spec(disks[1])],
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            cache_vdevs=[_spec(disks[2])],
            log_vdevs=[_spec(disks[3])],
        )
        pool = lz.open_pool(name=POOL_NAME)
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.support_vdevs.cache) == 1
        assert len(status.support_vdevs.log) == 1
    finally:
        _destroy()


# S3-8: special + dedup simultaneously (raidz1 storage, 9 disks)
def test_create_pool_with_special_and_dedup(make_disks):
    disks = make_disks(9)
    lz = truenas_pylibzfs.open_handle()
    storage = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(disks[0]), _spec(disks[1]), _spec(disks[2])],
    )
    special = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(disks[3]), _spec(disks[4]), _spec(disks[5])],
    )
    dedup = truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.RAIDZ1,
        children=[_spec(disks[6]), _spec(disks[7]), _spec(disks[8])],
    )
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[storage],
            special_vdevs=[special],
            dedup_vdevs=[dedup],
        )
        pool = lz.open_pool(name=POOL_NAME)
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
        assert len(status.support_vdevs.special) == 1
        assert len(status.support_vdevs.dedup) == 1
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


# S4-1: force=True with a valid topology: assert pool is actually created
def test_create_pool_force_valid_topology(make_disks):
    disks = make_disks(1)
    lz = truenas_pylibzfs.open_handle()
    try:
        lz.create_pool(
            name=POOL_NAME,
            storage_vdevs=[_spec(disks[0])],
            force=True,
        )
        pool = lz.open_pool(name=POOL_NAME)
        assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK
    finally:
        _destroy()
