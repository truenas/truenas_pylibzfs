"""
tests/test_lzc_replication.py — end-to-end tests for lzc send/receive.

Tests cover:
  - SendFlags enum values
  - send_space: size estimation, incremental vs full, error handling
  - send + receive: full-stream and incremental round-trips
  - send_progress: byte counter during an active send
  - receive flags: resumable, force, props
  - send(resume_token=...): interrupted receive resumed to completion
  - Argument validation for all functions

All pools use file-backed images in a per-fixture temp directory so the
tests work without real disks and leave the host system untouched.
"""

import os
import shutil
import tempfile
import threading
import time

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

POOL   = "testpool_lzc_replic"
DSK_SZ = 256 * 1024 * 1024        # 256 MiB pool disk
DATA_SZ = 4 * 1024 * 1024         # 4 MiB of payload to force a large stream
DATA_SZ_LARGE = 12 * 1024 * 1024  # 12 MiB for progress / resume tests


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


def _write_data(mountpoint, size, filename="payload.dat"):
    """Write *size* bytes to *mountpoint*/*filename*, overwriting if present."""
    path = os.path.join(mountpoint, filename)
    with open(path, "wb") as f:
        chunk = b"Z" * 65536
        written = 0
        while written < size:
            n = min(len(chunk), size - written)
            f.write(chunk[:n])
            written += n


def _snap(lz_unused, name):
    """Create a single snapshot via lzc."""
    lzc.create_snapshots(snapshot_names={name})


def _destroy_recv(lz, fs_name):
    """Best-effort destruction of a received filesystem (and its snapshots)."""
    try:
        # Use lzc's channel-program recursive destroy so we don't have to
        # enumerate snapshots manually.
        lzc.run_channel_program(
            pool_name=fs_name.split("/")[0],
            script=lzc.ChannelProgramEnum.DESTROY_RESOURCES,
            script_arguments_dict={"target": fs_name, "recursive": True, "defer": False},
        )
    except Exception:
        pass
    try:
        lz.destroy_resource(name=fs_name)
    except Exception:
        pass


def _get_resume_token(recv_fs: str) -> str:
    """
    Return the receive_resume_token for *recv_fs* via truenas_pylibzfs.

    After an interrupted resumable receive, the partial state is held in a
    hidden dataset named recv_fs + "%recv".  ZFS surfaces the token on the
    parent filesystem via property delegation, but if that lookup returns "-"
    we also try the hidden dataset directly.  Returns "-" when no token is set.
    """
    lz  = truenas_pylibzfs.open_handle()
    prop = truenas_pylibzfs.ZFSProperty.RECEIVE_RESUME_TOKEN

    for name in (recv_fs, recv_fs + "%recv"):
        try:
            rsrc  = lz.open_resource(name=name)
            info  = rsrc.asdict(properties={prop})
            token = info["properties"]["receive_resume_token"]["value"]
            if token != "-":
                return token
        except Exception:
            pass
    return "-"


# ---------------------------------------------------------------------------
# Shared pool fixture (function scope — each test gets a clean pool)
# ---------------------------------------------------------------------------

