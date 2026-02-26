import os
import pytest
import subprocess
import tempfile
import truenas_pylibzfs

ZPOOLStatus = truenas_pylibzfs.ZPOOLStatus
VDevState = truenas_pylibzfs.enums.VDevState

POOL_NAME = 'testpool_status'
DISK_SZ = 1024 * 1048576


# ---------------------------------------------------------------------------
# Disk / pool helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def make_disks():
    """
    Factory fixture: call make_disks(n) to get a list of n image file paths.
    All files are cleaned up on teardown.
    """
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix='pylibzfs_disks_')
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
        import shutil
        shutil.rmtree(d, ignore_errors=True)


def _create_pool(vdev_args):
    """Create POOL_NAME with the given vdev argument list."""
    subprocess.run(
        ['zpool', 'create', '-f', POOL_NAME] + vdev_args,
        check=True
    )


def _destroy_pool():
    subprocess.run(['zpool', 'destroy', '-f', POOL_NAME], check=False)


def _open_pool():
    lz = truenas_pylibzfs.open_handle()
    pool = lz.open_pool(name=POOL_NAME)
    return lz, pool


# ---------------------------------------------------------------------------
# Pool fixtures
# ---------------------------------------------------------------------------

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
def pool_raidz1(make_disks):
    disks = make_disks(3)
    _create_pool(['raidz'] + disks)
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


@pytest.fixture
def pool_raidz2(make_disks):
    disks = make_disks(4)
    _create_pool(['raidz2'] + disks)
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


@pytest.fixture
def pool_2x_mirror(make_disks):
    disks = make_disks(4)
    _create_pool(['mirror', disks[0], disks[1], 'mirror', disks[2], disks[3]])
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


@pytest.fixture
def pool_2x_raidz1(make_disks):
    disks = make_disks(6)
    _create_pool([
        'raidz', disks[0], disks[1], disks[2],
        'raidz', disks[3], disks[4], disks[5],
    ])
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


@pytest.fixture
def pool_2x_raidz2(make_disks):
    disks = make_disks(8)
    _create_pool([
        'raidz2', disks[0], disks[1], disks[2], disks[3],
        'raidz2', disks[4], disks[5], disks[6], disks[7],
    ])
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


@pytest.fixture
def pool_with_cache(make_disks):
    disks = make_disks(3)
    _create_pool(['mirror', disks[0], disks[1], 'cache', disks[2]])
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


@pytest.fixture
def pool_with_log(make_disks):
    disks = make_disks(3)
    _create_pool(['mirror', disks[0], disks[1], 'log', disks[2]])
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


@pytest.fixture
def pool_multi_support(make_disks):
    """mirror + 2 cache + 2 log + 2 special vdevs"""
    disks = make_disks(8)
    _create_pool([
        'mirror', disks[0], disks[1],
        'cache',  disks[2], disks[3],
        'log',    disks[4], disks[5],
        'special', disks[6], disks[7],
    ])
    lz, pool = _open_pool()
    try:
        yield lz, pool
    finally:
        _destroy_pool()


# ---------------------------------------------------------------------------
# Helper: recursively collect all vdevs (breadth-first)
# ---------------------------------------------------------------------------

def _all_vdevs(vdev):
    """Yield vdev and all descendants."""
    yield vdev
    if vdev.children is not None:
        for child in vdev.children:
            yield from _all_vdevs(child)


# ---------------------------------------------------------------------------
# Struct correctness / types
# ---------------------------------------------------------------------------

def test_status_fields_present(pool_stripe):
    lz, pool = pool_stripe
    status = pool.status()
    assert hasattr(status, 'status')
    assert hasattr(status, 'reason')
    assert hasattr(status, 'action')
    assert hasattr(status, 'message')
    assert hasattr(status, 'corrupted_files')
    assert hasattr(status, 'storage_vdevs')
    assert hasattr(status, 'support_vdevs')


def test_status_enum_type(pool_stripe):
    lz, pool = pool_stripe
    status = pool.status()
    assert isinstance(status.status, ZPOOLStatus)


def test_status_ok_reason_action_none(pool_stripe):
    lz, pool = pool_stripe
    status = pool.status()
    assert status.status == ZPOOLStatus.ZPOOL_STATUS_OK
    assert status.reason is None
    assert status.action is None


