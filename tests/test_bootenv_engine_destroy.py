"""Tests for truenas_bootenv.engine.destroy (needs root and zfs.ko).

BEs are built with engine.create so destroy runs against the state
create actually leaves behind, origin snapshots included.
"""

import os
import subprocess
import tempfile
import pytest
import truenas_pylibzfs
from truenas_bootenv import engine
from truenas_bootenv.errors import BEBusy, BEDestroyUnsafe, BEError

FST = truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
POOL = 'bootenvdestroy'
KVER = '6.18.35-production+truenas'

BE1 = f'{POOL}/ROOT/be1'
BE2 = f'{POOL}/ROOT/be2'
BE3 = f'{POOL}/ROOT/be3'
ELSEWHERE = f'{POOL}/ROOT/other'


@pytest.fixture
def be_layout(make_pool):
    lz, pool_hdl, _root = make_pool(POOL)
    lz.create_resource(
        name=f'{POOL}/ROOT', type=FST, properties={'canmount': 'off'},
    )
    lz.create_resource(
        name=BE1, type=FST,
        properties={'canmount': 'noauto', 'mountpoint': 'legacy'},
    )
    lz.create_resource(
        name=f'{BE1}/var', type=FST,
        properties={'canmount': 'noauto', 'mountpoint': '/var'},
    )
    lz.open_resource(name=BE1).set_user_properties(
        user_properties={'truenas:kernel_version': KVER},
    )
    return lz, pool_hdl, BE1


def _all_root_snapshots(lz, dataset):
    out = []
    for fs in engine._walk_topdown(lz.open_resource(name=dataset), []):
        out.extend(s.name for s in engine._snapshots(fs))
    return out


def test_running_be_refused(be_layout):
    lz, _pool_hdl, be1 = be_layout
    with pytest.raises(BEBusy):
        engine.destroy(lz, dataset=be1, running_ds=be1)
    assert engine._exists(lz, be1)


def test_activated_be_refused_via_fresh_bootfs(be_layout):
    lz, pool_hdl, be1 = be_layout
    pool_hdl.set_properties(properties={'bootfs': be1})
    with pytest.raises(BEBusy):
        engine.destroy(lz, dataset=be1, running_ds=ELSEWHERE)
    assert engine._exists(lz, be1)


def test_absent_be_is_success(be_layout):
    lz, _pool_hdl, _be1 = be_layout
    engine.destroy(lz, dataset=f'{POOL}/ROOT/ghost', running_ds=ELSEWHERE)


def test_mounted_be_refused_intact(be_layout):
    # a boot environment an operator mounted to recover files must be
    # refused whole, not partially destroyed leaf-first by the updater's
    # space-pruning (be1 has mountpoint=legacy so it mounts anywhere)
    lz, _pool_hdl, be1 = be_layout
    before = {f.name for f in engine._walk_topdown(lz.open_resource(name=be1), [])}
    d = tempfile.mkdtemp(prefix='mounttest_')
    subprocess.run(['mount', '-t', 'zfs', be1, d], check=True)
    try:
        with pytest.raises(BEBusy):
            engine.destroy(lz, dataset=be1, running_ds=ELSEWHERE)
        after = {f.name for f in engine._walk_topdown(lz.open_resource(name=be1), [])}
        assert after == before          # nothing destroyed
        assert lz.open_resource(name=be1).get_user_properties().get(
            'truenas:kernel_version') == KVER   # marker never cleared
    finally:
        subprocess.run(['umount', d])
        os.rmdir(d)


def _deepen(lz, be1):
    """Add a grandchild so the middle dataset is not the first victim."""
    lz.create_resource(
        name=f'{be1}/var/log', type=FST,
        properties={'canmount': 'noauto', 'mountpoint': 'legacy'},
    )