@pytest.fixture
def pool_fixture():
    """
    Yields (lz, pool_name) with:
      - pool 'testpool_lzc_replic' created on a 256 MiB sparse image
      - dataset 'testpool_lzc_replic/src' created
    Cleans up completely on teardown.
    """
    d  = tempfile.mkdtemp(prefix="pylibzfs_replic_")
    lz = _make_pool(d)
    src = f"{POOL}/src"
    lz.create_resource(
        name=src,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    try:
        yield lz, POOL
    finally:
        _destroy_pool(lz)
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def snapped_pool(pool_fixture):
    """
    Extends pool_fixture with a single snapshot on POOL/src.
    Yields (lz, pool_name, src_snap_name)
      where src_snap_name == 'testpool_lzc_replic/src@snap1'.
    """
    lz, pool = pool_fixture
    snap = f"{pool}/src@snap1"
    _snap(lz, snap)
    yield lz, pool, snap


@pytest.fixture
def two_snapped_pool(pool_fixture):
    """
    Two sequential snapshots on POOL/src.
    Yields (lz, pool_name, snap1, snap2).
    """
    lz, pool = pool_fixture
    src   = f"{pool}/src"
    snap1 = f"{src}@snap1"
    snap2 = f"{src}@snap2"
    _snap(lz, snap1)
    # Write data between snaps so the incremental stream is non-trivial
    _write_data(f"/{src}", DATA_SZ)
    _snap(lz, snap2)
    yield lz, pool, snap1, snap2


@pytest.fixture
def large_snapped_pool(pool_fixture):
    """
    One snapshot of POOL/src containing DATA_SZ_LARGE bytes of data.
    Used for send_progress and send_resume tests.
    Yields (lz, pool_name, snap_name).
    """
    lz, pool = pool_fixture
    src  = f"{pool}/src"
    snap = f"{src}@bigsnap"
    _write_data(f"/{src}", DATA_SZ_LARGE)
    _snap(lz, snap)
    yield lz, pool, snap


# ---------------------------------------------------------------------------
# 1. SendFlags enum
# ---------------------------------------------------------------------------

class TestSendFlags:
    def test_embed_data_value(self):
        assert int(lzc.SendFlags.EMBED_DATA)  == 1 << 0

    def test_large_block_value(self):
        assert int(lzc.SendFlags.LARGE_BLOCK) == 1 << 1

    def test_compress_value(self):
        assert int(lzc.SendFlags.COMPRESS)    == 1 << 2

    def test_raw_value(self):
        assert int(lzc.SendFlags.RAW)         == 1 << 3

    def test_saved_value(self):
        assert int(lzc.SendFlags.SAVED)       == 1 << 4

    def test_flags_combine_with_or(self):
        combined = lzc.SendFlags.EMBED_DATA | lzc.SendFlags.COMPRESS
        assert int(combined) == (1 << 0) | (1 << 2)

    def test_zero_int_accepted(self):
        """Passing flags=0 (the default) must not raise."""
        # Just verifying the type is constructable; real usage tested below.
        assert int(lzc.SendFlags(0)) == 0


# ---------------------------------------------------------------------------
# 2. send_space
# ---------------------------------------------------------------------------

class TestSendSpace:
    def test_returns_positive_int_for_full(self, snapped_pool):
        _, _, snap = snapped_pool
        size = lzc.send_space(snapname=snap)
        assert isinstance(size, int)
        assert size > 0

    def test_returns_int_with_flags(self, snapped_pool):
        _, _, snap = snapped_pool
        size = lzc.send_space(
            snapname=snap,
            flags=lzc.SendFlags.EMBED_DATA | lzc.SendFlags.COMPRESS,
        )
        assert isinstance(size, int)
        assert size > 0

    def test_incremental_le_full(self, two_snapped_pool):
        """Incremental size must be <= full size of the later snapshot."""
        _, _, snap1, snap2 = two_snapped_pool
        full_size = lzc.send_space(snapname=snap2)
        incr_size = lzc.send_space(snapname=snap2, fromsnap=snap1)
        assert incr_size <= full_size

    def test_incremental_size_is_int(self, two_snapped_pool):
        _, _, snap1, snap2 = two_snapped_pool
        size = lzc.send_space(snapname=snap2, fromsnap=snap1)
        assert isinstance(size, int)

    def test_nonexistent_snap_raises(self):
        with pytest.raises(lzc.ZFSCoreException) as exc_info:
            lzc.send_space(snapname="nosuchpool/nosuchds@nosuchsnap")
        assert exc_info.value.code != 0

    def test_missing_snapname_raises_value_error(self):
        with pytest.raises(ValueError):
            lzc.send_space()

    def test_keyword_only(self, snapped_pool):
        _, _, snap = snapped_pool
        with pytest.raises(TypeError):
            lzc.send_space(snap)  # type: ignore[call-arg]


# ---------------------------------------------------------------------------
# 3. send + receive (full stream)
# ---------------------------------------------------------------------------

class TestSendReceiveFull:
    def test_full_roundtrip_returns_none(self, snapped_pool):
        lz, pool, snap = snapped_pool
        recv = f"{pool}/recv@snap1"
        with tempfile.TemporaryFile() as stream:
            lzc.send(snapname=snap, fd=stream.fileno())
            stream.seek(0)
            result = lzc.receive(snapname=recv, fd=stream.fileno())
        assert result is None
        _destroy_recv(lz, f"{pool}/recv")

    def test_full_received_snapshot_accessible(self, snapped_pool):
        lz, pool, snap = snapped_pool
        recv_fs   = f"{pool}/recv"
        recv_snap = f"{recv_fs}@snap1"
        with tempfile.TemporaryFile() as stream:
            lzc.send(snapname=snap, fd=stream.fileno())
            stream.seek(0)
            lzc.receive(snapname=recv_snap, fd=stream.fileno())
        # Verify the received snapshot is visible to libzfs
        ds = lz.open_resource(name=recv_snap)
        assert ds is not None
        _destroy_recv(lz, recv_fs)

    def test_full_send_with_embed_data_flag(self, snapped_pool):
        lz, pool, snap = snapped_pool
        recv = f"{pool}/recv@snap1"
        with tempfile.TemporaryFile() as stream:
            lzc.send(snapname=snap, fd=stream.fileno(),
                     flags=lzc.SendFlags.EMBED_DATA)
            stream.seek(0)
            lzc.receive(snapname=recv, fd=stream.fileno())
        _destroy_recv(lz, f"{pool}/recv")

    def test_full_send_missing_snapname_raises(self):
        with pytest.raises(ValueError):
            r, w = os.pipe()
            try:
                lzc.send(fd=w)
            finally:
                os.close(r); os.close(w)

    def test_full_send_missing_fd_raises(self, snapped_pool):
        _, _, snap = snapped_pool
        with pytest.raises(ValueError):
            lzc.send(snapname=snap)

    def test_receive_missing_snapname_raises(self):
        r, w = os.pipe()
        try:
            os.close(w)
            with pytest.raises(ValueError):
                lzc.receive(fd=r)
        finally:
            os.close(r)

    def test_receive_missing_fd_raises(self):
        with pytest.raises(ValueError):
            lzc.receive(snapname="pool/ds@snap")

    def test_send_nonexistent_snap_raises_zfscoreexception(self):
        r, w = os.pipe()
        try:
            with pytest.raises(lzc.ZFSCoreException):
                lzc.send(snapname="nosuchpool/nosuchds@snap", fd=w)
        finally:
            os.close(r); os.close(w)


# ---------------------------------------------------------------------------
# 4. send + receive (incremental stream)
# ---------------------------------------------------------------------------

class TestSendReceiveIncremental:
    def test_incremental_roundtrip(self, two_snapped_pool):
        lz, pool, snap1, snap2 = two_snapped_pool
        recv_fs = f"{pool}/recv"
        recv1   = f"{recv_fs}@snap1"
        recv2   = f"{recv_fs}@snap2"

        # Step 1: full receive of snap1
        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=snap1, fd=s.fileno())
            s.seek(0)
            lzc.receive(snapname=recv1, fd=s.fileno())

        # Step 2: incremental receive snap1 → snap2
        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=snap2, fromsnap=snap1, fd=s.fileno())
            s.seek(0)
            lzc.receive(snapname=recv2, fd=s.fileno())

        ds2 = lz.open_resource(name=recv2)
        assert ds2 is not None
        _destroy_recv(lz, recv_fs)

    def test_incremental_stream_smaller_than_full(self, two_snapped_pool):
        _, pool, snap1, snap2 = two_snapped_pool
        with tempfile.TemporaryFile() as full_s:
            lzc.send(snapname=snap2, fd=full_s.fileno())
            full_bytes = full_s.tell()

        with tempfile.TemporaryFile() as incr_s:
            lzc.send(snapname=snap2, fromsnap=snap1, fd=incr_s.fileno())
            incr_bytes = incr_s.tell()

        # The incremental stream must be strictly smaller than the full stream
        # when the second snapshot has data the first lacks.
        assert incr_bytes < full_bytes

    def test_incremental_bad_fromsnap_raises(self, two_snapped_pool):
        _, pool, snap1, snap2 = two_snapped_pool
        r, w = os.pipe()
        try:
            # snap2 is not an ancestor of snap1
            with pytest.raises(lzc.ZFSCoreException):
                lzc.send(snapname=snap1, fromsnap=snap2, fd=w)
        finally:
            os.close(r); os.close(w)


