"""Guards ZFS user-namespace ("zone") isolation from the Python API.

A ZFS zone on Linux is a user namespace. The attacker modelled here is a
local user who runs `unshare -Ur` and becomes root inside a namespace of
their own. Not a container: containers are namespaced too, but never get
/dev/zfs, so they cannot reach ZFS at all.

The checks under test are crgetzoneid() in secpolicy_sys_config() and
secpolicy_zinject(), and the name matching in zone_dataset_visible().
Every expected value is what ZFS does today, so a failure means the
behaviour moved. That cuts both ways on purpose: a weakened check is the
regression worth catching, and a stricter one still needs someone to look.

A process cannot leave a user namespace, so no assertion can run in the
pytest process itself. Each one runs in a child, where _CHILD collapses a
single operation to a token and a JSON value. The token carries the
exception class name, because four error shapes have to stay apart:
ZFSException.code is an EZFS_*, lzc.ZFSCoreException.code is a raw errno,
lzc.rollback and lzc.wait raise a bare OSError subclass, and argument
handling raises plain RuntimeError.

RuntimeError itself is never caught: both ZFS classes subclass it, and an
operation that succeeded but failed to write zpool history raises one, so
catching it would report a false denial. A return code from inside a
namespace can lie too, so a denied mutation is confirmed from the global
zone. Nothing is skipped: an unusable host is reported as a failure.
"""

import contextlib
import json
import os
import pytest
import shutil
import signal
import subprocess
import sys
import time
import truenas_pylibzfs
from truenas_pylibzfs import lzc

POOL_NAME = 'testpool_userns'
DEL = POOL_NAME + '/del'
CHILD = DEL + '/child'
PRIV = POOL_NAME + '/priv'
NEIGH = DEL + 'X'            # a name that merely extends DEL, the prefix trap
SHORT = POOL_NAME + '/de'    # and one DEL extends, the trap from the other side
DELSNAP, CHILDSNAP, PRIVSNAP = DEL + '@s', CHILD + '@s', PRIV + '@s'
NSSNAP = DEL + '@ns'
FRESH = ('unshare', '-Urm')  # a namespace with no delegation
FST = truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM

_CHILD = """
import errno, json, os
import truenas_pylibzfs as p
from truenas_pylibzfs import lzc
POOL = %r
DEL, CHILD, PRIV = POOL + '/del', POOL + '/del/child', POOL + '/priv'
NEIGH, SHORT = DEL + 'X', POOL + '/de'
DELSNAP, CHILDSNAP, PRIVSNAP = DEL + '@s', CHILD + '@s', PRIV + '@s'
FST = p.ZFSType.ZFS_TYPE_FILESYSTEM
h = p.open_handle()
value, token = None, 'ALLOW'
try:
    %s
except (p.ZFSException, lzc.ZFSCoreException, OSError) as exc:
    name = getattr(exc, 'name', None) or errno.errorcode.get(getattr(exc, 'errno', None), '?')
    token = type(exc).__name__ + ':' + name
print(token + '\\t' + json.dumps(value))
"""

# thread method, not the default signal method: a wedged ZFS ioctl blocks the
# main thread inside a libzfs C call, where a Python SIGALRM handler cannot
# run. CI's global --timeout=120 uses the signal method, so this file sets its
# own; the 30 s per-subprocess budget does not nest inside it for the two
# CLI-heavy tests, and whichever bound fires first fails the run.
pytestmark = pytest.mark.timeout(timeout=120, method='thread')


def _require(tool):
    if shutil.which(tool) is None:
        raise RuntimeError(f'{tool} was not found on PATH; this file requires it')


def _run(where, argv, timeout=30):
    # start_new_session + killpg, not subprocess.run: unshare and nsenter exec
    # a child of their own, so a timeout would otherwise orphan a wedged probe
    # inside the namespace still holding /dev/zfs open
    cmd = [*where, *argv]
    _require(cmd[0])                  # the namespace-entry tool
    _require(argv[0])                 # the tool itself
    with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, start_new_session=True) as proc:
        try:
            out, err = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            out, err = proc.communicate()
            raise
        return subprocess.CompletedProcess(cmd, proc.returncode, out, err)


