import os
import pytest
import shutil
import tempfile
import truenas_pylibzfs

FAKE_DISK_DIR = '/tmp/truenas_pylibzfs_disks'
DISK_SZ = 1024 * 1048576
DISKS = ('d1.img',)
POOLS = ('testdozer',)

VDevType = truenas_pylibzfs.VDevType

# ---------------------------------------------------------------------------
# Shared helpers for new-style tests (256 MiB image files, factory pattern)
# ---------------------------------------------------------------------------

POOL_DISK_SZ = 256 * 1048576  # 256 MiB — small enough to be fast


def make_vdev_spec(path):
    """Return a FILE vdev spec for the given image-file path."""
    return truenas_pylibzfs.create_vdev_spec(
        vdev_type=truenas_pylibzfs.VDevType.FILE, name=path
    )


@pytest.fixture
def make_disks():
    """Factory fixture: make_disks(n, disk_sz=POOL_DISK_SZ) -> list[str].

    Creates *n* sparse image files in a fresh temp directory and returns their
    paths.  All directories are removed on fixture teardown.
    """
    dirs = []

    def _make(n, disk_sz=POOL_DISK_SZ):
        d = tempfile.mkdtemp(prefix='pylibzfs_')
        dirs.append(d)
        paths = []
        for i in range(n):
            p = os.path.join(d, f'd{i}.img')
            with open(p, 'w') as f:
                os.ftruncate(f.fileno(), disk_sz)
            paths.append(p)
        return paths

    yield _make

    for d in dirs:
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def make_pool(make_disks):
    """Factory fixture: make_pool(name) -> (lz, pool_handle, root_resource).

    Creates a single-disk stripe pool backed by a sparse image file.  All
    pools created through this factory are destroyed automatically on fixture
    teardown, so callers do not need their own try/finally blocks.
    """
    lz = truenas_pylibzfs.open_handle()
    created = []

    def _make(name, disk_sz=POOL_DISK_SZ):
        disks = make_disks(1, disk_sz=disk_sz)
        lz.create_pool(
            name=name,
            storage_vdevs=[make_vdev_spec(disks[0])],
            force=True,
        )
        pool_hdl = lz.open_pool(name=name)
        root = lz.open_resource(name=name)
        created.append(name)
        return lz, pool_hdl, root

    yield _make

    for name in reversed(created):
        try:
            lz.destroy_pool(name=name, force=True)
        except Exception:
            pass


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
