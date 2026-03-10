import os
import pytest
import shutil
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
# get_properties — NUMBER properties (value is int)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("prop,attr", [
    (truenas_pylibzfs.ZPOOLProperty.SIZE, "size"),
    (truenas_pylibzfs.ZPOOLProperty.GUID, "guid"),
])
def test_number_property_is_int(pool, prop, attr):
    _, p = pool
    props = p.get_properties(properties={prop})
    slot = getattr(props, attr)
    assert slot is not None
    assert isinstance(slot.value, int)
    assert slot.value > 0


# ---------------------------------------------------------------------------
# get_properties — STRING properties (value is str)
# ---------------------------------------------------------------------------

def test_comment_is_str(pool):
    _, p = pool
    props = p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.COMMENT})
    assert isinstance(props.comment.value, str)


def test_name_equals_pool_name(pool):
    _, p = pool
    props = p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.NAME})
    assert props.name.value == POOL_NAME


# ---------------------------------------------------------------------------
# get_properties — INDEX properties (value is str)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("prop,attr", [
    (truenas_pylibzfs.ZPOOLProperty.HEALTH, "health"),
    (truenas_pylibzfs.ZPOOLProperty.AUTOTRIM, "autotrim"),
    (truenas_pylibzfs.ZPOOLProperty.DELEGATION, "delegation"),
])
def test_index_property_is_str(pool, prop, attr):
    _, p = pool
    props = p.get_properties(properties={prop})
    slot = getattr(props, attr)
    assert slot is not None
    assert isinstance(slot.value, str)


def test_health_is_online_on_fresh_pool(pool):
    _, p = pool
    props = p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.HEALTH})
    assert props.health.value == "ONLINE"


# ---------------------------------------------------------------------------
# get_properties — unrequested slots are None
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("requested,absent_attr", [
    ({truenas_pylibzfs.ZPOOLProperty.SIZE}, "comment"),
    ({truenas_pylibzfs.ZPOOLProperty.COMMENT}, "size"),
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
    props = p.get_properties(properties=frozenset({truenas_pylibzfs.ZPOOLProperty.SIZE}))
    assert props.size is not None
    assert props.size.prop == truenas_pylibzfs.ZPOOLProperty.SIZE


# ---------------------------------------------------------------------------
# set_properties — comment roundtrip
# ---------------------------------------------------------------------------

def test_set_comment_roundtrip(pool):
    _, p = pool
    p.set_properties(properties={truenas_pylibzfs.ZPOOLProperty.COMMENT: "hello world"})
    assert p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.COMMENT}).comment.value == "hello world"


def test_set_comment_to_empty_string(pool):
    _, p = pool
    p.set_properties(properties={truenas_pylibzfs.ZPOOLProperty.COMMENT: "first"})
    p.set_properties(properties={truenas_pylibzfs.ZPOOLProperty.COMMENT: ""})
    assert p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.COMMENT}).comment.value == ""


def test_set_comment_string_key(pool):
    _, p = pool
    p.set_properties(properties={"comment": "via string key"})
    assert p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.COMMENT}).comment.value == "via string key"


# ---------------------------------------------------------------------------
# set_properties — bool and string coercion for INDEX props
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("value,expected", [
    (True, "on"),
    (False, "off"),
    ("on", "on"),
    ("off", "off"),
])
def test_autotrim_coercion(pool, value, expected):
    _, p = pool
    p.set_properties(properties={truenas_pylibzfs.ZPOOLProperty.AUTOTRIM: value})
    assert p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.AUTOTRIM}).autotrim.value == expected


# ---------------------------------------------------------------------------
# set_properties — multiple properties in a single call
# ---------------------------------------------------------------------------

def test_set_multiple_properties(pool):
    _, p = pool
    p.set_properties(properties={
        truenas_pylibzfs.ZPOOLProperty.COMMENT: "multi",
        truenas_pylibzfs.ZPOOLProperty.AUTOTRIM: True,
    })
    props = p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.COMMENT, truenas_pylibzfs.ZPOOLProperty.AUTOTRIM})
    assert props.comment.value == "multi"
    assert props.autotrim.value == "on"


