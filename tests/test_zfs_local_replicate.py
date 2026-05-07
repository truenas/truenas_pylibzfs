"""
tests/test_zfs_local_replicate.py - tests for the libzfs-side
local_replicate method on ZFSDataset / ZFSVolume.

Mirrors `zfs send -Rp [-w] | zfs recv [-F]` for filesystems and
`zfs send -p [-w] | zfs recv [-F]` for volumes.  Source dataset
properties are always embedded in the stream; pass `props={...}` to
override specific values on the destination.

Coverage:
  - Smoke / round-trip
  - Source LOCAL props land on the destination (RECEIVED there)
  - props= cmdprops override the embedded values
  - Recursive descendant replication
  - Leaf-dataset works without -R-on-a-leaf errors
  - raw=True with encrypted source
  - force=True on a dirty destination
  - Incremental (fromsnap)
  - Pre-flight failures: missing tosnap / fromsnap
  - Argument validation (tosnap/dest required, RAW/SAVED rejected)
  - Audit event fires
"""

import os
import shutil
import sys
import tempfile

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

POOL = "testpool_zfs_local_repl"
DSK_SZ = 256 * 1024 * 1024
DATA_SZ = 4 * 1024 * 1024
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


def _write_data(mountpoint, size, filename="payload.dat"):
    path = os.path.join(mountpoint, filename)
    chunk = b"Z" * 65536
    with open(path, "wb") as f:
        written = 0
        while written < size:
            n = min(len(chunk), size - written)
            f.write(chunk[:n])
            written += n


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


