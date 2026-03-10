import enum
import os
import pytest
import shutil
import tempfile
import truenas_pylibzfs

ZPOOLStatus = truenas_pylibzfs.libzfs_types.ZPOOLStatus
ZPOOLProperty = truenas_pylibzfs.libzfs_types.ZPOOLProperty

POOL_NAME = 'testpool_import'
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
        d = tempfile.mkdtemp(prefix='pylibzfs_import_disks_')
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


def _destroy_pool():
    try:
        lz = truenas_pylibzfs.open_handle()
        lz.destroy_pool(name=POOL_NAME, force=True)
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Fixture: exported pool ready to import
# ---------------------------------------------------------------------------

@pytest.fixture
def exported_pool(make_disks):
    """
    Single-disk stripe, exported and ready to be (re-)imported.
    Yields (lz, disk_dir, guid).
    """
    disks = make_disks(1)
    disk_dir = os.path.dirname(disks[0])
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(
        name=POOL_NAME,
        storage_vdevs=[
            truenas_pylibzfs.create_vdev_spec(
                vdev_type=truenas_pylibzfs.VDevType.FILE, name=disks[0]
            )
        ],
        force=True,
    )
    pool = lz.open_pool(name=POOL_NAME)
    guid = pool.get_properties(properties={ZPOOLProperty.GUID}).guid.value
    lz.export_pool(name=POOL_NAME)
    try:
        yield lz, disk_dir, guid
    finally:
        _destroy_pool()


# ---------------------------------------------------------------------------
# import_pool_find — discovery
# ---------------------------------------------------------------------------

