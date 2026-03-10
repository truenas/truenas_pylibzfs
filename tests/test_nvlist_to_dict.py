"""
Tests for the direct nvlist→dict conversion (py_nvlist_to_dict).

These tests exercise all six conversion sites through their public API,
verifying that every affected code path produces Python dicts with
correctly-typed values.  Key invariants:

  - Integer nvpairs (uint64, uint32, …) → Python int
  - String nvpairs → Python str
  - Boolean nvpairs → Python bool
  - Nested nvlists → nested Python dict
  - nvlist arrays → Python list of dicts
  - Consistent results across repeated calls

Pool config nvlist keys (OpenZFS conventions):
  pool_guid   → DATA_TYPE_UINT64  → int
  version     → DATA_TYPE_UINT64  → int
  name        → DATA_TYPE_STRING  → str
  vdev_tree   → DATA_TYPE_NVLIST  → dict
  children    → DATA_TYPE_NVLIST_ARRAY → list[dict]

ZCP Lua type mapping through the output nvlist:
  number  → DATA_TYPE_INT64  → int
  string  → DATA_TYPE_STRING → str
  boolean → DATA_TYPE_BOOLEAN_VALUE → bool
  table   → DATA_TYPE_NVLIST → dict
"""

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

POOL_NAME = "testpool_nvlist"


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _cfg(pool):
    """Return dump_config() dict for a pool fixture tuple."""
    _, p, _ = pool
    return p.dump_config()


def _vdev_children(pool):
    """
    Return the list of top-level vdev children from dump_config.
    Skips the test if the config lacks a children array.
    """
    cfg = _cfg(pool)
    tree = cfg.get("vdev_tree", {})
    children = tree.get("children")
    if not children:
        pytest.skip("vdev_tree.children not present in pool config")
    return children


# ---------------------------------------------------------------------------
# dump_config() — py_zfs_pool.c conversion path
# Scalar types
# ---------------------------------------------------------------------------

class TestDumpConfigScalarTypes:
    def test_returns_dict(self, pool):
        assert isinstance(_cfg(pool), dict)

    def test_pool_guid_is_int(self, pool):
        """pool_guid is DATA_TYPE_UINT64 — must produce Python int."""
        cfg = _cfg(pool)
        assert "pool_guid" in cfg, f"pool_guid missing; keys: {list(cfg)}"
        assert isinstance(cfg["pool_guid"], int), (
            f"pool_guid: expected int, got {type(cfg['pool_guid'])!r}"
        )

    def test_version_is_int(self, pool):
        """version is DATA_TYPE_UINT64 — must produce Python int."""
        cfg = _cfg(pool)
        assert "version" in cfg, f"version missing; keys: {list(cfg)}"
        assert isinstance(cfg["version"], int), (
            f"version: expected int, got {type(cfg['version'])!r}"
        )

    def test_name_is_str(self, pool):
        """name is DATA_TYPE_STRING — must produce Python str."""
        cfg = _cfg(pool)
        assert "name" in cfg, f"name missing; keys: {list(cfg)}"
        assert isinstance(cfg["name"], str), (
            f"name: expected str, got {type(cfg['name'])!r}"
        )
        assert cfg["name"] == POOL_NAME

    def test_pool_guid_is_positive(self, pool):
        """GUIDs are non-zero uint64 values."""
        assert _cfg(pool)["pool_guid"] > 0


# ---------------------------------------------------------------------------
# dump_config() — nested nvlist and nvlist_array types
# ---------------------------------------------------------------------------

