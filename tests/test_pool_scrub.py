"""
Tests for ZFSPool.scrub_info() and ZFSPool.scan().

Covers:
  - ScanFunction / ScanState / ScanScrubCmd enum availability and values
  - scrub_info() returns None on a pool that has never been scanned
  - scrub_info() returns struct_zpool_scrub after a scan has run
  - struct field types are correct
  - scan() starts a scrub; state transitions to SCANNING or FINISHED
  - scan(func=NONE) cancels a scan -> state == CANCELED or FINISHED
  - scan(cmd=PAUSE) pauses a scan -> pass_scrub_pause != 0
  - percentage is None when not actively scanning
  - Invalid func / cmd raise ValueError
"""

import os
import shutil
import subprocess
import tempfile

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import enums

ScanFunction = enums.ScanFunction
ScanState = enums.ScanState
ScanScrubCmd = enums.ScanScrubCmd

POOL_NAME = 'testpool_scrub'
DISK_SZ = 512 * 1024 * 1024  # 512 MiB — small enough for fast scrubs


# ---------------------------------------------------------------------------
# Disk / pool helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def make_disks():
    """Factory: make_disks(n) -> list of n image-file paths; cleaned up on exit."""
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix='pylibzfs_scrub_')
        dirs.append(d)
        paths = []
        for i in range(n):
            p = os.path.join(d, f'd{i}.img')
            with open(p, 'w') as f:
                os.ftruncate(f.fileno(), DISK_SZ)
            paths.append(p)
        return paths

    yield _make

    for d in dirs:
        shutil.rmtree(d, ignore_errors=True)


def _create_pool(vdev_args):
    subprocess.run(['zpool', 'create', '-f', POOL_NAME] + vdev_args, check=True)


def _destroy_pool():
    subprocess.run(['zpool', 'destroy', '-f', POOL_NAME], check=False)


def _open_pool():
    lz = truenas_pylibzfs.open_handle()
    pool = lz.open_pool(name=POOL_NAME)
    return lz, pool


@pytest.fixture
def pool_stripe(make_disks):
    disks = make_disks(1)
    _create_pool(disks)
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


@pytest.fixture
def pool_mirror(make_disks):
    disks = make_disks(2)
    _create_pool(['mirror'] + disks)
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


@pytest.fixture
def pool_after_scrub(pool_stripe):
    """Stripe pool that has had one complete scrub run."""
    subprocess.run(['zpool', 'scrub', '-w', POOL_NAME], check=True)
    lz, pool = _open_pool()
    yield lz, pool


# ---------------------------------------------------------------------------
# Enum availability / membership
# ---------------------------------------------------------------------------

def test_scan_function_enum_members():
    for name in ('NONE', 'SCRUB', 'RESILVER', 'ERRORSCRUB'):
        assert hasattr(ScanFunction, name), f'ScanFunction missing member: {name}'


def test_scan_state_enum_members():
    for name in ('NONE', 'SCANNING', 'FINISHED', 'CANCELED', 'ERRORSCRUBBING'):
        assert hasattr(ScanState, name), f'ScanState missing member: {name}'


def test_scan_scrub_cmd_enum_members():
    for name in ('NORMAL', 'PAUSE', 'FROM_LAST_TXG'):
        assert hasattr(ScanScrubCmd, name), f'ScanScrubCmd missing member: {name}'


def test_scan_function_values():
    assert int(ScanFunction.NONE) == 0
    assert int(ScanFunction.SCRUB) == 1
    assert int(ScanFunction.RESILVER) == 2
    assert int(ScanFunction.ERRORSCRUB) == 3


def test_scan_state_values():
    assert int(ScanState.NONE) == 0
    assert int(ScanState.SCANNING) == 1
    assert int(ScanState.FINISHED) == 2
    assert int(ScanState.CANCELED) == 3


def test_scan_scrub_cmd_values():
    assert int(ScanScrubCmd.NORMAL) == 0
    assert int(ScanScrubCmd.PAUSE) == 1
    assert int(ScanScrubCmd.FROM_LAST_TXG) == 2


# ---------------------------------------------------------------------------
# scrub() on a never-scrubbed pool
# ---------------------------------------------------------------------------

def test_scrub_returns_none_before_any_scan(pool_stripe):
    """A freshly created pool with no scan history returns None."""
    lz, pool = pool_stripe
    assert pool.scrub_info() is None


