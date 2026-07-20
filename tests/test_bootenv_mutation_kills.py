"""Mutation-kill tests for the destroy path.

Each test names the mutant it targets.
"""

import pytest
import subprocess
import truenas_pylibzfs
from truenas_bootenv import engine
from truenas_bootenv.errors import BEDestroyUnsafe, BEError

FST = truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
POOL = 'bootenvmut'
KVER = '6.18.35-test'

BE1 = f'{POOL}/ROOT/be1'
BE1X = f'{POOL}/ROOT/be1x'
BE2 = f'{POOL}/ROOT/be2'
BE3 = f'{POOL}/ROOT/be3'


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


def _all_snaps(lz, dataset):
    out = []
    for fs in engine._walk_topdown(lz.open_resource(name=dataset), []):
        out.extend(s.name for s in engine._snapshots(fs))
    return out


# kills M12_preflight_dropped and M13_preflight_root_only
def test_preflight_scans_children_before_any_destruction(
    be_layout, monkeypatch,
):
    lz, _p, be1 = be_layout
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={f'{be1}/var@pin'})
    lz.open_resource(name=f'{be1}/var@pin').clone(
        name=f'{POOL}/ROOT/pinclone', properties={'canmount': 'noauto'},
    )
    monkeypatch.setattr(
        engine, '_promote_externals', lambda lzh, subtree, ext: None,
    )
    calls = []
    real = engine._lzc_destroy_snapshots
    monkeypatch.setattr(
        engine, '_lzc_destroy_snapshots',
        lambda names: calls.append(set(names)) or real(names),
    )
    with pytest.raises(BEDestroyUnsafe):
        engine.destroy(lz, dataset=be1, running_ds=BE2)
    assert calls == []          # refused before any destructive step
    assert engine._exists(lz, f'{be1}/var')
    assert engine._exists(lz, be1)


# kills M20_final_grub_dropped
def test_destroy_run_grub_wires_through_on_success(be_layout, monkeypatch):
    lz, _p, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    calls = []
    monkeypatch.setattr(
        engine, '_update_grub', lambda lzh, pool: calls.append(pool),
    )
    engine.destroy(lz, dataset=BE2, running_ds=be1, run_grub=True)
    assert calls == [POOL]


# kills M07_prefix_missing_slash
def test_prefix_sharing_sibling_survives_destroy(be_layout):
    lz, _p, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE1X)
    engine.destroy(lz, dataset=be1, running_ds=BE1X)
    assert not engine._exists(lz, be1)
    assert engine._exists(lz, BE1X)
    assert engine._exists(lz, f'{BE1X}/var')
    assert engine._origin(lz.open_resource(name=BE1X)) is None


# kills M10_promote_newest_first (and M11_promote_sort_dropped if clone
# enumeration order ever differs from creation order)
def test_oldest_external_clone_receives_shared_history(be_layout):
    lz, _p, be1 = be_layout
    snap = f'{be1}@shared'
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={snap})
    lz.open_resource(name=snap).clone(
        name=BE2, properties={'canmount': 'noauto'},
    )
    lz.open_resource(name=snap).clone(
        name=BE3, properties={'canmount': 'noauto'},
    )
    engine.destroy(lz, dataset=be1, running_ds=BE2)
    assert engine._exists(lz, f'{BE2}@shared')
    assert engine._origin(lz.open_resource(name=BE2)) is None
    assert engine._origin(lz.open_resource(name=BE3)) == f'{BE2}@shared'


# kills M17_destroy_drop_snap_cleanup
def test_destroy_be_carrying_own_snapshot(be_layout):
    lz, _p, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={f'{BE2}@usersnap'})
    engine.destroy(lz, dataset=BE2, running_ds=be1)
    assert not engine._exists(lz, BE2)


# kills M01_refuse_skip_root
def test_destroy_refuses_volume_at_be_root(be_layout):
    lz, _p, be1 = be_layout
    subprocess.run(
        ['zfs', 'create', '-V', '16M', f'{POOL}/ROOT/volbe'], check=True,
    )
    with pytest.raises(BEError, match='contains a volume'):
        engine.destroy(lz, dataset=f'{POOL}/ROOT/volbe', running_ds=be1)
    assert engine._exists(lz, f'{POOL}/ROOT/volbe')


# kills M21_snapshots_unordered (if unsorted iteration is name-ordered
# or otherwise diverges from txg order)
def test_snapshots_helper_returns_txg_order(be_layout):
    lz, _p, be1 = be_layout
    for n in ('zzz', 'mmm', 'aaa'):
        truenas_pylibzfs.lzc.create_snapshots(snapshot_names={f'{be1}@{n}'})
    names = [
        s.name.split('@')[1]
        for s in engine._snapshots(lz.open_resource(name=be1))
    ]
    assert names == ['zzz', 'mmm', 'aaa']