def test_mounted_middle_dataset_refused_intact(be_layout):
    # the guard must scan the whole subtree, not just the boot environment
    # root. A mounted dataset in the MIDDLE is the dangerous shape: the
    # leaf-first teardown destroys everything below it first, so a guard
    # that only inspected the root would already have eaten the grandchild
    # by the time the mount refused
    lz, _pool_hdl, be1 = be_layout
    _deepen(lz, be1)
    before = {f.name for f in engine._walk_topdown(
        lz.open_resource(name=be1), [])}
    child = f'{be1}/var'
    d = tempfile.mkdtemp(prefix='mountchild_')
    subprocess.run(
        ['mount', '-t', 'zfs', '-o', 'zfsutil', child, d], check=True,
    )
    try:
        with pytest.raises(BEBusy):
            engine.destroy(lz, dataset=be1, running_ds=ELSEWHERE)
        after = {f.name for f in engine._walk_topdown(
            lz.open_resource(name=be1), [])}
        assert after == before          # the grandchild survived
        assert lz.open_resource(name=be1).get_user_properties().get(
            'truenas:kernel_version') == KVER
    finally:
        subprocess.run(['umount', d])
        os.rmdir(d)


def test_held_middle_snapshot_refused_intact(be_layout):
    # same shape for holds: a hold on a MIDDLE dataset's snapshot must
    # refuse before the teardown eats the grandchild below it
    lz, _pool_hdl, be1 = be_layout
    _deepen(lz, be1)
    before = {f.name for f in engine._walk_topdown(
        lz.open_resource(name=be1), [])}
    snap = f'{be1}/var@childhold'
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={snap})
    truenas_pylibzfs.lzc.create_holds(holds={(snap, 'claude-test')})
    try:
        with pytest.raises(BEBusy):
            engine.destroy(lz, dataset=be1, running_ds=ELSEWHERE)
        after = {f.name for f in engine._walk_topdown(
            lz.open_resource(name=be1), [])}
        assert after == before          # the grandchild survived
        assert lz.open_resource(name=be1).get_user_properties().get(
            'truenas:kernel_version') == KVER
    finally:
        truenas_pylibzfs.lzc.release_holds(holds={(snap, 'claude-test')})


def test_bad_path_refused(be_layout):
    lz, _pool_hdl, _be1 = be_layout
    with pytest.raises(BEError):
        engine.destroy(lz, dataset=POOL, running_ds=ELSEWHERE)


def test_space_named_dataset_destroyable(be_layout):
    # ZFS accepts interior spaces and create() refuses them, but an
    # existing space-named dataset under ROOT is listed by
    # list_environments and must stay destroyable, or the API that
    # lists it could never remove it (it also poisons the boot menu
    # generator until removed)
    lz, _pool_hdl, _be1 = be_layout
    victim = f'{POOL}/ROOT/sp be'
    lz.create_resource(
        name=victim, type=FST,
        properties={'canmount': 'noauto', 'mountpoint': 'legacy'},
    )
    names = [e.name for e in engine.list_environments(
        lz, pool_name=POOL, running_ds=ELSEWHERE)]
    assert 'sp be' in names
    engine.destroy(lz, dataset=victim, running_ds=ELSEWHERE)
    assert not engine._exists(lz, victim)


def test_interrupted_destroy_leaves_inert_remnant(be_layout, monkeypatch):
    # a crash partway through the leaf-first walk must not leave a
    # half boot environment that still looks bootable: the kernel
    # marker is cleared before the walk, and re-running destroy
    # finishes the job
    lz, _pool_hdl, be1 = be_layout
    calls = {'n': 0}
    real = engine._lzc_destroy_snapshots

    def dies_at_the_root(names):
        calls['n'] += 1
        if calls['n'] == 2:
            raise RuntimeError('injected crash before the root destroy')
        return real(names)

    monkeypatch.setattr(engine, '_lzc_destroy_snapshots', dies_at_the_root)
    with pytest.raises(RuntimeError, match='injected crash'):
        engine.destroy(lz, dataset=be1, running_ds=ELSEWHERE)
    assert engine._exists(lz, be1)
    assert not engine._exists(lz, f'{be1}/var')
    uprops = lz.open_resource(name=be1).get_user_properties()
    assert uprops.get('truenas:kernel_version') == '-'
    remnant = [e for e in engine.list_environments(
        lz, pool_name=POOL, running_ds=ELSEWHERE) if e.name == 'be1']
    assert remnant[0].can_activate is False
    monkeypatch.setattr(engine, '_lzc_destroy_snapshots', real)
    engine.destroy(lz, dataset=be1, running_ds=ELSEWHERE)
    assert not engine._exists(lz, be1)