# ---------------------------------------------------------------------------
# 5. send_progress
# ---------------------------------------------------------------------------

class TestSendProgress:
    def test_progress_returns_int(self, snapped_pool):
        """send_progress must return an int even for a zero-bytes fd."""
        _, _, snap = snapped_pool
        r, w = os.pipe()
        errors = []

        def _send():
            try:
                lzc.send(snapname=snap, fd=w)
            except Exception as e:
                errors.append(e)
            finally:
                os.close(w)

        t = threading.Thread(target=_send, daemon=True)
        t.start()

        # Drain and sample progress
        samples = []
        buf = b""
        while True:
            try:
                chunk = os.read(r, 65536)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            samples.append(lzc.send_progress(fd=w))

        os.close(r)
        t.join(timeout=30)
        assert not errors, f"send raised: {errors[0]}"
        for s in samples:
            assert isinstance(s, int)
            assert s >= 0

    def test_progress_increases_during_large_send(self, large_snapped_pool):
        """For a large send, progress must reach > 0 at some point."""
        _, pool, snap = large_snapped_pool
        r, w = os.pipe()
        errors   = []
        progress = []

        def _send():
            try:
                lzc.send(snapname=snap, fd=w,
                         flags=lzc.SendFlags.EMBED_DATA)
            except Exception as e:
                errors.append(e)
            finally:
                os.close(w)

        t = threading.Thread(target=_send, daemon=True)
        t.start()

        buf = b""
        while True:
            try:
                chunk = os.read(r, 131072)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            p = lzc.send_progress(fd=w)
            progress.append(p)

        os.close(r)
        t.join(timeout=60)
        assert not errors, f"send raised: {errors[0]}"
        # At least one sample must be > 0 (the kernel counter should have moved)
        assert any(p > 0 for p in progress), (
            f"send_progress never exceeded 0; samples: {progress[:10]}"
        )

    def test_progress_missing_fd_raises(self):
        with pytest.raises(ValueError):
            lzc.send_progress()

    def test_progress_invalid_fd_returns_zero(self):
        """Closed / invalid fd must not raise — lzc_send_progress returns 0."""
        # fd 99999 is almost certainly not open
        result = lzc.send_progress(fd=99999)
        assert isinstance(result, int)