# ---------------------------------------------------------------------------
# struct_zpool_scrub field presence and types (after a completed scrub)
# ---------------------------------------------------------------------------

_ALWAYS_INT_FIELDS = (
    'start_time', 'end_time',
    'to_examine', 'examined', 'skipped', 'processed',
    'issued', 'errors',
    'pass_exam', 'pass_start', 'pass_scrub_pause',
    'pass_scrub_spent_paused', 'pass_issued',
)

_ALL_FIELDS = _ALWAYS_INT_FIELDS + (
    'func', 'state',
    'error_scrub_func', 'error_scrub_state',
    'error_scrub_start', 'error_scrub_end',
    'error_scrub_examined', 'error_scrub_to_be_examined',
    'pass_error_scrub_pause',
    'percentage',
)


def test_scrub_returns_struct_after_scrub(pool_after_scrub):
    lz, pool = pool_after_scrub
    assert pool.scrub_info() is not None


def test_scrub_return_type_name(pool_after_scrub):
    lz, pool = pool_after_scrub
    assert type(pool.scrub_info()).__name__ == 'struct_zpool_scrub'


def test_scrub_struct_has_all_attributes(pool_after_scrub):
    lz, pool = pool_after_scrub
    s = pool.scrub_info()
    for attr in _ALL_FIELDS:
        assert hasattr(s, attr), f'struct_zpool_scrub missing attribute: {attr}'


def test_scrub_func_is_scan_function_enum(pool_after_scrub):
    lz, pool = pool_after_scrub
    assert isinstance(pool.scrub_info().func, ScanFunction)


def test_scrub_state_is_scan_state_enum(pool_after_scrub):
    lz, pool = pool_after_scrub
    assert isinstance(pool.scrub_info().state, ScanState)


def test_scrub_integer_fields_are_non_negative_int(pool_after_scrub):
    lz, pool = pool_after_scrub
    s = pool.scrub_info()
    for field in _ALWAYS_INT_FIELDS:
        val = getattr(s, field)
        assert isinstance(val, int), f'{field}: expected int, got {type(val).__name__}'
        assert val >= 0, f'{field}: expected non-negative, got {val}'


def test_scrub_func_is_scrub_after_scrub_run(pool_after_scrub):
    lz, pool = pool_after_scrub
    assert pool.scrub_info().func == ScanFunction.SCRUB


def test_scrub_state_is_finished_after_complete_scrub(pool_after_scrub):
    lz, pool = pool_after_scrub
    assert pool.scrub_info().state == ScanState.FINISHED


def test_scrub_start_time_nonzero(pool_after_scrub):
    lz, pool = pool_after_scrub
    assert pool.scrub_info().start_time > 0


def test_scrub_end_time_nonzero_after_finished(pool_after_scrub):
    lz, pool = pool_after_scrub
    assert pool.scrub_info().end_time > 0


def test_scrub_percentage_none_when_finished(pool_after_scrub):
    lz, pool = pool_after_scrub
    s = pool.scrub_info()
    assert s.state == ScanState.FINISHED
    assert s.percentage is None, \
        f'expected None for percentage when FINISHED, got {s.percentage!r}'



# ---------------------------------------------------------------------------
# error_scrub_* fields: consistently None or all typed
# ---------------------------------------------------------------------------

def test_error_scrub_fields_none_or_all_typed(pool_after_scrub):
    """
    Fields 15-21 (error-scrub) are either all None (old kernel) or all typed.
    """
    lz, pool = pool_after_scrub
    s = pool.scrub_info()

    error_int_fields = (
        'error_scrub_start', 'error_scrub_end',
        'error_scrub_examined', 'error_scrub_to_be_examined',
        'pass_error_scrub_pause',
    )

    if s.error_scrub_func is None:
        # All error-scrub fields must be None together
        for attr in ('error_scrub_state',) + error_int_fields:
            assert getattr(s, attr) is None, \
                f'expected None for {attr} when error_scrub_func is None'
    else:
        assert isinstance(s.error_scrub_func, ScanFunction)
        assert isinstance(s.error_scrub_state, ScanState)
        for attr in error_int_fields:
            val = getattr(s, attr)
            assert isinstance(val, int) and val >= 0, \
                f'expected non-negative int for {attr}, got {val!r}'


# ---------------------------------------------------------------------------
# scan() API — start and cancel
# ---------------------------------------------------------------------------

