"""Edge-case tests for truenas_bootenv.engine (root + zfs.ko).

Pins behaviors found by exploratory probing: zvol-bearing boot
environments, over-long target names, received properties, and an
unset pool bootfs.
"""

import pytest
import subprocess
import sys
import time
import truenas_pylibzfs
import types
from truenas_bootenv import engine
from truenas_bootenv.errors import BEError, BENotFound

FST = truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
POOL = 'bootenvedge'
KVER = '6.18.35-test'

BE1 = f'{POOL}/ROOT/be1'
BE2 = f'{POOL}/ROOT/be2'


def _sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True)


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
    lz.open_resource(name=BE1).set_user_properties(
        user_properties={'truenas:kernel_version': KVER},
    )
    return lz, pool_hdl, BE1


class TestVolumeChildren:
    # zectl silently destroys zvol children with the environment. We
    # refuse instead, since a volume in a boot environment is
    # misplaced user data

    @pytest.fixture
    def with_zvol(self, be_layout):
        lz, pool_hdl, be1 = be_layout
        _sh(f'zfs create -V 16M {be1}/vol0')
        return lz, pool_hdl, be1

    def test_create_refused_without_side_effects(self, with_zvol):
        lz, _pool_hdl, be1 = with_zvol
        with pytest.raises(BEError, match='contains a volume'):
            engine.create(lz, source_dataset=be1, target_dataset=BE2)
        assert not engine._exists(lz, BE2)
        # refusal happens before the snapshot step
        snaps = _sh(
            f'zfs list -H -t snapshot -o name -r {be1}').stdout.split()
        assert snaps == []

    def test_destroy_refused_and_zvol_intact(self, with_zvol):
        lz, _pool_hdl, be1 = with_zvol
        with pytest.raises(BEError, match='contains a volume'):
            engine.destroy(
                lz, dataset=be1, running_ds=f'{POOL}/ROOT/other',
            )
        assert engine._exists(lz, f'{be1}/vol0')
        assert engine._exists(lz, be1)


def test_oversized_child_path_fails_clean(be_layout):
    # target name valid on the root but a nested child path exceeds the
    # ZFS name limit: clone fails mid-tree, cleanup must leave nothing
    lz, _pool_hdl, be1 = be_layout
    deep = be1
    for _ in range(4):
        deep = f'{deep}/{"d" * 40}'
        _sh(f'zfs create -o canmount=noauto -o mountpoint=legacy {deep}')
    long_target = f'{POOL}/ROOT/{"t" * 80}'
    with pytest.raises(BEError) as excinfo:
        engine.create(lz, source_dataset=be1, target_dataset=long_target)
    # the failure comes from libzfs mid-clone, through the generic
    # translation branch: base BEError, not a subclass
    assert type(excinfo.value) is BEError
    assert not engine._exists(lz, long_target)
    leftover = _sh(
        f'zfs list -H -t snapshot -o name -r {POOL}/ROOT').stdout.split()
    assert leftover == []


def test_received_properties_copied_as_local(be_layout):
    # zectl copies LOCAL and RECEIVED properties onto clones; a
    # received boot environment (zfs send -p | recv) must clone with
    # its received values pinned as local
    lz, _pool_hdl, be1 = be_layout
    lz.open_resource(name=be1).set_properties(
        properties={'atime': 'off'}, remount=False,
    )
    _sh(f'zfs snapshot {be1}@send')
    recv = f'{POOL}/ROOT/recvbe'
    _sh(f'zfs send -p {be1}@send | zfs recv -x canmount {recv}')
    assert _sh(f'zfs get -H -o value,source atime {recv}'
               ).stdout.split() == ['off', 'received']
    lz.open_resource(name=recv).set_user_properties(
        user_properties={'truenas:kernel_version': KVER},
    )
    engine.create(lz, source_dataset=recv, target_dataset=BE2)
    assert _sh(f'zfs get -H -o value,source atime {BE2}'
               ).stdout.split() == ['off', 'local']


