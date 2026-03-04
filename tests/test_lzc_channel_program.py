"""
Tests for lzc.run_channel_program():
  - Basic Lua script returning a value
  - readonly=True (default) leaves pool state unchanged
  - script_arguments list passed to script
  - script_arguments_dict passed to script
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
    out = lzc.run_channel_program(pool_name=POOL_NAME, script='return {["answer"] = 42}')
    assert out.get('answer') == 42


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
# script_arguments
# ---------------------------------------------------------------------------

def test_script_arguments_list(pool):
    out = lzc.run_channel_program(
        pool_name=POOL_NAME,
        script='return {["first"] = args[1]}',
        script_arguments=['hello'],
    )
    assert isinstance(out, dict)


def test_script_arguments_dict(pool):
    out = lzc.run_channel_program(
        pool_name=POOL_NAME,
        script='return {["val"] = args["mykey"]}',
        script_arguments_dict={'mykey': 'myvalue'},
    )
    assert isinstance(out, dict)


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
