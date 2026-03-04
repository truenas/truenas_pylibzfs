import os
import pytest
import shutil
import tempfile
import truenas_pylibzfs

POOL_NAME = "testpool_userprop"
DISK_SZ = 1024 * 1048576


@pytest.fixture
def make_disk():
    dirs = []

    def _make():
        d = tempfile.mkdtemp(prefix="pylibzfs_userprop_disk_")
        dirs.append(d)
        p = os.path.join(d, "d0.img")
        with open(p, "w") as f:
            os.ftruncate(f.fileno(), DISK_SZ)
        return p

    yield _make

    for d in dirs:
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def pool(make_disk):
    disk = make_disk()
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(
        name=POOL_NAME,
        storage_vdevs=[
            truenas_pylibzfs.create_vdev_spec(vdev_type=truenas_pylibzfs.VDevType.FILE, name=disk)
        ],
        force=True,
    )
    p = lz.open_pool(name=POOL_NAME)
    try:
        yield lz, p
    finally:
        try:
            lz.destroy_pool(name=POOL_NAME, force=True)
        except Exception:
            pass


# ---------------------------------------------------------------------------
# get_user_properties — baseline
# ---------------------------------------------------------------------------

def test_get_user_properties_returns_empty_on_fresh_pool(pool):
    _, p = pool
    result = p.get_user_properties()
    assert isinstance(result, dict)
    assert result == {}


# ---------------------------------------------------------------------------
# set_user_properties / get_user_properties roundtrip
# ---------------------------------------------------------------------------

def test_set_and_get_single_property(pool):
    _, p = pool
    p.set_user_properties(user_properties={"org.example:tag": "hello"})
    result = p.get_user_properties()
    assert result["org.example:tag"] == "hello"


def test_set_and_get_multiple_properties(pool):
    _, p = pool
    props = {
        "org.example:alpha": "one",
        "org.example:beta": "two",
        "com.vendor:version": "1.2.3",
    }
    p.set_user_properties(user_properties=props)
    result = p.get_user_properties()
    for k, v in props.items():
        assert result[k] == v


def test_update_existing_property(pool):
    _, p = pool
    p.set_user_properties(user_properties={"org.example:tag": "first"})
    p.set_user_properties(user_properties={"org.example:tag": "second"})
    result = p.get_user_properties()
    assert result["org.example:tag"] == "second"


def test_set_empty_dict_is_noop(pool):
    _, p = pool
    p.set_user_properties(user_properties={"org.example:tag": "before"})
    p.set_user_properties(user_properties={})
    result = p.get_user_properties()
    assert result["org.example:tag"] == "before"


# ---------------------------------------------------------------------------
# set_user_properties — validation errors
# ---------------------------------------------------------------------------

def test_missing_colon_in_name_raises_value_error(pool):
    _, p = pool
    with pytest.raises(ValueError, match="colon"):
        p.set_user_properties(user_properties={"nocolon": "val"})


def test_non_string_value_raises_type_error(pool):
    _, p = pool
    with pytest.raises(TypeError):
        p.set_user_properties(user_properties={"org.example:tag": 42})


def test_non_dict_argument_raises_type_error(pool):
    _, p = pool
    with pytest.raises(TypeError):
        p.set_user_properties(user_properties=[("org.example:tag", "val")])


def test_missing_user_properties_kwarg_raises(pool):
    _, p = pool
    with pytest.raises((ValueError, TypeError)):
        p.set_user_properties()


def test_non_string_key_raises_type_error(pool):
    _, p = pool
    with pytest.raises(TypeError):
        p.set_user_properties(user_properties={42: "val"})
