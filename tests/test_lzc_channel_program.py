"""
Tests for lzc.run_channel_program().

Key ZCP conventions:
  - The output dict is wrapped: {"return": <lua_return_value>}
  - The input nvlist is passed as the first argument to the Lua main chunk;
    access it as:  local args = ...
  - script_arguments=['a','b'] → args.argv[1], args.argv[2]  (1-indexed)
  - script_arguments_dict={'k':'v'} → args['k']

Covers:
  - Basic Lua script returning a value (output nested under "return")
  - readonly=True (default) leaves pool state unchanged
  - script_arguments list accessible as args.argv
  - script_arguments_dict accessible as args keys
  - Missing pool_name raises
  - Missing script raises
  - Lua syntax error raises ZFSCoreException
  - keyword-only enforcement
"""

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

POOL_NAME = 'testpool_chanprog'


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


# ---------------------------------------------------------------------------
# Basic execution
# ---------------------------------------------------------------------------

def test_basic_return(pool):
    out = lzc.run_channel_program(pool_name=POOL_NAME, script='return {["result"] = "ok"}')
    assert isinstance(out, dict)


def test_return_value_accessible(pool):
    # ZCP wraps Lua return under "return" key: {"return": {"answer": 42}}
    out = lzc.run_channel_program(pool_name=POOL_NAME, script='return {["answer"] = 42}')
    assert out.get('return', {}).get('answer') == 42


def test_readonly_default(pool):
    out = lzc.run_channel_program(
        pool_name=POOL_NAME,
        script='return {}',
        readonly=True,
    )
    assert isinstance(out, dict)


def test_empty_return(pool):
    out = lzc.run_channel_program(pool_name=POOL_NAME, script='return {}')
    assert isinstance(out, dict)


# ---------------------------------------------------------------------------
# script_arguments — accessible as args.argv[N] (1-indexed) in Lua
# The input nvlist is passed as the first argument to the main chunk: local args = ...
# ---------------------------------------------------------------------------

def test_script_arguments_list(pool):
    script = 'local args = ... return {["first"] = args.argv[1]}'
    out = lzc.run_channel_program(
        pool_name=POOL_NAME,
        script=script,
        script_arguments=['hello'],
    )
    assert isinstance(out, dict)
    assert out.get('return', {}).get('first') == 'hello'


def test_script_arguments_dict(pool):
    # script_arguments_dict keys are merged directly into the args nvlist
    script = 'local args = ... return {["val"] = args["mykey"]}'
    out = lzc.run_channel_program(
        pool_name=POOL_NAME,
        script=script,
        script_arguments_dict={'mykey': 'myvalue'},
    )
    assert isinstance(out, dict)
    assert out.get('return', {}).get('val') == 'myvalue'


# ---------------------------------------------------------------------------
# Error cases
# ---------------------------------------------------------------------------

def test_missing_pool_name_raises(pool):
    with pytest.raises((TypeError, ValueError)):
        lzc.run_channel_program(script='return {}')


def test_missing_script_raises(pool):
    with pytest.raises((TypeError, ValueError)):
        lzc.run_channel_program(pool_name=POOL_NAME)


def test_syntax_error_raises(pool):
    with pytest.raises(lzc.ZFSCoreException):
        lzc.run_channel_program(pool_name=POOL_NAME, script='this is not valid lua {{{')


def test_nonexistent_pool_raises(pool):
    with pytest.raises((lzc.ZFSCoreException, FileNotFoundError, ValueError)):
        lzc.run_channel_program(pool_name='no_such_pool_xyz', script='return {}')


def test_keyword_only(pool):
    with pytest.raises(TypeError):
        lzc.run_channel_program(POOL_NAME, 'return {}')


# ---------------------------------------------------------------------------
# script_arguments — argv is built from the items actually yielded, and each
# string outlives the argv entry that points into its UTF-8 buffer
# ---------------------------------------------------------------------------

class LyingLength:
    """Reports one item but yields many."""

    COUNT = 256

    def __len__(self):
        return 1

    def __iter__(self):
        return iter([f'arg{i}' for i in range(self.COUNT)])


def test_script_arguments_len_shorter_than_iteration(pool):
    """
    argv must hold every item yielded. Sizing it from __len__ wrote
    COUNT - 1 pointers past an allocation with room for one.
    """
    script = ('local args = ... '
              'return {["count"] = #args.argv, ["last"] = args.argv[256]}')
    out = lzc.run_channel_program(
        pool_name=POOL_NAME,
        script=script,
        script_arguments=LyingLength(),
    )
    assert out.get('return', {}).get('count') == LyingLength.COUNT
    assert out.get('return', {}).get('last') == f'arg{LyingLength.COUNT - 1}'


class UnretainedItems:
    """
    __len__ agrees with the item count, but each string is built by the
    generator and referenced only by the iteration itself. Releasing an item
    before its UTF-8 buffer is read leaves argv pointing into freed memory,
    which the next allocation in the loop reuses.
    """

    COUNT = 8

    def __len__(self):
        return self.COUNT

    def __iter__(self):
        for i in range(self.COUNT):
            # built here and not interned, so the iteration holds the only
            # reference to it
            yield f'unretained-{i}'


def test_script_arguments_items_released_during_iteration(pool):
    script = ('local args = ... '
              'return {["count"] = #args.argv, ["first"] = args.argv[1], '
              '["last"] = args.argv[8]}')
    out = lzc.run_channel_program(
        pool_name=POOL_NAME,
        script=script,
        script_arguments=UnretainedItems(),
    )
    ret = out.get('return', {})
    assert ret.get('count') == UnretainedItems.COUNT
    assert ret.get('first') == 'unretained-0'
    assert ret.get('last') == f'unretained-{UnretainedItems.COUNT - 1}'


def test_script_arguments_generator(pool):
    """A bare generator (no __len__) is accepted."""
    script = 'local args = ... return {["second"] = args.argv[2]}'
    out = lzc.run_channel_program(
        pool_name=POOL_NAME,
        script=script,
        script_arguments=(f'arg{i}' for i in range(3)),
    )
    assert out.get('return', {}).get('second') == 'arg1'


def test_script_arguments_empty(pool):
    """An empty list contributes no argv entry at all, so args.argv is nil."""
    script = ('local args = ... '
              'if args.argv == nil then return {["argv"] = "absent"} end '
              'return {["argv"] = "present"}')
    out = lzc.run_channel_program(
        pool_name=POOL_NAME,
        script=script,
        script_arguments=[],
    )
    assert out.get('return', {}).get('argv') == 'absent'


def test_script_arguments_non_string_raises(pool):
    with pytest.raises(TypeError):
        lzc.run_channel_program(
            pool_name=POOL_NAME,
            script='return {}',
            script_arguments=['ok', 42],
        )


def test_script_arguments_not_iterable_raises(pool):
    with pytest.raises(TypeError):
        lzc.run_channel_program(
            pool_name=POOL_NAME,
            script='return {}',
            script_arguments=42,
        )