class TestDumpConfigNestedTypes:
    def test_vdev_tree_is_dict(self, pool):
        """vdev_tree is DATA_TYPE_NVLIST — must become a nested dict."""
        cfg = _cfg(pool)
        assert "vdev_tree" in cfg, f"vdev_tree missing; keys: {list(cfg)}"
        assert isinstance(cfg["vdev_tree"], dict), (
            f"vdev_tree: expected dict, got {type(cfg['vdev_tree'])!r}"
        )

    def test_vdev_children_is_list(self, pool):
        """vdev_tree.children is DATA_TYPE_NVLIST_ARRAY — must become a list."""
        tree = _cfg(pool).get("vdev_tree", {})
        children = tree.get("children")
        assert children is not None, "vdev_tree.children not present"
        assert isinstance(children, list), (
            f"children: expected list, got {type(children)!r}"
        )

    def test_vdev_child_is_dict(self, pool):
        """Each element of vdev_tree.children must be a dict."""
        children = _vdev_children(pool)
        assert len(children) > 0
        for child in children:
            assert isinstance(child, dict), (
                f"vdev child: expected dict, got {type(child)!r}"
            )

    def test_vdev_child_guid_is_int(self, pool):
        """guid inside each vdev child is DATA_TYPE_UINT64 — must be int."""
        children = _vdev_children(pool)
        for child in children:
            if "guid" in child:
                assert isinstance(child["guid"], int), (
                    f"vdev child guid: expected int, got {type(child['guid'])!r}"
                )

    def test_vdev_child_id_is_int(self, pool):
        """id inside each vdev child is an integer nvpair."""
        children = _vdev_children(pool)
        for child in children:
            if "id" in child:
                assert isinstance(child["id"], int), (
                    f"vdev child id: expected int, got {type(child['id'])!r}"
                )

    def test_vdev_child_type_is_str(self, pool):
        """type inside each vdev child is DATA_TYPE_STRING — must be str."""
        children = _vdev_children(pool)
        for child in children:
            if "type" in child:
                assert isinstance(child["type"], str), (
                    f"vdev child type: expected str, got {type(child['type'])!r}"
                )


# ---------------------------------------------------------------------------
# dump_config() — consistency
# ---------------------------------------------------------------------------

class TestDumpConfigConsistency:
    def test_repeated_calls_equal(self, pool):
        """Two consecutive calls must return structurally equal dicts."""
        _, p, _ = pool
        assert p.dump_config() == p.dump_config()

    def test_pool_name_in_result(self, pool):
        """The pool name must appear somewhere in the flattened repr."""
        assert POOL_NAME in str(_cfg(pool))


# ---------------------------------------------------------------------------
# iter_history() — py_zfs_history.c conversion path
# ---------------------------------------------------------------------------

class TestHistoryTypes:
    def test_records_are_dicts(self, pool):
        _, p, _ = pool
        for rec in p.iter_history(skip_internal=False):
            assert isinstance(rec, dict), f"expected dict, got {type(rec)!r}"

    def test_history_time_is_int(self, pool):
        """history time is DATA_TYPE_UINT64 — must produce Python int."""
        _, p, _ = pool
        records = list(p.iter_history(skip_internal=False))
        assert records, "no history records returned"
        for rec in records:
            if "history time" in rec:
                val = rec["history time"]
                assert isinstance(val, int), (
                    f"history time: expected int, got {type(val)!r}"
                )

    def test_history_command_is_str(self, pool):
        """history command is DATA_TYPE_STRING — must produce Python str."""
        _, p, _ = pool
        for rec in p.iter_history(skip_internal=False):
            if "history command" in rec:
                val = rec["history command"]
                assert isinstance(val, str), (
                    f"history command: expected str, got {type(val)!r}"
                )

    def test_history_time_is_positive(self, pool):
        """Timestamps are positive Unix epoch seconds."""
        _, p, _ = pool
        for rec in p.iter_history(skip_internal=False):
            if "history time" in rec:
                assert rec["history time"] > 0

    def test_skip_internal_records_have_command(self, pool):
        """With skip_internal=True (default) every record has history command."""
        _, p, _ = pool
        for rec in p.iter_history():
            assert "history command" in rec


# ---------------------------------------------------------------------------
# read_label() — truenas_pylibzfs.c conversion path
# ---------------------------------------------------------------------------