def _get_local_prop(lz, ds_name, prop):
    """Return the raw string of a LOCAL or RECEIVED property, else None."""
    rsrc = lz.open_resource(name=ds_name)
    props = rsrc.get_properties(properties={prop}, get_source=True)
    val = getattr(props, prop.name.lower(), None)
    if val is None or val.source is None:
        return None
    if val.source.type.name not in ("LOCAL", "RECEIVED"):
        return None
    return val.raw


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def pool_fixture():
    d = tempfile.mkdtemp(prefix="pylibzfs_zlrepl_")
    lz = _make_pool(d)
    try:
        yield lz, POOL
    finally:
        _destroy_pool(lz)
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def snapped_dataset(pool_fixture):
    """Pool + POOL/src dataset with one snapshot 'snap1' and DATA_SZ bytes."""
    lz, pool = pool_fixture
    src = f"{pool}/src"
    lz.create_resource(
        name=src, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    src_rsrc = lz.open_resource(name=src)
    src_rsrc.mount()
    _write_data(f"/{src}", DATA_SZ)
    _snap(f"{src}@snap1")
    yield lz, pool, src_rsrc


@pytest.fixture
def propful_dataset(pool_fixture):
    """POOL/src with deliberate LOCAL property values, plus snap 'snap1'."""
    lz, pool = pool_fixture
    src = f"{pool}/src"
    lz.create_resource(
        name=src,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        properties={
            truenas_pylibzfs.ZFSProperty.COMPRESSION: "lz4",
            truenas_pylibzfs.ZFSProperty.RECORDSIZE: "65536",
            truenas_pylibzfs.ZFSProperty.ATIME: "off",
        },
    )
    src_rsrc = lz.open_resource(name=src)
    src_rsrc.mount()
    _write_data(f"/{src}", DATA_SZ)
    _snap(f"{src}@snap1")
    yield lz, pool, src_rsrc


@pytest.fixture
def nested_dataset(pool_fixture):
    """POOL/src + POOL/src/child, both with data, recursive snap 'snap1'."""
    lz, pool = pool_fixture
    src = f"{pool}/src"
    child = f"{pool}/src/child"
    lz.create_resource(
        name=src, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    lz.open_resource(name=src).mount()
    lz.create_resource(
        name=child, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    lz.open_resource(name=child).mount()
    _write_data(f"/{src}", DATA_SZ)
    _write_data(f"/{child}", DATA_SZ // 2)
    # Recursive snapshot on src and child sharing the same snap name.
    lzc.create_snapshots(snapshot_names={
        f"{src}@snap1", f"{child}@snap1",
    })
    yield lz, pool, lz.open_resource(name=src)


@pytest.fixture
def volume_dataset(pool_fixture):
    """POOL/vol zvol (16 MiB) with one snapshot 'snap1'.  Matches the
    shape of snapped_dataset but for volumes."""
    lz, pool = pool_fixture
    vol = f"{pool}/vol"
    lz.create_resource(
        name=vol,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_VOLUME,
        properties={
            truenas_pylibzfs.ZFSProperty.VOLSIZE: str(16 * 1024 * 1024),
        },
    )
    vol_rsrc = lz.open_resource(name=vol)
    _snap(f"{vol}@snap1")
    yield lz, pool, vol_rsrc


@pytest.fixture
def encrypted_dataset(pool_fixture):
    """Encrypted POOL/enc with one snap, key loaded."""
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
    _snap(f"{enc_name}@snap1")
    yield lz, pool, enc_rsrc


# ===========================================================================
# 1. Smoke / round-trip
# ===========================================================================

class TestSmoke:
    def test_full_roundtrip_returns_none(self, snapped_dataset):
        """
        Before:                  After:
          POOL/src                 POOL/src
          POOL/src@snap1           POOL/src@snap1
                                   POOL/recv
                                   POOL/recv@snap1
        """
        lz, pool, src = snapped_dataset
        result = src.local_replicate(tosnap="snap1", dest=f"{pool}/recv")
        assert result is None
        _destroy_recv(lz, f"{pool}/recv")

    def test_destination_visible_after_replicate(self, snapped_dataset):
        """
        Before:                  After:
          POOL/src                 POOL/src
          POOL/src@snap1           POOL/src@snap1
                                   POOL/recv         (openable)
                                   POOL/recv@snap1   (openable)
        """
        lz, pool, src = snapped_dataset
        src.local_replicate(tosnap="snap1", dest=f"{pool}/recv")
        ds = lz.open_resource(name=f"{pool}/recv")
        assert ds is not None
        _destroy_recv(lz, f"{pool}/recv")

    def test_keyword_only(self, snapped_dataset):
        _, pool, src = snapped_dataset
        with pytest.raises(TypeError):
            src.local_replicate("snap1", f"{pool}/recv")  # type: ignore


# ===========================================================================
# 2. Property propagation
# ===========================================================================

class TestProps:
    def test_local_props_carried_to_destination(self, propful_dataset):
        """Source LOCAL props land on the destination as RECEIVED.
        -R always embeds source properties in the stream.

        Before:                          After:
          POOL/src                         POOL/src
            compression=lz4   (LOCAL)        (unchanged)
            atime=off         (LOCAL)
            recordsize=64K    (LOCAL)
          POOL/src@snap1                   POOL/src@snap1
                                           POOL/recv
                                             compression=lz4   (RECEIVED)
                                             atime=off         (RECEIVED)
                                             recordsize=64K    (RECEIVED)
                                           POOL/recv@snap1
        """
        lz, pool, src = propful_dataset
        dest = f"{pool}/recv"
        src.local_replicate(tosnap="snap1", dest=dest)
        try:
            assert _get_local_prop(
                lz, dest, truenas_pylibzfs.ZFSProperty.COMPRESSION
            ) == "lz4"
            assert _get_local_prop(
                lz, dest, truenas_pylibzfs.ZFSProperty.ATIME
            ) == "off"
            # recordsize raw form may render as "64K" or "65536"
            # depending on libzfs's human-readable mode; just assert
            # SOMETHING was preserved and not the default (128K).
            rs = _get_local_prop(
                lz, dest, truenas_pylibzfs.ZFSProperty.RECORDSIZE
            )
            assert rs is not None and rs not in ("128K", "131072")
        finally:
            _destroy_recv(lz, dest)

    def test_props_override_embedded(self, propful_dataset):
        """User-supplied props (cmdprops) override the embedded values:
        compression=zstd from props beats the source's lz4; atime=off
        survives because it was not in the override set.

        Before:                          After:
          POOL/src                         POOL/src             (unchanged)
            compression=lz4   (LOCAL)
            atime=off         (LOCAL)
          POOL/src@snap1                   POOL/src@snap1
                                           POOL/recv
                                             compression=zstd  (LOCAL)
                                             atime=off         (RECEIVED)
                                           POOL/recv@snap1
        """
        lz, pool, src = propful_dataset
        dest = f"{pool}/recv"
        src.local_replicate(
            tosnap="snap1", dest=dest,
            props={"compression": "zstd"},
        )
        try:
            assert _get_local_prop(
                lz, dest, truenas_pylibzfs.ZFSProperty.COMPRESSION
            ) == "zstd"
            assert _get_local_prop(
                lz, dest, truenas_pylibzfs.ZFSProperty.ATIME
            ) == "off"
        finally:
            _destroy_recv(lz, dest)

    def test_user_property_override_applied(self, snapped_dataset):
        """User properties (`module:property` namespace) ride through
        the zfs-mode cmdprops path.

        Before:                              After:
          POOL/src                             POOL/src              (unchanged)
          POOL/src@snap1                       POOL/src@snap1
                                               POOL/recv_user_prop
                                                 org.myapp:tag=v2   (LOCAL)
                                               POOL/recv_user_prop@snap1
        """
        lz, pool, src = snapped_dataset
        dest = f"{pool}/recv_user_prop"
        src.local_replicate(
            tosnap="snap1", dest=dest,
            props={"org.myapp:tag": "v2"},
        )
        try:
            recvd = lz.open_resource(name=dest)
            user_props = recvd.get_user_properties()
            assert user_props.get("org.myapp:tag") == "v2"
        finally:
            _destroy_recv(lz, dest)

    def test_exclude_props_inherits_from_parent(self, propful_dataset):
        """exclude_props tells the receive to drop a property from the
        stream's RECEIVED slot, so the destination inherits it.

        Before:                          After:
          POOL/src                         POOL/src              (unchanged)
            compression=lz4   (LOCAL)
            atime=off         (LOCAL)
          POOL/src@snap1                   POOL/src@snap1
                                           POOL/recv_exclude
                                             compression=<inherited from POOL>
                                             atime=off         (RECEIVED)
                                           POOL/recv_exclude@snap1
        """
        lz, pool, src = propful_dataset
        dest = f"{pool}/recv_exclude"
        src.local_replicate(
            tosnap="snap1", dest=dest,
            exclude_props={"compression"},
        )
        try:
            assert _get_local_prop(
                lz, dest, truenas_pylibzfs.ZFSProperty.COMPRESSION
            ) is None
            # atime was not excluded, should still arrive as RECEIVED.
            assert _get_local_prop(
                lz, dest, truenas_pylibzfs.ZFSProperty.ATIME
            ) == "off"
        finally:
            _destroy_recv(lz, dest)

    def test_exclude_props_overlap_with_props_rejected(self, snapped_dataset):
        """Same property in both props and exclude_props is rejected
        with ValueError - the kernel's behaviour on such a collision
        is undefined."""
        _, pool, src = snapped_dataset
        with pytest.raises(ValueError, match="both props and exclude_props"):
            src.local_replicate(
                tosnap="snap1", dest=f"{pool}/recv",
                props={"compression": "zstd"},
                exclude_props={"compression"},
            )

    def test_exclude_props_combined_with_props(self, propful_dataset):
        """props and exclude_props compose on disjoint property names.

        Before:                          After:
          POOL/src                         POOL/src              (unchanged)
            compression=lz4   (LOCAL)
            atime=off         (LOCAL)
          POOL/src@snap1                   POOL/src@snap1
                                           POOL/recv_both
                                             compression=zstd  (LOCAL)
                                             atime=<inherited from POOL>
                                           POOL/recv_both@snap1
        """
        lz, pool, src = propful_dataset
        dest = f"{pool}/recv_both"
        src.local_replicate(
            tosnap="snap1", dest=dest,
            props={"compression": "zstd"},
            exclude_props={"atime"},
        )
        try:
            assert _get_local_prop(
                lz, dest, truenas_pylibzfs.ZFSProperty.COMPRESSION
            ) == "zstd"
            assert _get_local_prop(
                lz, dest, truenas_pylibzfs.ZFSProperty.ATIME
            ) is None
        finally:
            _destroy_recv(lz, dest)


# ===========================================================================
# 3. Recursion (always-on)
# ===========================================================================

class TestRecursive:
    def test_descendants_replicated(self, nested_dataset):
        """-R ships descendants of the source.

        Before:                          After:
          POOL/src                         POOL/src              (unchanged)
          POOL/src/child                   POOL/src/child
          POOL/src@snap1                   POOL/src@snap1
          POOL/src/child@snap1             POOL/src/child@snap1
                                           POOL/recv
                                           POOL/recv/child
                                           POOL/recv@snap1
                                           POOL/recv/child@snap1
        """
        lz, pool, src = nested_dataset
        dest = f"{pool}/recv"
        src.local_replicate(tosnap="snap1", dest=dest)
        try:
            assert lz.open_resource(name=f"{dest}/child") is not None
            assert lz.open_resource(name=f"{dest}/child@snap1") is not None
        finally:
            _destroy_recv(lz, dest)

    def test_leaf_dataset_no_children(self, snapped_dataset):
        """-R on a leaf dataset (no descendants) succeeds.

        Before:                          After:
          POOL/src                         POOL/src              (unchanged)
          POOL/src@snap1                   POOL/src@snap1
                                           POOL/recv
                                           POOL/recv@snap1
        """
        lz, pool, src = snapped_dataset
        dest = f"{pool}/recv"
        src.local_replicate(tosnap="snap1", dest=dest)
        try:
            assert lz.open_resource(name=dest) is not None
        finally:
            _destroy_recv(lz, dest)


# ===========================================================================
# 4. Incremental
# ===========================================================================

class TestIncremental:
    def test_fromsnap_roundtrip(self, snapped_dataset):
        """Initial full then snap1 -> snap2 incremental.

        Before final replicate:           After final replicate:
          POOL/src                          POOL/src              (unchanged)
          POOL/src@snap1                    POOL/src@snap1
          POOL/src@snap2  (with new data)   POOL/src@snap2
          POOL/recv         (from snap1)    POOL/recv
          POOL/recv@snap1                   POOL/recv@snap1
                                            POOL/recv@snap2
        """
        lz, pool, src = snapped_dataset
        dest = f"{pool}/recv"
        # Initial full
        src.local_replicate(tosnap="snap1", dest=dest)
        # Add data + new snap, replicate incrementally.
        _write_data(f"/{pool}/src", DATA_SZ // 2, filename="more.dat")
        _snap(f"{pool}/src@snap2")
        src.local_replicate(tosnap="snap2", dest=dest, fromsnap="snap1")
        try:
            assert lz.open_resource(name=f"{dest}@snap2") is not None
        finally:
            _destroy_recv(lz, dest)


# ===========================================================================
# 5. raw= for encrypted streams
# ===========================================================================

class TestRaw:
    def test_raw_encrypted(self, encrypted_dataset):
        """raw=True replicates an encrypted dataset as-is.

        Before:                          After:
          POOL/enc_src   (encrypted)       POOL/enc_src        (unchanged)
          POOL/enc_src@snap1               POOL/enc_src@snap1
                                           POOL/enc_dst        (encrypted)
                                           POOL/enc_dst@snap1
        """
        lz, pool, src = encrypted_dataset
        dest = f"{pool}/enc_dst"
        src.local_replicate(tosnap="snap1", dest=dest, raw=True)
        try:
            ds = lz.open_resource(name=dest)
            assert ds.encrypted
        finally:
            _destroy_recv(lz, dest)

    def test_raw_flag_on_unencrypted_source(self, snapped_dataset):
        """raw=True on an unencrypted dataset is a no-op-style success.

        Before:                          After:
          POOL/src        (unencrypted)    POOL/src              (unchanged)
          POOL/src@snap1                   POOL/src@snap1
                                           POOL/recv_raw_plain   (unencrypted)
                                           POOL/recv_raw_plain@snap1
        """
        lz, pool, src = snapped_dataset
        dest = f"{pool}/recv_raw_plain"
        src.local_replicate(tosnap="snap1", dest=dest, raw=True)
        try:
            assert lz.open_resource(name=dest) is not None
        finally:
            _destroy_recv(lz, dest)

    def test_re_encrypt_with_different_key(self, snapped_dataset, tmp_path):
        """Encrypt the destination via cmdprops, with a key file
        dropped on disk before the call.

        Before:                          After:
          POOL/src        (unencrypted)    POOL/src              (unchanged)
          POOL/src@snap1                   POOL/src@snap1
          /tmp/<key>      (passphrase)     POOL/recv_reencrypted (encrypted
                                                                  with the
                                                                  new key)
                                           POOL/recv_reencrypted@snap1
        """
        lz, pool, src = snapped_dataset
        key_path = tmp_path / "wrappingkey"
        # ZFS requires keyformat=passphrase keys to be 8+ characters.
        key_path.write_bytes(b"newpassphrase123")
        dest = f"{pool}/recv_reencrypted"
        src.local_replicate(
            tosnap="snap1", dest=dest,
            props={
                "encryption": "on",
                "keyformat": "passphrase",
                "keylocation": f"file://{key_path}",
            },
        )
        try:
            ds = lz.open_resource(name=dest)
            assert ds.encrypted, (
                "destination should be encrypted after props-driven "
                "encryption=on receive"
            )
        finally:
            _destroy_recv(lz, dest)


# ===========================================================================
# 5b. nomount=
# ===========================================================================

class TestNomount:
    def test_default_auto_mounts(self, snapped_dataset):
        """zfs_receive auto-mounts the destination on success.

        Before:                          After:
          POOL/src                         POOL/src                  (unchanged)
          POOL/src@snap1                   POOL/src@snap1
                                           POOL/recv_default_mount   (mounted)
                                           POOL/recv_default_mount@snap1
        """
        lz, pool, src = snapped_dataset
        dest = f"{pool}/recv_default_mount"
        src.local_replicate(tosnap="snap1", dest=dest)
        try:
            mp = lz.open_resource(name=dest).get_mountpoint()
            assert mp is not None
            assert os.path.ismount(mp), (
                f"expected dest to be auto-mounted at {mp}"
            )
        finally:
            _destroy_recv(lz, dest)

    def test_nomount_skips_auto_mount(self, snapped_dataset):
        """nomount=True (zfs receive -u) suppresses the auto-mount.

        Before:                          After:
          POOL/src                         POOL/src              (unchanged)
          POOL/src@snap1                   POOL/src@snap1
                                           POOL/recv_nomount     (NOT mounted)
                                           POOL/recv_nomount@snap1
        """
        lz, pool, src = snapped_dataset
        dest = f"{pool}/recv_nomount"
        src.local_replicate(
            tosnap="snap1", dest=dest, nomount=True,
        )
        try:
            recvd = lz.open_resource(name=dest)
            assert recvd is not None
            mp = recvd.get_mountpoint()
            # mountpoint property still has a value (inherited from
            # the source's stream), but nothing should be mounted at it.
            if mp is not None:
                assert not os.path.ismount(mp), (
                    f"nomount=True did not suppress auto-mount at {mp}"
                )
        finally:
            _destroy_recv(lz, dest)


# ===========================================================================
# 6. force= for dirty destination
# ===========================================================================

class TestForce:
    def test_force_harmless_on_fresh_destination(self, snapped_dataset):
        """force=True is a no-op on a fresh destination.

        Before:                          After:
          POOL/src                         POOL/src              (unchanged)
          POOL/src@snap1                   POOL/src@snap1
                                           POOL/recv_force_fresh
                                           POOL/recv_force_fresh@snap1
        """
        lz, pool, src = snapped_dataset
        dest = f"{pool}/recv_force_fresh"
        src.local_replicate(tosnap="snap1", dest=dest, force=True)
        try:
            assert lz.open_resource(name=dest) is not None
            assert lz.open_resource(name=f"{dest}@snap1") is not None
        finally:
            _destroy_recv(lz, dest)

    def test_force_rolls_back_dirty_destination(self, snapped_dataset):
        """force=True rolls back a destination whose current state has
        diverged from its last received snapshot.

        Before final replicate:           After final replicate:
          POOL/src@snap1                    POOL/src@snap1            (unchanged)
          POOL/src@snap2  (with new data)   POOL/src@snap2
          POOL/recv         (from snap1
                             plus locally
                             written
                             dirty.dat)
          POOL/recv@snap1                   POOL/recv@snap1
                                            POOL/recv@snap2           (no
                                            POOL/recv         (rolled  dirty.dat
                                                               back)    on dest)
        """
        lz, pool, src = snapped_dataset
        dest = f"{pool}/recv"
        # The source's mountpoint (/{pool}/src) is embedded in the
        # stream via -R; override it on the destination so the recv
        # does not try to auto-mount on top of the source's mount.
        recv_mp = f"/{pool}/recv"
        # Populate the destination with the initial replication.
        # zfs_receive auto-mounts the destination on success, so no
        # explicit mount() call is needed before writing to it.
        src.local_replicate(
            tosnap="snap1", dest=dest,
            props={"mountpoint": recv_mp},
        )
        # Add fresh data on the source and a second snap.
        _write_data(f"/{pool}/src", DATA_SZ // 4, filename="extra.dat")
        _snap(f"{pool}/src@snap2")
        # Mutate the destination (so its current state diverges) and
        # let the kernel roll it back via force=True.
        recv_rsrc = lz.open_resource(name=dest)
        _write_data(recv_mp, 1024, filename="dirty.dat")
        recv_rsrc.unmount()
        src.local_replicate(
            tosnap="snap2", dest=dest, fromsnap="snap1", force=True,
            props={"mountpoint": recv_mp},
        )
        try:
            assert lz.open_resource(name=f"{dest}@snap2") is not None
        finally:
            _destroy_recv(lz, dest)


# ===========================================================================
# 7. Pre-flight failures
# ===========================================================================

class TestPreflight:
    def test_missing_tosnap_raises(self, snapped_dataset):
        _, pool, src = snapped_dataset
        with pytest.raises(truenas_pylibzfs.ZFSException):
            src.local_replicate(tosnap="nosuch", dest=f"{pool}/recv")

    def test_missing_fromsnap_raises(self, snapped_dataset):
        _, pool, src = snapped_dataset
        with pytest.raises(truenas_pylibzfs.ZFSException):
            src.local_replicate(
                tosnap="snap1", dest=f"{pool}/recv", fromsnap="nosuch"
            )

    def test_recv_into_missing_parent_raises(self, snapped_dataset):
        """Receive into a dataset path whose parent does not exist
        must fail.  The kernel cannot create intermediate datasets;
        only the leaf dest@snap is created during recv."""
        _, pool, src = snapped_dataset
        with pytest.raises(truenas_pylibzfs.ZFSException):
            src.local_replicate(
                tosnap="snap1", dest=f"{pool}/no/such/parent",
            )

    def test_fromsnap_on_wrong_dataset_raises(self, snapped_dataset):
        """fromsnap is a suffix-only name: it gets joined to the source
        dataset's name during pre-flight (zfs_preflight builds
        f'{src_name}@{fromsnap}').  A snapshot that exists on a
        DIFFERENT dataset under the same suffix is invisible from the
        source's perspective and the pre-flight rejects it."""
        lz, pool, src = snapped_dataset
        # Create another dataset with a snapshot whose suffix happens
        # to match what we'll pass as fromsnap.
        other = f"{pool}/other"
        lz.create_resource(
            name=other, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )
        _snap(f"{other}@only_here")
        try:
            with pytest.raises(truenas_pylibzfs.ZFSException):
                src.local_replicate(
                    tosnap="snap1", dest=f"{pool}/recv",
                    fromsnap="only_here",
                )
        finally:
            lzc.destroy_snapshots(snapshot_names={f"{other}@only_here"})
            lz.destroy_resource(name=other)


# ===========================================================================
# 8. Argument validation
# ===========================================================================

class TestArgValidation:
    def test_tosnap_required(self, snapped_dataset):
        _, pool, src = snapped_dataset
        with pytest.raises(ValueError, match="tosnap"):
            src.local_replicate(dest=f"{pool}/recv")  # type: ignore

    def test_dest_required(self, snapped_dataset):
        _, pool, src = snapped_dataset
        with pytest.raises(ValueError, match="dest"):
            src.local_replicate(tosnap="snap1")  # type: ignore

    def test_raw_in_send_flags_rejected(self, snapped_dataset):
        _, pool, src = snapped_dataset
        with pytest.raises(ValueError, match="raw"):
            src.local_replicate(
                tosnap="snap1", dest=f"{pool}/recv",
                send_flags=lzc.SendFlags.RAW,
            )

    def test_saved_in_send_flags_rejected(self, snapped_dataset):
        _, pool, src = snapped_dataset
        with pytest.raises(ValueError, match="SAVED"):
            src.local_replicate(
                tosnap="snap1", dest=f"{pool}/recv",
                send_flags=lzc.SendFlags.SAVED,
            )

    def test_progress_callback_must_be_callable(self, snapped_dataset):
        _, pool, src = snapped_dataset
        with pytest.raises(TypeError, match="callable"):
            src.local_replicate(
                tosnap="snap1", dest=f"{pool}/recv",
                progress_callback=42,  # type: ignore
            )

    def test_progress_interval_must_be_positive(self, snapped_dataset):
        _, pool, src = snapped_dataset
        with pytest.raises(ValueError, match="positive"):
            src.local_replicate(
                tosnap="snap1", dest=f"{pool}/recv",
                progress_callback=lambda *a: None,
                progress_interval_seconds=0,
            )

    def test_resumable_kwarg_rejected(self, snapped_dataset):
        """resumable= is only valid on lzc.local_replicate.  The
        resource-object wrapper does not list it in its kwnames, so
        passing it raises TypeError from PyArg_ParseTupleAndKeywords."""
        _, pool, src = snapped_dataset
        with pytest.raises(TypeError):
            src.local_replicate(
                tosnap="snap1", dest=f"{pool}/recv",
                resumable=True,  # type: ignore
            )

    def test_resume_token_kwarg_rejected(self, snapped_dataset):
        """resume_token= is only valid on lzc.local_replicate.  Same
        rejection pattern as resumable= above."""
        _, pool, src = snapped_dataset
        with pytest.raises(TypeError):
            src.local_replicate(
                tosnap="snap1", dest=f"{pool}/recv",
                resume_token="opaque",  # type: ignore
            )


# ===========================================================================
# 9. Audit event firing
# ===========================================================================

class TestAudit:
    def test_audit_event_fires(self, snapped_dataset):
        lz, pool, src = snapped_dataset
        events = []

        def hook(event, args):
            if event.endswith(".ZFSResource.local_replicate"):
                events.append((event, args))

        sys.addaudithook(hook)
        src.local_replicate(tosnap="snap1", dest=f"{pool}/recv")
        try:
            assert len(events) == 1
            event, args = events[0]
            # args[0] is the source name PyObject; event was fired.
            assert event.endswith(".ZFSResource.local_replicate")
        finally:
            _destroy_recv(lz, f"{pool}/recv")


# ===========================================================================
# 10. ZFSVolume path
# ===========================================================================

def _history_lines(lz, pool):
    """Return all 'history command' strings on `pool` in chronological
    order, skipping internal events that don't carry a command."""
    return [
        rec["history command"]
        for rec in lz.open_pool(name=pool).iter_history()
        if "history command" in rec
    ]


class TestVolume:
    """ZFSVolume.local_replicate hits the non-recursive zfs-mode branch
    (recursive=False, no -R in history)."""

    def test_volume_replicate_roundtrip(self, volume_dataset):
        """
        Before:                          After:
          POOL/vol         (ZVOL)          POOL/vol             (unchanged)
          POOL/vol@snap1                   POOL/vol@snap1
                                           POOL/recv_vol        (ZVOL)
                                           POOL/recv_vol@snap1
        """
        lz, pool, vol = volume_dataset
        dest = f"{pool}/recv_vol"
        vol.local_replicate(tosnap="snap1", dest=dest)
        try:
            recvd = lz.open_resource(name=dest)
            assert recvd is not None
            assert recvd.type == truenas_pylibzfs.ZFSType.ZFS_TYPE_VOLUME
            assert lz.open_resource(name=f"{dest}@snap1") is not None
        finally:
            _destroy_recv(lz, dest)

    def test_volume_history_omits_dash_R(self, volume_dataset):
        """The synthesized history line for a volume replicate must
        carry -p but NOT -R; volumes have no non-snapshot descendants
        and the wrapper sets recursive=False for ZFSVolume."""
        lz, pool, vol = volume_dataset
        dest = f"{pool}/recv_vol_hist"
        vol.local_replicate(tosnap="snap1", dest=dest)
        try:
            cmds = _history_lines(lz, pool)
            matches = [c for c in cmds
                       if "zfs receive" in c and dest in c]
            assert matches, f"no history line found in {cmds[-5:]}"
            line = matches[-1]
            assert " -p" in line, f"missing -p in: {line}"
            assert " -R" not in line, (
                f"unexpected -R on volume replicate: {line}"
            )
        finally:
            _destroy_recv(lz, dest)

    def test_filesystem_history_includes_dash_R(self, snapped_dataset):
        """Counterpart to test_volume_history_omits_dash_R: filesystem
        path renders both -R and -p."""
        lz, pool, src = snapped_dataset
        dest = f"{pool}/recv_fs_hist"
        src.local_replicate(tosnap="snap1", dest=dest)
        try:
            cmds = _history_lines(lz, pool)
            matches = [c for c in cmds
                       if "zfs receive" in c and dest in c]
            assert matches, f"no history line found in {cmds[-5:]}"
            line = matches[-1]
            assert " -R" in line, f"missing -R in: {line}"
            assert " -p" in line, f"missing -p in: {line}"
        finally:
            _destroy_recv(lz, dest)


# ===========================================================================
# 11. Clones under -R
# ===========================================================================

class TestClones:
    def test_clone_origin_preserved_under_R(self, pool_fixture):
        """The receive side recreates clone relationships when -R
        ships both the origin snapshot and the clone; the dest
        clone's origin property points at the dest's golden snapshot.

        Before:                          After:
          POOL/cparent                     POOL/cparent          (unchanged)
          POOL/cparent/golden
          POOL/cparent/clone
              origin=POOL/cparent/golden@base
          POOL/cparent/golden@base
          POOL/cparent@T
          POOL/cparent/golden@T
          POOL/cparent/clone@T
                                           POOL/recv_clones
                                           POOL/recv_clones/golden
                                           POOL/recv_clones/clone
                                               origin=POOL/recv_clones/golden@base
                                           POOL/recv_clones/golden@base
                                           POOL/recv_clones@T
                                           POOL/recv_clones/golden@T
                                           POOL/recv_clones/clone@T
        """
        lz, pool = pool_fixture
        parent = f"{pool}/cparent"
        golden = f"{parent}/golden"
        clone = f"{parent}/clone"
        dest = f"{pool}/recv_clones"

        # Build the structure: parent/golden with data, snapshot it,
        # clone the snapshot into parent/clone (making clone a
        # sibling of golden within parent's subtree).
        lz.create_resource(
            name=parent, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )
        lz.create_resource(
            name=golden, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )
        lz.open_resource(name=golden).mount()
        _write_data(f"/{golden}", DATA_SZ)
        _snap(f"{golden}@base")
        lz.open_resource(name=f"{golden}@base").clone(name=clone)

        # All three (parent, golden, clone) need a snapshot named "T"
        # for -R to ship them as part of one stream.
        lzc.create_snapshots(snapshot_names={
            f"{parent}@T", f"{golden}@T", f"{clone}@T",
        })

        try:
            src = lz.open_resource(name=parent)
            src.local_replicate(tosnap="T", dest=dest)
            try:
                # The destination clone exists and points at the
                # destination's golden@base, not the source's.
                dest_clone = lz.open_resource(name=f"{dest}/clone")
                assert dest_clone is not None
                origin = dest_clone.get_properties(properties={
                    truenas_pylibzfs.ZFSProperty.ORIGIN
                }).origin.value
                assert origin == f"{dest}/golden@base", (
                    f"clone origin not rewritten to dest side: {origin}"
                )
            finally:
                _destroy_recv(lz, dest)
        finally:
            # Clean up the source-side structure.
            for snap_name in (
                f"{clone}@T", f"{golden}@T", f"{parent}@T",
                f"{golden}@base",
            ):
                try:
                    lzc.destroy_snapshots(snapshot_names={snap_name})
                except Exception:
                    pass
            for ds in (clone, golden, parent):
                try:
                    lz.destroy_resource(name=ds)
                except Exception:
                    pass