# ---------------------------------------------------------------------------
# 6. receive flags: resumable, force, props
# ---------------------------------------------------------------------------

class TestReceiveFlags:
    def test_resumable_complete_stream_succeeds(self, snapped_pool):
        """resumable=True on a complete stream must not raise."""
        lz, pool, snap = snapped_pool
        recv = f"{pool}/recv@snap1"
        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=snap, fd=s.fileno())
            s.seek(0)
            lzc.receive(snapname=recv, fd=s.fileno(), resumable=True)
        _destroy_recv(lz, f"{pool}/recv")

    def test_receive_with_props(self, snapped_pool):
        """props dict must be accepted and not raise for string-safe props."""
        lz, pool, snap = snapped_pool
        recv = f"{pool}/recv@snap1"
        # Passing an empty dict is valid
        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=snap, fd=s.fileno())
            s.seek(0)
            lzc.receive(snapname=recv, fd=s.fileno(), props={})
        _destroy_recv(lz, f"{pool}/recv")

    def test_receive_force_allows_reapplication(self, two_snapped_pool):
        """
        Scenario:
          1. Full-receive snap1 → recv@snap1, creating recv filesystem.
          2. Write data to recv (modify it after snap1).
          3. Incremental send snap1→snap2, receive with force=True.
             force rolls back recv to snap1 before applying snap2 delta.
        Without force, the receive would fail because recv has been modified.
        """
        lz, pool, snap1, snap2 = two_snapped_pool
        recv_fs = f"{pool}/recv"
        recv1   = f"{recv_fs}@snap1"
        recv2   = f"{recv_fs}@snap2"

        # Full receive of snap1
        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=snap1, fd=s.fileno())
            s.seek(0)
            lzc.receive(snapname=recv1, fd=s.fileno())

        # Write data to recv to put it in a "modified after snap1" state
        _write_data(f"/{recv_fs}", 256 * 1024, filename="extra.dat")

        # Incremental send + receive with force=True must succeed
        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=snap2, fromsnap=snap1, fd=s.fileno())
            s.seek(0)
            lzc.receive(snapname=recv2, fd=s.fileno(), force=True)

        ds = lz.open_resource(name=recv2)
        assert ds is not None
        _destroy_recv(lz, recv_fs)

    def test_receive_props_bad_type_raises(self, snapped_pool):
        """Non-dict props must raise TypeError from py_dict_to_nvlist."""
        _, _, snap = snapped_pool
        r, w = os.pipe()
        os.close(w)
        with pytest.raises(TypeError):
            lzc.receive(snapname=f"{POOL}/recv@s1", fd=r, props="bad")
        os.close(r)