def test_marker_restored_when_the_clear_itself_raises_late(
        be_layout, monkeypatch):
    # the binding writes the pool history only after the property is
    # committed, so clearing truenas:kernel_version can raise with '-'
    # already on disk. The restore must still run, or an intact boot
    # environment is stranded unbootable and needs a manual zfs set
    lz, _pool_hdl, be1 = be_layout
    real_open = lz.open_resource

    class _ClearRaisesAfterWriting:
        def __init__(self, real):
            self._real = real

        def __getattr__(self, name):
            return getattr(self._real, name)

        def open_resource(self, *, name):
            resource = self._real.open_resource(name=name)
            return _Root(resource) if name == be1 else resource

    class _Root:
        def __init__(self, real):
            self._real = real
            self.cleared = False

        def __getattr__(self, name):
            return getattr(self._real, name)

        def set_user_properties(self, *, user_properties):
            self._real.set_user_properties(user_properties=user_properties)
            if user_properties.get('truenas:kernel_version') == '-':
                # the marker is now on disk; the binding raises anyway
                raise RuntimeError('injected post-write history failure')

    with pytest.raises(RuntimeError, match='post-write history failure'):
        engine.destroy(
            _ClearRaisesAfterWriting(lz), dataset=be1, running_ds=ELSEWHERE,
        )

    # nothing was destroyed, so the boot environment is still whole and
    # must still be bootable
    assert engine._exists(lz, be1)
    assert engine._exists(lz, f'{be1}/var')
    uprops = real_open(name=be1).get_user_properties()
    assert uprops.get('truenas:kernel_version') == KVER


def test_refused_destroy_with_intact_subtree_restores_marker(
        be_layout, monkeypatch):
    # a refusal that destroyed nothing (here the very first snapshot
    # batch) must leave the boot environment exactly as it was,
    # kernel marker included
    lz, _pool_hdl, be1 = be_layout

    def refuses(names):
        raise RuntimeError('injected refusal before any destruction')

    monkeypatch.setattr(engine, '_lzc_destroy_snapshots', refuses)
    with pytest.raises(RuntimeError, match='injected refusal'):
        engine.destroy(lz, dataset=be1, running_ds=ELSEWHERE)
    assert engine._exists(lz, be1)
    assert engine._exists(lz, f'{be1}/var')
    uprops = lz.open_resource(name=be1).get_user_properties()
    assert uprops.get('truenas:kernel_version') == KVER


