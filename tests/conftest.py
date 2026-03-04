import os
import pytest
import shutil
import truenas_pylibzfs

FAKE_DISK_DIR = '/tmp/truenas_pylibzfs_disks'
DISK_SZ = 1024 * 1048576
DISKS = ('d1.img',)
POOLS = ('testdozer',)

VDevType = truenas_pylibzfs.VDevType


@pytest.fixture
def disks():
    out = []
    os.makedirs(FAKE_DISK_DIR, exist_ok=True)
    for disk in DISKS:
        path = os.path.join(FAKE_DISK_DIR, disk)
        with open(path, 'w') as f:
            os.ftruncate(f.fileno(), DISK_SZ)

        out.append(path)

    try:
        yield out
    finally:
        shutil.rmtree(FAKE_DISK_DIR)


@pytest.fixture
def data_pool1(disks):
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(
        name=POOLS[0],
        storage_vdevs=[
            truenas_pylibzfs.create_vdev_spec(vdev_type=VDevType.FILE, name=disks[0])
        ],
        force=True,
    )

    try:
        yield POOLS[0]
    finally:
        try:
            lz.destroy_pool(name=POOLS[0], force=True)
        except Exception:
            pass


@pytest.fixture
def root_dataset(data_pool1):
    """ basic pytest fixture that returns an open libzfs handle, and a ZFSDataset
    handle on the root dataset of our first pool created """
    lz = truenas_pylibzfs.open_handle()
    return (lz, lz.open_resource(name=data_pool1))


@pytest.fixture
def dataset(root_dataset):
    """ fixture that creates a temporary dataset in our first pool and yields the
    libzfs handle, the root dataset handle, and this temporary dataset handle """
    lz, root = root_dataset
    rsrc_name = f'{root.name}/test1'
    lz.create_resource(name=rsrc_name, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
    rsrc = lz.open_resource(name=rsrc_name)

    try:
        yield (lz, root, rsrc)

    finally:
        lz.destroy_resource(name=rsrc_name)