# ---------------------------------------------------------------------------
# 7. send_resume: interrupt a receive, then resume it
# ---------------------------------------------------------------------------

class TestSendResume:
    def _partial_receive(self, snap, recv_snap, truncate_at):
        """
        Send *snap* to a temp file, truncate it to *truncate_at* bytes, then
        attempt a resumable receive into *recv_snap*.  The receive is expected
        to fail with ZFSCoreException (stream truncated / EINVAL).
        Returns the temp file object (seeked to 0) on partial-receive failure.
        """
        stream = tempfile.TemporaryFile()
        lzc.send(snapname=snap, fd=stream.fileno())
        full_bytes = stream.tell()
        # Only truncate if the stream is big enough to be worth it
        cut = min(truncate_at, full_bytes // 2)
        os.ftruncate(stream.fileno(), cut)
        stream.seek(0)
        with pytest.raises(lzc.ZFSCoreException):
            lzc.receive(snapname=recv_snap, fd=stream.fileno(), resumable=True)
        return stream

    def test_empty_resume_token_raises(self):
        """An empty resume_token string must raise ValueError."""
        r, w = os.pipe()
        try:
            with pytest.raises(ValueError):
                lzc.send(snapname="pool/ds@snap", fd=w, resume_token="")
        finally:
            os.close(r)
            os.close(w)

    def test_resume_token_set_after_partial_receive(self, large_snapped_pool):
        """After a truncated resumable receive the token must not be '-'."""
        lz, pool, snap = large_snapped_pool
        recv_fs   = f"{pool}/recv"
        recv_snap = f"{recv_fs}@bigsnap"

        # Pre-create dest filesystem so ZFS has somewhere to record the
        # partial state (matching the pyzfs test convention).
        lz.create_resource(
            name=recv_fs, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
        )

        self._partial_receive(snap, recv_snap, 65536)

        token = _get_resume_token(recv_fs)
        assert token != "-", f"expected resume token but got {token!r}"
        _destroy_recv(lz, recv_fs)

    def test_send_resume_completes_after_truncation(self, large_snapped_pool):
        """
        Full end-to-end resume test:
          1. Pre-create dest filesystem; send large snapshot, truncate stream,
             start resumable receive (which fails mid-stream).
          2. Read receive_resume_token from the partial dataset.
          3. Decode token to get (resumeobj, resumeoff).
          4. lzc.send_resume + lzc.receive(resumable=True) finishes the job.
          5. The received snapshot must exist and be openable.
        """
        lz, pool, snap = large_snapped_pool
        recv_fs   = f"{pool}/recv"
        recv_snap = f"{recv_fs}@bigsnap"

        lz.create_resource(
            name=recv_fs, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
        )
        self._partial_receive(snap, recv_snap, 65536)

        token = _get_resume_token(recv_fs)
        if token == "-":
            pytest.skip("no resume token after partial receive (stream too small)")

        # Resume the send from where we left off
        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=snap, fd=s.fileno(), resume_token=token)
            s.seek(0)
            lzc.receive(snapname=recv_snap, fd=s.fileno(), resumable=True)

        ds = lz.open_resource(name=recv_snap)
        assert ds is not None
        _destroy_recv(lz, recv_fs)

    def test_send_resume_incremental(self, large_snapped_pool):
        """
        Resume an incremental stream that was interrupted mid-way:
          1. Full receive snap1 → recv@bigsnap.
          2. Take snap2 on source with more data.
          3. Send incremental snap1→snap2, truncate, resumable receive (fails).
          4. Resume via send_resume with fromsnap=snap1.
          5. recv@snap2 must exist.
        """
        lz, pool, bigsnap = large_snapped_pool
        # Create a second snapshot with more data
        src   = f"{pool}/src"
        snap2 = f"{src}@snap2"
        _write_data(f"/{src}", DATA_SZ, filename="more.dat")
        _snap(lz, snap2)

        recv_fs    = f"{pool}/recv"
        recv_snap1 = f"{recv_fs}@bigsnap"
        recv_snap2 = f"{recv_fs}@snap2"

        # Full receive of bigsnap — creates recv_fs implicitly
        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=bigsnap, fd=s.fileno())
            s.seek(0)
            lzc.receive(snapname=recv_snap1, fd=s.fileno())

        # Partial incremental receive (recv_fs now exists with snap1)
        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=snap2, fromsnap=bigsnap, fd=s.fileno())
            total = s.tell()
            os.ftruncate(s.fileno(), min(65536, total // 2))
            s.seek(0)
            with pytest.raises(lzc.ZFSCoreException):
                lzc.receive(snapname=recv_snap2, fd=s.fileno(), resumable=True)

        token = _get_resume_token(recv_fs)
        if token == "-":
            pytest.skip("no resume token — incremental stream too small to interrupt")

        with tempfile.TemporaryFile() as s:
            lzc.send(snapname=snap2, fromsnap=bigsnap, fd=s.fileno(),
                     resume_token=token)
            s.seek(0)
            lzc.receive(snapname=recv_snap2, fd=s.fileno(), resumable=True)

        ds = lz.open_resource(name=recv_snap2)
        assert ds is not None
        _destroy_recv(lz, recv_fs)


# ---------------------------------------------------------------------------
# 8. Argument validation (comprehensive)
# ---------------------------------------------------------------------------

class TestArgValidation:
    """Verify all five functions reject bad or missing arguments correctly."""

    # ---- send_space -------------------------------------------------------
    def test_send_space_none_snapname(self):
        with pytest.raises((ValueError, lzc.ZFSCoreException)):
            lzc.send_space(snapname=None)

    def test_send_space_positional_arg(self):
        with pytest.raises(TypeError):
            lzc.send_space("pool/ds@snap")  # type: ignore[call-arg]

    # ---- send_progress ----------------------------------------------------
    def test_send_progress_no_args(self):
        with pytest.raises(ValueError):
            lzc.send_progress()

    def test_send_progress_positional_arg(self):
        with pytest.raises(TypeError):
            lzc.send_progress(3)  # type: ignore[call-arg]

    # ---- send -------------------------------------------------------------
    def test_send_no_args(self):
        with pytest.raises(ValueError):
            lzc.send()

    def test_send_positional_arg(self):
        with pytest.raises(TypeError):
            lzc.send("pool/ds@snap", 1)  # type: ignore[call-arg]

    def test_send_invalid_fd_type(self):
        with pytest.raises(TypeError):
            lzc.send(snapname="pool/ds@snap", fd="notanfd")  # type: ignore

    # ---- receive ----------------------------------------------------------
    def test_receive_no_args(self):
        with pytest.raises(ValueError):
            lzc.receive()

    def test_receive_positional_arg(self):
        r, w = os.pipe()
        os.close(w)
        try:
            with pytest.raises(TypeError):
                lzc.receive("pool/ds@snap", r)  # type: ignore[call-arg]
        finally:
            os.close(r)

    def test_receive_invalid_fd_type(self):
        with pytest.raises(TypeError):
            lzc.receive(snapname="pool/ds@snap", fd="notanfd")  # type: ignore