def test_scan_start_scrub(pool_stripe):
    """scan(func=SCRUB) starts a scrub; state is SCANNING or FINISHED."""
    lz, pool = pool_stripe
    pool.scan(func=ScanFunction.SCRUB)
    s = pool.scrub_info()
    assert s is not None
    assert s.state in (ScanState.SCANNING, ScanState.FINISHED), \
        f'unexpected state after starting scrub: {s.state}'


def test_scan_cancel(pool_stripe):
    """scan(func=NONE) cancels an ongoing scan -> CANCELED or FINISHED."""
    lz, pool = pool_stripe
    pool.scan(func=ScanFunction.SCRUB)
    pool.scan(func=ScanFunction.NONE)
    s = pool.scrub_info()
    assert s is not None
    assert s.state in (ScanState.CANCELED, ScanState.FINISHED), \
        f'expected CANCELED or FINISHED after cancel, got {s.state}'


def test_scan_cancel_on_mirror(pool_mirror):
    """Mirror pool with data gives a better chance of catching in-flight cancel."""
    lz, pool = pool_mirror
    # Write data so the scrub has something to process
    subprocess.run(
        ['dd', 'if=/dev/urandom', f'of=/{POOL_NAME}/fill', 'bs=1M', 'count=64'],
        check=False, capture_output=True,
    )
    subprocess.run(['sync'], check=False)

    pool.scan(func=ScanFunction.SCRUB)
    pool.scan(func=ScanFunction.NONE)
    s = pool.scrub_info()
    assert s is not None
    assert s.state in (ScanState.CANCELED, ScanState.FINISHED)


# ---------------------------------------------------------------------------
# scan() API — pause
# ---------------------------------------------------------------------------

def test_scan_pause_sets_pause_timestamp(pool_stripe):
    """scan(cmd=PAUSE) sets pass_scrub_pause to non-zero."""
    lz, pool = pool_stripe
    pool.scan(func=ScanFunction.SCRUB)
    s = pool.scrub_info()
    if s is None or s.state != ScanState.SCANNING:
        pytest.skip('scrub completed before pause could be issued')

    pool.scan(func=ScanFunction.SCRUB, cmd=ScanScrubCmd.PAUSE)
    s2 = pool.scrub_info()
    assert s2 is not None
    assert s2.pass_scrub_pause != 0, \
        'expected pass_scrub_pause != 0 after pausing scrub'


# ---------------------------------------------------------------------------
# scan() — invalid arguments
# ---------------------------------------------------------------------------

def test_scan_missing_func_raises(pool_stripe):
    lz, pool = pool_stripe
    with pytest.raises(ValueError):
        pool.scan()


def test_scan_out_of_range_func_raises(pool_stripe):
    lz, pool = pool_stripe
    with pytest.raises((ValueError, OverflowError)):
        pool.scan(func=9999)


def test_scan_negative_func_raises(pool_stripe):
    lz, pool = pool_stripe
    with pytest.raises((ValueError, OverflowError)):
        pool.scan(func=-1)


def test_scan_out_of_range_cmd_raises(pool_stripe):
    lz, pool = pool_stripe
    with pytest.raises((ValueError, OverflowError)):
        pool.scan(func=ScanFunction.SCRUB, cmd=9999)


# ---------------------------------------------------------------------------
# Computed fields during an active scan
# ---------------------------------------------------------------------------

def test_scrub_percentage_valid_while_scanning(pool_stripe):
    """While scanning, percentage is a float in [0, 100] or None."""
    lz, pool = pool_stripe
    pool.scan(func=ScanFunction.SCRUB)
    s = pool.scrub_info()
    if s is None or s.state != ScanState.SCANNING:
        pytest.skip('scrub not in SCANNING state')

    pct = s.percentage
    assert pct is None or (isinstance(pct, float) and 0.0 <= pct <= 100.0), \
        f'unexpected percentage value: {pct!r}'



# ---------------------------------------------------------------------------
# Successive scrub() calls return consistent results
# ---------------------------------------------------------------------------

def test_scrub_successive_calls_consistent(pool_after_scrub):
    """Calling scrub() twice in a row returns the same state and func."""
    lz, pool = pool_after_scrub
    s1 = pool.scrub_info()
    s2 = pool.scrub_info()
    assert s1 is not None and s2 is not None
    assert s1.state == s2.state
    assert s1.func == s2.func
