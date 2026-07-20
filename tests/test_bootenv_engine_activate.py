"""Tests for truenas_bootenv.engine.activate (needs root and zfs.ko)."""

import pytest
import subprocess
import truenas_pylibzfs
from truenas_bootenv import engine
from truenas_bootenv.errors import BEError, BENotFound

FST = truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
POOL = 'bootenvactivate'


def _create_fs(lz, name, **props):
    properties = {'canmount': 'noauto', 'mountpoint': 'legacy'}
    properties.update(props)
    lz.create_resource(name=name, type=FST, properties=properties)


def _prop(lz, name, prop):
    a = lz.open_resource(name=name).asdict(properties={prop})
    return a['properties'][list(a['properties'])[0]]['value']


def _bootfs(pool_hdl):
    props = pool_hdl.get_properties(
        properties={truenas_pylibzfs.ZPOOLProperty.BOOTFS}
    )
    return props.bootfs.value


@pytest.fixture
def be_layout(make_pool):
    """Mimic the installer's BE layout on a scratch pool.

    <pool>/ROOT             canmount=off
    <pool>/ROOT/be1         the boot environment, with two children
    <pool>/ROOT/be1/var
    <pool>/ROOT/be1/var/log
    """
    lz, pool_hdl, _root = make_pool(POOL)
    lz.create_resource(
        name=f'{POOL}/ROOT', type=FST, properties={'canmount': 'off'},
    )
    be1 = f'{POOL}/ROOT/be1'
    for name in (be1, f'{be1}/var', f'{be1}/var/log'):
        _create_fs(lz, name)
    lz.open_resource(name=be1).set_user_properties(
        user_properties={'truenas:kernel_version': '6.12.0-test'},
    )
    return lz, pool_hdl, be1


def test_missing_be_raises_benotfound(be_layout):
    lz, _pool_hdl, _be1 = be_layout
    with pytest.raises(BENotFound):
        engine.activate(lz, dataset=f'{POOL}/ROOT/nosuchbe')


def test_pool_root_is_refused(be_layout):
    # activating a pool root would set canmount=noauto across the
    # whole pool
    lz, _pool_hdl, _be1 = be_layout
    with pytest.raises(BEError):
        engine.activate(lz, dataset=POOL)
    assert _prop(lz, POOL, truenas_pylibzfs.ZFSProperty.CANMOUNT) != 'noauto'


def test_be_child_is_refused(be_layout):
    lz, _pool_hdl, be1 = be_layout
    with pytest.raises(BEError):
        engine.activate(lz, dataset=f'{be1}/var')


def test_sets_pool_bootfs_to_full_dataset_path(be_layout):
    lz, pool_hdl, be1 = be_layout
    engine.activate(lz, dataset=be1)
    assert _bootfs(pool_hdl) == be1


def test_sets_canmount_noauto_on_whole_subtree(be_layout):
    lz, _pool_hdl, be1 = be_layout
    # simulate drift: a child was flipped to canmount=on
    lz.open_resource(name=f'{be1}/var').set_properties(
        properties={'canmount': 'on'}, remount=False,
    )
    engine.activate(lz, dataset=be1)
    for name in (be1, f'{be1}/var', f'{be1}/var/log'):
        assert _prop(lz, name, truenas_pylibzfs.ZFSProperty.CANMOUNT) == 'noauto', name


def test_activating_a_clone_promotes_it(be_layout):
    lz, _pool_hdl, be1 = be_layout
    # clone be2 off be1, zectl create style
    snap = f'{be1}@snap1'
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={snap})
    be2 = f'{POOL}/ROOT/be2'
    lz.open_resource(name=snap).clone(
        name=be2, properties={'canmount': 'noauto', 'mountpoint': 'legacy'},
    )
    lz.open_resource(name=be2).set_user_properties(
        user_properties={'truenas:kernel_version': '6.12.0-test'},
    )
    assert engine._origin(lz.open_resource(name=be2)) == snap

    engine.activate(lz, dataset=be2)

    # be2 now owns the data: its origin is cleared and be1 depends on it
    assert engine._origin(lz.open_resource(name=be2)) is None
    assert engine._origin(lz.open_resource(name=be1)) == f'{be2}@snap1'


def test_non_clone_be_is_not_promoted(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.activate(lz, dataset=be1)
    assert engine._origin(lz.open_resource(name=be1)) is None


def test_reactivating_activated_be_is_idempotent(be_layout):
    lz, pool_hdl, be1 = be_layout
    engine.activate(lz, dataset=be1)
    engine.activate(lz, dataset=be1)
    assert _bootfs(pool_hdl) == be1


def test_activate_does_not_mount_anything(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.activate(lz, dataset=be1)
    # the binding types 'mounted' as a bool; None means never mounted
    for name in (be1, f'{be1}/var', f'{be1}/var/log'):
        assert _prop(lz, name, truenas_pylibzfs.ZFSProperty.MOUNTED) in (None, False), name


def test_walk_precedes_bootfs_and_errors_are_typed(be_layout):
    # a snapshot name on both sides of the clone makes promote fail
    # mid-walk: bootfs must still be untouched (the write comes after
    # the walk) and the error must be BEError, not a raw ZFSException
    lz, pool_hdl, be1 = be_layout
    # @dup exists on both sides of the clone, so promote's snapshot
    # migration hits EEXIST
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={f'{be1}@dup'})
    engine.create(lz, source_dataset=be1, target_dataset=f'{POOL}/ROOT/be2')
    truenas_pylibzfs.lzc.create_snapshots(
        snapshot_names={f'{POOL}/ROOT/be2@dup'},
    )
    engine.activate(lz, dataset=be1)   # be1 the baseline bootfs
    with pytest.raises(engine.BEError) as excinfo:
        engine.activate(lz, dataset=f'{POOL}/ROOT/be2')  # promote hits @dup
    assert not isinstance(excinfo.value, truenas_pylibzfs.ZFSException)
    assert _bootfs(pool_hdl) == be1     # ordering: bootfs never moved


def test_zvol_bearing_be_refused_on_activate(be_layout):
    # a volume anywhere in the BE is refused up front instead of
    # failing mid-walk on the volume's filesystem-only properties
    lz, pool_hdl, be1 = be_layout
    before = _bootfs(pool_hdl)
    subprocess.run(f'zfs create -V 16M {be1}/vol0', shell=True,
                   capture_output=True)
    with pytest.raises(BEError, match='contains a volume'):
        engine.activate(lz, dataset=be1)
    assert _bootfs(pool_hdl) == before      # nothing was changed


def test_activating_engine_created_be_promotes_whole_subtree(be_layout):
    # a create()-made BE has clone children; every one must be
    # promoted, not just the root
    lz, _pool_hdl, be1 = be_layout
    be2 = f'{POOL}/ROOT/be2'
    engine.create(lz, source_dataset=be1, target_dataset=be2)
    engine.activate(lz, dataset=be2)
    for rel in ('', '/var', '/var/log'):
        assert engine._origin(lz.open_resource(name=f'{be2}{rel}')) is None, rel
        origin = engine._origin(lz.open_resource(name=f'{be1}{rel}'))
        assert origin is not None and origin.startswith(f'{be2}{rel}@'), rel
