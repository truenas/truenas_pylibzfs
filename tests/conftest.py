import os
import pytest
import shutil
import subprocess
import truenas_pylibzfs

TEST_ALTROOT = '/tmp/altroot'
FAKE_DISK_DIR = '/tmp/truenas_pylibzfs_disks'
DISK_SZ = 1024 * 1048576
DISKS = ('d1.img',)
POOLS = ('testdozer',)


@pytest.fixture
def disks():
    out = []
    os.makedirs(FAKE_DISK_DIR, exist_ok=True)
    os.makedirs(TEST_ALTROOT, exist_ok=True)
    for disk in DISKS:
        path = os.path.join(FAKE_DISK_DIR, disk)
        with open(path, 'w') as f:
            os.ftruncate(f.fileno(), DISK_SZ)

        out.append(path)

    try:
        yield out
    finally:
        shutil.rmtree(FAKE_DISK_DIR)
        shutil.rmtree(TEST_ALTROOT)


@pytest.fixture
def data_pool1(disks):
    # TODO: add replace with truenas_pylibzfs methods once they are complete
    subprocess.run(['zpool', 'create', POOLS[0], disks[0], '-R', TEST_ALTROOT])

    try:
        yield POOLS[0]
    finally:
        subprocess.run(['zpool', 'destroy', POOLS[0]])


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
