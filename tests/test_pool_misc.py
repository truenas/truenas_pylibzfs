"""
Tests for miscellaneous ZFSPool methods:
  - dump_config()
  - root_dataset()
  - prefetch()
  - ddt_prune() argument validation
  - asdict() structure
  - sync_pool() and refresh_stats()
"""

import pytest
import truenas_pylibzfs

POOL_NAME = 'testpool_misc'


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


# ---------------------------------------------------------------------------
# dump_config
# ---------------------------------------------------------------------------

class TestDumpConfig:
    def test_returns_dict(self, pool):
        _, p, _ = pool
        result = p.dump_config()
        assert isinstance(result, dict)

    def test_contains_pool_name(self, pool):
        _, p, _ = pool
        result = p.dump_config()
        assert POOL_NAME in str(result)

    def test_no_args_required(self, pool):
        _, p, _ = pool
        p.dump_config()  # must not raise


# ---------------------------------------------------------------------------
# root_dataset
# ---------------------------------------------------------------------------

class TestRootDataset:
    def test_returns_object(self, pool):
        _, p, _ = pool
        ds = p.root_dataset()
        assert ds is not None

    def test_name_matches_pool(self, pool):
        _, p, _ = pool
        ds = p.root_dataset()
        assert ds.name == POOL_NAME

    def test_has_name_attribute(self, pool):
        _, p, _ = pool
        ds = p.root_dataset()
        assert hasattr(ds, 'name')


# ---------------------------------------------------------------------------
# prefetch
# ---------------------------------------------------------------------------

class TestPrefetch:
    def test_returns_none(self, pool):
        _, p, _ = pool
        assert p.prefetch() is None

    def test_callable_without_args(self, pool):
        _, p, _ = pool
        p.prefetch()  # must not raise


# ---------------------------------------------------------------------------
# ddt_prune argument validation
# ---------------------------------------------------------------------------

class TestDdtPrune:
    def test_no_args_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError)):
            p.ddt_prune()

    def test_both_args_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError)):
            p.ddt_prune(days=1, percentage=50)

    def test_negative_days_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError, OverflowError)):
            p.ddt_prune(days=-1)

    def test_percentage_zero_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError)):
            p.ddt_prune(percentage=0)

    def test_percentage_over_100_raises(self, pool):
        _, p, _ = pool
        with pytest.raises((TypeError, ValueError)):
            p.ddt_prune(percentage=101)

    def test_keyword_only(self, pool):
        _, p, _ = pool
        with pytest.raises(TypeError):
            p.ddt_prune(1)


# ---------------------------------------------------------------------------
# asdict
# ---------------------------------------------------------------------------

class TestAsdict:
    def test_returns_dict(self, pool):
        _, p, _ = pool
        assert isinstance(p.asdict(), dict)

    def test_has_name_key(self, pool):
        _, p, _ = pool
        assert 'name' in p.asdict()

    def test_name_matches_pool(self, pool):
        _, p, _ = pool
        assert p.asdict()['name'] == POOL_NAME

    def test_has_guid_key(self, pool):
        _, p, _ = pool
        assert 'guid' in p.asdict()

    def test_guid_is_int(self, pool):
        _, p, _ = pool
        assert isinstance(p.asdict()['guid'], int)

    def test_asdict_false_returns_struct(self, pool):
        _, p, _ = pool
        result = p.asdict(asdict=False)
        assert not isinstance(result, dict)
        assert hasattr(result, 'name')

    def test_status_present(self, pool):
        _, p, _ = pool
        assert 'status' in p.asdict()


# ---------------------------------------------------------------------------
# sync_pool and refresh_stats
# ---------------------------------------------------------------------------

class TestSyncAndRefresh:
    def test_sync_pool_returns_none(self, pool):
        _, p, _ = pool
        assert p.sync_pool() is None

    def test_refresh_stats_callable(self, pool):
        _, p, _ = pool
        result = p.refresh_stats()
        assert result is None or isinstance(result, dict)
