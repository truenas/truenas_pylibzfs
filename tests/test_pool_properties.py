import os
import pytest
import shutil
import subprocess
import tempfile
import truenas_pylibzfs

POOL_NAME = "testpool_props"
DISK_SZ = 1024 * 1048576


@pytest.fixture
def make_disk():
    dirs = []

    def _make():
        d = tempfile.mkdtemp(prefix="pylibzfs_props_disk_")
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
    subprocess.run(["zpool", "create", "-f", POOL_NAME, disk], check=True)
    lz = truenas_pylibzfs.open_handle()
    p = lz.open_pool(name=POOL_NAME)
    try:
        yield lz, p
    finally:
        subprocess.run(["zpool", "destroy", "-f", POOL_NAME], check=False)


# ---------------------------------------------------------------------------
# get_properties — NUMBER properties (value is int, raw is decimal string)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("prop,attr", [
    (truenas_pylibzfs.enums.ZPOOLProperty.SIZE, "size"),
    (truenas_pylibzfs.enums.ZPOOLProperty.GUID, "guid"),
])
def test_number_property_value_is_int(pool, prop, attr):
    _, p = pool
    props = p.get_properties(properties={prop})
    slot = getattr(props, attr)
    assert slot is not None
    assert isinstance(slot.value, int)
    assert slot.value > 0


@pytest.mark.parametrize("prop,attr", [
    (truenas_pylibzfs.enums.ZPOOLProperty.SIZE, "size"),
    (truenas_pylibzfs.enums.ZPOOLProperty.GUID, "guid"),
])
def test_number_property_raw_is_decimal_string(pool, prop, attr):
    _, p = pool
    props = p.get_properties(properties={prop})
    slot = getattr(props, attr)
    assert isinstance(slot.raw, str)
    assert slot.raw.isdigit()


# ---------------------------------------------------------------------------
# get_properties — STRING properties (value is str)
# ---------------------------------------------------------------------------

def test_comment_value_is_str(pool):
    _, p = pool
    props = p.get_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.COMMENT})
    assert isinstance(props.comment.value, str)


def test_name_equals_pool_name(pool):
    _, p = pool
    props = p.get_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.NAME})
    assert props.name.value == POOL_NAME


# ---------------------------------------------------------------------------
# get_properties — INDEX properties (value is str)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("prop,attr", [
    (truenas_pylibzfs.enums.ZPOOLProperty.HEALTH, "health"),
    (truenas_pylibzfs.enums.ZPOOLProperty.AUTOTRIM, "autotrim"),
    (truenas_pylibzfs.enums.ZPOOLProperty.DELEGATION, "delegation"),
])
def test_index_property_value_is_str(pool, prop, attr):
    _, p = pool
    props = p.get_properties(properties={prop})
    slot = getattr(props, attr)
    assert slot is not None
    assert isinstance(slot.value, str)


def test_health_is_online_on_fresh_pool(pool):
    _, p = pool
    props = p.get_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.HEALTH})
    assert props.health.value == "ONLINE"


# ---------------------------------------------------------------------------
# get_properties — unrequested slots are None
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("requested,absent_attr", [
    ({truenas_pylibzfs.enums.ZPOOLProperty.SIZE}, "comment"),
    ({truenas_pylibzfs.enums.ZPOOLProperty.COMMENT}, "size"),
    (set(), "guid"),
    (set(), "health"),
])
def test_unrequested_slot_is_none(pool, requested, absent_attr):
    _, p = pool
    props = p.get_properties(properties=requested)
    assert getattr(props, absent_attr) is None


# ---------------------------------------------------------------------------
# get_properties — argument validation
# ---------------------------------------------------------------------------

def test_missing_properties_arg_raises(pool):
    _, p = pool
    with pytest.raises(TypeError):
        p.get_properties()


def test_accepts_frozenset(pool):
    _, p = pool
    props = p.get_properties(properties=frozenset({truenas_pylibzfs.enums.ZPOOLProperty.SIZE}))
    assert props.size is not None


# ---------------------------------------------------------------------------
# set_properties — comment roundtrip
# ---------------------------------------------------------------------------

def test_set_comment_roundtrip(pool):
    _, p = pool
    p.set_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.COMMENT: "hello world"})
    assert p.get_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.COMMENT}).comment.value == "hello world"


def test_set_comment_to_empty_string(pool):
    _, p = pool
    p.set_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.COMMENT: "first"})
    p.set_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.COMMENT: ""})
    assert p.get_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.COMMENT}).comment.value == ""


def test_set_comment_string_key(pool):
    _, p = pool
    p.set_properties(properties={"comment": "via string key"})
    assert p.get_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.COMMENT}).comment.value == "via string key"


# ---------------------------------------------------------------------------
# set_properties — bool and string coercion for INDEX props
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("value,expected_raw", [
    (True, "on"),
    (False, "off"),
    ("on", "on"),
    ("off", "off"),
])
def test_autotrim_coercion(pool, value, expected_raw):
    _, p = pool
    p.set_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.AUTOTRIM: value})
    assert p.get_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.AUTOTRIM}).autotrim.raw == expected_raw