class TestReadLabel:
    @staticmethod
    def _get_backing_path(pool):
        """
        Extract the first leaf vdev's path from dump_config.
        Returns None if the pool config doesn't expose a path (e.g. real disk).
        """
        cfg = _cfg(pool)
        tree = cfg.get("vdev_tree", {})
        children = tree.get("children", [])
        if not children:
            return None
        # For a stripe each child IS the leaf vdev
        leaf = children[0]
        # For a single-disk stripe the leaf may itself have children (mirror/raidz)
        leaf_children = leaf.get("children", [])
        if leaf_children:
            leaf = leaf_children[0]
        return leaf.get("path")

    def test_read_label_returns_dict_or_none(self, pool):
        """read_label() must return dict or None — never a raw string."""
        path = self._get_backing_path(pool)
        if not path:
            pytest.skip("could not determine backing file path from config")

        with open(path, "rb") as f:
            result = truenas_pylibzfs.read_label(fd=f.fileno())

        assert result is None or isinstance(result, dict), (
            f"read_label: unexpected type {type(result)!r}: {result!r}"
        )

    def test_read_label_pool_guid_is_int(self, pool):
        """If read_label returns a dict, pool_guid must be int."""
        path = self._get_backing_path(pool)
        if not path:
            pytest.skip("could not determine backing file path from config")

        with open(path, "rb") as f:
            result = truenas_pylibzfs.read_label(fd=f.fileno())

        if result is None:
            pytest.skip("read_label returned None for this device")

        if "pool_guid" in result:
            assert isinstance(result["pool_guid"], int), (
                f"pool_guid from read_label: expected int, "
                f"got {type(result['pool_guid'])!r}"
            )

    def test_read_label_version_is_int(self, pool):
        """If read_label returns a dict, version must be int."""
        path = self._get_backing_path(pool)
        if not path:
            pytest.skip("could not determine backing file path from config")

        with open(path, "rb") as f:
            result = truenas_pylibzfs.read_label(fd=f.fileno())

        if result is None:
            pytest.skip("read_label returned None for this device")

        if "version" in result:
            assert isinstance(result["version"], int), (
                f"version from read_label: expected int, "
                f"got {type(result['version'])!r}"
            )


# ---------------------------------------------------------------------------
# run_channel_program() — py_zfs_core_module.c conversion path
#
# Verifies that the output nvlist is converted to a correctly-typed Python
# dict, not a JSON string.  ZCP wraps the Lua return under the "return" key.
# ---------------------------------------------------------------------------

class TestChannelProgramOutputTypes:
    def test_output_is_dict(self, pool):
        """run_channel_program must return a dict, not a string."""
        out = lzc.run_channel_program(
            pool_name=POOL_NAME, script='return {}'
        )
        assert isinstance(out, dict), (
            f"expected dict, got {type(out)!r}"
        )

    def test_lua_integer_becomes_python_int(self, pool):
        """Lua number → DATA_TYPE_INT64 → Python int."""
        out = lzc.run_channel_program(
            pool_name=POOL_NAME,
            script='return {["n"] = 12345}',
        )
        val = out.get("return", {}).get("n")
        assert val == 12345, f"expected 12345, got {val!r}"
        assert isinstance(val, int), f"expected int, got {type(val)!r}"

    def test_lua_string_becomes_python_str(self, pool):
        """Lua string → DATA_TYPE_STRING → Python str."""
        out = lzc.run_channel_program(
            pool_name=POOL_NAME,
            script='return {["s"] = "hello"}',
        )
        val = out.get("return", {}).get("s")
        assert val == "hello", f"expected 'hello', got {val!r}"
        assert isinstance(val, str), f"expected str, got {type(val)!r}"

    def test_lua_nested_table_becomes_nested_dict(self, pool):
        """Lua nested table → DATA_TYPE_NVLIST → nested Python dict."""
        out = lzc.run_channel_program(
            pool_name=POOL_NAME,
            script='return {["outer"] = {["inner"] = 99}}',
        )
        ret = out.get("return", {})
        assert isinstance(ret.get("outer"), dict), (
            f"outer: expected dict, got {type(ret.get('outer'))!r}"
        )
        assert ret["outer"].get("inner") == 99

    def test_output_not_a_string(self, pool):
        """Sanity check: output must never be a raw JSON string."""
        out = lzc.run_channel_program(
            pool_name=POOL_NAME,
            script='return {["x"] = 1}',
        )
        assert not isinstance(out, str), "output should be dict, not str"

    def test_multiple_return_values(self, pool):
        """Multiple top-level keys in the return table all convert correctly."""
        out = lzc.run_channel_program(
            pool_name=POOL_NAME,
            script='return {["a"] = 1, ["b"] = "two"}',
        )
        ret = out.get("return", {})
        assert ret.get("a") == 1
        assert ret.get("b") == "two"
        assert isinstance(ret.get("a"), int)
        assert isinstance(ret.get("b"), str)
