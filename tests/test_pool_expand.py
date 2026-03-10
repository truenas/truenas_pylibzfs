"""
Tests for ZFSPool.expand_info().

Covers:
  - expand_info() returns None on a pool that has never had a RAIDZ expansion
  - expand_info() returns struct_zpool_expand after a RAIDZ expansion
  - struct_zpool_expand field presence and types
"""

import os
import time

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import enums

from conftest import make_vdev_spec

ScanState = enums.ScanState
VDevType = truenas_pylibzfs.VDevType

POOL_NAME = 'testpool_expand'
ZPOOLProperty = truenas_pylibzfs.libzfs_types.ZPOOLProperty


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def pool_raidz(make_disks):
    """Create a raidz1 pool with 3 disks; yields (lz, pool)."""
    disks = make_disks(4)
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(
        name=POOL_NAME,
        storage_vdevs=[
            truenas_pylibzfs.create_vdev_spec(
                vdev_type=VDevType.RAIDZ1,
                children=[make_vdev_spec(disks[0]), make_vdev_spec(disks[1]),
                          make_vdev_spec(disks[2])],
            )
        ],
        force=True,
    )
    lz.open_resource(name=POOL_NAME).mount()
    pool = lz.open_pool(name=POOL_NAME)
    try:
        yield lz, pool, disks
    finally:
        try:
            lz.destroy_pool(name=POOL_NAME, force=True)
        except Exception:
            pass


@pytest.fixture
def pool_after_expand(pool_raidz):
    """Raidz1 pool that has had a RAIDZ expansion completed."""
    lz, pool, disks = pool_raidz

    # Write some data so there is something to reflow
    chunk = os.urandom(1 << 20)  # 1 MB
    with open(f'/{POOL_NAME}/testfile', 'wb') as f:
        for _ in range(4):
            f.write(chunk)

    # Attach 4th disk to trigger raidz expansion
    pool.attach_vdev(device='raidz1-0', new_device=make_vdev_spec(disks[3]), force=True)

    # Wait for expansion to finish
    for _ in range(300):
        pool = lz.open_pool(name=POOL_NAME)
        exp = pool.expand_info()
        if exp is not None and exp.state != ScanState.SCANNING:
            break
        time.sleep(0.1)

    yield lz, pool


# ---------------------------------------------------------------------------
# expand_info() on a pool that has never been expanded
# ---------------------------------------------------------------------------

def test_expand_returns_none_before_any_expansion(pool_raidz):
    """A freshly created pool with no expansion history returns None."""
    lz, pool, disks = pool_raidz
    assert pool.expand_info() is None


# ---------------------------------------------------------------------------
# struct_zpool_expand field presence and types (after a completed expansion)
# ---------------------------------------------------------------------------

_INT_FIELDS = (
    'expanding_vdev', 'start_time', 'end_time',
    'to_reflow', 'reflowed', 'waiting_for_resilver',
)


def test_expand_returns_struct_after_expansion(pool_after_expand):
    """expand_info() returns a struct_zpool_expand after expansion."""
    lz, pool = pool_after_expand
    exp = pool.expand_info()
    assert exp is not None
    assert type(exp).__name__ == 'struct_zpool_expand'


def test_expand_has_all_fields(pool_after_expand):
    """struct_zpool_expand has all expected attributes."""
    lz, pool = pool_after_expand
    exp = pool.expand_info()
    assert exp is not None
    for attr in ('state', *_INT_FIELDS):
        assert hasattr(exp, attr), f'struct_zpool_expand missing attribute: {attr}'


def test_expand_state_is_scan_state(pool_after_expand):
    """state field is a ScanState enum."""
    lz, pool = pool_after_expand
    assert isinstance(pool.expand_info().state, ScanState)


def test_expand_state_is_finished(pool_after_expand):
    """After waiting for expansion, state should be FINISHED."""
    lz, pool = pool_after_expand
    assert pool.expand_info().state == ScanState.FINISHED


def test_expand_int_fields_are_int(pool_after_expand):
    """All numeric fields are ints."""
    lz, pool = pool_after_expand
    exp = pool.expand_info()
    for attr in _INT_FIELDS:
        assert isinstance(getattr(exp, attr), int), f'{attr} is not int'


def test_expand_start_time_nonzero(pool_after_expand):
    """start_time should be a nonzero unix timestamp."""
    lz, pool = pool_after_expand
    assert pool.expand_info().start_time > 0


def test_expand_end_time_nonzero(pool_after_expand):
    """end_time should be nonzero after a completed expansion."""
    lz, pool = pool_after_expand
    assert pool.expand_info().end_time > 0


def test_expand_to_reflow_nonzero(pool_after_expand):
    """to_reflow should be nonzero since we wrote data before expanding."""
    lz, pool = pool_after_expand
    assert pool.expand_info().to_reflow > 0


def test_expand_reflowed_nonzero(pool_after_expand):
    """reflowed should be nonzero after a completed expansion."""
    lz, pool = pool_after_expand
    assert pool.expand_info().reflowed > 0
