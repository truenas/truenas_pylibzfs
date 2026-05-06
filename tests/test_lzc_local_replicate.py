"""
tests/test_lzc_local_replicate.py - end-to-end tests for lzc.local_replicate.

local_replicate is a single-call wrapper around lzc.send + lzc.receive
that streams data through an internal pipe with the GIL released for
the entire transfer.  These tests exercise the full surface:

  - Smoke / round-trip / data integrity
  - Incremental replication
  - SendFlags handling (acceptance + rejection of RAW / SAVED)
  - raw=True for encrypted datasets
  - props on the receive side
  - force on a modified destination
  - Error paths (source missing, dest parent missing, dest exists)
  - GIL is released for the duration
  - Audit event firing
  - History entry on success
  - Argument validation
  - No fd / thread leaks
  - Progress callback firing during transfer
  - Concurrent invocations on disjoint dataset pairs

The pool fixtures mirror those in tests/test_lzc_replication.py.
"""

import os
import shutil
import sys
import tempfile
import threading
import time

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

POOL = "testpool_lzc_local_repl"
DSK_SZ = 256 * 1024 * 1024
DATA_SZ = 4 * 1024 * 1024
DATA_SZ_LARGE = 16 * 1024 * 1024
PASSPHRASE = "Cats12345"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_pool(tmpdir):
    path = os.path.join(tmpdir, "d0.img")
    with open(path, "w") as f:
        os.ftruncate(f.fileno(), DSK_SZ)
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(
        name=POOL,
        storage_vdevs=[
            truenas_pylibzfs.create_vdev_spec(
                vdev_type=truenas_pylibzfs.VDevType.FILE, name=path
            )
        ],
        force=True,
    )
    return lz


def _destroy_pool(lz):
    try:
        lz.destroy_pool(name=POOL, force=True)
    except Exception:
        pass


def _write_data(mountpoint, size, filename="payload.dat", random=False):
    path = os.path.join(mountpoint, filename)
    chunk = os.urandom(65536) if random else b"Z" * 65536
    with open(path, "wb") as f:
        written = 0
        while written < size:
            n = min(len(chunk), size - written)
            f.write(chunk[:n])
            written += n


def _read_data(mountpoint, filename="payload.dat"):
    with open(os.path.join(mountpoint, filename), "rb") as f:
        return f.read()


def _snap(name):
    lzc.create_snapshots(snapshot_names={name})


def _destroy_recv(lz, fs_name):
    try:
        lzc.run_channel_program(
            pool_name=fs_name.split("/")[0],
            script=lzc.ChannelProgramEnum.DESTROY_RESOURCES,
            script_arguments_dict={
                "target": fs_name, "recursive": True, "defer": False
            },
        )
    except Exception:
        pass
    try:
        lz.destroy_resource(name=fs_name)
    except Exception:
        pass