# ---------------------------------------------------------------------------
# set_properties — multiple properties in a single call
# ---------------------------------------------------------------------------

def test_set_multiple_properties(pool):
    _, p = pool
    p.set_properties(properties={
        truenas_pylibzfs.enums.ZPOOLProperty.COMMENT: "multi",
        truenas_pylibzfs.enums.ZPOOLProperty.AUTOTRIM: True,
    })
    props = p.get_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.COMMENT, truenas_pylibzfs.enums.ZPOOLProperty.AUTOTRIM})
    assert props.comment.value == "multi"
    assert props.autotrim.raw == "on"


# ---------------------------------------------------------------------------
# set_properties — readonly and setonce rejection
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("prop,value", [
    (truenas_pylibzfs.enums.ZPOOLProperty.SIZE, 1024),
    (truenas_pylibzfs.enums.ZPOOLProperty.GUID, 12345),
    (truenas_pylibzfs.enums.ZPOOLProperty.HEALTH, "ONLINE"),
    (truenas_pylibzfs.enums.ZPOOLProperty.ALLOCATED, 0),
])
def test_readonly_prop_raises(pool, prop, value):
    _, p = pool
    with pytest.raises(ValueError, match="read-only"):
        p.set_properties(properties={prop: value})


# ---------------------------------------------------------------------------
# set_properties — bad key types
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("bad_key", [
    42,          # plain int, not a ZPOOLProperty instance
    3.14,        # float
    b"comment",  # bytes
])
def test_set_bad_key_type_raises(pool, bad_key):
    _, p = pool
    with pytest.raises(TypeError):
        p.set_properties(properties={bad_key: "val"})


def test_set_unknown_string_key_raises(pool):
    _, p = pool
    with pytest.raises(ValueError):
        p.set_properties(properties={"not_a_real_property": "val"})


# ---------------------------------------------------------------------------
# set_properties — bad value types
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("bad_val", [
    [1, 2, 3],
    None,
    {"key": "val"},
])
def test_set_bad_value_type_raises(pool, bad_val):
    _, p = pool
    with pytest.raises(TypeError):
        p.set_properties(properties={truenas_pylibzfs.enums.ZPOOLProperty.COMMENT: bad_val})


# ---------------------------------------------------------------------------
# set_properties — argument validation
# ---------------------------------------------------------------------------

def test_set_missing_properties_arg_raises(pool):
    _, p = pool
    with pytest.raises(TypeError):
        p.set_properties()


def test_set_non_dict_raises(pool):
    _, p = pool
    with pytest.raises(TypeError):
        p.set_properties(properties=[truenas_pylibzfs.enums.ZPOOLProperty.COMMENT])


# ---------------------------------------------------------------------------
# property_sets — contents
# ---------------------------------------------------------------------------

def test_zpool_readonly_properties_is_frozenset():
    assert isinstance(truenas_pylibzfs.property_sets.ZPOOL_READONLY_PROPERTIES, frozenset)


def test_zpool_properties_is_frozenset():
    assert isinstance(truenas_pylibzfs.property_sets.ZPOOL_PROPERTIES, frozenset)


@pytest.mark.parametrize("prop", [
    truenas_pylibzfs.enums.ZPOOLProperty.SIZE,
    truenas_pylibzfs.enums.ZPOOLProperty.GUID,
    truenas_pylibzfs.enums.ZPOOLProperty.HEALTH,
    truenas_pylibzfs.enums.ZPOOLProperty.ALLOCATED,
])
def test_known_readonly_in_frozenset(prop):
    assert prop in truenas_pylibzfs.property_sets.ZPOOL_READONLY_PROPERTIES


@pytest.mark.parametrize("prop", [
    truenas_pylibzfs.enums.ZPOOLProperty.COMMENT,
    truenas_pylibzfs.enums.ZPOOLProperty.AUTOTRIM,
    truenas_pylibzfs.enums.ZPOOLProperty.DELEGATION,
    truenas_pylibzfs.enums.ZPOOLProperty.AUTOREPLACE,
])
def test_known_writable_in_frozenset(prop):
    assert prop in truenas_pylibzfs.property_sets.ZPOOL_PROPERTIES


def test_propsets_are_disjoint():
    assert not (truenas_pylibzfs.property_sets.ZPOOL_PROPERTIES & truenas_pylibzfs.property_sets.ZPOOL_READONLY_PROPERTIES)


# ---------------------------------------------------------------------------
# property_sets — usable as get_properties input
# ---------------------------------------------------------------------------

def test_get_all_writable_properties(pool):
    _, p = pool
    props = p.get_properties(properties=truenas_pylibzfs.property_sets.ZPOOL_PROPERTIES)
    assert props.comment is not None
    assert props.autotrim is not None


def test_get_all_readonly_properties(pool):
    _, p = pool
    props = p.get_properties(properties=truenas_pylibzfs.property_sets.ZPOOL_READONLY_PROPERTIES)
    assert props.size is not None
    assert props.guid is not None
    assert props.health is not None
