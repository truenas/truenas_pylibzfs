import contextlib
import os
import re
import pytest
import shutil
import subprocess
import tempfile
import time
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


# ---------------------------------------------------------------------------
# Fault injection (zinject) helpers, exposed as fixtures so the import chain
# stays inside pytest's fixture machinery. zinject(8) absence is reported on
# first use rather than at conftest import, so test files that don't touch
# fault injection are unaffected.
# ---------------------------------------------------------------------------

_ZINJECT_BIN = shutil.which("zinject")
_HANDLER_ID_RE = re.compile(r"^\s*(\d+)\b")


def _require_zinject():
    if _ZINJECT_BIN is None:
        raise RuntimeError(
            "zinject(8) was not found on PATH; the fault-injection test "
            "suite requires it. Install the ZFS userland on this host."
        )
    return _ZINJECT_BIN


def _list_handler_ids():
    bin_ = _require_zinject()
    result = subprocess.run(
        [bin_], capture_output=True, text=True, check=True,
    )
    ids = set()
    for line in result.stdout.splitlines():
        match = _HANDLER_ID_RE.match(line)
        if match:
            ids.add(int(match.group(1)))
    return ids


def _clear_handlers(ids):
    bin_ = _require_zinject()
    for handler_id in ids:
        subprocess.run(
            [bin_, "-c", str(handler_id)],
            capture_output=True, text=True, check=True,
        )


@contextlib.contextmanager
def _inject(*args):
    bin_ = _require_zinject()
    before = _list_handler_ids()
    subprocess.run(
        [bin_, *map(str, args)],
        capture_output=True, text=True, check=True,
    )
    after = _list_handler_ids()
    new_ids = after - before
    if not new_ids:
        raise RuntimeError(
            f"zinject {' '.join(map(str, args))} reported success but "
            "no new handler appeared"
        )
    try:
        yield new_ids
    finally:
        _clear_handlers(new_ids)


def _zinject_action(*args):
    """Run a one-shot zinject action (e.g. -d dev -A fault pool).

    Unlike error injection, actions do not register a handler, so there
    is nothing to list or clear afterwards; recovery is the caller's
    responsibility (zpool clear, or destroying the pool).
    """
    bin_ = _require_zinject()
    subprocess.run(
        [bin_, *map(str, args)],
        capture_output=True, text=True, check=True,
    )


def _walk_vdevs(top_vdev):
    yield top_vdev
    if top_vdev.children:
        for child in top_vdev.children:
            yield from _walk_vdevs(child)


def _refreshed_status(pool):
    pool.refresh_stats()
    return pool.status(get_stats=True)


def _wait_for_error_count(pool, kind, minimum=1, timeout=10.0):
    deadline = time.monotonic() + timeout
    last_total = 0
    while time.monotonic() < deadline:
        status = _refreshed_status(pool)
        total = 0
        for top in status.storage_vdevs:
            for vdev in _walk_vdevs(top):
                if vdev.stats is not None:
                    total += getattr(vdev.stats, kind)
        if total >= minimum:
            return total
        last_total = total
        time.sleep(0.1)
    raise AssertionError(
        f"timed out waiting for {kind} >= {minimum} "
        f"(last observed total: {last_total})"
    )


def _wait_for_vdev_state(pool, predicate, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = _refreshed_status(pool)
        for top in status.storage_vdevs:
            for vdev in _walk_vdevs(top):
                if predicate(vdev):
                    return vdev
        time.sleep(0.1)
    raise AssertionError("timed out waiting for vdev state predicate")


@pytest.fixture
def inject():
    """Context manager wrapping zinject; clears only the handlers it installs."""
    return _inject


@pytest.fixture
def zinject_action():
    """Run a one-shot zinject action that registers no handler."""
    return _zinject_action


@pytest.fixture
def wait_for_error_count():
    """Poll pool.status() until sum(vdev.stats.<kind>) >= minimum."""
    return _wait_for_error_count


@pytest.fixture
def wait_for_vdev_state():
    """Poll pool.status() until any vdev satisfies the predicate."""
    return _wait_for_vdev_state