def test_destroy_with_unset_bootfs(be_layout):
    # a fresh pool has no bootfs; the activated-BE guard must treat
    # that as "nothing activated", not compare against the '-' marker
    lz, _pool_hdl, be1 = be_layout
    assert engine._pool_bootfs(lz, POOL) is None
    engine.destroy(lz, dataset=be1, running_ds=f'{POOL}/ROOT/other')
    assert not engine._exists(lz, be1)


def test_list_environments_facts(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    engine.activate(lz, dataset=be1)
    entries = {e.name: e for e in engine.list_environments(
        lz, pool_name=POOL, running_ds=BE2,
    )}
    assert set(entries) == {'be1', 'be2'}
    assert entries['be1'].dataset == be1
    assert entries['be1'].activated is True
    assert entries['be1'].active is False
    assert entries['be2'].active is True
    assert entries['be2'].activated is False
    assert entries['be2'].can_activate is True   # create copied the kernel
    assert entries['be1'].keep is None
    # bounds tight enough to tell the two integer fields apart: a
    # current unix time is far above any plausible byte count here
    assert abs(entries['be1'].created - time.time()) < 3600
    assert 0 < entries['be1'].used_bytes < 10**9


def test_list_environments_keep_and_kernel_facts(be_layout):
    lz, _pool_hdl, be1 = be_layout
    lz.open_resource(name=be1).set_user_properties(
        user_properties={'zectl:keep': 'True'},
    )
    entry = engine.list_environments(
        lz, pool_name=POOL, running_ds=None,
    )[0]
    assert entry.keep is True
    assert entry.can_activate is True


def test_list_environments_kernel_less_be_cannot_activate(be_layout):
    lz, _pool_hdl, _be1 = be_layout
    lz.create_resource(
        name=f'{POOL}/ROOT/nokernel', type=FST,
        properties={'canmount': 'noauto', 'mountpoint': 'legacy'},
    )
    lz.create_resource(
        name=f'{POOL}/ROOT/dashkernel', type=FST,
        properties={'canmount': 'noauto', 'mountpoint': 'legacy'},
    )
    lz.open_resource(name=f'{POOL}/ROOT/dashkernel').set_user_properties(
        user_properties={'truenas:kernel_version': '-'},
    )
    entries = {e.name: e for e in engine.list_environments(
        lz, pool_name=POOL, running_ds=None,
    )}
    assert entries['be1'].can_activate is True
    assert entries['nokernel'].can_activate is False
    assert entries['dashkernel'].can_activate is False


def test_list_environments_skips_zvol_children(be_layout):
    # a stray zvol under <pool>/ROOT is not a boot environment and
    # must not appear in (or break) the listing
    lz, _pool_hdl, _be1 = be_layout
    _sh(f'zfs create -V 16M {POOL}/ROOT/vol0')
    names = {e.name for e in engine.list_environments(
        lz, pool_name=POOL, running_ds=None,
    )}
    assert 'vol0' not in names
    assert 'be1' in names


def test_list_environments_missing_root_raises(be_layout):
    lz, _pool_hdl, _be1 = be_layout
    with pytest.raises(BENotFound):
        engine.list_environments(
            lz, pool_name='nosuchpool', running_ds=None,
        )


def test_hostile_kernel_version_is_not_activatable(be_layout):
    # characters the sh-based menu generator cannot embed safely make
    # the BE non-bootable for listing and activate alike
    lz, _pool_hdl, be1 = be_layout
    lz.open_resource(name=be1).set_user_properties(
        user_properties={'truenas:kernel_version': "6.1'; echo pwned"},
    )
    entry = engine.list_environments(
        lz, pool_name=POOL, running_ds=None,
    )[0]
    assert entry.can_activate is False
    with pytest.raises(BEError, match='usable'):
        engine.activate(lz, dataset=be1)


def test_grub_pending_missing_root_reads_false(be_layout):
    lz, _pool_hdl, _be1 = be_layout
    assert engine.grub_pending(lz, 'nosuchpool') is False


def test_create_refuses_cross_pool_target(be_layout):
    # refused before any snapshot is taken
    lz, _pool_hdl, be1 = be_layout
    with pytest.raises(BEError, match='different pools'):
        engine.create(
            lz, source_dataset=be1,
            target_dataset='otherpool/ROOT/newbe',
        )
    snaps = _sh(
        f'zfs list -H -t snapshot -o name -r {POOL}/ROOT'
    ).stdout.split()
    assert snaps == []


def test_grub_pending_marker_roundtrip(be_layout):
    # raw property reads pin the on-disk contract middleware
    # reconciles at startup, not just the engine's own accessors
    lz, _pool_hdl, _be1 = be_layout
    root = f'{POOL}/ROOT'
    assert engine.grub_pending(lz, POOL) is False
    engine.set_grub_pending(lz, POOL, True)
    raw = lz.open_resource(name=root).get_user_properties()
    assert raw.get('truenas:grub_pending') == '1'
    assert engine.grub_pending(lz, POOL) is True
    engine.set_grub_pending(lz, POOL, False)
    raw = lz.open_resource(name=root).get_user_properties()
    assert 'truenas:grub_pending' not in raw     # removed, not set to '0'
    assert engine.grub_pending(lz, POOL) is False
    # a foreign value that is not '1' must read as not pending
    lz.open_resource(name=root).set_user_properties(
        user_properties={'truenas:grub_pending': '0'},
    )
    assert engine.grub_pending(lz, POOL) is False


def test_sync_boot_pool_smoke(be_layout):
    lz, _pool_hdl, _be1 = be_layout
    engine.sync_boot_pool(lz, POOL)


def test_set_keep_writes_exact_spelling(be_layout):
    lz, _pool_hdl, be1 = be_layout
    engine.set_keep(lz, be1, True)
    assert (lz.open_resource(name=be1).get_user_properties()
            .get('zectl:keep') == 'True')
    assert engine.list_environments(
        lz, pool_name=POOL, running_ds=None,
    )[0].keep is True
    engine.set_keep(lz, be1, False)
    assert (lz.open_resource(name=be1).get_user_properties()
            .get('zectl:keep') == 'False')
    # 'False' must map to False, never truthy-string True
    assert engine.list_environments(
        lz, pool_name=POOL, running_ds=None,
    )[0].keep is False


def test_set_keep_missing_dataset_is_typed_not_found(be_layout):
    lz, _pool_hdl, _be1 = be_layout
    with pytest.raises(BENotFound):
        engine.set_keep(lz, f'{POOL}/ROOT/ghost', True)


def test_promote_children_promotes_clone_children_only(be_layout):
    # a create()-made BE is clones all the way down; the boot-time hook
    # promotes the children and leaves the root alone
    lz, _pool_hdl, be1 = be_layout
    _sh(f'zfs create -o canmount=noauto -o mountpoint=legacy {be1}/var')
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    promoted, failures = engine.promote_children(lz, BE2)
    assert failures == []
    assert [p[0] for p in promoted] == [f'{BE2}/var']
    assert promoted[0][1].startswith(f'{be1}/var@')   # the logged origin
    assert engine._origin(lz.open_resource(name=f'{BE2}/var')) is None
    assert engine._origin(lz.open_resource(name=BE2)) is not None  # root kept

    # second run finds nothing left to promote
    assert engine.promote_children(lz, BE2) == ([], [])


def test_promote_children_failure_does_not_stop_siblings(be_layout):
    # a snapshot name on both sides of one child's clone point makes
    # that child's promote fail with EEXIST; the other child must
    # still be promoted and the failure reported, not raised
    lz, _pool_hdl, be1 = be_layout
    for child in ('var', 'etc'):
        _sh(f'zfs create -o canmount=noauto -o mountpoint=legacy '
            f'{be1}/{child}')
    _sh(f'zfs snapshot {be1}/var@dup')
    engine.create(lz, source_dataset=be1, target_dataset=BE2)
    _sh(f'zfs snapshot {BE2}/var@dup')
    promoted, failures = engine.promote_children(lz, BE2)
    assert [p[0] for p in promoted if p[0].endswith('/etc')], promoted
    assert [f for f in failures
            if f[0].endswith('/var') and f[1]], failures


def _fake_statmount_modules(monkeypatch, statmount):
    fake_mod = types.SimpleNamespace(statmount=statmount)
    monkeypatch.setitem(sys.modules, 'truenas_os_pyutils.mount', fake_mod)
    monkeypatch.setitem(
        sys.modules, 'truenas_os_pyutils',
        types.SimpleNamespace(mount=fake_mod),
    )


def test_running_dataset_real_body(monkeypatch):
    recorded = {}

    def fake_statmount(*, path):
        recorded['path'] = path
        return {'mount_source': 'bp/ROOT/current', 'fs_type': 'zfs'}

    _fake_statmount_modules(monkeypatch, fake_statmount)
    assert engine.running_dataset() == 'bp/ROOT/current'
    assert recorded['path'] == '/'


def test_running_dataset_non_zfs_root_maps_to_none(monkeypatch):
    def fake_statmount(*, path):
        return {'mount_source': '/dev/sda1', 'fs_type': 'ext4'}

    _fake_statmount_modules(monkeypatch, fake_statmount)
    assert engine.running_dataset() is None


def test_running_dataset_failure_raises_typed_error(monkeypatch):
    # detection failure must fail closed: the destroy guards derive
    # from this answer, so it must never read as 'no running BE'
    def broken(*, path):
        raise RuntimeError('statmount broke')

    _fake_statmount_modules(monkeypatch, broken)
    with pytest.raises(BEError):
        engine.running_dataset()


def test_running_dataset_unnamed_zfs_root_falls_back_to_mount_table(monkeypatch):
    # kernels without STATMOUNT_SB_SOURCE report mount_source as None;
    # the dataset is then recovered from the mount table, as zectl did
    def fake_statmount(*, path):
        return {'mount_source': None, 'fs_type': 'zfs'}

    _fake_statmount_modules(monkeypatch, fake_statmount)
    monkeypatch.setattr(engine, '_root_dataset_from_mounts',
                        lambda: 'bp/ROOT/from-mounts')
    assert engine.running_dataset() == 'bp/ROOT/from-mounts'


def test_running_dataset_unnamed_and_no_mount_entry_raises(monkeypatch):
    # both statmount and the mount table failing to name the root must
    # fail closed, never read as there being no running boot environment
    def fake_statmount(*, path):
        return {'mount_source': None, 'fs_type': 'zfs'}

    _fake_statmount_modules(monkeypatch, fake_statmount)
    monkeypatch.setattr(engine, '_root_dataset_from_mounts', lambda: None)
    with pytest.raises(BEError):
        engine.running_dataset()


def test_root_dataset_from_mounts_parses_zfs_root(monkeypatch, tmp_path):
    mounts = tmp_path / 'mounts'
    mounts.write_text(
        'sysfs /sys sysfs rw 0 0\n'
        'boot-pool/ROOT/be1 / zfs ro,xattr 0 0\n'
        'tmpfs /tmp tmpfs rw 0 0\n'
    )
    monkeypatch.setattr(engine, '_PROC_MOUNTS', str(mounts))
    assert engine._root_dataset_from_mounts() == 'boot-pool/ROOT/be1'


def test_root_dataset_from_mounts_none_when_root_not_zfs(monkeypatch, tmp_path):
    mounts = tmp_path / 'mounts'
    mounts.write_text('/dev/sda1 / ext4 rw 0 0\ntmpfs /tmp tmpfs rw 0 0\n')
    monkeypatch.setattr(engine, '_PROC_MOUNTS', str(mounts))
    assert engine._root_dataset_from_mounts() is None


def test_internal_clone_refused_before_any_destruction(be_layout):
    # a clone whose origin is another member of the same subtree cannot
    # be promoted away externally; destroy must refuse before touching
    # anything rather than fail midway through the leaf walk
    lz, _pool_hdl, be1 = be_layout
    _sh(f'zfs create -o canmount=noauto -o mountpoint=legacy {be1}/data')
    _sh(f'zfs snapshot {be1}/data@internal')
    _sh(f'zfs clone -o canmount=noauto -o mountpoint=legacy '
        f'{be1}/data@internal {be1}/aclone')
    with pytest.raises(BEError, match='inside the same'):
        engine.destroy(lz, dataset=be1, running_ds=f'{POOL}/ROOT/other')
    # nothing was destroyed
    for suffix in ('', '/data', '/aclone'):
        assert engine._exists(lz, f'{be1}{suffix}'), suffix
    assert engine._exists(lz, f'{be1}/data@internal')