def test_destroy_clone_reclaims_source_origin_snapshots(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    assert _all_root_snapshots(lz, be1)          # origins exist on source
    engine.destroy(lz, dataset=BE2, running_ds=be1)
    assert not engine._exists(lz, BE2)
    # with the clone gone, nothing pins the source snapshots
    assert _all_root_snapshots(lz, be1) == []


def test_destroying_source_preserves_dependent_sibling(be_layout):
    # destroying be1 must promote be2 so it survives with the shared
    # data
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    engine.destroy(lz, dataset=be1, running_ds=BE2)
    assert not engine._exists(lz, be1)
    assert engine._exists(lz, BE2)
    assert engine._exists(lz, f'{BE2}/var')
    assert engine._origin(lz.open_resource(name=BE2)) is None   # independent


def test_two_dependent_siblings_both_survive(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    engine.create(lz, source_dataset=be1, target_dataset=BE3)
    engine.destroy(lz, dataset=be1, running_ds=BE2)
    assert not engine._exists(lz, be1)
    for survivor in (BE2, BE3):
        assert engine._exists(lz, survivor)
        assert engine._exists(lz, f'{survivor}/var')
        origin = engine._origin(lz.open_resource(name=survivor))
        # survivors may depend on each other, never on the destroyed BE
        assert origin is None or not origin.startswith(f'{be1}@')


def test_preflight_refuses_if_promotion_missed(be_layout, monkeypatch):
    # no-op the promotion pass: be2 stays an external clone, and the
    # pre-flight scan must refuse before anything is destroyed
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    monkeypatch.setattr(
        engine, '_promote_externals', lambda lzh, subtree, external: None,
    )
    with pytest.raises(BEDestroyUnsafe):
        engine.destroy(lz, dataset=be1, running_ds=BE2)
    assert engine._exists(lz, be1)                # nothing was destroyed
    assert engine._exists(lz, f'{be1}/var')
    assert engine._exists(lz, BE2)


def test_held_snapshot_raises_bebusy(be_layout):
    # a hold on the boot environment's own snapshot refuses the destroy
    # with the whole subtree intact. The root's snapshots are torn down
    # last, so without the pre-teardown refusal the leaf-first walk would
    # destroy be1/var before the held root snapshot stopped it, stranding
    # an inert half-destroyed remnant
    lz, _pool_hdl, be1 = be_layout
    before = {f.name for f in engine._walk_topdown(
        lz.open_resource(name=be1), [])}
    snap = f'{be1}@manual'
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={snap})
    truenas_pylibzfs.lzc.create_holds(holds={(snap, 'claude-test')})
    try:
        with pytest.raises(BEBusy):
            engine.destroy(lz, dataset=be1, running_ds=ELSEWHERE)
        after = {f.name for f in engine._walk_topdown(
            lz.open_resource(name=be1), [])}
        assert after == before          # nothing destroyed
        assert lz.open_resource(name=be1).get_user_properties().get(
            'truenas:kernel_version') == KVER   # marker never cleared
    finally:
        truenas_pylibzfs.lzc.release_holds(holds={(snap, 'claude-test')})


def test_held_origin_snapshot_does_not_fail_destroy(be_layout):
    # a hold on the origin snapshot must not fail the clone's destroy.
    # The origin is left behind until the hold is released
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    origin = engine._origin(lz.open_resource(name=BE2))
    truenas_pylibzfs.lzc.create_holds(holds={(origin, 'claude-test')})
    try:
        engine.destroy(lz, dataset=BE2, running_ds=be1)   # must succeed
        assert not engine._exists(lz, BE2)
        assert engine._exists(lz, origin)                 # left in place
    finally:
        truenas_pylibzfs.lzc.release_holds(holds={(origin, 'claude-test')})


def test_held_shared_history_evacuated_by_promotion(be_layout):
    # promotion moves snapshots and their holds between datasets, so
    # the promote-externals pass carries the migrated held snapshot
    # back to the surviving dependent and the hold never blocks the
    # destroy. Found by fuzzing (seed 4). Contrast
    # test_held_snapshot_raises_bebusy
    lz, pool_hdl, be1 = be_layout
    snap = f'{be1}@migrating'
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={snap})
    truenas_pylibzfs.lzc.create_holds(holds={(snap, 'claude-test')})
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    engine.activate(lz, dataset=BE2)     # promotes BE2: snapshot migrates
    migrated = f'{BE2}@migrating'
    assert engine._exists(lz, migrated)
    assert not engine._exists(lz, snap)

    pool_hdl.set_properties(properties={'bootfs': be1})  # unguard BE2
    try:
        # destroy promotes be1 (external dependent of BE2@suffix),
        # which carries @migrating and its hold back to be1
        engine.destroy(lz, dataset=BE2, running_ds=be1)
        assert not engine._exists(lz, BE2)
        assert engine._exists(lz, snap)          # back home, hold intact
    finally:
        truenas_pylibzfs.lzc.release_holds(holds={(snap, 'claude-test')})


def test_destroy_is_rerunnable_after_partial_failure(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    engine.destroy(lz, dataset=BE2, running_ds=be1)
    engine.destroy(lz, dataset=BE2, running_ds=be1)   # idempotent rerun
    assert not engine._exists(lz, BE2)
