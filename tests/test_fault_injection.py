"""Fault-injection coverage for error paths file-backed pools cannot reach.

The `inject`, `zinject_action`, `wait_for_error_count`, and
`wait_for_vdev_state` fixtures are provided by conftest.py and raise on
first use if zinject(8) is missing -- a hard failure, not a skip.

Design constraints, validated against the ZFS source:

- Device-level faults are only ever injected into one leg of a two-way
  mirror. The healthy leg keeps every write and txg sync succeeding
  (vdev_mirror_io_done fails a write only when zero children succeed),
  so the pool can never enter the suspended state. That matters because
  suspension is otherwise unavoidable for failed allocating writes --
  failmode=continue does NOT prevent it (zio_done only consults failmode
  for ENXIO reads) -- and a suspended pool parks sync/fsync/destroy in
  uninterruptible kernel sleeps that pytest-timeout's signal method
  cannot interrupt, wedging the CI job until the workflow timeout kills
  it. Removing a zinject handler does not resume a suspended pool; only
  zpool clear (zio_resume) does.

- Read-path faults are driven by a scrub, not by reading files back:
  scrub issues physical reads of every allocated block on every mirror
  leg, while a normal read of freshly written data is satisfied from the
  dbuf/ARC caches without any device I/O (zinject -a flushes only what
  the ARC can evict, so it is not sufficient).

- The vdev state-transition test uses the explicit "zinject -A fault"
  action. Injected I/O errors cannot fault a vdev here: the injection
  layer skips label-region I/O, and probe I/O only matches "-T probe"
  (zio_handle_device_injection_impl, zio_match_iotype), so the
  vdev_probe() issued after an injected error always succeeds and the
  leaf stays ONLINE.
"""

import os
import subprocess
import time

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc


VDevType = truenas_pylibzfs.VDevType
VDevState = truenas_pylibzfs.libzfs_types.VDevState
ScanFunction = truenas_pylibzfs.libzfs_types.ScanFunction
ScanState = truenas_pylibzfs.libzfs_types.ScanState

PAYLOAD = b"X" * (256 * 1024)

# Thread-method watchdog instead of the session default (signal): if a
# regression ever wedges a thread in an uninterruptible ZFS ioctl, the
# watchdog thread still dumps all stacks and aborts the run instead of
# stalling silently until the CI job timeout.
pytestmark = pytest.mark.timeout(timeout=60, method="thread")


def _file_spec(path):
    return truenas_pylibzfs.create_vdev_spec(vdev_type=VDevType.FILE, name=path)


def _mirror_spec(paths):
    return truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.MIRROR,
        children=[_file_spec(p) for p in paths],
    )


def _write_payload(mountpoint, name="victim"):
    path = os.path.join(mountpoint, name)
    with open(path, "wb") as fh:
        fh.write(PAYLOAD)
        fh.flush()
        os.fsync(fh.fileno())
    subprocess.run(["sync"], check=True)
    return path


@pytest.fixture
def mirror_pool(make_disks):
    disks = make_disks(2)
    lz = truenas_pylibzfs.open_handle()
    name = "zinject_mirror"
    lz.create_pool(
        name=name,
        storage_vdevs=[_mirror_spec(disks)],
        force=True,
    )
    pool = lz.open_pool(name=name)
    root = lz.open_resource(name=name)
    root.mount()
    try:
        yield lz, pool, root, disks
    finally:
        # Belt and braces: clear injected error state (and resume the
        # pool if a regression ever suspended it) so destroy cannot
        # hang. zinject handlers are already removed by this point --
        # the inject fixture clears them before teardown runs.
        try:
            pool.clear()
        except Exception:
            pass
        try:
            lz.destroy_pool(name=name, force=True)
        except Exception:
            pass


def test_read_error_counter_increments(mirror_pool, inject, wait_for_error_count):
    """Device-level EIO on reads (-e io -T read) bumps vdev read_errors.

    A scrub reads every allocated block from both mirror legs straight
    from disk, so the injected leg is guaranteed to see read I/O while
    the healthy leg keeps the pool alive.
    """
    _lz, pool, root, disks = mirror_pool
    _write_payload(root.get_mountpoint())

    with inject("-d", disks[0], "-e", "io", "-T", "read", "-f", "100", pool.name):
        pool.scan(func=ScanFunction.SCRUB)
        wait_for_error_count(pool, "read_errors", minimum=1, timeout=10)


def test_write_error_counter_increments(mirror_pool, inject, wait_for_error_count):
    """Device-level EIO on writes (-e io -T write) bumps vdev write_errors.

    fsync pushes the payload to both mirror legs; the injected leg fails
    and is counted, while the healthy leg completes the write so neither
    the fsync nor the txg sync behind it can block.
    """
    _lz, pool, root, disks = mirror_pool

    with inject("-d", disks[0], "-e", "io", "-T", "write", "-f", "100", pool.name):
        _write_payload(root.get_mountpoint(), name="write_victim")
        wait_for_error_count(pool, "write_errors", minimum=1, timeout=10)