# ---------------------------------------------------------------------------
# set_properties — readonly and setonce rejection
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("prop,value", [
    (truenas_pylibzfs.ZPOOLProperty.SIZE, 1024),
    (truenas_pylibzfs.ZPOOLProperty.GUID, 12345),
    (truenas_pylibzfs.ZPOOLProperty.HEALTH, "ONLINE"),
    (truenas_pylibzfs.ZPOOLProperty.ALLOCATED, 0),
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
        p.set_properties(properties={truenas_pylibzfs.ZPOOLProperty.COMMENT: bad_val})


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
        p.set_properties(properties=[truenas_pylibzfs.ZPOOLProperty.COMMENT])


# ---------------------------------------------------------------------------
# property_sets — contents
# ---------------------------------------------------------------------------

def test_zpool_readonly_properties_is_frozenset():
    assert isinstance(truenas_pylibzfs.property_sets.ZPOOL_READONLY_PROPERTIES, frozenset)


def test_zpool_properties_is_frozenset():
    assert isinstance(truenas_pylibzfs.property_sets.ZPOOL_PROPERTIES, frozenset)


def test_zpool_class_space_is_frozenset():
    assert isinstance(truenas_pylibzfs.property_sets.ZPOOL_CLASS_SPACE, frozenset)


def test_zpool_space_is_frozenset():
    assert isinstance(truenas_pylibzfs.property_sets.ZPOOL_SPACE, frozenset)


@pytest.mark.parametrize("prop", [
    truenas_pylibzfs.ZPOOLProperty.SIZE,
    truenas_pylibzfs.ZPOOLProperty.GUID,
    truenas_pylibzfs.ZPOOLProperty.HEALTH,
    truenas_pylibzfs.ZPOOLProperty.ALLOCATED,
])
def test_known_readonly_in_frozenset(prop):
    assert prop in truenas_pylibzfs.property_sets.ZPOOL_READONLY_PROPERTIES


@pytest.mark.parametrize("prop", [
    truenas_pylibzfs.ZPOOLProperty.COMMENT,
    truenas_pylibzfs.ZPOOLProperty.AUTOTRIM,
    truenas_pylibzfs.ZPOOLProperty.DELEGATION,
    truenas_pylibzfs.ZPOOLProperty.AUTOREPLACE,
])
def test_known_writable_in_frozenset(prop):
    assert prop in truenas_pylibzfs.property_sets.ZPOOL_PROPERTIES


def test_propsets_are_disjoint():
    assert not (truenas_pylibzfs.property_sets.ZPOOL_PROPERTIES & truenas_pylibzfs.property_sets.ZPOOL_READONLY_PROPERTIES)


@pytest.mark.parametrize("prop", [
    truenas_pylibzfs.ZPOOLProperty.CLASS_NORMAL_SIZE,
    truenas_pylibzfs.ZPOOLProperty.CLASS_SPECIAL_USED,
    truenas_pylibzfs.ZPOOLProperty.CLASS_DEDUP_FREE,
    truenas_pylibzfs.ZPOOLProperty.CLASS_LOG_CAPACITY,
    truenas_pylibzfs.ZPOOLProperty.CLASS_ELOG_SIZE,
    truenas_pylibzfs.ZPOOLProperty.CLASS_SPECIAL_ELOG_FRAGMENTATION,
])
def test_class_props_in_zpool_class_space(prop):
    assert prop in truenas_pylibzfs.property_sets.ZPOOL_CLASS_SPACE


@pytest.mark.parametrize("prop", [
    truenas_pylibzfs.ZPOOLProperty.COMMENT,
    truenas_pylibzfs.ZPOOLProperty.AUTOTRIM,
])
def test_non_class_props_not_in_zpool_class_space(prop):
    assert prop not in truenas_pylibzfs.property_sets.ZPOOL_CLASS_SPACE


def test_zpool_space_is_superset_of_class_space():
    assert truenas_pylibzfs.property_sets.ZPOOL_CLASS_SPACE <= truenas_pylibzfs.property_sets.ZPOOL_SPACE


@pytest.mark.parametrize("prop", [
    truenas_pylibzfs.ZPOOLProperty.SIZE,
    truenas_pylibzfs.ZPOOLProperty.FREE,
    truenas_pylibzfs.ZPOOLProperty.ALLOCATED,
    truenas_pylibzfs.ZPOOLProperty.FRAGMENTATION,
    truenas_pylibzfs.ZPOOLProperty.CAPACITY,
    truenas_pylibzfs.ZPOOLProperty.AVAILABLE,
    truenas_pylibzfs.ZPOOLProperty.USED,
    truenas_pylibzfs.ZPOOLProperty.DEDUPRATIO,
    truenas_pylibzfs.ZPOOLProperty.CLASS_NORMAL_SIZE,
])
def test_space_props_in_zpool_space(prop):
    assert prop in truenas_pylibzfs.property_sets.ZPOOL_SPACE


@pytest.mark.parametrize("prop", [
    truenas_pylibzfs.ZPOOLProperty.COMMENT,
    truenas_pylibzfs.ZPOOLProperty.AUTOTRIM,
    truenas_pylibzfs.ZPOOLProperty.DELEGATION,
])
def test_non_space_props_not_in_zpool_space(prop):
    assert prop not in truenas_pylibzfs.property_sets.ZPOOL_SPACE


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


def test_get_class_space_properties(pool):
    _, p = pool
    props = p.get_properties(properties=truenas_pylibzfs.property_sets.ZPOOL_CLASS_SPACE)
    assert props.class_normal_size is not None
    assert isinstance(props.class_normal_size.value, int)


def test_get_space_properties(pool):
    _, p = pool
    props = p.get_properties(properties=truenas_pylibzfs.property_sets.ZPOOL_SPACE)
    assert props.size is not None
    assert props.free is not None
    assert props.class_normal_size is not None


# ---------------------------------------------------------------------------
# struct_zpool_prop_type — new fields: prop, raw, source
# ---------------------------------------------------------------------------

def test_prop_field_is_zpool_property_enum(pool):
    _, p = pool
    props = p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.SIZE})
    slot = props.size
    assert slot is not None
    assert slot.prop == truenas_pylibzfs.ZPOOLProperty.SIZE


