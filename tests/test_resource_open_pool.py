"""
Tests for ZFSResource.open_pool():
  - returns a ZFSPool object
  - pool name matches the resource's pool attribute
  - works on root dataset, child dataset, and volume
  - multiple calls return independent handles
"""

import pytest
import truenas_pylibzfs

POOL_NAME = 'testpool_open_pool'


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


class TestOpenPoolBasic:
    def test_returns_zfspool(self, pool):
        lz, _, root = pool
        result = root.open_pool()
        assert isinstance(result, truenas_pylibzfs.libzfs_types.ZFSPool)

    def test_pool_name_matches(self, pool):
        lz, _, root = pool
        result = root.open_pool()
        assert result.name == POOL_NAME

    def test_pool_name_matches_resource_pool_attr(self, pool):
        lz, _, root = pool
        result = root.open_pool()
        assert result.name == root.pool

    def test_no_args_accepted(self, pool):
        lz, _, root = pool
        root.open_pool()  # must not raise

    def test_positional_arg_raises(self, pool):
        lz, _, root = pool
        with pytest.raises(TypeError):
            root.open_pool(POOL_NAME)


class TestOpenPoolOnChildDataset:
    def test_child_dataset_returns_pool(self, pool):
        lz, _, root = pool
        child_name = f'{POOL_NAME}/child_ds'
        lz.create_resource(
            name=child_name,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )
        try:
            child = lz.open_resource(name=child_name)
            result = child.open_pool()
            assert isinstance(result, truenas_pylibzfs.libzfs_types.ZFSPool)
            assert result.name == POOL_NAME
        finally:
            lz.destroy_resource(name=child_name)

    def test_nested_child_returns_correct_pool(self, pool):
        lz, _, root = pool
        parent_name = f'{POOL_NAME}/nested'
        child_name = f'{POOL_NAME}/nested/deep'
        lz.create_resource(
            name=parent_name,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )
        lz.create_resource(
            name=child_name,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )
        try:
            deep = lz.open_resource(name=child_name)
            result = deep.open_pool()
            assert result.name == POOL_NAME
        finally:
            lz.destroy_resource(name=child_name)
            lz.destroy_resource(name=parent_name)


class TestOpenPoolIndependence:
    def test_multiple_calls_return_independent_handles(self, pool):
        lz, _, root = pool
        p1 = root.open_pool()
        p2 = root.open_pool()
        assert p1 is not p2
        assert p1.name == p2.name

    def test_returned_pool_can_call_root_dataset(self, pool):
        lz, _, root = pool
        p = root.open_pool()
        ds = p.root_dataset()
        assert ds.name == POOL_NAME