def test_status_asdict_keys(pool_stripe):
    lz, pool = pool_stripe
    d = pool.status(asdict=True)
    assert isinstance(d, dict)
    for key in ('status', 'reason', 'action', 'message',
                'corrupted_files', 'storage_vdevs', 'support_vdevs'):
        assert key in d, f'missing key: {key}'

    # storage_vdevs should be a tuple of dicts
    assert isinstance(d['storage_vdevs'], tuple)
    for vdev in d['storage_vdevs']:
        assert isinstance(vdev, dict)
        for vkey in ('name', 'vdev_type', 'guid', 'state', 'stats', 'children'):
            assert vkey in vdev, f'vdev missing key: {vkey}'

    # support_vdevs should be a dict with the four categories
    sv = d['support_vdevs']
    assert isinstance(sv, dict)
    for cat in ('cache', 'log', 'special', 'dedup'):
        assert cat in sv


def test_status_get_stats_false(pool_mirror):
    lz, pool = pool_mirror
    status = pool.status(get_stats=False)
    for top_vdev in status.storage_vdevs:
        for vdev in _all_vdevs(top_vdev):
            assert vdev.stats is None, \
                f'expected stats=None for {vdev.name}, got {vdev.stats}'


def test_status_get_stats_true(pool_mirror):
    lz, pool = pool_mirror
    status = pool.status(get_stats=True)
    for top_vdev in status.storage_vdevs:
        for vdev in _all_vdevs(top_vdev):
            s = vdev.stats
            assert s is not None, f'expected stats for {vdev.name}'
            for field in ('allocated', 'space', 'dspace', 'pspace',
                          'rsize', 'esize', 'read_errors', 'write_errors',
                          'checksum_errors', 'initialize_errors',
                          'dio_verify_errors', 'self_healed_bytes'):
                val = getattr(s, field)
                assert isinstance(val, int) and val >= 0, \
                    f'{vdev.name}.stats.{field} = {val!r}'


def test_vdev_state_enum(pool_mirror):
    lz, pool = pool_mirror
    status = pool.status()
    for top_vdev in status.storage_vdevs:
        for vdev in _all_vdevs(top_vdev):
            assert isinstance(vdev.state, VDevState), \
                f'expected VDevState for {vdev.name}, got {type(vdev.state)}'


def test_vdev_guid_is_int(pool_mirror):
    lz, pool = pool_mirror
    status = pool.status()
    for top_vdev in status.storage_vdevs:
        for vdev in _all_vdevs(top_vdev):
            assert isinstance(vdev.guid, int), \
                f'expected int guid for {vdev.name}, got {type(vdev.guid)}'


def test_corrupted_files_is_tuple(pool_stripe):
    lz, pool = pool_stripe
    status = pool.status()
    assert isinstance(status.corrupted_files, tuple)


# ---------------------------------------------------------------------------
# Topology / storage vdevs
# ---------------------------------------------------------------------------

def test_stripe_topology(pool_stripe):
    lz, pool = pool_stripe
    status = pool.status()
    assert len(status.storage_vdevs) == 1
    vdev = status.storage_vdevs[0]
    assert vdev.vdev_type == 'file'
    assert vdev.children is None


def test_mirror_topology(pool_mirror):
    lz, pool = pool_mirror
    status = pool.status()
    assert len(status.storage_vdevs) == 1
    vdev = status.storage_vdevs[0]
    assert vdev.vdev_type == 'mirror'
    assert vdev.children is not None
    assert len(vdev.children) == 2


def test_raidz1_topology(pool_raidz1):
    lz, pool = pool_raidz1
    status = pool.status()
    assert len(status.storage_vdevs) == 1
    vdev = status.storage_vdevs[0]
    assert vdev.vdev_type == 'raidz1'
    assert vdev.children is not None
    assert len(vdev.children) == 3


def test_raidz2_topology(pool_raidz2):
    lz, pool = pool_raidz2
    status = pool.status()
    assert len(status.storage_vdevs) == 1
    vdev = status.storage_vdevs[0]
    assert vdev.vdev_type == 'raidz2'
    assert vdev.children is not None
    assert len(vdev.children) == 4


def test_2x_mirror_topology(pool_2x_mirror):
    lz, pool = pool_2x_mirror
    status = pool.status()
    assert len(status.storage_vdevs) == 2
    for vdev in status.storage_vdevs:
        assert vdev.vdev_type == 'mirror'
        assert vdev.children is not None
        assert len(vdev.children) == 2


def test_2x_raidz1_topology(pool_2x_raidz1):
    lz, pool = pool_2x_raidz1
    status = pool.status()
    assert len(status.storage_vdevs) == 2
    for vdev in status.storage_vdevs:
        assert vdev.vdev_type == 'raidz1'
        assert vdev.children is not None
        assert len(vdev.children) == 3