def _child(where, op, pool=POOL_NAME):
    r = _run(where, [sys.executable, '-c', _CHILD % (pool, OPS[op])])
    assert r.returncode == 0, f'probe {op} did not run: {r.stderr[-300:]}'
    token, tab, value = r.stdout.partition('\t')
    assert tab, f'probe {op} printed no token line: {r.stdout[-300:]!r}'
    try:
        return token, json.loads(value)
    except ValueError:
        raise AssertionError(f'probe {op} value is not JSON: {value[-200:]!r}')


def _probe(where, op, pool=POOL_NAME):
    return _child(where, op, pool)[0]


def _value(where, op):
    token, value = _child(where, op)
    assert token == 'ALLOW', f'{op} produced no value: {token}'
    return value


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


@pytest.fixture
def layout(pool):
    lz = pool[0]
    lz.create_resource(name=DEL, type=FST, properties={'zoned': 'on'})
    lz.create_resource(name=CHILD, type=FST)
    lz.create_resource(name=NEIGH, type=FST, properties={'zoned': 'on'})
    lz.create_resource(name=SHORT, type=FST, properties={'zoned': 'on'})
    lz.create_resource(name=PRIV, type=FST)
    lzc.create_snapshots(snapshot_names=[DELSNAP, CHILDSNAP, PRIVSNAP])
    return lz


