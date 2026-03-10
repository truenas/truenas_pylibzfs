"""
Tests for the dict→nvlist conversion direction (py_dict_to_nvlist).

Round-trip verification via lzc.run_channel_program(): Python values are
encoded into the kwargs nvlist, passed to a Lua ZCP script that echoes them
back, and the returned nvlist is decoded to Python.  This exercises every
supported input type and the error cases for rejected inputs.

Supported Python→nvlist mappings under test:
  str         → DATA_TYPE_STRING
  bool        → DATA_TYPE_BOOLEAN_VALUE
  int (≥0)    → DATA_TYPE_INT64 / DATA_TYPE_UINT64
  int (<0)    → DATA_TYPE_INT64
  dict        → DATA_TYPE_NVLIST
  list[str]   → DATA_TYPE_STRING_ARRAY
  list[int]   → DATA_TYPE_INT64_ARRAY / DATA_TYPE_UINT64_ARRAY
  list[bool]  → DATA_TYPE_BOOLEAN_ARRAY
  list[dict]  → DATA_TYPE_NVLIST_ARRAY

Error cases:
  empty list      → ValueError
  mixed-type list → ValueError
  non-str key     → TypeError
"""

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

POOL_NAME = "testpool_d2n"


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _run(pool, script, args=None):
    """Run a ZCP script with optional args dict; return the 'return' subtable."""
    kwargs = {}
    if args is not None:
        kwargs["args"] = args
    out = lzc.run_channel_program(pool_name=POOL_NAME, script=script, **kwargs)
    return out.get("return", {})


# ---------------------------------------------------------------------------
# Scalar round-trips
# ---------------------------------------------------------------------------

class TestScalarRoundTrip:
    def test_str_roundtrip(self, pool):
        """str → DATA_TYPE_STRING → str."""
        ret = _run(pool,
                   'args = ...; return {["v"] = args["k"]}',
                   args={"k": "hello"})
        assert ret.get("v") == "hello"
        assert isinstance(ret.get("v"), str)

    def test_bool_true_roundtrip(self, pool):
        """True → DATA_TYPE_BOOLEAN_VALUE → True."""
        ret = _run(pool,
                   'args = ...; return {["v"] = args["k"]}',
                   args={"k": True})
        assert ret.get("v") is True

    def test_bool_false_roundtrip(self, pool):
        """False → DATA_TYPE_BOOLEAN_VALUE → False."""
        ret = _run(pool,
                   'args = ...; return {["v"] = args["k"]}',
                   args={"k": False})
        assert ret.get("v") is False

    def test_positive_int_roundtrip(self, pool):
        """Positive int → DATA_TYPE_INT64/UINT64 → int."""
        ret = _run(pool,
                   'args = ...; return {["v"] = args["k"]}',
                   args={"k": 42})
        assert ret.get("v") == 42
        assert isinstance(ret.get("v"), int)

    def test_negative_int_roundtrip(self, pool):
        """Negative int → DATA_TYPE_INT64 → int."""
        ret = _run(pool,
                   'args = ...; return {["v"] = args["k"]}',
                   args={"k": -7})
        assert ret.get("v") == -7
        assert isinstance(ret.get("v"), int)

    def test_nested_dict_roundtrip(self, pool):
        """dict value → DATA_TYPE_NVLIST → nested dict."""
        ret = _run(pool,
                   'args = ...; return {["sub"] = args["d"]}',
                   args={"d": {"x": 99}})
        sub = ret.get("sub")
        assert isinstance(sub, dict)
        assert sub.get("x") == 99


# ---------------------------------------------------------------------------
# List round-trips
# ---------------------------------------------------------------------------

class TestListRoundTrip:
    def test_list_of_str(self, pool):
        """list[str] → DATA_TYPE_STRING_ARRAY — ZCP returns the element count."""
        ret = _run(pool,
                   'args = ...; '
                   'local t = args["lst"]; '
                   'return {["n"] = #t, ["v0"] = t[1]}',
                   args={"lst": ["a", "b", "c"]})
        assert ret.get("n") == 3
        assert ret.get("v0") == "a"

    def test_list_of_positive_int(self, pool):
        """list[int≥0] → DATA_TYPE_UINT64_ARRAY — ZCP returns first element."""
        ret = _run(pool,
                   'args = ...; '
                   'local t = args["lst"]; '
                   'return {["n"] = #t, ["v0"] = t[1]}',
                   args={"lst": [10, 20, 30]})
        assert ret.get("n") == 3
        assert ret.get("v0") == 10

    def test_list_of_negative_int(self, pool):
        """list[int with negatives] → DATA_TYPE_INT64_ARRAY."""
        ret = _run(pool,
                   'args = ...; '
                   'local t = args["lst"]; '
                   'return {["n"] = #t, ["v0"] = t[1]}',
                   args={"lst": [-1, -2, -3]})
        assert ret.get("n") == 3
        assert ret.get("v0") == -1

    def test_list_of_bool(self, pool):
        """list[bool] → DATA_TYPE_BOOLEAN_ARRAY — ZCP returns first element."""
        ret = _run(pool,
                   'args = ...; '
                   'local t = args["lst"]; '
                   'return {["n"] = #t, ["v0"] = t[1]}',
                   args={"lst": [True, False, True]})
        assert ret.get("n") == 3
        assert ret.get("v0") is True

    def test_list_of_dict(self, pool):
        """list[dict] → DATA_TYPE_NVLIST_ARRAY — ZCP returns first sub-element."""
        ret = _run(pool,
                   'args = ...; '
                   'local t = args["lst"]; '
                   'return {["n"] = #t, ["v0x"] = t[1]["x"]}',
                   args={"lst": [{"x": 5}, {"x": 6}]})
        assert ret.get("n") == 2
        assert ret.get("v0x") == 5


# ---------------------------------------------------------------------------
# Error cases
# ---------------------------------------------------------------------------

class TestErrorCases:
    def test_empty_list_raises_value_error(self, pool):
        """Empty list must raise ValueError before calling ZCP."""
        with pytest.raises(ValueError, match="empty"):
            lzc.run_channel_program(
                pool_name=POOL_NAME,
                script='return {}',
                args={"k": []}
            )

    def test_mixed_type_list_raises_value_error(self, pool):
        """Mixed-type list must raise ValueError."""
        with pytest.raises(ValueError, match="mixed"):
            lzc.run_channel_program(
                pool_name=POOL_NAME,
                script='return {}',
                args={"k": [1, "two"]}
            )

    def test_non_string_key_raises_type_error(self, pool):
        """Non-string dict key must raise TypeError."""
        with pytest.raises(TypeError):
            lzc.run_channel_program(
                pool_name=POOL_NAME,
                script='return {}',
                args={42: "value"}
            )
