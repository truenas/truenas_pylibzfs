"""
tests/test_pool_missing_device.py — vdev identity for devices missing at
pool import time.

When a leaf vdev cannot be opened during import, the kernel marks it
ZPOOL_CONFIG_NOT_PRESENT and zpool_vdev_name() degrades the display name
to the bare guid (this is the "<guid>  UNAVAIL  was /path" case in
zpool status).  The struct_vdev `path` field must still carry the stored
ZPOOL_CONFIG_PATH so consumers can tell which device is gone.

The scenario is reproduced with a two-file mirror: export, delete one
backing file, re-import.  NOT_PRESENT is only set during import
(SPA_LOAD_IMPORT), so deleting the file while the pool is imported would
not exercise this path.
"""

import os
import pytest
import truenas_pylibzfs

VDevState = truenas_pylibzfs.libzfs_types.VDevState
VDevType = truenas_pylibzfs.VDevType

POOL_NAME = 'testpool_missing_dev'


def _mirror(disks):
    return truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[
            truenas_pylibzfs.create_vdev_spec(vdev_type=VDevType.FILE, name=d)
            for d in disks
        ],
    )


@pytest.fixture
def degraded_pool(make_disks):
    """
    Two-file mirror imported with one backing file deleted.
    Yields (lz, pool, present_path, missing_path).
    """
    lz = truenas_pylibzfs.open_handle()
    disks = make_disks(2)
    lz.create_pool(name=POOL_NAME, storage_vdevs=[_mirror(disks)], force=True)
    try:
        lz.export_pool(name=POOL_NAME)
        os.unlink(disks[1])
        pool = lz.import_pool(name=POOL_NAME, device=os.path.dirname(disks[0]))
        yield lz, pool, disks[0], disks[1]
    finally:
        try:
            lz.destroy_pool(name=POOL_NAME, force=True)
        except Exception:
            pass


def _split_children(mirror):
    """Return (present, missing) children of the degraded mirror."""
    a, b = mirror.children
    return (a, b) if b.name == str(b.guid) else (b, a)


def test_missing_device_name_is_guid(degraded_pool):
    lz, pool, present_path, missing_path = degraded_pool
    mirror = pool.status().storage_vdevs[0]
    assert mirror.vdev_type == 'mirror'
    assert mirror.state == VDevState.DEGRADED

    present, missing = _split_children(mirror)
    # libzfs degrades the display name of a NOT_PRESENT device to its guid
    assert missing.name == str(missing.guid)
    assert missing.state == VDevState.CANT_OPEN
    # the healthy sibling is unaffected
    assert present.name == present_path
    assert present.state == VDevState.ONLINE


def test_missing_device_path_is_retained(degraded_pool):
    lz, pool, present_path, missing_path = degraded_pool
    mirror = pool.status().storage_vdevs[0]
    present, missing = _split_children(mirror)

    # the stored config path survives even though the device is gone;
    # this is what "was /path" in zpool status is printed from
    assert missing.path == missing_path
    assert present.path == present_path
    assert mirror.path is None


def test_missing_device_asdict(degraded_pool):
    lz, pool, present_path, missing_path = degraded_pool
    mirror = pool.status(asdict=True)['storage_vdevs'][0]

    children = {c['path']: c for c in mirror['children']}
    assert set(children) == {present_path, missing_path}

    missing = children[missing_path]
    assert missing['name'] == str(missing['guid'])
    assert missing['state'] == VDevState.CANT_OPEN
    assert children[present_path]['name'] == present_path
