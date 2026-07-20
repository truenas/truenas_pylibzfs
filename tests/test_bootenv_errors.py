"""Unit tests for truenas_bootenv.errors (pure Python, no ZFS needed)."""

import errno
import pytest
from truenas_bootenv import errors

ALL_SUBCLASSES = [
    errors.BENotFound,
    errors.BEExists,
    errors.BEBusy,
    errors.BEDestroyUnsafe,
    errors.BEGrubError,
]


@pytest.mark.parametrize('exc_type', ALL_SUBCLASSES)
def test_all_subclass_beerror(exc_type):
    # middleware catches BEError to handle every engine failure
    assert issubclass(exc_type, errors.BEError)
    assert issubclass(exc_type, Exception)


def test_message_is_str():
    e = errors.BENotFound("'foo' not found")
    assert str(e) == "'foo' not found"


def test_errno_defaults_to_none():
    assert errors.BEError('x').errno is None


def test_errno_carried_when_given():
    e = errors.BEExists('already there', errno.EEXIST)
    assert e.errno == errno.EEXIST


def test_subclasses_are_catchable_as_beerror():
    with pytest.raises(errors.BEError):
        raise errors.BEBusy('busy')


def test_package_reexports_match():
    # __init__ re-exports must stay in sync with errors.py
    import truenas_bootenv
    for exc_type in [errors.BEError] + ALL_SUBCLASSES:
        assert getattr(truenas_bootenv, exc_type.__name__) is exc_type
