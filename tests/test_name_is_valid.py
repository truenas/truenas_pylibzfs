import pytest
import truenas_pylibzfs

ZFSType = truenas_pylibzfs.ZFSType


# ---------------------------------------------------------------------------
# Dataset (filesystem / volume / snapshot) names
# ---------------------------------------------------------------------------

@pytest.mark.parametrize('name', [
    'pool/dataset',
    'pool/a/b/c',
    'pool/dataset_with_underscores',
    'pool/dataset-with-dashes',
    'pool/dataset:colon',
    'pool/dataset.dot',
    'pool/dataset with spaces',
])
def test_valid_filesystem_names(name):
    assert truenas_pylibzfs.name_is_valid(name=name, type=ZFSType.ZFS_TYPE_FILESYSTEM) is True


@pytest.mark.parametrize('name', [
    'pool/dataset@snap',
    'pool/a/b@mysnap',
])
def test_valid_snapshot_names(name):
    assert truenas_pylibzfs.name_is_valid(name=name, type=ZFSType.ZFS_TYPE_SNAPSHOT) is True


@pytest.mark.parametrize('name', [
    'pool/vol',
    'pool/a/b/vol',
])
def test_valid_volume_names(name):
    assert truenas_pylibzfs.name_is_valid(name=name, type=ZFSType.ZFS_TYPE_VOLUME) is True


@pytest.mark.parametrize('name', [
    '',                        # empty
    'pool/bad!name',           # invalid character
    'pool/dataset@snap@snap',  # double @
    'a' * 1024,                # way too long
    'pool/données',            # non-ASCII (Latin)
    'pool/数据集',             # non-ASCII (CJK)
    'pool/dataset ',           # trailing space
])
def test_invalid_filesystem_names(name):
    assert truenas_pylibzfs.name_is_valid(name=name, type=ZFSType.ZFS_TYPE_FILESYSTEM) is False


def test_snapshot_name_invalid_as_filesystem():
    # @ is not allowed in a filesystem name
    assert truenas_pylibzfs.name_is_valid(name='pool/ds@snap', type=ZFSType.ZFS_TYPE_FILESYSTEM) is False


def test_filesystem_name_invalid_as_snapshot():
    # no @ means it can't be a snapshot
    assert truenas_pylibzfs.name_is_valid(name='pool/ds', type=ZFSType.ZFS_TYPE_SNAPSHOT) is False


# ---------------------------------------------------------------------------
# Pool names
# ---------------------------------------------------------------------------

@pytest.mark.parametrize('name', [
    'mypool',
    'pool123',
    'pool_with_underscores',
    'pool-with-dashes',
])
def test_valid_pool_names(name):
    assert truenas_pylibzfs.name_is_valid(name=name, type=ZFSType.ZFS_TYPE_POOL) is True


@pytest.mark.parametrize('name', [
    '',            # empty
    'bad!pool',    # invalid character
    'données',     # non-ASCII (Latin)
    '数据池',      # non-ASCII (CJK)
    'bad/pool',    # slash not allowed in pool name
    'bad@pool',    # @ not allowed
    'mirror',      # reserved word
    'raidz',       # reserved word
    'draid',       # reserved word
    'spare',       # reserved word
    'log',         # reserved word
    'tank ',       # trailing space
])
def test_invalid_pool_names(name):
    assert truenas_pylibzfs.name_is_valid(name=name, type=ZFSType.ZFS_TYPE_POOL) is False


# ---------------------------------------------------------------------------
# Argument validation
# ---------------------------------------------------------------------------

def test_missing_name_raises():
    with pytest.raises(ValueError):
        truenas_pylibzfs.name_is_valid(type=ZFSType.ZFS_TYPE_FILESYSTEM)


def test_missing_type_raises():
    with pytest.raises(ValueError):
        truenas_pylibzfs.name_is_valid(name='pool/dataset')


def test_invalid_type_raises():
    with pytest.raises(TypeError):
        truenas_pylibzfs.name_is_valid(name='pool/dataset', type='ZFS_TYPE_FILESYSTEM')


def test_positional_args_rejected():
    with pytest.raises(TypeError):
        truenas_pylibzfs.name_is_valid('pool/dataset', ZFSType.ZFS_TYPE_FILESYSTEM)
