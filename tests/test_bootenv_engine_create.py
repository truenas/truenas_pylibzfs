"""Tests for truenas_bootenv.engine.create (needs root and zfs.ko).

The boot environment fixture mirrors the layout captured from a real
TrueNAS 27.0 system: varied per-child local properties, a child whose
mountpoint does not mirror its dataset path, posix acltype on one
grandchild, readonly=on on another child.
"""

import errno
import pytest
import truenas_pylibzfs
from truenas_bootenv import engine, naming
from truenas_bootenv.errors import BEError, BEExists, BENotFound

FST = truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
POOL = 'bootenvcreate'
KVER = '6.18.35-production+truenas'

BE2 = f'{POOL}/ROOT/be2'

ALL_CHILDREN = (
    '', '/conf', '/var', '/var/ca-certificates', '/var/log',
    '/var/log/journal',
)


def _mkfs(lz, name, **props):
    properties = {'canmount': 'noauto'}
    properties.update(props)
    lz.create_resource(name=name, type=FST, properties=properties)


def _props_with_source(lz, name, wanted):
    a = lz.open_resource(name=name).asdict(
        properties=truenas_pylibzfs.property_sets.ZFS_FILESYSTEM_PROPERTIES,
        get_source=True,
    )
    out = {}
    for prop, entry in a['properties'].items():
        if prop in wanted and entry:
            out[prop] = (entry['raw'], entry['source']['type'])
    return out


@pytest.fixture
def be_layout(make_pool):
    """Real-system BE layout: see the module docstring."""
    lz, pool_hdl, _root = make_pool(POOL)
    lz.create_resource(
        name=f'{POOL}/ROOT', type=FST, properties={'canmount': 'off'},
    )
    lz.open_resource(name=f'{POOL}/ROOT').set_user_properties(
        user_properties={'org.zectl:bootloader': 'grub'},
    )
    be1 = f'{POOL}/ROOT/be1'
    _mkfs(lz, be1, mountpoint='legacy')
    _mkfs(lz, f'{be1}/conf', mountpoint='/conf', readonly='on',
          exec='off', setuid='off')
    _mkfs(lz, f'{be1}/var', mountpoint='/var', atime='off', devices='off')
    _mkfs(lz, f'{be1}/var/ca-certificates',
          mountpoint='/var/local/ca-certificates')
    _mkfs(lz, f'{be1}/var/log', mountpoint='/var/log', exec='off')
    _mkfs(lz, f'{be1}/var/log/journal', mountpoint='/var/log/journal',
          acltype='posix', aclmode='discard')
    lz.open_resource(name=be1).set_user_properties(
        user_properties={
            'truenas:kernel_version': KVER,
            'zectl:keep': 'False',
        },
    )
    return lz, pool_hdl, be1


