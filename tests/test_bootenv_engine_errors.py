"""Error-translation tests for truenas_bootenv.engine (root + zfs.ko).

Covers the paths the happy-path suites never reach: the ZFSException
translation wrapper, _lzc_error's errno branches, _exists re-raising
non-ENOENT failures, and the update-grub subprocess handling.
"""

import errno
import pytest
import subprocess
import truenas_pylibzfs
import types
from truenas_bootenv import engine
from truenas_bootenv.errors import (
    BEBusy,
    BEDestroyUnsafe,
    BEError,
    BEGrubError,
    BENotFound,
)

FST = truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
POOL = 'bootenverrs'
KVER = '6.18.35-test'

BE1 = f'{POOL}/ROOT/be1'
BE2 = f'{POOL}/ROOT/be2'


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


class TestExistsErrorPropagation:
    def test_non_enoent_zfs_error_propagates(self, be_layout):
        lz, _pool_hdl, _be1 = be_layout
        # an invalid name fails with EZFS_INVALIDNAME, which _exists
        # must NOT interpret as "does not exist"
        with pytest.raises(truenas_pylibzfs.ZFSException):
            engine._exists(lz, f'{POOL}/ROOT/bad@@name@@here')


class TestZfsErrorTranslation:
    def test_busy_destroy_becomes_bebusy(self, be_layout, tmp_path):
        # destroy refuses a mounted boot environment up front, so harvest
        # a real EZFS_BUSY from libzfs (destroying a mounted dataset
        # directly) and push it through the boundary wrapper: it must
        # translate to BEBusy, not the base BEError
        lz, _pool_hdl, be1 = be_layout
        engine.create(lz, source_dataset=be1, target_dataset=BE2)
        child = lz.open_resource(name=f'{BE2}/var')
        child.set_properties(
            properties={'mountpoint': str(tmp_path)}, remount=False,
        )
        child.mount()
        try:
            try:
                lz.destroy_resource(name=f'{BE2}/var')
            except truenas_pylibzfs.ZFSException as e:
                harvested = e

            @engine._translating_zfs_errors
            def boom():
                raise harvested

            with pytest.raises(BEBusy):
                boom()
        finally:
            lz.open_resource(name=f'{BE2}/var').unmount()

    def test_generic_zfs_error_becomes_beerror(self, be_layout):
        # the naming pre-checks block every crafted-name route to this
        # branch, so harvest a real non-ENOENT ZFSException from libzfs
        # and push it through the boundary wrapper: it must translate
        # to base BEError, not a subclass
        lz, _pool_hdl, _be1 = be_layout
        try:
            lz.open_resource(name='no/such@@bad##name')
        except truenas_pylibzfs.ZFSException as e:
            harvested = e

        @engine._translating_zfs_errors
        def boom():
            raise harvested

        with pytest.raises(BEError) as excinfo:
            boom()
        assert type(excinfo.value) is BEError
        assert 'ZFS error' in str(excinfo.value)


class TestLzcErrorMapping:
    def _exc(self, *pairs):
        return types.SimpleNamespace(errors=tuple(pairs))

    def test_ebusy_maps_to_bebusy(self):
        e = engine._lzc_error(self._exc(('p/ds@s', errno.EBUSY)), 'ctx')
        assert isinstance(e, BEBusy)
        assert e.errno == errno.EBUSY

    def test_eexist_destroying_means_clones(self):
        e = engine._lzc_error(self._exc(('p/ds@s', errno.EEXIST)), 'ctx')
        assert isinstance(e, BEDestroyUnsafe)

    def test_eexist_creating_means_collision(self):
        e = engine._lzc_error(
            self._exc(('p/ds@s', errno.EEXIST)), 'ctx', creating=True,
        )
        assert isinstance(e, BEError)
        assert not isinstance(e, BEDestroyUnsafe)
        assert 'already exists' in str(e)

    def test_unknown_errno_falls_through_to_beerror(self):
        e = engine._lzc_error(self._exc(('p/ds@s', errno.EIO)), 'ctx')
        assert type(e) is BEError

    def test_no_errors_attribute_falls_through(self):
        e = engine._lzc_error(RuntimeError('boom'), 'ctx')
        assert type(e) is BEError

    def test_create_snapshots_collision_raises_typed(self, be_layout):
        lz, _pool_hdl, be1 = be_layout
        truenas_pylibzfs.lzc.create_snapshots(snapshot_names={f'{be1}@dup'})
        with pytest.raises(BEError, match='already exists'):
            engine._lzc_create_snapshots({f'{be1}@dup'})


class _FakePoolLzh:
    """Stub handle: open_pool(name=).sync_pool() recorder."""

    def __init__(self):
        self.synced = []

    def open_pool(self, *, name):
        outer = self

        class _P:
            def sync_pool(self):
                outer.synced.append(name)

        return _P()


