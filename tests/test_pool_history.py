"""
Tests for ZFSPool.iter_history().

Pools are created via truenas_pylibzfs.create_pool() and operations are
performed through the library so that each action generates a history entry.
Tests then verify that iter_history() surfaces those entries correctly.

History entries written by truenas_pylibzfs carry the default prefix
"truenas-pylibzfs: " followed by the operation string.

ZFS history nvlist keys use spaces, not underscores:
  'history time', 'history command', 'history hostname', etc.
Records without a 'history command' key are "internal" events.
"""

import os
import shutil
import tempfile
import time

import pytest
import truenas_pylibzfs

POOL_A = "testpool_hist_a"
POOL_B = "testpool_hist_b"
DISK_SZ = 256 * 1048576  # 256 MiB
HIST_PREFIX = "truenas-pylibzfs: "


def _make_disk(tmpdir):
    path = os.path.join(tmpdir, "d0.img")
    with open(path, "w") as f:
        os.ftruncate(f.fileno(), DISK_SZ)
    return path


def _vdev(path):
    return truenas_pylibzfs.create_vdev_spec(
        vdev_type=truenas_pylibzfs.VDevType.FILE, name=path
    )


def _commands(pool):
    """Return all 'history command' strings from pool history (user-visible)."""
    return [
        rec["history command"]
        for rec in pool.iter_history()
        if "history command" in rec
    ]


@pytest.fixture
def pool_a():
    """Create POOL_A via truenas_pylibzfs. Yields (lz, pool)."""
    d = tempfile.mkdtemp(prefix="pylibzfs_hist_a_")
    disk = _make_disk(d)
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(name=POOL_A, storage_vdevs=[_vdev(disk)])
    pool = lz.open_pool(name=POOL_A)
    try:
        yield lz, pool
    finally:
        try:
            lz.destroy_pool(name=POOL_A, force=True)
        except Exception:
            pass
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def pool_b():
    """Create POOL_B via truenas_pylibzfs. Yields (lz, pool)."""
    d = tempfile.mkdtemp(prefix="pylibzfs_hist_b_")
    disk = _make_disk(d)
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(name=POOL_B, storage_vdevs=[_vdev(disk)])
    pool = lz.open_pool(name=POOL_B)
    try:
        yield lz, pool
    finally:
        try:
            lz.destroy_pool(name=POOL_B, force=True)
        except Exception:
            pass
        shutil.rmtree(d, ignore_errors=True)


# ---------------------------------------------------------------------------
# Basic iterator protocol
# ---------------------------------------------------------------------------

def test_iter_history_returns_iterator(pool_a):
    """iter_history() must return a proper iterator (iter(it) is it)."""
    _, p = pool_a
    it = p.iter_history()
    assert hasattr(it, "__iter__")
    assert hasattr(it, "__next__")
    assert iter(it) is it


def test_iter_history_records_are_dicts(pool_a):
    """Every yielded record must be a dict."""
    _, p = pool_a
    for rec in p.iter_history(skip_internal=False):
        assert isinstance(rec, dict), f"expected dict, got {type(rec)}"


def test_iter_history_has_time_field(pool_a):
    """Every record must have 'history time' as an int."""
    _, p = pool_a
    records = list(p.iter_history(skip_internal=False))
    assert records, "no history records found"
    for rec in records:
        assert "history time" in rec, f"missing 'history time' in {rec}"
        assert isinstance(rec["history time"], int)


# ---------------------------------------------------------------------------
# Internal-event filtering
# ---------------------------------------------------------------------------

def test_skip_internal_default(pool_a):
    """With skip_internal=True (default) every record must have
    'history command' — records without it are internal and must be hidden."""
    _, p = pool_a
    records = list(p.iter_history())
    assert records, "no records returned with skip_internal=True"
    for rec in records:
        assert "history command" in rec, (
            f"non-command record leaked through skip_internal filter: {rec}"
        )