def test_creates_full_mirrored_subtree(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    for rel in ALL_CHILDREN:
        assert engine._exists(lz, f'{BE2}{rel}'), rel


def test_local_properties_copied_verbatim(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    local = truenas_pylibzfs.PropertySource.LOCAL
    wanted = ('mountpoint', 'readonly', 'exec', 'atime', 'acltype',
              'canmount')
    checks = (
        (f'{BE2}', 'mountpoint', 'legacy'),
        (f'{BE2}/conf', 'readonly', 'on'),
        (f'{BE2}/conf', 'exec', 'off'),
        (f'{BE2}/var', 'atime', 'off'),
        (f'{BE2}/var/ca-certificates', 'mountpoint',
         '/var/local/ca-certificates'),
        (f'{BE2}/var/log/journal', 'acltype', 'posix'),
    )
    for name, prop, expected in checks:
        got = _props_with_source(lz, name, wanted)
        assert got[prop] == (expected, local), (name, prop, got.get(prop))


def test_canmount_forced_noauto_everywhere(be_layout):
    lz, _pool_hdl, be1 = be_layout
    # drift the source: be1/var canmount=on must NOT propagate
    lz.open_resource(name=f'{be1}/var').set_properties(
        properties={'canmount': 'on'}, remount=False,
    )
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    local = truenas_pylibzfs.PropertySource.LOCAL
    for rel in ALL_CHILDREN:
        got = _props_with_source(lz, f'{BE2}{rel}', ('canmount',))
        assert got['canmount'] == ('noauto', local), rel


def test_kernel_version_copied_and_keep_not(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    uprops = lz.open_resource(name=BE2).get_user_properties()
    assert uprops.get('truenas:kernel_version') == KVER
    assert 'zectl:keep' not in uprops


def test_inherited_user_props_not_pinned_locally(be_layout):
    # org.zectl:bootloader is inherited from <pool>/ROOT on the source;
    # the clone must keep inheriting it rather than pin a local copy
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    inherited = lz.open_resource(name=BE2).get_user_properties()
    assert inherited.get('org.zectl:bootloader') == 'grub'
    # a local pin would survive inheritance changes; prove it did not:
    lz.open_resource(name=f'{POOL}/ROOT').set_user_properties(
        user_properties={'org.zectl:bootloader': 'changed'},
    )
    assert lz.open_resource(name=BE2).get_user_properties().get(
        'org.zectl:bootloader') == 'changed'


def test_inherited_native_props_keep_inheriting(be_layout):
    lz, _pool_hdl, be1 = be_layout
    lz.open_resource(name=f'{POOL}/ROOT').set_properties(
        properties={'compression': 'zstd'}, remount=False,
    )
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    got = _props_with_source(lz, BE2, ('compression',))
    assert got['compression'][1] == truenas_pylibzfs.PropertySource.INHERITED


def test_local_quota_none_does_not_break_the_clone(be_layout):
    # zfs set quota=none stores the value zero with LOCAL source, and a
    # numeric zero in a clone property list is refused by libzfs with
    # "use 'none' to disable quota/refquota". A real system reaches this
    # state whenever a quota is set and later cleared (the middleware
    # audit dataset does exactly that), so the clone must translate it
    lz, _pool_hdl, be1 = be_layout
    lz.open_resource(name=f'{be1}/var').set_properties(
        properties={'quota': 'none', 'refquota': 'none'}, remount=False,
    )
    lz.open_resource(name=f'{be1}/var/log').set_properties(
        properties={'refquota': str(1024 ** 3)}, remount=False,
    )
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    local = truenas_pylibzfs.PropertySource.LOCAL
    got = _props_with_source(lz, f'{BE2}/var', ('quota', 'refquota'))
    assert got['quota'] == ('0', local)
    assert got['refquota'] == ('0', local)
    # a genuine refquota value still copies verbatim
    got = _props_with_source(lz, f'{BE2}/var/log', ('refquota',))
    assert got['refquota'] == (str(1024 ** 3), local)


def test_activated_count_tracking_does_not_break_the_clone(be_layout):
    # setting a filesystem or snapshot limit permanently activates ZFS
    # count tracking for the subtree: from then on the READONLY
    # filesystem_count/snapshot_count properties appear with LOCAL
    # source on every newly created dataset under it. A create property
    # list containing a readonly property is refused outright, so the
    # collector must exclude readonly properties the way zectl's
    # clone_prop_cb did
    lz, _pool_hdl, be1 = be_layout
    lz.open_resource(name=be1).set_properties(
        properties={'filesystem_limit': '100'}, remount=False,
    )
    _mkfs(lz, f'{be1}/opt', mountpoint='/opt')
    # the precondition this test exists for: a child created after
    # activation really carries the readonly counts with LOCAL source
    got = _props_with_source(
        lz, f'{be1}/opt', ('filesystem_count', 'snapshot_count'),
    )
    local = truenas_pylibzfs.PropertySource.LOCAL
    assert 'filesystem_count' in got and 'snapshot_count' in got, got
    assert got['filesystem_count'][1] == local
    assert got['snapshot_count'][1] == local
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    assert engine._exists(lz, f'{BE2}/opt')


def test_source_snapshots_remain_as_clone_origins(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    origin = engine._origin(lz.open_resource(name=BE2))
    assert origin is not None and origin.startswith(f'{be1}@')


def test_missing_source_raises(be_layout):
    lz, _pool_hdl, _be1 = be_layout
    with pytest.raises(BENotFound):
        engine.create(lz, source_dataset=f'{POOL}/ROOT/ghost',
                      target_dataset=BE2)


def test_existing_target_refused_without_writes(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    before = {s.name for s in engine._snapshots(
        lz.open_resource(name=be1))}
    with pytest.raises(BEExists):
        engine.create(lz, source_dataset=be1, target_dataset=BE2)
    after = {s.name for s in engine._snapshots(
        lz.open_resource(name=be1))}
    assert before == after


@pytest.mark.parametrize('absence', ['dash', 'empty', 'missing'])
def test_kernel_less_source_refused(be_layout, absence):
    # all three spellings of a missing kernel version must refuse:
    # the literal '-' marker, an empty value, and the property never
    # having been set at all
    lz, _pool_hdl, be1 = be_layout
    rsrc = lz.open_resource(name=be1)
    if absence == 'dash':
        rsrc.set_user_properties(
            user_properties={'truenas:kernel_version': '-'},
        )
    elif absence == 'empty':
        rsrc.set_user_properties(
            user_properties={'truenas:kernel_version': ''},
        )
    else:
        rsrc.inherit_property(property='truenas:kernel_version')
    with pytest.raises(BEError):
        engine.create(lz, source_dataset=be1, target_dataset=BE2)
    assert not engine._exists(lz, BE2)


def test_hostile_kernel_source_refused(be_layout):
    # a source whose kernel version the boot menu generator cannot
    # embed safely must refuse to be cloned, not propagate the value
    # onto a new boot environment
    lz, _pool_hdl, be1 = be_layout
    lz.open_resource(name=be1).set_user_properties(
        user_properties={'truenas:kernel_version': "6.1'; echo pwned"},
    )
    with pytest.raises(BEError, match='embed safely'):
        engine.create(lz, source_dataset=be1, target_dataset=BE2)
    assert not engine._exists(lz, BE2)


def test_bad_paths_refused(be_layout):
    lz, _pool_hdl, be1 = be_layout
    with pytest.raises(BEError):
        engine.create(lz, source_dataset=be1, target_dataset=POOL)
    with pytest.raises(BEError):
        engine.create(lz, source_dataset=POOL, target_dataset=BE2)


def test_snapshot_suffix_collision_bumped(be_layout, monkeypatch):
    lz, _pool_hdl, be1 = be_layout
    fixed = '2026-07-03-10:00:00.000001'
    monkeypatch.setattr(naming, 'snapshot_suffix', lambda now_ns=None: fixed)
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={f'{be1}@{fixed}'})
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    origin = engine._origin(lz.open_resource(name=BE2))
    assert origin == f'{be1}@{fixed}-1'


def test_failure_cleans_up_target_and_snapshots(be_layout, monkeypatch):
    lz, _pool_hdl, be1 = be_layout
    real = engine._clone_props
    calls = {'n': 0}

    def explode_on_third(dataset):
        calls['n'] += 1
        if calls['n'] == 3:
            raise RuntimeError('injected failure')
        return real(dataset)

    monkeypatch.setattr(engine, '_clone_props', explode_on_third)
    with pytest.raises(RuntimeError, match='injected failure'):
        engine.create(lz, source_dataset=be1, target_dataset=BE2)
    assert not engine._exists(lz, BE2)
    leftover = [s.name for s in engine._snapshots(
        lz.open_resource(name=be1))]
    assert leftover == []


def test_child_level_suffix_collision_also_bumped(be_layout, monkeypatch):
    # the collision scan must cover the WHOLE subtree: a stale snapshot
    # on a child dataset alone forces the bump
    lz, _pool_hdl, be1 = be_layout
    fixed = '2026-07-05-10:00:00.000001'
    monkeypatch.setattr(naming, 'snapshot_suffix', lambda now_ns=None: fixed)
    truenas_pylibzfs.lzc.create_snapshots(
        snapshot_names={f'{be1}/var@{fixed}'},
    )
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    origin = engine._origin(lz.open_resource(name=BE2))
    assert origin == f'{be1}@{fixed}-1'


class _FlakyDestroyHandle:
    """The real handle, but the first destroy_resource blows up.

    destroy_resource cannot be monkeypatched onto the C extension type,
    so create() is handed this delegating wrapper instead.
    """

    def __init__(self, real):
        self._real = real
        self.attempts = []
        self.failed_once = False

    def __getattr__(self, name):
        return getattr(self._real, name)

    def destroy_resource(self, *, name):
        self.attempts.append(name)
        if not self.failed_once:
            self.failed_once = True
            raise RuntimeError('injected cleanup failure')
        return self._real.destroy_resource(name=name)


def test_cleanup_continues_past_a_failing_victim(be_layout, monkeypatch):
    # per-victim isolation: one failing destroy during cleanup must not
    # abort the walk over the remaining victims, and must never mask the
    # original create error
    lz, _pool_hdl, be1 = be_layout
    real_clone_props = engine._clone_props
    calls = {'n': 0}

    def explode_on_third(dataset):
        calls['n'] += 1
        if calls['n'] == 3:
            raise RuntimeError('injected create failure')
        return real_clone_props(dataset)

    monkeypatch.setattr(engine, '_clone_props', explode_on_third)
    handle = _FlakyDestroyHandle(lz)
    with pytest.raises(RuntimeError, match='injected create failure'):
        engine.create(handle, source_dataset=be1, target_dataset=BE2)

    # the injected cleanup failure really fired, and the original create
    # error still surfaced rather than being masked by it
    assert handle.failed_once
    # two datasets were cloned before the third failed, and the cleanup
    # attempted BOTH: the first one raised, the walk carried on anyway
    assert len(handle.attempts) == 2
    # leaf-first, so the boot environment root goes last
    assert handle.attempts[-1] == BE2


def test_cleanup_reclaims_snapshots_when_the_snapshot_call_raised_late(
        be_layout, monkeypatch):
    # lzc writes the pool history only after the snapshots exist, so
    # create_snapshots can raise with them already on disk. They must
    # still be reclaimed, or every source snapshot leaks forever
    lz, _pool_hdl, be1 = be_layout
    real = engine._lzc_create_snapshots

    def raises_after_creating(names):
        real(names)                       # the snapshots now exist
        raise RuntimeError('injected post-snapshot history failure')

    monkeypatch.setattr(engine, '_lzc_create_snapshots', raises_after_creating)
    with pytest.raises(RuntimeError, match='post-snapshot history failure'):
        engine.create(lz, source_dataset=be1, target_dataset=BE2)

    assert engine._snapshots(lz.open_resource(name=be1)) == []


def test_cleanup_leaves_a_racers_snapshots_alone(be_layout, monkeypatch):
    # a name collision means someone else already holds those snapshot
    # names. They are not ours, so the rollback must not reclaim them --
    # this is why the created flag cannot simply be set before the call
    lz, _pool_hdl, be1 = be_layout
    fixed = '2026-07-13-10:00:00.000001'
    monkeypatch.setattr(naming, 'snapshot_suffix', lambda now_ns=None: fixed)
    real = engine._lzc_create_snapshots

    def collides(names):
        # the racer takes the names after our bump loop checked them
        truenas_pylibzfs.lzc.create_snapshots(snapshot_names=set(names))
        real(names)                       # now genuinely collides -> BEExists

    monkeypatch.setattr(engine, '_lzc_create_snapshots', collides)
    # a snapshot-suffix collision is system state, not the caller's input,
    # so it stays a plain BEError carrying EEXIST (middleware -> CallError),
    # NOT a BEExists (which means the clone target already exists)
    with pytest.raises(BEError) as excinfo:
        engine.create(lz, source_dataset=be1, target_dataset=BE2)
    assert not isinstance(excinfo.value, BEExists)
    assert excinfo.value.errno == errno.EEXIST

    # the racer's snapshots survive untouched
    survivors = [s.name for s in engine._snapshots(lz.open_resource(name=be1))]
    assert f'{be1}@{fixed}' in survivors


def test_cleanup_never_masks_the_original_error(be_layout, monkeypatch):
    # the finally block probes ZFS to see which snapshots survived. If that
    # probe itself fails (a suspended pool, an I/O error) it must not escape
    # and replace the real reason create() failed
    lz, _pool_hdl, be1 = be_layout
    real_props = engine._clone_props
    real_exists = engine._exists
    # _exists also runs in create()'s own pre-checks, so only start failing
    # it once the create has actually broken and cleanup is under way
    state = {'n': 0, 'cleaning_up': False}

    def explode_on_third(dataset):
        state['n'] += 1
        if state['n'] == 3:
            state['cleaning_up'] = True
            raise RuntimeError('the real failure')
        return real_props(dataset)

    def probe(lzh, name):
        if state['cleaning_up']:
            raise RuntimeError('cleanup probe blew up')
        return real_exists(lzh, name)

    monkeypatch.setattr(engine, '_clone_props', explode_on_third)
    monkeypatch.setattr(engine, '_exists', probe)
    # the caller must hear the real reason, not how the cleanup went
    with pytest.raises(RuntimeError, match='the real failure'):
        engine.create(lz, source_dataset=be1, target_dataset=BE2)


def test_rollback_runs_on_a_base_exception(be_layout, monkeypatch):
    # the binding's history retry loop calls PyErr_CheckSignals, which
    # raises KeyboardInterrupt AFTER the ZFS action has committed. That is
    # a BaseException, so an except-Exception rollback guard would skip
    # cleanup and strand the half-built target plus every source snapshot
    lz, _pool_hdl, be1 = be_layout
    real_props = engine._clone_props
    calls = {'n': 0}

    def interrupt_on_third(dataset):
        calls['n'] += 1
        if calls['n'] == 3:
            raise KeyboardInterrupt('injected Ctrl-C')
        return real_props(dataset)

    monkeypatch.setattr(engine, '_clone_props', interrupt_on_third)
    with pytest.raises(KeyboardInterrupt):
        engine.create(lz, source_dataset=be1, target_dataset=BE2)

    # the rollback ran anyway: no half-built target, no leaked snapshots
    assert not engine._exists(lz, BE2)
    assert engine._snapshots(lz.open_resource(name=be1)) == []


def test_cleanup_undoes_a_clone_that_raised_after_creating_it(
        be_layout, monkeypatch):
    # ZFSSnapshot.clone() writes the pool history AFTER zfs_clone() has
    # already made the dataset, so it can raise with the clone on disk.
    # The rollback must still undo it, or the target is stranded and every
    # retry fails with BEExists
    lz, _pool_hdl, be1 = be_layout
    real_props = engine._clone_props

    class _CloneRaisesAfterCreating:
        def __init__(self, real):
            self._real = real

        def __getattr__(self, name):
            return getattr(self._real, name)

        def open_resource(self, *, name):
            resource = self._real.open_resource(name=name)
            if '@' not in name:
                return resource
            return _Snap(self._real, resource)

    class _Snap:
        def __init__(self, real_lzh, real_snap):
            self._real_lzh = real_lzh
            self._real = real_snap

        def __getattr__(self, name):
            return getattr(self._real, name)

        def clone(self, *, name, properties):
            self._real.clone(name=name, properties=properties)
            # the dataset now exists; the binding raises anyway
            raise RuntimeError('injected post-clone history failure')

    monkeypatch.setattr(engine, '_clone_props', real_props)
    with pytest.raises(RuntimeError, match='post-clone history failure'):
        engine.create(
            _CloneRaisesAfterCreating(lz),
            source_dataset=be1, target_dataset=BE2,
        )

    # the clone really was created, and the rollback really removed it
    assert not engine._exists(lz, BE2)
    assert engine._snapshots(lz.open_resource(name=be1)) == []


class _DeadDestroyHandle:
    """The real handle, but every destroy_resource fails."""

    def __init__(self, real):
        self._real = real

    def __getattr__(self, name):
        return getattr(self._real, name)

    def destroy_resource(self, *, name):
        raise RuntimeError('injected cleanup failure')


def test_cleanup_reclaims_the_snapshots_it_still_can(be_layout, monkeypatch):
    # when a clone survives the rollback it keeps pinning its own source
    # snapshot, which fails the batch destroy. The source snapshots that
    # nothing pins must still be reclaimed rather than all of them leaking
    lz, _pool_hdl, be1 = be_layout
    real_clone_props = engine._clone_props
    calls = {'n': 0}

    def explode_on_third(dataset):
        calls['n'] += 1
        if calls['n'] == 3:
            raise RuntimeError('injected create failure')
        return real_clone_props(dataset)

    monkeypatch.setattr(engine, '_clone_props', explode_on_third)
    with pytest.raises(RuntimeError, match='injected create failure'):
        engine.create(
            _DeadDestroyHandle(lz), source_dataset=be1, target_dataset=BE2,
        )

    # two clones were made and none could be destroyed, so exactly those
    # two source snapshots stay pinned; every other one is reclaimed
    survivors = []
    for fs in engine._walk_topdown(lz.open_resource(name=be1), []):
        survivors.extend(s.name for s in engine._snapshots(fs))
    assert len(survivors) == 2
    for name in survivors:
        assert lz.open_resource(name=name).get_clones()


def test_cleanup_does_not_destroy_a_target_it_did_not_create(
        be_layout, monkeypatch):
    # a target that appears between create()'s existence pre-check and its
    # clone (an external zfs create, or the CLI racing middleware, which
    # share no lock) is a stranger's dataset: the root clone fails, so the
    # rollback has nothing recorded and must not touch it
    lz, _pool_hdl, be1 = be_layout
    real = engine._lzc_create_snapshots

    def racing(names):
        out = real(names)
        # the window: someone else creates the target, with data under it
        _mkfs(lz, BE2)
        _mkfs(lz, f'{BE2}/precious')
        return out

    monkeypatch.setattr(engine, '_lzc_create_snapshots', racing)
    with pytest.raises(BEError):
        engine.create(lz, source_dataset=be1, target_dataset=BE2)

    # the stranger's datasets survive untouched
    assert engine._exists(lz, BE2)
    assert engine._exists(lz, f'{BE2}/precious')
    # our own source snapshots are still reclaimed
    assert engine._snapshots(lz.open_resource(name=be1)) == []
