"""Unit tests for truenas_bootenv.naming (pure Python, no ZFS needed)."""

import pytest
import re
from truenas_bootenv import naming


class TestSnapshotSuffix:
    # 2026-07-01 14:03:05.123456789 UTC as nanoseconds
    FIXED_NS = 1_782_914_585_123_456_789

    def test_format(self):
        s = naming.snapshot_suffix(now_ns=self.FIXED_NS)
        # date-time part depends on the box's timezone; the shape does not
        assert re.fullmatch(
            r'\d{4}-\d{2}-\d{2}-\d{2}:\d{2}:\d{2}\.\d{6}', s
        ), s

    def test_microseconds_from_clock(self):
        s = naming.snapshot_suffix(now_ns=self.FIXED_NS)
        assert s.endswith('.123456')

    def test_only_valid_zfs_component_chars(self):
        s = naming.snapshot_suffix()
        assert naming.validate_component(s) is None

    def test_second_resolution_collision_is_distinct(self):
        # Two creates within the same second differ in the microsecond tail
        a = naming.snapshot_suffix(now_ns=self.FIXED_NS)
        b = naming.snapshot_suffix(now_ns=self.FIXED_NS + 1_000)  # +1us
        assert a != b


class TestBump:
    def test_first_bump_appends_counter(self):
        assert (naming.bump('2026-07-01-14:03:05.123456')
                == '2026-07-01-14:03:05.123456-1')

    def test_bump_increments_existing_counter(self):
        assert (naming.bump('2026-07-01-14:03:05.123456-1')
                == '2026-07-01-14:03:05.123456-2')
        assert (naming.bump('2026-07-01-14:03:05.123456-9')
                == '2026-07-01-14:03:05.123456-10')

    def test_bump_result_is_valid_component(self):
        s = naming.bump(naming.snapshot_suffix())
        assert naming.validate_component(s) is None

    def test_bump_without_any_dash(self):
        assert naming.bump('plain') == 'plain-1'

    def test_bump_sequence_is_strictly_increasing(self):
        s = naming.snapshot_suffix()
        seen = {s}
        for _ in range(5):
            s = naming.bump(s)
            assert s not in seen
            seen.add(s)


class TestRelPath:
    def test_root_itself_is_empty(self):
        assert naming.rel_path('p/ROOT/a', 'p/ROOT/a') == ''

    def test_direct_child(self):
        assert naming.rel_path('p/ROOT/a', 'p/ROOT/a/var') == 'var'

    def test_nested_child(self):
        assert naming.rel_path('p/ROOT/a', 'p/ROOT/a/var/log') == 'var/log'

    def test_sibling_with_shared_string_prefix_rejected(self):
        # 'p/ROOT/ab' shares the string prefix 'p/ROOT/a' but is NOT a
        # descendant; a naive startswith() check would wrongly accept it.
        with pytest.raises(ValueError):
            naming.rel_path('p/ROOT/a', 'p/ROOT/ab')

    def test_unrelated_dataset_rejected(self):
        with pytest.raises(ValueError):
            naming.rel_path('p/ROOT/a', 'p/OTHER/x')


class TestMapTarget:
    def test_root_maps_to_target_root(self):
        assert (naming.map_target('p/ROOT/a', 'p/ROOT/b', 'p/ROOT/a')
                == 'p/ROOT/b')

    def test_child_maps_under_target(self):
        assert (naming.map_target('p/ROOT/a', 'p/ROOT/b', 'p/ROOT/a/var')
                == 'p/ROOT/b/var')

    def test_nested_child_maps_under_target(self):
        assert naming.map_target(
            'p/ROOT/a', 'p/ROOT/b', 'p/ROOT/a/var/log'
        ) == 'p/ROOT/b/var/log'


class TestValidateBeDataset:
    @pytest.mark.parametrize('dataset', [
        'boot-pool/ROOT/25.10.0',
        'boot-pool/ROOT/25.10.0-1',
        'tank/ROOT/my_be',
    ])
    def test_valid_be_paths(self, dataset):
        assert naming.validate_be_dataset(dataset) is None

    @pytest.mark.parametrize('dataset', [
        'tank',                        # bare pool: would walk the whole pool
        'boot-pool/ROOT',              # the BE container, not a BE
        'boot-pool/ROOT/be1/var',      # a child inside a BE
        'boot-pool/data/be1',          # not under ROOT
        'boot-pool/root/be1',          # ROOT is case-sensitive
        'boot-pool/ROOT/bad@name',     # invalid component
        '',
    ])
    def test_invalid_be_paths(self, dataset):
        assert naming.validate_be_dataset(dataset) is not None


class TestValidateComponent:
    @pytest.mark.parametrize('name', [
        '25.10.0',
        '25.10.0-1',
        'my_be',
        'pre.release:test',   # colon is legal in ZFS components
    ])
    def test_valid_names(self, name):
        assert naming.validate_component(name) is None

    @pytest.mark.parametrize('name', [
        'a/b',                # '/' creates a child dataset
        'a@b',                # '@' is the snapshot delimiter
        'a#b',                # '#' is the bookmark delimiter
        'a!b',
        '\xfcn\xefcode',      # 'unicode' with u-umlaut / i-diaeresis
    ])
    def test_invalid_characters(self, name):
        assert naming.validate_component(name) is not None

    def test_empty_rejected(self):
        assert naming.validate_component('') is not None

    @pytest.mark.parametrize('name', ['.', '..'])
    def test_reserved_names_rejected(self, name):
        assert naming.validate_component(name) is not None

    def test_overlong_component_rejected(self):
        assert naming.validate_component('a' * 256) is not None
        assert naming.validate_component('a' * 255) is None

    @pytest.mark.parametrize('name', [
        'be1 ', ' ', 'a ', 'pre release ',   # trailing
        ' lead', 'in ner',                   # leading / interior
    ])
    def test_any_space_rejected(self, name):
        # ZFS accepts interior spaces, but BE names must round-trip the
        # sh-based grub menu generator whose unquoted expansions break
        # on them (see naming.py); trailing spaces additionally match
        # the binding's name_is_valid() refusal
        assert naming.validate_component(name) is not None


class TestBeDataset:
    def test_composes_valid_path(self):
        assert (naming.be_dataset('boot-pool', '25.10.0')
                == 'boot-pool/ROOT/25.10.0')

    def test_invalid_name_raises_with_reason(self):
        with pytest.raises(ValueError, match='invalid character'):
            naming.be_dataset('boot-pool', 'bad name')

    def test_invalid_pool_raises(self):
        with pytest.raises(ValueError):
            naming.be_dataset('b@d', 'be1')

    def test_overlong_total_path_raises(self):
        with pytest.raises(ValueError):
            naming.be_dataset('p', 'a' * 249)


class TestBumpEdge:
    def test_all_digit_dashless_suffix_appends_not_increments(self):
        # rpartition('-') on a dashless string yields an empty separator;
        # the sep guard must append '-1', never treat '123' as a counter
        assert naming.bump('123') == '123-1'


class TestValidateBeDatasetEdge:
    def test_total_length_cap(self):
        # 'p/ROOT/' + 249 = 256 > 255
        assert naming.validate_be_dataset('p/ROOT/' + 'a' * 249) is not None
        assert naming.validate_be_dataset('p/ROOT/' + 'a' * 248) is None

    def test_pool_component_validated(self):
        assert naming.validate_be_dataset('bad pool /ROOT/be') is not None
        assert naming.validate_be_dataset('b@d/ROOT/be') is not None