class TestUpdateGrub:
    def test_success_path_runs_grub_then_syncs_pool(self, monkeypatch):
        calls = []
        monkeypatch.setattr(
            engine.subprocess, 'run',
            lambda *a, **kw: calls.append(a) or None,
        )
        fake = _FakePoolLzh()
        engine._update_grub(fake, 'somepool')
        assert calls and calls[0][0] == ['update-grub']
        # crash durability: the menu rename must be txg-committed
        assert fake.synced == ['somepool']

    def test_failure_raises_begruberror_without_sync(self, monkeypatch):
        def fail(cmd, **kw):
            raise subprocess.CalledProcessError(1, cmd, output=b'boom out')

        monkeypatch.setattr(engine.subprocess, 'run', fail)
        fake = _FakePoolLzh()
        with pytest.raises(BEGrubError, match='boom out'):
            engine._update_grub(fake, 'somepool')
        assert fake.synced == []

    def test_activate_run_grub_wires_through(self, be_layout, monkeypatch):
        lz, _pool_hdl, be1 = be_layout
        calls = []
        monkeypatch.setattr(
            engine, '_update_grub', lambda lzh, pool: calls.append(pool),
        )
        engine.activate(lz, dataset=be1, run_grub=True)
        assert calls == [POOL]

    def test_create_run_grub_wires_through(self, be_layout, monkeypatch):
        lz, _pool_hdl, be1 = be_layout
        calls = []
        monkeypatch.setattr(
            engine, '_update_grub', lambda lzh, pool: calls.append(pool),
        )
        engine.create(
            lz, source_dataset=be1, target_dataset=BE2, run_grub=True,
        )
        assert calls == [POOL]

    def test_destroy_of_absent_be_still_regenerates_menu(
        self, be_layout, monkeypatch,
    ):
        lz, _pool_hdl, be1 = be_layout
        calls = []
        monkeypatch.setattr(
            engine, '_update_grub', lambda lzh, pool: calls.append(pool),
        )
        engine.destroy(
            lz, dataset=f'{POOL}/ROOT/ghost', running_ds=be1, run_grub=True,
        )
        assert calls == [POOL]

    # a menu failure after the verb's own work has committed must say so:
    # the caller of the CLI otherwise cannot tell whether the activate or
    # destroy took effect before update-grub broke. create() already
    # reports "was created successfully, but ..." the same way

    def _boom(self, monkeypatch):
        def fail(lzh, pool):
            raise BEGrubError('update-grub failed: boom')

        monkeypatch.setattr(engine, '_update_grub', fail)

    def test_activate_grub_failure_reports_activation_succeeded(
        self, be_layout, monkeypatch,
    ):
        lz, _pool_hdl, be1 = be_layout
        self._boom(monkeypatch)
        with pytest.raises(BEGrubError, match='was activated successfully'):
            engine.activate(lz, dataset=be1, run_grub=True)
        # the activation itself really took effect first
        assert engine._pool_bootfs(lz, POOL) == be1

    def test_destroy_grub_failure_reports_destroy_succeeded(
        self, be_layout, monkeypatch,
    ):
        lz, _pool_hdl, be1 = be_layout
        engine.create(lz, source_dataset=be1, target_dataset=BE2)
        self._boom(monkeypatch)
        with pytest.raises(BEGrubError, match='was destroyed successfully'):
            engine.destroy(lz, dataset=BE2, running_ds=be1, run_grub=True)
        assert not engine._exists(lz, BE2)

    def test_destroy_of_absent_grub_failure_says_already_removed(
        self, be_layout, monkeypatch,
    ):
        lz, _pool_hdl, be1 = be_layout
        self._boom(monkeypatch)
        with pytest.raises(BEGrubError, match='was already removed'):
            engine.destroy(
                lz, dataset=f'{POOL}/ROOT/ghost', running_ds=be1,
                run_grub=True,
            )


class TestWrapperNoentAndErrno:
    def test_harvested_noent_becomes_benotfound(self, be_layout):
        lz, _pool_hdl, _be1 = be_layout
        try:
            lz.open_resource(name=f'{POOL}/ROOT/definitely-absent')
        except truenas_pylibzfs.ZFSException as e:
            harvested = e

        @engine._translating_zfs_errors
        def boom():
            raise harvested

        with pytest.raises(BENotFound):
            boom()

    def test_busy_translation_carries_errno(self, be_layout, tmp_path):
        lz, _pool_hdl, be1 = be_layout
        engine.create(lz, source_dataset=be1, target_dataset=BE2)
        child = lz.open_resource(name=f'{BE2}/var')
        child.set_properties(
            properties={'mountpoint': str(tmp_path)}, remount=False,
        )
        child.mount()
        try:
            with pytest.raises(BEBusy) as excinfo:
                engine.destroy(lz, dataset=BE2, running_ds=be1)
            assert excinfo.value.errno == errno.EBUSY
        finally:
            lz.open_resource(name=f'{BE2}/var').unmount()
        engine.destroy(lz, dataset=BE2, running_ds=be1)