class TestImportPoolFind:

    def test_find_returns_list(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert isinstance(result, list)

    def test_find_discovers_exported_pool(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1

    def test_find_entry_has_status_fields(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1
        entry = result[0]
        assert hasattr(entry, 'status')
        assert hasattr(entry, 'reason')
        assert hasattr(entry, 'action')
        assert hasattr(entry, 'corrupted_files')
        assert hasattr(entry, 'storage_vdevs')
        assert hasattr(entry, 'support_vdevs')
        assert hasattr(entry, 'spares')

    def test_find_status_is_zpool_status_enum(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1
        assert isinstance(result[0].status, ZPOOLStatus)

    def test_find_storage_vdevs_is_tuple(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1
        assert isinstance(result[0].storage_vdevs, tuple)

    def test_find_spares_is_tuple(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1
        assert isinstance(result[0].spares, tuple)

    def test_find_corrupted_files_is_tuple(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1
        assert isinstance(result[0].corrupted_files, tuple)

    def test_find_no_device_returns_list(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        # Default blkid-based scan — may or may not discover the pool;
        # we only verify the return type.
        result = lz.import_pool_find()
        assert isinstance(result, list)

    def test_find_empty_dir_returns_empty(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        tmpdir = tempfile.mkdtemp(prefix='pylibzfs_empty_')
        try:
            result = lz.import_pool_find(device=tmpdir)
            assert result == []
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

    def test_find_keyword_only(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        with pytest.raises(TypeError):
            lz.import_pool_find(disk_dir)  # type: ignore[call-arg]

    def test_find_storage_vdevs_nonempty_for_stripe(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1
        entry = result[0]
        assert len(entry.storage_vdevs) >= 1

    def test_find_support_vdevs_has_categories(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1
        sv = result[0].support_vdevs
        assert hasattr(sv, 'cache')
        assert hasattr(sv, 'log')
        assert hasattr(sv, 'special')
        assert hasattr(sv, 'dedup')

    def test_find_stripe_has_no_support_vdevs(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1
        sv = result[0].support_vdevs
        assert sv.cache == ()
        assert sv.log == ()
        assert sv.special == ()
        assert sv.dedup == ()

    def test_find_stripe_has_no_spares(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        result = lz.import_pool_find(device=disk_dir)
        assert len(result) >= 1
        assert result[0].spares == ()


# ---------------------------------------------------------------------------
# import_pool — import by name
# ---------------------------------------------------------------------------

class TestImportPoolByName:

    def test_import_by_name_returns_zfspool(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir)
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()

    def test_import_by_name_pool_name_matches(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir)
        try:
            assert pool.name == POOL_NAME
        finally:
            _destroy_pool()

    def test_import_nonexistent_name_raises(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        with pytest.raises(truenas_pylibzfs.ZFSException):
            lz.import_pool(name='no_such_pool_xyz', device=disk_dir)

    def test_import_keyword_only(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        with pytest.raises(TypeError):
            lz.import_pool(POOL_NAME)  # type: ignore[call-arg]


# ---------------------------------------------------------------------------
# import_pool — import by GUID
# ---------------------------------------------------------------------------

class TestImportPoolByGUID:

    def test_import_by_guid_returns_zfspool(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(guid=guid, device=disk_dir)
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()

    def test_import_by_guid_pool_name_matches(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(guid=guid, device=disk_dir)
        try:
            assert pool.name == POOL_NAME
        finally:
            _destroy_pool()

    def test_import_nonexistent_guid_raises(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        with pytest.raises(truenas_pylibzfs.ZFSException):
            lz.import_pool(guid=0xDEADBEEFCAFEBABE, device=disk_dir)


# ---------------------------------------------------------------------------
# import_pool — parameter validation
# ---------------------------------------------------------------------------

class TestImportPoolValidation:

    def test_neither_name_nor_guid_raises_value_error(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(ValueError):
            lz.import_pool()

    def test_both_name_and_guid_raises_value_error(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        with pytest.raises(ValueError):
            lz.import_pool(name=POOL_NAME, guid=guid)

    def test_guid_must_be_int(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(TypeError):
            lz.import_pool(guid='not-an-int')  # type: ignore[arg-type]

    def test_name_must_be_str(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(TypeError):
            lz.import_pool(name=12345)  # type: ignore[arg-type]

    def test_altroot_must_be_str(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(TypeError):
            lz.import_pool(name=POOL_NAME, altroot=99)  # type: ignore[arg-type]

    def test_temporary_name_must_be_str(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(TypeError):
            lz.import_pool(name=POOL_NAME, temporary_name=42)  # type: ignore[arg-type]

    def test_properties_must_be_dict(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(TypeError):
            lz.import_pool(name=POOL_NAME, properties='not-a-dict')  # type: ignore[arg-type]

    def test_properties_invalid_string_key_raises_value_error(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(ValueError):
            # Properties are validated before the import attempt
            lz.import_pool(name=POOL_NAME, properties={'notaprop_xyz': 'val'})

    def test_properties_non_string_non_enum_key_raises_type_error(self):
        lz = truenas_pylibzfs.open_handle()
        with pytest.raises(TypeError):
            lz.import_pool(name=POOL_NAME, properties={99: 'val'})  # type: ignore[arg-type]

    def test_properties_value_must_be_str(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        with pytest.raises(TypeError):
            lz.import_pool(name=POOL_NAME, properties={'comment': 42})  # type: ignore[arg-type]


# ---------------------------------------------------------------------------
# import_pool — ZPOOLProperty enum keys for properties dict
# ---------------------------------------------------------------------------

class TestImportPoolPropertiesEnum:

    def test_zpool_property_is_int_enum(self):
        assert issubclass(ZPOOLProperty, enum.IntEnum)

    def test_zpool_property_in_root_module(self):
        assert hasattr(truenas_pylibzfs, 'ZPOOLProperty')
        assert truenas_pylibzfs.ZPOOLProperty is ZPOOLProperty

    def test_zpool_property_in_libzfs_types(self):
        assert hasattr(truenas_pylibzfs.libzfs_types, 'ZPOOLProperty')
        assert truenas_pylibzfs.libzfs_types.ZPOOLProperty is ZPOOLProperty

    def test_string_key_accepted(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir,
                              properties={'comment': 'test'})
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()

    def test_enum_key_accepted(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(
            name=POOL_NAME, device=disk_dir,
            properties={ZPOOLProperty.COMMENT: 'enum-comment'},
        )
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()

    def test_mixed_string_and_enum_keys_accepted(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(
            name=POOL_NAME, device=disk_dir,
            properties={
                ZPOOLProperty.COMMENT: 'enum-comment',
                'ashift': '0',
            },
        )
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()

    def test_none_properties_accepted(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir, properties=None)
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()

    def test_empty_properties_accepted(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir, properties={})
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()


# ---------------------------------------------------------------------------
# import_pool — flags
# ---------------------------------------------------------------------------

class TestImportPoolFlags:

    def test_allow_missing_log_accepted(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir,
                              allow_missing_log=True)
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()

    def test_force_accepted(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir, force=True)
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()

    def test_force_with_guid(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(guid=guid, device=disk_dir, force=True)
        try:
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()


# ---------------------------------------------------------------------------
# import_pool — altroot
# ---------------------------------------------------------------------------

class TestImportPoolAltroot:

    def test_altroot_returns_zfspool(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        altroot = tempfile.mkdtemp(prefix='pylibzfs_altroot_')
        try:
            pool = lz.import_pool(name=POOL_NAME, device=disk_dir,
                                  altroot=altroot)
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()
            shutil.rmtree(altroot, ignore_errors=True)

    def test_altroot_and_enum_property_combined(self, exported_pool):
        lz, disk_dir, guid = exported_pool
        altroot = tempfile.mkdtemp(prefix='pylibzfs_altroot_')
        try:
            pool = lz.import_pool(
                name=POOL_NAME, device=disk_dir,
                altroot=altroot,
                properties={ZPOOLProperty.COMMENT: 'altroot-test'},
            )
            assert type(pool).__name__ == 'ZFSPool'
        finally:
            _destroy_pool()
            shutil.rmtree(altroot, ignore_errors=True)


# ---------------------------------------------------------------------------
# import_pool — temporary_name
# ---------------------------------------------------------------------------

class TestImportPoolTemporaryName:

    def test_temporary_name_imports_under_temp_name(self, exported_pool):
        temp_name = POOL_NAME + '_temp'
        lz, disk_dir, guid = exported_pool
        pool = lz.import_pool(name=POOL_NAME, device=disk_dir,
                              temporary_name=temp_name)
        try:
            assert pool.name == temp_name
        finally:
            try:
                lz.destroy_pool(name=temp_name, force=True)
            except Exception:
                pass
            _destroy_pool()