def test_raw_field_is_str(pool):
    _, p = pool
    props = p.get_properties(properties={
        truenas_pylibzfs.ZPOOLProperty.SIZE,
        truenas_pylibzfs.ZPOOLProperty.COMMENT,
    })
    assert isinstance(props.size.raw, str)
    assert isinstance(props.comment.raw, str)


def test_readonly_stat_source_is_none(pool):
    """Read-only stats (SIZE, GUID, etc.) should have source=None."""
    _, p = pool
    props = p.get_properties(properties={
        truenas_pylibzfs.ZPOOLProperty.SIZE,
        truenas_pylibzfs.ZPOOLProperty.GUID,
    })
    assert props.size.source is None
    assert props.guid.source is None


def test_writable_prop_source_is_property_source(pool):
    """Writable props (COMMENT, AUTOTRIM) should have a PropertySource source."""
    _, p = pool
    props = p.get_properties(properties={
        truenas_pylibzfs.ZPOOLProperty.COMMENT,
        truenas_pylibzfs.ZPOOLProperty.AUTOTRIM,
    })
    assert isinstance(props.comment.source, truenas_pylibzfs.PropertySource)
    assert isinstance(props.autotrim.source, truenas_pylibzfs.PropertySource)


def test_prop_raw_consistent_with_value_for_numeric(pool):
    """For numeric props, raw should be a decimal string matching int(value)."""
    _, p = pool
    props = p.get_properties(properties={truenas_pylibzfs.ZPOOLProperty.SIZE})
    slot = props.size
    assert slot is not None
    assert int(slot.raw) == slot.value