@contextlib.contextmanager
def _userns():
    # `zfs zone` takes an nsfile path, so the namespace must outlive the probes
    _require('unshare')
    ours = os.readlink('/proc/self/ns/user')
    proc = subprocess.Popen(['unshare', '-Urm', 'sleep', '300'], text=True,
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    for _ in range(500):
        if proc.poll() is not None:
            raise RuntimeError(f'unshare(1) exited {proc.returncode}: {proc.stderr.read()[:200]}')
        if os.readlink(f'/proc/{proc.pid}/ns/user') != ours:
            break
        time.sleep(0.01)
    else:
        proc.kill()
        proc.wait()
        raise RuntimeError('unshare(1) produced no user namespace on this host')
    try:
        yield proc.pid
    finally:
        proc.kill()
        proc.wait()


@pytest.fixture
def holder():
    with _userns() as pid:
        yield pid


@pytest.fixture
def delegated(layout, holder):
    nsfile = f'/proc/{holder}/ns/user'
    r = _run((), ['zfs', 'zone', nsfile, DEL])
    assert r.returncode == 0, f'zfs zone failed: {r.stderr}'
    yield ('nsenter', '-t', str(holder), '--all')
    _run((), ['zfs', 'unzone', nsfile, DEL])


@pytest.fixture
def tenant(layout):
    # a second namespace holding NEIGH. a namespace with no delegation leaves
    # zone_dataset_visible at the zds == NULL check; this one reaches the list
    # walk, the only place a prefix or separator slip can leak across tenants
    with _userns() as pid:
        nsfile = f'/proc/{pid}/ns/user'
        r = _run((), ['zfs', 'zone', nsfile, NEIGH])
        assert r.returncode == 0, f'zfs zone failed: {r.stderr}'
        yield ('nsenter', '-t', str(pid), '--all')
        _run((), ['zfs', 'unzone', nsfile, NEIGH])


# probe expressions, one physical statement each, %-interpolated into _CHILD
_ITER = "n = []; h.%s(callback=lambda o, s: s.append(o.name) or True, state=n); value = sorted(n)"
_COMPRESS = "h.open_resource(name=%s).set_properties(properties={p.ZFSProperty.COMPRESSION: 'lz4'})"
_SCRATCH = "s = %s + '/scratch'; h.create_resource(name=s, type=FST); h.destroy_resource(name=s)"
_CHANPROG = "lzc.run_channel_program(pool_name=POOL, script='return 1', readonly=%s)"

OPS = {
    'identity': "value = (os.stat('/proc/self/ns/user').st_ino, os.geteuid())",
    # pool administration: the config family
    'zpool_events': "value = any(True for _ in h.zpool_events(blocking=False))",
    'channel_program_ro': _CHANPROG % True,
    'channel_program_rw': _CHANPROG % False,
    'clear': "h.open_pool(name=POOL).clear()",
    'ddt_prune': "h.open_pool(name=POOL).ddt_prune(percentage=1)",
    'scan_scrub': "h.open_pool(name=POOL).scan(func=p.libzfs_types.ScanFunction.SCRUB)",
    'set_pool_comment':
        "h.open_pool(name=POOL).set_properties(properties={p.ZPOOLProperty.COMMENT: 'x'})",
    'iter_history': "value = len([r for r in h.open_pool(name=POOL).iter_history()])",
    # the whole pool, not one dataset; a separate ioctl registration each
    'export_pool': "h.export_pool(name=POOL)",
    'destroy_pool': "h.destroy_pool(name=POOL, force=True)",
    # no filesystem_properties: mountpoint/sharenfs/sharesmb are refused
    # EZFS_ZONED by zfs_valid_proplist() before the ioctl, which would mask
    # the secpolicy_sys_config refusal this test is for; the default feature@
    # pool props pylibzfs always sends carry no zone check, so this create
    # does reach ZFS_IOC_POOL_CREATE
    'create_pool': "spec = p.create_vdev_spec(vdev_type=p.VDevType.FILE,"
                   " name=os.environ['PROBE_VDEV']);"
                   " h.create_pool(name=POOL + '_stray', storage_vdevs=[spec], force=True)",
    # enumeration, sorted so exact-set comparisons are order-independent
    'iter_pools': _ITER % 'iter_pools',
    'iter_root_fs': _ITER % 'iter_root_filesystems',
    'poolroot_iter_fs': _ITER % 'open_resource(name=POOL).iter_filesystems',
    'del_iter_fs': _ITER % 'open_resource(name=DEL).iter_filesystems',
    'del_iter_snaps': _ITER % 'open_resource(name=DEL).iter_snapshots',
    # visibility
    'open_pool': "value = h.open_pool(name=POOL).name",
    'open_del': "value = h.open_resource(name=DEL).name",
    'open_child': "value = h.open_resource(name=CHILD).name",
    'open_poolroot': "value = h.open_resource(name=POOL).name",
    'open_neigh': "value = h.open_resource(name=NEIGH).name",
    'open_priv': "value = h.open_resource(name=PRIV).name",
    'open_short': "value = h.open_resource(name=SHORT).name",
    'open_privsnap': "value = h.open_resource(name=PRIVSNAP).name",
    # write scope; the create probes clean up after themselves
    'create_under_del': _SCRATCH % 'DEL',
    'create_in_poolroot': _SCRATCH % 'POOL',
    'set_del_compression': _COMPRESS % 'DEL',
    'set_poolroot_compression': _COMPRESS % 'POOL',
    'set_priv_compression': _COMPRESS % 'PRIV',
    'set_del_zoned': "h.open_resource(name=DEL).set_properties("
                     " properties={p.ZFSProperty.ZONED: 'off'})",
    'create_priv_snap': "lzc.create_snapshots(snapshot_names=[PRIV + '@probe'])",
    # snapshots: the separator gap
    'open_delsnap': "value = h.open_resource(name=DELSNAP).name",
    'open_childsnap': "value = h.open_resource(name=CHILDSNAP).name",
    'send_space_delsnap': "value = lzc.send_space(snapname=DELSNAP)",
    'send_space_childsnap': "value = lzc.send_space(snapname=CHILDSNAP)",
    'send_space_privsnap': "value = lzc.send_space(snapname=PRIVSNAP)",
    'hold_privsnap': "lzc.create_holds(holds=[(PRIVSNAP, 'nstag')])",
    'destroy_delsnap': "lzc.destroy_snapshots(snapshot_names=[DELSNAP])",
    'rollback_del': "value = lzc.rollback(resource_name=DEL)",
    'create_del_snap': "lzc.create_snapshots(snapshot_names=[DEL + '@ns'])",
    'hold_delsnap': "lzc.create_holds(holds=[(DELSNAP, 'nstag')])",
    # the ungated surface
    'status': "value = h.open_pool(name=POOL).status().storage_vdevs[0].name",
    'dump_config': "value = h.open_pool(name=POOL).dump_config()"
                   "['vdev_tree']['children'][0]['path']",
    'sync_pool': "h.open_pool(name=POOL).sync_pool()",
    'wait_scrub': "value = lzc.wait(pool_name=POOL, activity=lzc.ZpoolWaitActivity.SCRUB)",
    'wait_bogus_pool': "value = lzc.wait(pool_name='no_such_pool_zz',"
                       " activity=lzc.ZpoolWaitActivity.SCRUB)",
}


# ---------------------------------------------------------------------------
# harness honesty
# ---------------------------------------------------------------------------

def test_probe_runs_in_a_foreign_namespace(layout):
    # kills both false positives: the probe ran globally, or uid unmapped
    inum, uid = _value(FRESH, 'identity')
    assert inum != os.stat('/proc/self/ns/user').st_ino, 'the probe ran in the host namespace'
    assert uid == 0, 'the probe uid is not mapped to root'


# secpolicy_sys_config; all eight were ALLOW before its zone check existed
CONFIG_OPS = [
    ('clear', 'ZFSException:EZFS_PERM'),
    ('ddt_prune', 'ZFSException:EZFS_PERM'),
    ('scan_scrub', 'ZFSException:EZFS_PERM'),          # NONE would cancel an operator's scrub
    ('set_pool_comment', 'ZFSException:EZFS_PERM'),
    ('iter_history', 'ZFSException:EZFS_PERM'),        # host-information leak
    ('zpool_events', 'ZFSException:EZFS_PERM'),
    ('channel_program_ro', 'ZFSCoreException:EPERM'),  # raw errno through lzc
    # one ioctl registration; readonly only flips ZCP_ARG_SYNC
    ('channel_program_rw', 'ZFSCoreException:EPERM'),
]


@pytest.mark.parametrize('op', [c[0] for c in CONFIG_OPS])
def test_global_zone_allows_pool_administration(layout, op):
    # the positive control for both denial tables, and a signature-drift canary
    assert _probe((), op) == 'ALLOW', f'{op} fails in the global zone; the probe is broken'


# ---------------------------------------------------------------------------
# undelegated namespace
# ---------------------------------------------------------------------------
# the reachable case: any local user can enter one of these unaided


# secpolicy_sys_config; the only ops reaching it with no pool
FRESH_OPS = [
    ('zpool_events', 'ZFSException:EZFS_PERM'),        # needs no pool name
    ('channel_program_ro', 'ZFSCoreException:EPERM'),  # straight to the ioctl
    # one ioctl registration; readonly only flips ZCP_ARG_SYNC
    ('channel_program_rw', 'ZFSCoreException:EPERM'),
]


@pytest.mark.parametrize('op,token', FRESH_OPS)
def test_undelegated_namespace_denied_pool_administration(layout, op, token):
    assert _probe(FRESH, op) == token


def test_undelegated_namespace_sees_nothing(layout):
    # enumeration denial has no error shape: spa_all_configs omits the pool
    assert POOL_NAME in _value((), 'iter_pools'), 'the probe enumerates nothing anywhere'
    assert POOL_NAME in _value((), 'iter_root_fs'), 'the probe enumerates nothing anywhere'
    assert _value(FRESH, 'iter_pools') == []
    assert _value(FRESH, 'iter_root_fs') == []
    assert _probe(FRESH, 'open_pool') == 'ZFSException:EZFS_NOENT'
    assert _probe(FRESH, 'rollback_del') == 'FileNotFoundError:ENOENT'


def test_undelegated_namespace_cannot_zone_a_dataset(layout):
    # the headline attack, CLI half: zfs_open's stat ioctl fails
    # zfs_secpolicy_read (no zoneid entry, so ENOENT) and `zfs zone` bails
    # before ZFS_IOC_USERNS_ATTACH is issued at all; the attach gate itself is
    # covered by test_delegated_namespace_cannot_change_its_own_delegation
    r = _run(FRESH, ['zfs', 'zone', '/proc/self/ns/user', POOL_NAME])
    assert r.returncode != 0, 'a namespace attached the pool root to itself'
    assert 'does not exist' in r.stderr
    assert _probe(FRESH, 'open_priv') == 'ZFSException:EZFS_NOENT'


def test_namespace_cannot_inject_faults(delegated):
    # secpolicy_zinject; inject answers EACCES, not EPERM, so the
    # string is 'Permission denied'. zfs_secpolicy_inject passes no dataset
    # name, so a delegation cannot scope the family: denied with and without one
    assert _run((), ['zinject', '-a']).returncode == 0, 'zinject is unusable here'
    for where in (FRESH, delegated):
        r = _run(where, ['zinject', '-a'])
        assert r.returncode != 0, 'a namespace registered a fault injection'
        assert 'Permission denied' in r.stderr


def test_undelegated_namespace_snapshot_destroy_is_a_no_op(layout):
    # destroy_snaps returns 0 for an invisible name; only global state tells
    assert _probe(FRESH, 'destroy_delsnap') == 'ALLOW'
    assert layout.open_resource(name=DELSNAP).name == DELSNAP, 'the snapshot was destroyed'


# create_pool is in no sweep, so this token was measured on its own; removing
# the secpolicy zone check does fail this test. the refusal is
# secpolicy_sys_config's, not libzfs's: zfs_secpolicy_config returns EPERM and
# zpool_create has no EPERM arm of its own
CREATE_POOL_TOKEN = 'ZFSException:EZFS_PERM'


def test_undelegated_namespace_cannot_create_a_pool(make_disks, monkeypatch):
    # the vdev is made outside the namespace, so this turns on the permission
    # check and not on whether a namespace can obtain a disk
    monkeypatch.setenv('PROBE_VDEV', make_disks(1)[0])
    try:
        assert _probe(FRESH, 'create_pool') == CREATE_POOL_TOKEN
    finally:
        with contextlib.suppress(Exception):
            truenas_pylibzfs.open_handle().destroy_pool(name=POOL_NAME + '_stray', force=True)


# ---------------------------------------------------------------------------
# delegation lifecycle
# ---------------------------------------------------------------------------
# from here down the namespace holds a delegated dataset. no consumer of this
# library sets `zoned`, so these guard the ZFS gate, not a shipped path


def test_zone_grants_and_unzone_revokes_visibility(layout, holder):
    # without this pre-check, every delegated test passes with zoning broken
    ns = ('nsenter', '-t', str(holder), '--all')
    nsfile = f'/proc/{holder}/ns/user'
    assert _value(ns, 'iter_root_fs') == [], 'the namespace saw the pool before delegation'
    assert _run((), ['zfs', 'zone', nsfile, DEL]).returncode == 0
    try:
        assert _value(ns, 'iter_root_fs') == [POOL_NAME], 'zfs zone granted nothing'
        assert _run((), ['zfs', 'unzone', nsfile, DEL]).returncode == 0
        assert _value(ns, 'iter_root_fs') == [], 'the delegation survived zfs unzone'
    finally:
        # spl-zone.c has no userns-exit hook, so a failed oracle above would
        # leave the entry and its pinned user_namespace until module unload
        _run((), ['zfs', 'unzone', nsfile, DEL])


# ---------------------------------------------------------------------------
# delegated namespace: pool administration
# ---------------------------------------------------------------------------

@pytest.mark.parametrize('op,token', CONFIG_OPS)
def test_delegated_namespace_denied_pool_administration(delegated, op, token):
    # harder than the fresh table: the pool is visible, so ENOENT masks nothing
    assert _probe(delegated, op) == token


@pytest.fixture
def spare(make_pool, holder):
    # a throwaway pool: on a regression these two ops really do take it away.
    # mountpoint=none because export and destroy must be possible for a
    # permitted caller; on a mounted pool they fail EZFS_BUSY, a pool-state
    # error rather than a permission one, and the test would prove nothing
    name = POOL_NAME + '_spare'
    nsfile = f'/proc/{holder}/ns/user'
    lz, _, root = make_pool(name)
    root.set_properties(properties={truenas_pylibzfs.ZFSProperty.MOUNTPOINT: 'none'})
    lz.create_resource(name=name + '/del', type=FST, properties={'zoned': 'on'})
    r = _run((), ['zfs', 'zone', nsfile, name + '/del'])
    assert r.returncode == 0, f'zfs zone failed: {r.stderr}'
    yield name
    # destroying the pool does not release the entry: spl-zone.c has no
    # userns-exit hook, so the delegation and its pinned user_namespace would
    # outlive the pool until the module unloads
    _run((), ['zfs', 'unzone', nsfile, name + '/del'])


@pytest.mark.parametrize('op', ['export_pool', 'destroy_pool'])
def test_delegated_namespace_cannot_take_the_whole_pool(spare, holder, op):
    # escalation from one dataset to the pool: EZFS_BUSY here would mean the
    # pool was still mounted and nothing was tested
    ns = ('nsenter', '-t', str(holder), '--all')
    assert _probe(ns, op, pool=spare) == 'ZFSException:EZFS_PERM'


def test_delegated_namespace_cannot_change_its_own_delegation(delegated):
    # attach's only gate below secpolicy is uid_eq(GLOBAL_ROOT_UID); -Ur passes
    r = _run(delegated, ['zfs', 'zone', '/proc/self/ns/user', POOL_NAME])
    assert r.returncode != 0, 'a namespace attached the pool root to itself'
    assert 'permission denied' in r.stderr
    r = _run(delegated, ['zfs', 'unzone', '/proc/self/ns/user', DEL])
    assert r.returncode != 0
    assert 'permission denied' in r.stderr
    r = _run(delegated, ['zfs', 'zone', '/proc/self/ns/user', PRIV])
    assert 'does not exist' in r.stderr  # an invisible target leaks nothing
    assert _probe(delegated, 'open_priv') == 'ZFSException:EZFS_NOENT', 'the self-attach took'


def test_delegated_namespace_can_grant_only_on_its_own_dataset(delegated):
    # set_fsacl's secpolicy is only zfs_dozonecheck; CAP_SYS_ADMIN is namespaced, so
    # secpolicy_zfs passes and dsl_deleg_can_allow never runs. DEL is writable; the
    # pool root is visible but zoned=off, which is the EPERM arm
    assert _run(delegated, ['zfs', 'allow', '-u', 'root', 'snapshot', DEL]).returncode == 0
    assert 'snapshot' in _run((), ['zfs', 'allow', DEL]).stdout, 'the grant did not take'
    r = _run(delegated, ['zfs', 'allow', '-u', 'root', 'destroy', POOL_NAME])
    assert r.returncode != 0, 'a namespace granted itself permissions on the pool root'
    assert 'destroy' not in _run((), ['zfs', 'allow', POOL_NAME]).stdout, 'the grant took'


# ---------------------------------------------------------------------------
# delegated namespace: confinement
# ---------------------------------------------------------------------------

def test_delegated_namespace_enumerates_only_its_subtree(delegated, layout):
    # the prefix traps pass vacuously if layout loses them: an absent name is
    # not enumerated, and opening one is ENOENT for the wrong reason
    for t in (NEIGH, SHORT):
        assert layout.open_resource(name=t).name == t, f'prefix trap {t} is gone'
    # exact sets: `in` passes on a whole-pool leak; delX appears if '/' relaxes
    assert _value(delegated, 'iter_pools') == [POOL_NAME]
    assert _value(delegated, 'poolroot_iter_fs') == [DEL]
    assert _value(delegated, 'del_iter_fs') == [CHILD]
    # list-yes / open-no is the shape of the '@' gap; a separator fix written
    # as a blanket denial on the enumeration path breaks this line
    assert _value(delegated, 'del_iter_snaps') == [DELSNAP]


VISIBLE = [
    ('open_del', 'ALLOW'),                      # positive control
    ('open_child', 'ALLOW'),                    # '/'-children are reachable
    ('open_poolroot', 'ALLOW'),                 # the parent stays visible
    ('open_neigh', 'ZFSException:EZFS_NOENT'),  # a shared prefix is no path
    ('open_short', 'ZFSException:EZFS_NOENT'),  # nor is being one
    ('open_priv', 'ZFSException:EZFS_NOENT'),   # invisible, never EPERM
    ('open_privsnap', 'ZFSException:EZFS_NOENT'),
    ('send_space_privsnap', 'ZFSCoreException:ENOENT'),  # zfs_secpolicy_read
    ('hold_privsnap', 'ZFSCoreException:ENOENT'),        # a hold is a DoS primitive
]


@pytest.mark.parametrize('op,token', VISIBLE)
def test_delegated_namespace_sees_only_its_own_datasets(delegated, op, token):
    # guards the memcmp plus both '/' bytes: dataset[zd_len] and zd_dsname[dsnamelen]
    assert _probe(delegated, op) == token


WRITE_SCOPE = [
    ('create_under_del', 'ALLOW'),                          # positive control
    ('set_del_compression', 'ALLOW'),
    ('set_del_zoned', 'ZFSException:EZFS_PERM'),             # ZFS_PROP_ZONED: !INGLOBALZONE
    ('create_in_poolroot', 'ZFSException:EZFS_PERM'),       # libzfs check_parents: parent !zoned
    ('set_poolroot_compression', 'ZFSException:EZFS_PERM'),  # the same !zoned arm, in-kernel
    ('set_priv_compression', 'ZFSException:EZFS_NOENT'),    # invisible stays hidden
    ('create_priv_snap', 'ZFSCoreException:ENOENT'),        # the raw-errno shape
    # the poolroot EPERM rows are decided by zoned; its !writable arm is unreachable
]


@pytest.mark.parametrize('op,token', WRITE_SCOPE)
def test_delegated_namespace_write_scope(delegated, op, token):
    # the kernel checks visibility before writability, so an invisible name is ENOENT,
    # never EPERM
    assert _probe(delegated, op) == token


def test_nested_namespace_loses_the_delegation(delegated):
    # the lookup keys on the exact ns inum, so unsharing again forfeits it
    nested = (*delegated, 'unshare', '-Urm')
    assert _value(nested, 'iter_root_fs') == [], 'a nested namespace inherited the delegation'
    assert _probe(nested, 'open_del') == 'ZFSException:EZFS_NOENT'


def test_two_delegations_never_see_each_other(delegated, tenant):
    # DEL and NEIGH share a prefix and are held by different namespaces, so a
    # relaxed separator byte leaks one tenant's dataset into the other
    assert _probe(delegated, 'open_del') == 'ALLOW', 'the first delegation is not live'
    assert _probe(tenant, 'open_neigh') == 'ALLOW', 'the second delegation is not live'
    assert _probe(tenant, 'open_del') == 'ZFSException:EZFS_NOENT'
    assert _probe(delegated, 'open_neigh') == 'ZFSException:EZFS_NOENT'


def test_peer_namespace_never_sees_another_namespaces_delegation(delegated):
    # a sibling of the holder, spawned while the delegation is live: the
    # lookup keys on the exact ns inum, so a namespace at the same nesting
    # level as the holder must match nothing
    assert _value(FRESH, 'iter_root_fs') == [], 'a peer namespace saw the delegation'
    assert _probe(FRESH, 'open_del') == 'ZFSException:EZFS_NOENT'


def test_delegated_namespace_can_snapshot_and_hold_its_own_dataset(delegated, layout):
    # both trim the name before the visibility check, so they work where
    # opening the same snapshot does not; an early separator fix that checked
    # spa_open_common() saw the untrimmed name and broke exactly this, so
    # these are what separate the '@' gap from a blanket denial of '@'
    assert _probe(delegated, 'create_del_snap') == 'ALLOW'
    assert layout.open_resource(name=NSSNAP).name == NSSNAP, 'the snapshot was not created'
    assert _probe(delegated, 'hold_delsnap') == 'ALLOW'
    assert 'nstag' in layout.open_resource(name=DELSNAP).get_holds(), 'the hold did not take'


# ---------------------------------------------------------------------------
# the separator gap
# ---------------------------------------------------------------------------

# open gap: only '/' is a separator, so a delegated dataset cannot reach its
# own snapshot. wherever zone_dataset_visible() also accepts '@' and '#',
# the first three rows fail, which is the signal to update them. The last
# two are the controls.
# get_holds/clone of DELSNAP are the same zfs_open() failure as the first row
# today; add clone_delsnap_into_poolroot (-> PERM, a second gate) if it lands.
SEPARATOR = [
    ('open_delsnap', 'ZFSException:EZFS_NOENT'),
    ('send_space_delsnap', 'ZFSCoreException:ENOENT'),  # raw errno through lzc
    # the return code lies: secpolicy drops an ENOENT name as 'already
    # destroyed'; once '@' is a separator the destroy is real, the token is
    # still ALLOW, and the second assert below is the only thing that notices
    ('destroy_delsnap', 'ALLOW'),
    ('open_childsnap', 'ALLOW'),                        # control: a reachable snap
    ('send_space_childsnap', 'ALLOW'),                  # control
]


@pytest.mark.parametrize('op,token', SEPARATOR)
def test_delegated_namespace_cannot_reach_its_own_snapshot(delegated, layout, op, token):
    assert _probe(delegated, op) == token
    assert layout.open_resource(name=DELSNAP).name == DELSNAP, 'the snapshot was destroyed'


# ---------------------------------------------------------------------------
# holes, not contracts: a failure here may be good news
# ---------------------------------------------------------------------------

def test_namespace_reaches_the_ungated_surface(delegated):
    # inverted polarity: closing one must be a deliberate, visible event
    # both hand back a host vdev path. equality catches a redaction scoped to
    # the namespace, the leading '/' one applied to both zones; a gated call is
    # caught by _value itself. asserting ALLOW alone would notice none of them
    for op in ('status', 'dump_config'):
        leaked = _value(delegated, op)
        assert leaked == _value((), op), f'{op} is redacted for the namespace; update this row'
        assert leaked.startswith('/'), f'{op} no longer yields a host path; update this row'
    assert _probe(delegated, 'sync_pool') == 'ALLOW', 'POOL_SYNC is now gated; update this row'
    # wait_scrub returns False everywhere, so its ALLOW is only evidence while a
    # bogus name still errors: the ioctl framing layer resolves the name in
    # pool_status_check's spa_open, before secpolicy or zfs_ioc_wait ever run
    assert _probe(FRESH, 'wait_bogus_pool') == 'FileNotFoundError:ENOENT'
    assert _probe(FRESH, 'wait_scrub') == 'ALLOW', 'ZFS_IOC_WAIT is now gated; update this row'


# ---------------------------------------------------------------------------
# global zone vs a zoned dataset
# ---------------------------------------------------------------------------

def test_zoned_dataset_refuses_global_zone_mountpoint(layout):
    # the userland half of the contract, and the layout fixture's self-check
    with pytest.raises(truenas_pylibzfs.ZFSException) as exc:
        layout.open_resource(name=CHILD).set_properties(
            properties={truenas_pylibzfs.ZFSProperty.MOUNTPOINT: '/mnt/userns'})
    assert exc.value.code == truenas_pylibzfs.ZFSError.EZFS_ZONED


def test_zoned_dataset_refuses_global_zone_rename(layout):
    # a second libzfs zone gate: patching only set_properties is still caught
    with pytest.raises(truenas_pylibzfs.ZFSException) as exc:
        layout.open_resource(name=DEL).rename(new_name=DEL + 'r')
    assert exc.value.code == truenas_pylibzfs.ZFSError.EZFS_ZONED