def test_checksum_error_counter_increments(mirror_pool, inject, wait_for_error_count):
    """Device-level bit flips on reads (-e corrupt) bump vdev checksum_errors.

    Mirror scrub verifies checksums per child (zio_vdev_child_io moves
    the checksum-verify stage onto the child when it has the block
    pointer), so the failure is attributed to the corrupted leg and then
    self-healed from the healthy copy. -T read is required: the bit-flip
    injection is only valid for read zios.
    """
    _lz, pool, root, disks = mirror_pool
    _write_payload(root.get_mountpoint())

    with inject("-d", disks[0], "-e", "corrupt", "-T", "read", "-f", "100", pool.name):
        pool.scan(func=ScanFunction.SCRUB)
        wait_for_error_count(pool, "checksum_errors", minimum=1, timeout=10)


def test_vdev_fault_action_faults_leaf(mirror_pool, zinject_action, wait_for_vdev_state):
    """zinject -A fault transitions the target leaf vdev to FAULTED.

    vdev_fault() only downgrades the request to DEGRADED when the leaf
    holds the only valid copy of some data; a freshly synced two-way
    mirror leg never does, so FAULTED is deterministic. The pool must
    remain writable through the surviving leg.
    """
    _lz, pool, root, disks = mirror_pool

    zinject_action("-d", disks[0], "-A", "fault", pool.name)

    # pool.status() reports leaf names as basenames, not full paths,
    # so we match on "leaf vdev in FAULTED state" rather than path
    # equality. Only one leaf is faulted, so this is unambiguous.
    wait_for_vdev_state(
        pool,
        lambda v: not v.children and v.state == VDevState.FAULTED,
        timeout=10,
    )

    _write_payload(root.get_mountpoint(), name="post_fault")


PASSPHRASE = "Cats1234"


def _ensure_mounted(rsrc):
    """Mount rsrc unless ZFS already auto-mounted it; return the mountpoint."""
    mountpoint = rsrc.get_mountpoint()
    if mountpoint is None or not os.path.ismount(mountpoint):
        rsrc.mount()
        mountpoint = rsrc.get_mountpoint()
    return mountpoint


def _wait_for_scan_end(pool, timeout=60.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        info = pool.scrub_info()
        if info is None or info.state != ScanState.SCANNING:
            return
        time.sleep(0.1)
    raise AssertionError("timed out waiting for the scrub to finish")


def test_status_survives_errlog_entry_for_destroyed_snapshot(make_pool, inject, tmp_path):
    """pool.status() must not raise when the error log holds an entry it
    cannot resolve to a live dataset.

    The kernel keys each head_errlog entry by the head dataset of the
    objset the error was seen in. When that lookup fails at sync time (a
    locked encrypted dataset makes the decrypting hold fail) it falls back
    to keying the entry by the raw objset id. If that objset is a snapshot
    that is later destroyed, zpool_get_errlog() aborts with ENOENT and
    libzfs raises EZFS_NOENT. pool.status() must treat that like an
    unavailable error log, as `zpool status` does, rather than failing.
    """
    lz, pool, root = make_pool("zinject_errlog")
    ds_name = f"{pool.name}/enc"
    snap_name = f"{ds_name}@snap"

    crypto = lz.resource_cryptography_config(keyformat="passphrase", key=PASSPHRASE)
    lz.create_resource(
        name=ds_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        crypto=crypto,
    )
    # Mount under tmp_path so the test also runs on a read-only root.
    root.set_properties(
        properties={truenas_pylibzfs.ZFSProperty.MOUNTPOINT: str(tmp_path / "pool")}
    )
    _ensure_mounted(root)
    rsrc = lz.open_resource(name=ds_name)
    mountpoint = _ensure_mounted(rsrc)

    victim = _write_payload(mountpoint)
    victim_obj = os.stat(victim).st_ino
    lzc.create_snapshots(snapshot_names=[snap_name])
    # Leave the snapshot as the only holder of the block so the scrub
    # visits it under the snapshot's objset.
    os.unlink(victim)
    subprocess.run(["sync"], check=True)

    snap_objset = (
        lz.open_resource(name=snap_name)
        .get_properties(properties={truenas_pylibzfs.ZFSProperty.OBJSETID})
        .objsetid.value
    )

    # Lock the dataset so the errlog sync cannot resolve the head dataset.
    rsrc.unmount(unload_encryption_key=True)

    bookmark = f"{snap_objset:x}:{victim_obj:x}:0:0:0"
    with inject("-b", bookmark, pool.name):
        pool.scan(func=ScanFunction.SCRUB)
        _wait_for_scan_end(pool)
    subprocess.run(["zpool", "sync", pool.name], check=True)

    rsrc.crypto().load_key(key=PASSPHRASE)
    # With the snapshot still present the entry resolves.
    assert lz.open_pool(name=pool.name).status().corrupted_files

    lzc.destroy_snapshots(snapshot_names=[snap_name])

    status = lz.open_pool(name=pool.name).status()
    assert status.corrupted_files == ()
    assert status.name == pool.name