def test_2x_raidz2_topology(pool_2x_raidz2):
    lz, pool = pool_2x_raidz2
    status = pool.status()
    assert len(status.storage_vdevs) == 2
    for vdev in status.storage_vdevs:
        assert vdev.vdev_type == 'raidz2'
        assert vdev.children is not None
        assert len(vdev.children) == 4


# ---------------------------------------------------------------------------
# Support vdevs
# ---------------------------------------------------------------------------

def test_support_vdevs_empty(pool_mirror):
    lz, pool = pool_mirror
    sv = pool.status().support_vdevs
    assert sv.cache == ()
    assert sv.log == ()
    assert sv.special == ()
    assert sv.dedup == ()


def test_support_vdevs_single_cache(pool_with_cache):
    lz, pool = pool_with_cache
    sv = pool.status().support_vdevs
    assert len(sv.cache) == 1
    assert sv.log == ()
    assert sv.special == ()
    assert sv.dedup == ()


def test_support_vdevs_single_log(pool_with_log):
    lz, pool = pool_with_log
    sv = pool.status().support_vdevs
    assert len(sv.log) == 1
    assert sv.cache == ()
    assert sv.special == ()
    assert sv.dedup == ()


def test_support_vdevs_multi_cache(pool_multi_support):
    lz, pool = pool_multi_support
    sv = pool.status().support_vdevs
    assert len(sv.cache) == 2


def test_support_vdevs_multi_log(pool_multi_support):
    lz, pool = pool_multi_support
    sv = pool.status().support_vdevs
    assert len(sv.log) == 2


def test_support_vdevs_multi_special(pool_multi_support):
    lz, pool = pool_multi_support
    sv = pool.status().support_vdevs
    assert len(sv.special) == 2


def test_support_vdevs_all_types(pool_multi_support):
    lz, pool = pool_multi_support
    sv = pool.status().support_vdevs
    assert len(sv.cache) > 0
    assert len(sv.log) > 0
    assert len(sv.special) > 0
    assert sv.dedup == ()


# ---------------------------------------------------------------------------
# Status conditions
# ---------------------------------------------------------------------------

def test_status_ok(pool_stripe):
    lz, pool = pool_stripe
    assert pool.status().status == ZPOOLStatus.ZPOOL_STATUS_OK


def test_status_offline_dev(pool_2x_mirror, make_disks):
    lz, pool = pool_2x_mirror

    # Identify a leaf vdev in the first mirror to take offline
    status = pool.status()
    first_mirror = status.storage_vdevs[0]
    victim = first_mirror.children[0]

    subprocess.run(['zpool', 'offline', POOL_NAME, victim.name], check=True)
    try:
        pool.refresh_stats()
        status = pool.status()
        assert status.status == ZPOOLStatus.ZPOOL_STATUS_OFFLINE_DEV
        assert isinstance(status.reason, str) and status.reason
        assert isinstance(status.action, str) and status.action

        # First mirror: one child should be OFFLINE
        first_mirror = status.storage_vdevs[0]
        child_states = [c.state for c in first_mirror.children]
        assert VDevState.OFFLINE in child_states, \
            f'expected OFFLINE child in first mirror, got {child_states}'

        # Second mirror: all children should be ONLINE
        second_mirror = status.storage_vdevs[1]
        for child in second_mirror.children:
            assert child.state == VDevState.ONLINE, \
                f'expected ONLINE in second mirror, got {child.state}'
    finally:
        subprocess.run(['zpool', 'online', POOL_NAME, victim.name], check=False)


# ---------------------------------------------------------------------------
# Slow_ios leaf-only rule
# ---------------------------------------------------------------------------

def test_slow_ios_none_for_parent_vdev(pool_mirror):
    lz, pool = pool_mirror
    status = pool.status(get_stats=True)
    top_vdev = status.storage_vdevs[0]
    assert top_vdev.vdev_type == 'mirror'
    # Parent (mirror) vdev: slow_ios must be None
    assert top_vdev.stats.slow_ios is None, \
        f'expected slow_ios=None for mirror vdev, got {top_vdev.stats.slow_ios}'
    # Children (leaf disk) vdevs: slow_ios must be an int
    for child in top_vdev.children:
        assert isinstance(child.stats.slow_ios, int), \
            f'expected int slow_ios for {child.name}, got {child.stats.slow_ios!r}'