def test_include_internal(pool_a):
    """With skip_internal=False some records must lack 'history command'
    (pool creation always generates internal events)."""
    _, p = pool_a
    internal = [
        rec for rec in p.iter_history(skip_internal=False)
        if "history command" not in rec
    ]
    assert internal, "no internal events found with skip_internal=False"


# ---------------------------------------------------------------------------
# Pool-creation history entry
# ---------------------------------------------------------------------------

def test_create_pool_history_entry(pool_a):
    """create_pool() via truenas_pylibzfs must produce a history entry
    containing 'zpool create <pool_name>'."""
    _, p = pool_a
    cmds = _commands(p)
    assert cmds, "no command records found"
    expected = f"zpool create {POOL_A}"
    assert any(expected in cmd for cmd in cmds), (
        f"expected '{expected}' in history; commands: {cmds}"
    )


# ---------------------------------------------------------------------------
# Pool property operations
# ---------------------------------------------------------------------------

def test_set_properties_history_entry(pool_a):
    """set_properties() must write 'zpool set (properties) <pool_name>'."""
    _, p = pool_a
    p.set_properties(
        properties={truenas_pylibzfs.enums.ZPOOLProperty.AUTOREPLACE: "on"}
    )
    cmds = _commands(p)
    expected = f"zpool set (properties) {POOL_A}"
    assert any(expected in cmd for cmd in cmds), (
        f"expected '{expected}' in history; commands: {cmds}"
    )


def test_set_user_properties_history_entry(pool_a):
    """set_user_properties() must write 'zpool set (user properties) <pool_name>'."""
    _, p = pool_a
    p.set_user_properties(
        user_properties={"org.truenas:test_key": "test_value"}
    )
    cmds = _commands(p)
    expected = f"zpool set (user properties) {POOL_A}"
    assert any(expected in cmd for cmd in cmds), (
        f"expected '{expected}' in history; commands: {cmds}"
    )


def test_upgrade_history_entry(pool_a):
    """upgrade() must write 'zpool upgrade <pool_name>'."""
    _, p = pool_a
    p.upgrade()
    cmds = _commands(p)
    expected = f"zpool upgrade {POOL_A}"
    assert any(expected in cmd for cmd in cmds), (
        f"expected '{expected}' in history; commands: {cmds}"
    )


def test_scan_history_entry(pool_a):
    """scan(POOL_SCAN_SCRUB) must write 'zpool scrub <pool_name>'."""
    _, p = pool_a
    p.scan(func=truenas_pylibzfs.enums.ScanFunction.SCRUB)
    cmds = _commands(p)
    expected = f"zpool scrub {POOL_A}"
    assert any(expected in cmd for cmd in cmds), (
        f"expected '{expected}' in history; commands: {cmds}"
    )


def test_clear_history_entry(pool_a):
    """clear() must write 'zpool clear <pool_name>'."""
    _, p = pool_a
    p.clear()
    cmds = _commands(p)
    expected = f"zpool clear {POOL_A}"
    assert any(expected in cmd for cmd in cmds), (
        f"expected '{expected}' in history; commands: {cmds}"
    )


# ---------------------------------------------------------------------------
# Dataset operations
# ---------------------------------------------------------------------------