def _open_fd_count():
    """Return the count of currently open fds in this process."""
    return len(os.listdir("/proc/self/fd"))


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def pool_fixture():
    """Yields (lz, pool_name) with a 256 MiB file-backed pool plus a
    mounted source dataset POOL/src."""
    d = tempfile.mkdtemp(prefix="pylibzfs_lrepl_")
    lz = _make_pool(d)
    src = f"{POOL}/src"
    lz.create_resource(
        name=src, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    lz.open_resource(name=src).mount()
    try:
        yield lz, POOL
    finally:
        _destroy_pool(lz)
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def snapped_pool(pool_fixture):
    """Pool fixture + a single snapshot POOL/src@snap1 with DATA_SZ payload."""
    lz, pool = pool_fixture
    src = f"{pool}/src"
    snap = f"{src}@snap1"
    _write_data(f"/{src}", DATA_SZ)
    _snap(snap)
    yield lz, pool, snap


@pytest.fixture
def two_snapped_pool(pool_fixture):
    """Pool fixture + two sequential snapshots with data between them."""
    lz, pool = pool_fixture
    src = f"{pool}/src"
    snap1 = f"{src}@snap1"
    snap2 = f"{src}@snap2"
    _write_data(f"/{src}", DATA_SZ)
    _snap(snap1)
    _write_data(f"/{src}", DATA_SZ, filename="more.dat")
    _snap(snap2)
    yield lz, pool, snap1, snap2


@pytest.fixture
def large_block_snapped_pool(pool_fixture):
    """Source dataset created with recordsize=1M so LARGE_BLOCK has effect."""
    lz, pool = pool_fixture
    src = f"{pool}/src_lb"
    lz.create_resource(
        name=src,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        properties={truenas_pylibzfs.ZFSProperty.RECORDSIZE: "1M"},
    )
    lz.open_resource(name=src).mount()
    _write_data(f"/{src}", DATA_SZ_LARGE)
    snap = f"{src}@snap1"
    _snap(snap)
    yield lz, pool, snap


@pytest.fixture
def encrypted_snapped_pool(pool_fixture):
    """Encrypted (passphrase) source dataset with data and one snapshot.

    Yields (lz, pool, snap_name, enc_resource).
    """
    lz, pool = pool_fixture
    enc_name = f"{pool}/enc_src"
    crypto = lz.resource_cryptography_config(
        keyformat="passphrase", key=PASSPHRASE
    )
    lz.create_resource(
        name=enc_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        crypto=crypto,
    )
    enc_rsrc = lz.open_resource(name=enc_name)
    enc_rsrc.mount()
    _write_data(f"/{enc_name}", DATA_SZ)
    snap = f"{enc_name}@snap1"
    _snap(snap)
    yield lz, pool, snap, enc_rsrc


# ===========================================================================
# 1. Smoke / round-trip
# ===========================================================================

class TestLocalReplicateSmoke:
    def test_full_roundtrip_returns_none(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest = f"{pool}/recv@snap1"
        result = lzc.local_replicate(source=snap, dest=dest)
        assert result is None
        _destroy_recv(lz, f"{pool}/recv")

    def test_full_received_snapshot_accessible(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        dest = f"{dest_fs}@snap1"
        lzc.local_replicate(source=snap, dest=dest)
        ds = lz.open_resource(name=dest)
        assert ds is not None
        _destroy_recv(lz, dest_fs)

    def test_full_received_data_matches_source(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        dest = f"{dest_fs}@snap1"
        lzc.local_replicate(source=snap, dest=dest)
        lz.open_resource(name=dest_fs).mount()
        try:
            assert _read_data(f"/{pool}/src") == _read_data(f"/{dest_fs}")
        finally:
            _destroy_recv(lz, dest_fs)

    def test_keyword_only(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(TypeError):
            lzc.local_replicate(snap, f"{pool}/recv@snap1")  # type: ignore


# ===========================================================================
# 2. Incremental
# ===========================================================================

class TestLocalReplicateIncremental:
    def test_incremental_roundtrip(self, two_snapped_pool):
        lz, pool, snap1, snap2 = two_snapped_pool
        dest_fs = f"{pool}/recv"
        dest1 = f"{dest_fs}@snap1"
        dest2 = f"{dest_fs}@snap2"
        lzc.local_replicate(source=snap1, dest=dest1)
        lzc.local_replicate(source=snap2, dest=dest2, fromsnap=snap1)
        assert lz.open_resource(name=dest1) is not None
        assert lz.open_resource(name=dest2) is not None
        _destroy_recv(lz, dest_fs)

    def test_incremental_data_matches(self, two_snapped_pool):
        lz, pool, snap1, snap2 = two_snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(source=snap1, dest=f"{dest_fs}@snap1")
        lzc.local_replicate(
            source=snap2, dest=f"{dest_fs}@snap2", fromsnap=snap1
        )
        lz.open_resource(name=dest_fs).mount()
        try:
            assert (_read_data(f"/{pool}/src", filename="more.dat")
                    == _read_data(f"/{dest_fs}", filename="more.dat"))
        finally:
            _destroy_recv(lz, dest_fs)

    def test_incremental_bad_fromsnap_raises(self, two_snapped_pool):
        _, pool, snap1, snap2 = two_snapped_pool
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(
                source=snap1, dest=f"{pool}/recv@snap1", fromsnap=snap2
            )

    def test_incremental_missing_fromsnap_raises(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                fromsnap=f"{pool}/src@nosuch",
            )


# ===========================================================================
# 3. SendFlags handling
# ===========================================================================

class TestLocalReplicateFlags:
    def test_embed_data_flag(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            send_flags=lzc.SendFlags.EMBED_DATA,
        )
        assert lz.open_resource(name=f"{dest_fs}@snap1") is not None
        _destroy_recv(lz, dest_fs)

    def test_compress_flag(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            send_flags=lzc.SendFlags.COMPRESS,
        )
        assert lz.open_resource(name=f"{dest_fs}@snap1") is not None
        _destroy_recv(lz, dest_fs)

    def test_large_block_flag(self, large_block_snapped_pool):
        lz, pool, snap = large_block_snapped_pool
        dest_fs = f"{pool}/recv_lb"
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            send_flags=lzc.SendFlags.LARGE_BLOCK,
        )
        assert lz.open_resource(name=f"{dest_fs}@snap1") is not None
        _destroy_recv(lz, dest_fs)

    def test_combined_flags(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            send_flags=lzc.SendFlags.EMBED_DATA | lzc.SendFlags.COMPRESS,
        )
        assert lz.open_resource(name=f"{dest_fs}@snap1") is not None
        _destroy_recv(lz, dest_fs)

    def test_raw_in_send_flags_rejected(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(ValueError, match="raw"):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                send_flags=lzc.SendFlags.RAW,
            )

    def test_saved_in_send_flags_rejected(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(ValueError, match="SAVED"):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                send_flags=lzc.SendFlags.SAVED,
            )

    def test_raw_in_send_flags_rejected_combined(self, snapped_pool):
        """RAW must be rejected even when combined with valid bits."""
        _, pool, snap = snapped_pool
        with pytest.raises(ValueError, match="raw"):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                send_flags=lzc.SendFlags.COMPRESS | lzc.SendFlags.RAW,
            )


# ===========================================================================
# 4. raw= for encrypted streams
# ===========================================================================

class TestLocalReplicateRaw:
    def test_raw_replicates_encrypted_with_loaded_key(
        self, encrypted_snapped_pool
    ):
        """raw=True works when the wrapping key is loaded on the source."""
        lz, pool, snap, enc_rsrc = encrypted_snapped_pool
        dest_fs = f"{pool}/enc_dst"
        dest = f"{dest_fs}@snap1"
        lzc.local_replicate(source=snap, dest=dest, raw=True)
        ds = lz.open_resource(name=dest_fs)
        assert ds.encrypted
        _destroy_recv(lz, dest_fs)

    def test_raw_replicates_encrypted_with_unloaded_key(
        self, encrypted_snapped_pool
    ):
        """raw=True must succeed even when the wrapping key has been
        unloaded -- raw streams ship encrypted blocks as-is and do not
        need the plaintext key on either side."""
        lz, pool, snap, enc_rsrc = encrypted_snapped_pool
        # Atomic umount + key unload.  A separate enc_rsrc.umount()
        # followed by crypto().unload_key() races with the kernel's
        # mount-reference release and frequently fails with EBUSY.
        enc_rsrc.unmount(unload_encryption_key=True)
        assert enc_rsrc.crypto().info().key_is_loaded is False

        dest_fs = f"{pool}/enc_dst"
        dest = f"{dest_fs}@snap1"
        lzc.local_replicate(source=snap, dest=dest, raw=True)
        ds = lz.open_resource(name=dest_fs)
        assert ds.encrypted
        _destroy_recv(lz, dest_fs)

    def test_non_raw_on_unloaded_encrypted_source_fails(
        self, encrypted_snapped_pool
    ):
        """raw=False on an encrypted source whose key is not loaded must
        fail (ZFS cannot decrypt to send a plaintext stream)."""
        lz, pool, snap, enc_rsrc = encrypted_snapped_pool
        enc_rsrc.unmount(unload_encryption_key=True)

        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(source=snap, dest=f"{pool}/enc_dst@snap1")


# ===========================================================================
# 5. props
# ===========================================================================

class TestLocalReplicateProps:
    def test_empty_props_accepted(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(source=snap, dest=f"{dest_fs}@snap1", props={})
        assert lz.open_resource(name=f"{dest_fs}@snap1") is not None
        _destroy_recv(lz, dest_fs)

    def test_props_compression_applied(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            props={"compression": "lz4"},
        )
        ds = lz.open_resource(name=dest_fs)
        info = ds.asdict(properties={truenas_pylibzfs.ZFSProperty.COMPRESSION})
        assert info["properties"]["compression"]["value"] == "lz4"
        _destroy_recv(lz, dest_fs)

    def test_props_readonly_applied(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            props={"readonly": "on"},
        )
        ds = lz.open_resource(name=dest_fs)
        info = ds.asdict(properties={truenas_pylibzfs.ZFSProperty.READONLY})
        assert info["properties"]["readonly"]["value"] == "on"
        _destroy_recv(lz, dest_fs)

    def test_props_non_dict_raises_type_error(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(TypeError):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1", props="bad"
            )


# ===========================================================================
# 6. force=
# ===========================================================================

class TestLocalReplicateForce:
    def test_no_force_on_modified_dest_fails(self, two_snapped_pool):
        lz, pool, snap1, snap2 = two_snapped_pool
        dest_fs = f"{pool}/recv"
        # Full receive of snap1
        lzc.local_replicate(source=snap1, dest=f"{dest_fs}@snap1")
        lz.open_resource(name=dest_fs).mount()
        # Modify dest after snap1
        _write_data(f"/{dest_fs}", 64 * 1024, filename="taint.dat")
        # Incremental without force must fail
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(
                source=snap2, dest=f"{dest_fs}@snap2", fromsnap=snap1,
            )
        _destroy_recv(lz, dest_fs)

    def test_force_true_on_modified_dest_succeeds(self, two_snapped_pool):
        lz, pool, snap1, snap2 = two_snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(source=snap1, dest=f"{dest_fs}@snap1")
        lz.open_resource(name=dest_fs).mount()
        _write_data(f"/{dest_fs}", 64 * 1024, filename="taint.dat")
        lzc.local_replicate(
            source=snap2, dest=f"{dest_fs}@snap2",
            fromsnap=snap1, force=True,
        )
        assert lz.open_resource(name=f"{dest_fs}@snap2") is not None
        _destroy_recv(lz, dest_fs)

    def test_force_on_fresh_dest_is_harmless(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1", force=True,
        )
        assert lz.open_resource(name=f"{dest_fs}@snap1") is not None
        _destroy_recv(lz, dest_fs)


# ===========================================================================
# 7. Error paths
# ===========================================================================

class TestLocalReplicateErrors:
    def test_source_missing_raises(self, pool_fixture):
        _, pool = pool_fixture
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(
                source=f"{pool}/nosuch@snap", dest=f"{pool}/recv@snap1",
            )

    def test_dest_parent_missing_raises(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/no/such/parent@snap1",
            )

    def test_dest_already_exists_without_force(self, snapped_pool):
        """Replicating twice into the same dest snap fails the second time."""
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        dest = f"{dest_fs}@snap1"
        lzc.local_replicate(source=snap, dest=dest)
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(source=snap, dest=dest)
        _destroy_recv(lz, dest_fs)

    def test_recv_error_does_not_kill_process_via_sigpipe(
        self, large_block_snapped_pool
    ):
        """Force a recv-side error mid-stream and verify the call returns
        a ZFSCoreException rather than the whole process dying with
        SIGPIPE.  Without the pthread_sigmask in the send thread the
        kernel write() inside lzc_send would deliver SIGPIPE and tear
        down the test process."""
        lz, pool, snap = large_block_snapped_pool
        dest_fs = f"{pool}/recv_lb"
        dest = f"{dest_fs}@snap1"
        # First replicate succeeds and creates the dest
        lzc.local_replicate(source=snap, dest=dest)
        # A second attempt with the same dest fails -- and importantly
        # the failure is reported as a Python exception, not a signal.
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(source=snap, dest=dest)
        _destroy_recv(lz, dest_fs)

    def test_dest_exists_error_message_is_actionable(self, snapped_pool):
        """A dest-already-exists EEXIST should produce a str(e) that names
        the errno, the dest path, and a description -- enough that a
        user can act on it without reading attributes interactively."""
        import errno as _errno

        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        dest = f"{dest_fs}@snap1"
        lzc.local_replicate(source=snap, dest=dest)
        try:
            with pytest.raises(lzc.ZFSCoreException) as exc_info:
                lzc.local_replicate(source=snap, dest=dest)
            e = exc_info.value
            text = str(e)
            assert "EEXIST" in text, f"missing errno name: {text!r}"
            assert dest in text, f"missing dest path: {text!r}"
            # strerror text for EEXIST is "File exists" on Linux glibc.
            assert "exists" in text.lower(), (
                f"missing description: {text!r}"
            )
            # Named-attribute contract is preserved.
            assert e.code == _errno.EEXIST
            assert e.name == "EEXIST"
            assert e.msg.startswith("lzc_receive() failed")
            assert dest in e.msg
        finally:
            _destroy_recv(lz, dest_fs)

    def test_dest_exists_with_force_still_errors(self, snapped_pool):
        """force=True does NOT destroy a pre-existing destination snapshot;
        the receive still fails with EEXIST.  Documented behavior; this
        test pins it so a future change that silently makes force
        destructive would have to break this test consciously."""
        import errno as _errno

        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        dest = f"{dest_fs}@snap1"
        lzc.local_replicate(source=snap, dest=dest)
        try:
            with pytest.raises(lzc.ZFSCoreException) as exc_info:
                lzc.local_replicate(source=snap, dest=dest, force=True)
            assert exc_info.value.code == _errno.EEXIST
        finally:
            _destroy_recv(lz, dest_fs)

    def test_source_missing_error_message_is_actionable(self, pool_fixture):
        """A missing source can surface as ENOENT from the send side or as
        a stream-decode error from the recv side, depending on timing.
        Either way the user should get an errno name and at least one of
        the dataset paths -- not the bare ('msg', None) tuple-repr."""
        _, pool = pool_fixture
        bad_src = f"{pool}/nosuch@snap"
        bad_dst = f"{pool}/recv@snap1"
        with pytest.raises(lzc.ZFSCoreException) as exc_info:
            lzc.local_replicate(source=bad_src, dest=bad_dst)
        text = str(exc_info.value)
        assert "[" in text and "]" in text, (
            f"missing errno-name bracket: {text!r}"
        )
        assert (bad_src in text) or (bad_dst in text), (
            f"missing dataset context: {text!r}"
        )


# ===========================================================================
# 8. GIL released for the duration
# ===========================================================================

class TestLocalReplicateGIL:
    def test_python_thread_makes_progress_during_replicate(
        self, large_block_snapped_pool
    ):
        """A CPU-bound Python thread must continue advancing while the
        main thread runs local_replicate.  If the GIL were held the
        counter would stay at zero until the call returned."""
        lz, pool, snap = large_block_snapped_pool
        dest_fs = f"{pool}/recv_lb"
        dest = f"{dest_fs}@snap1"

        stop = threading.Event()
        counter = [0]

        def _busy():
            while not stop.is_set():
                counter[0] += 1

        t = threading.Thread(target=_busy, daemon=True)
        t.start()

        # Sample counter just before the call starts to avoid counting
        # bumps that happened in the small startup window.
        baseline = counter[0]
        lzc.local_replicate(source=snap, dest=dest)
        delta = counter[0] - baseline
        stop.set()
        t.join(timeout=5)

        # Even on a heavily loaded system, a busy loop should advance
        # significantly during a 16 MiB replicate.
        assert delta > 1000, (
            f"counter advanced by only {delta} during replicate -- "
            "GIL may not have been released"
        )
        _destroy_recv(lz, dest_fs)


# ===========================================================================
# 9. Audit
# ===========================================================================

# Module-level capture so the audit hook (which cannot be uninstalled)
# always has a target it can append to without leaking references.
_AUDIT_EVENTS = []


def _audit_hook(event, args):
    if event == "truenas_pylibzfs.lzc.local_replicate":
        _AUDIT_EVENTS.append((event, args))


sys.addaudithook(_audit_hook)


class TestLocalReplicateAudit:
    def test_audit_event_fired(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest = f"{pool}/recv@snap1"
        before = len(_AUDIT_EVENTS)
        lzc.local_replicate(source=snap, dest=dest)
        events = _AUDIT_EVENTS[before:]
        assert len(events) == 1
        _, args = events[0]
        # (source, dest, fromsnap, effective_flags, force, raw, props)
        assert args[0] == snap
        assert args[1] == dest
        assert args[2] is None
        assert args[3] == 0
        assert args[4] is False
        assert args[5] is False
        assert args[6] is None
        _destroy_recv(lz, f"{pool}/recv")

    def test_audit_event_carries_kwargs(self, two_snapped_pool):
        lz, pool, snap1, snap2 = two_snapped_pool
        dest_fs = f"{pool}/recv"
        # Establish a baseline first (uses default kwargs)
        lzc.local_replicate(source=snap1, dest=f"{dest_fs}@snap1")
        before = len(_AUDIT_EVENTS)
        lzc.local_replicate(
            source=snap2, dest=f"{dest_fs}@snap2",
            fromsnap=snap1,
            send_flags=int(lzc.SendFlags.COMPRESS),
            force=True,
            props={"compression": "off"},
        )
        events = _AUDIT_EVENTS[before:]
        assert len(events) == 1
        _, args = events[0]
        assert args[2] == snap1                        # fromsnap
        assert args[3] == int(lzc.SendFlags.COMPRESS)  # effective_flags
        assert args[4] is True                         # force
        assert args[5] is False                        # raw
        assert args[6] == {"compression": "off"}       # props
        _destroy_recv(lz, dest_fs)

    def test_audit_event_effective_flags_include_raw(
        self, encrypted_snapped_pool
    ):
        """raw=True is folded into effective_flags before auditing so
        downstream audit consumers see the on-wire flag set, not the
        user-facing kwarg split."""
        lz, pool, snap, _ = encrypted_snapped_pool
        dest = f"{pool}/recv_enc@snap1"
        before = len(_AUDIT_EVENTS)
        lzc.local_replicate(source=snap, dest=dest, raw=True)
        events = _AUDIT_EVENTS[before:]
        assert len(events) == 1
        _, args = events[0]
        assert args[3] & int(lzc.SendFlags.RAW)
        assert args[5] is True                         # raw kwarg also set
        _destroy_recv(lz, f"{pool}/recv_enc")


# ===========================================================================
# 10. History
# ===========================================================================

class TestLocalReplicateHistory:
    def test_history_logged_on_success(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        dest = f"{dest_fs}@snap1"
        lzc.local_replicate(source=snap, dest=dest)
        cmds = [
            rec["history command"]
            for rec in lz.open_pool(name=pool).iter_history()
            if "history command" in rec
        ]
        # The most recent entry should mention the receive of dest.
        assert any(
            "zfs receive" in c and dest_fs in c for c in cmds
        ), f"no zfs receive entry mentioning {dest_fs} found in {cmds[-5:]}"
        _destroy_recv(lz, dest_fs)

    def test_no_history_logged_on_failure(self, pool_fixture):
        lz, pool = pool_fixture
        before = [
            rec.get("history command", "")
            for rec in lz.open_pool(name=pool).iter_history()
        ]
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(
                source=f"{pool}/nosuch@snap", dest=f"{pool}/recv@snap1",
            )
        after = [
            rec.get("history command", "")
            for rec in lz.open_pool(name=pool).iter_history()
        ]
        assert after == before

    def test_history_renders_send_and_recv_flags(self, two_snapped_pool):
        """Flags passed via send_flags / force / raw / props should appear
        in the synthesized 'zfs send | zfs receive' history entry as the
        equivalent CLI mnemonics so the log is useful for troubleshooting.
        """
        lz, pool, snap1, snap2 = two_snapped_pool
        dest_fs = f"{pool}/recv"
        # Baseline first so the incremental has a base to apply against.
        lzc.local_replicate(source=snap1, dest=f"{dest_fs}@snap1")
        lzc.local_replicate(
            source=snap2, dest=f"{dest_fs}@snap2",
            fromsnap=snap1,
            send_flags=(int(lzc.SendFlags.LARGE_BLOCK)
                        | int(lzc.SendFlags.COMPRESS)),
            force=True,
            props={"readonly": "on"},
        )
        cmds = [
            rec["history command"]
            for rec in lz.open_pool(name=pool).iter_history()
            if "history command" in rec
        ]
        # The most recent zfs-send-pipe-receive entry must carry every
        # flag we set.  Anchor on the dest path so we do not match the
        # earlier baseline call.
        matches = [c for c in cmds if "zfs receive" in c
                   and f"{dest_fs}@snap2" in c]
        assert matches, f"no incremental history line found in {cmds[-5:]}"
        line = matches[-1]
        assert " -L" in line, f"missing -L (LARGE_BLOCK) in: {line}"
        assert " -c" in line, f"missing -c (COMPRESS) in: {line}"
        assert " -F" in line, f"missing -F (force) in: {line}"
        assert " -i " in line, f"missing -i (incremental) in: {line}"
        assert "-o property overrides" in line, (
            f"missing props marker in: {line}"
        )
        _destroy_recv(lz, dest_fs)


# ===========================================================================
# 11. Argument validation
# ===========================================================================

class TestLocalReplicateArgValidation:
    def test_no_args(self):
        with pytest.raises(ValueError):
            lzc.local_replicate()

    def test_source_none(self):
        with pytest.raises(ValueError):
            lzc.local_replicate(source=None, dest="pool/r@s")

    def test_dest_none(self):
        with pytest.raises(ValueError):
            lzc.local_replicate(source="pool/s@s", dest=None)

    def test_source_missing(self):
        with pytest.raises(ValueError):
            lzc.local_replicate(dest="pool/r@s")

    def test_dest_missing(self):
        with pytest.raises(ValueError):
            lzc.local_replicate(source="pool/s@s")

    def test_positional_args_rejected(self):
        with pytest.raises(TypeError):
            lzc.local_replicate("pool/s@s", "pool/r@s")  # type: ignore

    def test_send_flags_wrong_type(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(TypeError):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                send_flags="bad",  # type: ignore
            )

    def test_props_wrong_type(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(TypeError):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                props="bad",  # type: ignore
            )

    def test_unknown_kwarg_rejected(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(TypeError):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                bogus=True,  # type: ignore
            )


# ===========================================================================
# 12. Cleanup (no fd / thread leaks)
# ===========================================================================

class TestLocalReplicateCleanup:
    def test_no_fd_leak_on_success(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        before = _open_fd_count()
        lzc.local_replicate(source=snap, dest=f"{dest_fs}@snap1")
        # Allow a brief grace period for GC of any temporary objects.
        time.sleep(0.05)
        after = _open_fd_count()
        _destroy_recv(lz, dest_fs)
        assert after == before, f"fd leak: {before} -> {after}"

    def test_no_fd_leak_on_recv_error(self, snapped_pool):
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        # Pre-populate dest so the second call errors with EEXIST.
        lzc.local_replicate(source=snap, dest=f"{dest_fs}@snap1")
        before = _open_fd_count()
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(source=snap, dest=f"{dest_fs}@snap1")
        time.sleep(0.05)
        after = _open_fd_count()
        _destroy_recv(lz, dest_fs)
        assert after == before, f"fd leak: {before} -> {after}"

    def test_no_fd_leak_on_send_error(self, pool_fixture):
        _, pool = pool_fixture
        before = _open_fd_count()
        with pytest.raises(lzc.ZFSCoreException):
            lzc.local_replicate(
                source=f"{pool}/nosuch@snap", dest=f"{pool}/recv@snap1",
            )
        time.sleep(0.05)
        after = _open_fd_count()
        assert after == before, f"fd leak: {before} -> {after}"

    def test_no_python_thread_leak(self, snapped_pool):
        """The C-level pthread spawned by local_replicate should not
        leave any Python-visible threads behind."""
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        before = threading.active_count()
        lzc.local_replicate(source=snap, dest=f"{dest_fs}@snap1")
        time.sleep(0.05)
        after = threading.active_count()
        _destroy_recv(lz, dest_fs)
        assert after == before


# ===========================================================================
# 13. Progress callback
# ===========================================================================


# A "big enough" payload that comfortably exceeds 1 second of pipe-driven
# transfer on the test runner, so the 1s polling interval (the lowest
# legal value) gets at least one tick.  Random bytes so compression does
# not shrink the on-wire stream below the threshold.
_PROGRESS_PAYLOAD = 192 * 1024 * 1024


@pytest.fixture
def progress_pool():
    """Larger file-backed pool so the progress tests have room for a
    payload that takes >1s to push through the pipe.  Independent of
    pool_fixture so the rest of the suite keeps the smaller default."""
    d = tempfile.mkdtemp(prefix="pylibzfs_lrepl_prog_")
    path = os.path.join(d, "d0.img")
    with open(path, "w") as f:
        os.ftruncate(f.fileno(), 1024 * 1024 * 1024)  # 1 GiB sparse
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(
        name=POOL,
        storage_vdevs=[
            truenas_pylibzfs.create_vdev_spec(
                vdev_type=truenas_pylibzfs.VDevType.FILE, name=path
            )
        ],
        force=True,
    )
    src = f"{POOL}/src_prog"
    lz.create_resource(
        name=src, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    lz.open_resource(name=src).mount()
    _write_data(f"/{src}", _PROGRESS_PAYLOAD, random=True)
    snap = f"{src}@snap1"
    _snap(snap)
    try:
        yield lz, POOL, snap
    finally:
        try:
            lz.destroy_pool(name=POOL, force=True)
        except Exception:
            pass
        shutil.rmtree(d, ignore_errors=True)


class TestLocalReplicateProgress:
    """progress_callback fires while the transfer is in flight, mirrors
    the (callback, state) shape used by the iter_* methods, and re-raises
    callback exceptions after the transfer settles."""

    def test_callback_invoked(self, progress_pool):
        lz, pool, snap = progress_pool
        dest_fs = f"{pool}/recv_prog"
        seen = []

        def cb(written, total, state):
            seen.append((written, total, state))

        t0 = time.monotonic()
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            progress_callback=cb,
        )
        elapsed = time.monotonic() - t0

        if elapsed < 1.0:
            pytest.skip(
                f"transfer too fast ({elapsed:.2f}s < 1s) for "
                "the integer-second polling interval"
            )

        assert seen, "progress callback was never invoked"
        for w, total, state in seen:
            assert total > 0
            assert 0 <= w <= total
            assert state is None
        # Monotonic non-decreasing.
        for prev, curr in zip(seen, seen[1:]):
            assert curr[0] >= prev[0], (
                f"written counter decreased: {prev[0]} -> {curr[0]}"
            )
        _destroy_recv(lz, dest_fs)

    def test_state_passed_through(self, progress_pool):
        lz, pool, snap = progress_pool
        dest_fs = f"{pool}/recv_prog2"
        bag = {"hits": 0}
        identities = []

        def cb(_w, _t, state):
            state["hits"] += 1
            identities.append(state)

        t0 = time.monotonic()
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            progress_callback=cb,
            progress_state=bag,
        )
        elapsed = time.monotonic() - t0

        if elapsed < 1.0:
            pytest.skip(f"transfer too fast ({elapsed:.2f}s) to test state")

        assert bag["hits"] >= 1
        assert all(s is bag for s in identities)
        _destroy_recv(lz, dest_fs)

    def test_state_default_is_none(self, progress_pool):
        lz, pool, snap = progress_pool
        dest_fs = f"{pool}/recv_prog3"
        states_seen = []

        def cb(_w, _t, state):
            states_seen.append(state)

        t0 = time.monotonic()
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            progress_callback=cb,
        )
        elapsed = time.monotonic() - t0

        if elapsed < 1.0:
            pytest.skip(f"transfer too fast ({elapsed:.2f}s)")

        assert states_seen
        assert all(s is None for s in states_seen)
        _destroy_recv(lz, dest_fs)

    def test_callback_none_unchanged(self, snapped_pool):
        """Default behaviour with progress_callback=None matches the
        no-progress baseline -- this is the cheap sanity check that the
        new code path stays opt-in."""
        lz, pool, snap = snapped_pool
        dest_fs = f"{pool}/recv"
        lzc.local_replicate(
            source=snap, dest=f"{dest_fs}@snap1",
            progress_callback=None,
        )
        assert lz.open_resource(name=f"{dest_fs}@snap1") is not None
        _destroy_recv(lz, dest_fs)

    def test_callback_exception_does_not_abort_transfer(self, progress_pool):
        """A buggy progress callback must not turn a successful
        replication into a failure.  The exception is routed through
        sys.unraisablehook (matching signal handlers, thread targets,
        and atexit hooks); the function returns None and the
        destination dataset is fully received."""
        lz, pool, snap = progress_pool
        dest_fs = f"{pool}/recv_prog4"

        unraised = []
        prev_hook = sys.unraisablehook
        sys.unraisablehook = lambda args: unraised.append(args)
        try:
            def cb(_w, _t, _s):
                raise RuntimeError("boom")

            t0 = time.monotonic()
            # No exception expected.
            lzc.local_replicate(
                source=snap, dest=f"{dest_fs}@snap1",
                progress_callback=cb,
            )
            elapsed = time.monotonic() - t0
        finally:
            sys.unraisablehook = prev_hook

        # The receive completed, so the destination dataset exists.
        assert lz.open_resource(name=f"{dest_fs}@snap1") is not None

        if elapsed >= 1.0:
            # The callback got at least one chance to fire; assert the
            # exception was handed to unraisablehook.
            assert unraised, "callback raised but unraisablehook was not invoked"
            assert any(isinstance(u.exc_value, RuntimeError) and
                       str(u.exc_value) == "boom" for u in unraised)

        _destroy_recv(lz, dest_fs)

    def test_callback_not_callable_raises(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(TypeError):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                progress_callback="not callable",  # type: ignore
            )

    def test_interval_zero_rejected(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(ValueError):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                progress_callback=lambda *_: None,
                progress_interval_seconds=0,
            )

    def test_interval_negative_rejected(self, snapped_pool):
        _, pool, snap = snapped_pool
        with pytest.raises(ValueError):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                progress_callback=lambda *_: None,
                progress_interval_seconds=-5,
            )

    def test_interval_float_rejected(self, snapped_pool):
        """The `i` format spec rejects floats with TypeError."""
        _, pool, snap = snapped_pool
        with pytest.raises(TypeError):
            lzc.local_replicate(
                source=snap, dest=f"{pool}/recv@snap1",
                progress_callback=lambda *_: None,
                progress_interval_seconds=1.5,  # type: ignore
            )


# ===========================================================================
# 14. Concurrent invocations
# ===========================================================================

class TestLocalReplicateConcurrent:
    """N parallel local_replicate calls on disjoint dataset pairs must
    all succeed.  The docstring promises the GIL is released for the
    duration of both ioctls; this test guards against a future
    regression that introduces a global lock around the call."""

    def test_concurrent_replicates_succeed(self, pool_fixture):
        lz, pool = pool_fixture
        N = 4
        # Per-pair payload kept small so 4 source datasets + 4 receives
        # fit comfortably in the 256 MiB pool fixture.  Random bytes so
        # ZFS compression cannot shrink the on-wire stream below the
        # point where parallelism is observable.
        payload = 8 * 1024 * 1024

        sources = []
        for i in range(N):
            src_fs = f"{pool}/csrc{i}"
            lz.create_resource(
                name=src_fs,
                type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
            )
            lz.open_resource(name=src_fs).mount()
            _write_data(f"/{src_fs}", payload, random=True)
            snap = f"{src_fs}@snap1"
            _snap(snap)
            sources.append((src_fs, snap))

        try:
            # ---- Serial baseline ----
            serial_dests = [f"{pool}/cdst_serial{i}" for i in range(N)]
            t0 = time.monotonic()
            for (_, snap), dest_fs in zip(sources, serial_dests):
                lzc.local_replicate(source=snap, dest=f"{dest_fs}@snap1")
            t_serial = time.monotonic() - t0
            for d in serial_dests:
                _destroy_recv(lz, d)

            # ---- Concurrent run ----
            par_dests = [f"{pool}/cdst_par{i}" for i in range(N)]
            errors = [None] * N

            def _worker(idx, snap, dest_fs):
                try:
                    lzc.local_replicate(
                        source=snap, dest=f"{dest_fs}@snap1",
                    )
                except Exception as e:
                    errors[idx] = e

            threads = [
                threading.Thread(
                    target=_worker,
                    args=(i, sources[i][1], par_dests[i]),
                )
                for i in range(N)
            ]
            t0 = time.monotonic()
            for t in threads:
                t.start()
            for t in threads:
                t.join(timeout=120)
            t_par = time.monotonic() - t0

            # Hard requirement: every concurrent call succeeded and
            # left a usable destination snapshot.
            assert all(e is None for e in errors), (
                f"concurrent calls produced errors: {errors}"
            )
            for d in par_dests:
                assert lz.open_resource(name=f"{d}@snap1") is not None

            # Soft timing assertion, gated on the serial baseline being
            # long enough to rise above scheduling noise.  On a fast
            # runner with too-small payload both runs are sub-second
            # and the comparison is meaningless.
            if t_serial > 1.0:
                assert t_par < t_serial * 0.7, (
                    f"concurrent run was not faster than serial "
                    f"(serial={t_serial:.2f}s, parallel={t_par:.2f}s) - "
                    "check for an unexpected internal lock"
                )

            for d in par_dests:
                _destroy_recv(lz, d)
        finally:
            for src_fs, _ in sources:
                _destroy_recv(lz, src_fs)