def test_create_resource_history_entry(pool_a):
    """create_resource() must write 'zfs create <dataset_name>'."""
    lz, p = pool_a
    ds_name = f"{POOL_A}/testds"
    lz.create_resource(
        name=ds_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    try:
        cmds = _commands(p)
        assert any(f"zfs create {ds_name}" in cmd for cmd in cmds), (
            f"expected 'zfs create {ds_name}' in history; commands: {cmds}"
        )
    finally:
        lz.destroy_resource(name=ds_name)


def test_create_resource_with_properties_history_entry(pool_a):
    """create_resource() with properties must write
    'zfs create <name> with properties: ...'."""
    lz, p = pool_a
    ds_name = f"{POOL_A}/testds_props"
    lz.create_resource(
        name=ds_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        properties={truenas_pylibzfs.ZFSProperty.ATIME: "off"},
    )
    try:
        cmds = _commands(p)
        assert any(
            f"zfs create {ds_name} with properties:" in cmd for cmd in cmds
        ), (
            f"expected 'zfs create {ds_name} with properties:' in history; "
            f"commands: {cmds}"
        )
    finally:
        lz.destroy_resource(name=ds_name)


def test_dataset_set_properties_history_entry(pool_a):
    """dataset.set_properties() must write 'zfs update <name> with properties: ...'."""
    lz, p = pool_a
    ds_name = f"{POOL_A}/testds_setprop"
    lz.create_resource(
        name=ds_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    try:
        ds = lz.open_resource(name=ds_name)
        ds.set_properties(
            properties={truenas_pylibzfs.ZFSProperty.ATIME: "off"}
        )
        cmds = _commands(p)
        assert any(f"zfs update {ds_name}" in cmd for cmd in cmds), (
            f"expected 'zfs update {ds_name}' in history; commands: {cmds}"
        )
    finally:
        lz.destroy_resource(name=ds_name)


def test_dataset_set_user_properties_history_entry(pool_a):
    """dataset.set_user_properties() must write
    'zfs update <name> with user properties: ...'."""
    lz, p = pool_a
    ds_name = f"{POOL_A}/testds_userprop"
    lz.create_resource(
        name=ds_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    try:
        ds = lz.open_resource(name=ds_name)
        ds.set_user_properties(
            user_properties={"org.truenas:mykey": "myval"}
        )
        cmds = _commands(p)
        assert any(
            f"zfs update {ds_name} with user properties:" in cmd
            for cmd in cmds
        ), (
            f"expected 'zfs update {ds_name} with user properties:' "
            f"in history; commands: {cmds}"
        )
    finally:
        lz.destroy_resource(name=ds_name)


def test_dataset_inherit_property_history_entry(pool_a):
    """dataset.inherit_property() must write 'zfs inherit <prop> <name>'."""
    lz, p = pool_a
    ds_name = f"{POOL_A}/testds_inherit"
    # Create with atime=off so we can inherit it back
    lz.create_resource(
        name=ds_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        properties={truenas_pylibzfs.ZFSProperty.ATIME: "off"},
    )
    try:
        ds = lz.open_resource(name=ds_name)
        ds.inherit_property(property=truenas_pylibzfs.ZFSProperty.ATIME)
        cmds = _commands(p)
        assert any(
            "zfs inherit" in cmd and ds_name in cmd for cmd in cmds
        ), (
            f"expected 'zfs inherit ... {ds_name}' in history; commands: {cmds}"
        )
    finally:
        lz.destroy_resource(name=ds_name)


def test_destroy_resource_history_entry(pool_a):
    """destroy_resource() must write 'zfs destroy <name>'."""
    lz, p = pool_a
    ds_name = f"{POOL_A}/testds_destroy"
    lz.create_resource(
        name=ds_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    )
    lz.destroy_resource(name=ds_name)
    cmds = _commands(p)
    assert any(f"zfs destroy {ds_name}" in cmd for cmd in cmds), (
        f"expected 'zfs destroy {ds_name}' in history; commands: {cmds}"
    )


# ---------------------------------------------------------------------------
# History entries carry the truenas-pylibzfs prefix
# ---------------------------------------------------------------------------

def test_history_entries_carry_prefix(pool_a):
    """Every command written by truenas_pylibzfs must include the prefix."""
    _, p = pool_a
    # clear() is a simple operation that always adds a prefixed entry
    p.clear()
    our_cmds = [cmd for cmd in _commands(p) if HIST_PREFIX in cmd]
    assert our_cmds, (
        f"no history commands with prefix '{HIST_PREFIX}' found"
    )


# ---------------------------------------------------------------------------
# Timestamp filtering (since / until)
# ---------------------------------------------------------------------------

def test_since_excludes_earlier_records(pool_a):
    """Records before `since` must not appear."""
    _, p = pool_a
    all_recs = list(p.iter_history(skip_internal=False))
    assert len(all_recs) >= 2, "need at least 2 records to test since"

    # Use the last record's timestamp as since — only it (and ties) return.
    last_ts = all_recs[-1]["history time"]
    filtered = list(p.iter_history(skip_internal=False, since=last_ts))
    assert filtered, "since=last_ts should still yield at least one record"
    for rec in filtered:
        assert rec["history time"] >= last_ts, (
            f"record before since={last_ts} leaked through: {rec}"
        )


def test_until_excludes_later_records(pool_a):
    """Records after `until` must not appear."""
    _, p = pool_a
    all_recs = list(p.iter_history(skip_internal=False))
    assert len(all_recs) >= 2, "need at least 2 records to test until"

    first_ts = all_recs[0]["history time"]
    filtered = list(p.iter_history(skip_internal=False, until=first_ts))
    assert filtered, "until=first_ts should still yield at least one record"
    for rec in filtered:
        assert rec["history time"] <= first_ts, (
            f"record after until={first_ts} leaked through: {rec}"
        )


def test_since_until_range(pool_a):
    """since + until together form an inclusive time window."""
    _, p = pool_a
    before = int(time.time())
    p.clear()
    after = int(time.time())

    windowed = list(p.iter_history(since=before, until=after))
    assert windowed, (
        f"no records in window [{before}, {after}]; "
        f"all records: {list(p.iter_history())}"
    )
    for rec in windowed:
        ts = rec["history time"]
        assert before <= ts <= after, (
            f"record outside window [{before}, {after}]: ts={ts}"
        )


def test_since_zero_means_no_lower_bound(pool_a):
    """since=0 (default) must not filter anything out."""
    _, p = pool_a
    all_recs = list(p.iter_history(skip_internal=False))
    since_zero = list(p.iter_history(skip_internal=False, since=0))
    assert len(all_recs) == len(since_zero), (
        "since=0 filtered records unexpectedly"
    )


def test_until_zero_means_no_upper_bound(pool_a):
    """until=0 (default) must not filter anything out."""
    _, p = pool_a
    all_recs = list(p.iter_history(skip_internal=False))
    until_zero = list(p.iter_history(skip_internal=False, until=0))
    assert len(all_recs) == len(until_zero), (
        "until=0 filtered records unexpectedly"
    )


def test_future_since_yields_nothing(pool_a):
    """since=far_future must yield no records."""
    _, p = pool_a
    far_future = 2**62
    result = list(p.iter_history(skip_internal=False, since=far_future))
    assert result == [], (
        f"expected empty result for since=far_future, got {result}"
    )


def test_zero_until_with_future_since_yields_nothing(pool_a):
    """since past the end of history with until=0 must yield nothing."""
    _, p = pool_a
    far_future = 2**62
    result = list(p.iter_history(skip_internal=False, since=far_future, until=0))
    assert result == []


# ---------------------------------------------------------------------------
# Independent iterators on different pools
# ---------------------------------------------------------------------------

def test_iter_history_multiple_pools(pool_a, pool_b):
    """Iterators on different pools are independent: each pool's history
    references only its own resources."""
    lz_a, pa = pool_a
    lz_b, pb = pool_b

    pa.set_user_properties(user_properties={"org.truenas:pool": "a"})
    pb.set_user_properties(user_properties={"org.truenas:pool": "b"})

    ds_a = f"{POOL_A}/ds_a"
    ds_b = f"{POOL_B}/ds_b"
    lz_a.create_resource(
        name=ds_a, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
    )
    lz_b.create_resource(
        name=ds_b, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
    )

    cmds_a = _commands(pa)
    cmds_b = _commands(pb)

    for cmd in cmds_a:
        assert POOL_B not in cmd, (
            f"pool B name found in pool A history: {cmd!r}"
        )

    for cmd in cmds_b:
        assert POOL_A not in cmd, (
            f"pool A name found in pool B history: {cmd!r}"
        )
