import os
import pytest
import shutil
import tempfile
import truenas_pylibzfs

POOL_NAME = "testpool_features"
DISK_SZ = 1024 * 1048576

VALID_STATES = {"DISABLED", "ENABLED", "ACTIVE"}
WELL_KNOWN_FEATURES = {"async_destroy", "lz4_compress"}


@pytest.fixture
def make_disks():
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix="pylibzfs_feat_disks_")
        dirs.append(d)
        paths = []
        for i in range(n):
            p = os.path.join(d, f"d{i}.img")
            with open(p, "w") as f:
                os.ftruncate(f.fileno(), DISK_SZ)
            paths.append(p)
        return paths

    yield _make

    for d in dirs:
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def pool(make_disks):
    disks = make_disks(1)
    lz = truenas_pylibzfs.open_handle()
    lz.create_pool(
        name=POOL_NAME,
        storage_vdevs=[
            truenas_pylibzfs.create_vdev_spec(vdev_type=truenas_pylibzfs.VDevType.FILE, name=disks[0])
        ],
        force=True,
    )
    pool = lz.open_pool(name=POOL_NAME)
    try:
        yield lz, pool
    finally:
        try:
            lz.destroy_pool(name=POOL_NAME, force=True)
        except Exception:
            pass


def test_get_features_returns_dict(pool):
    lz, p = pool
    features = p.get_features()
    assert isinstance(features, dict)
    assert len(features) > 0


def test_get_features_entry_is_struct(pool):
    lz, p = pool
    features = p.get_features()
    for name, info in features.items():
        assert isinstance(name, str)
        assert type(info).__name__ == "struct_zpool_feature"
        assert hasattr(info, "guid")
        assert hasattr(info, "description")
        assert hasattr(info, "state")


def test_get_features_valid_states(pool):
    lz, p = pool
    features = p.get_features()
    for name, info in features.items():
        assert info.state in VALID_STATES, f"{name} has invalid state: {info.state}"


def test_get_features_well_known_present(pool):
    lz, p = pool
    features = p.get_features()
    for feat in WELL_KNOWN_FEATURES:
        assert feat in features, f"expected well-known feature {feat!r} not found"


def test_get_features_guid_is_string(pool):
    lz, p = pool
    features = p.get_features()
    for name, info in features.items():
        assert isinstance(info.guid, str)
        assert isinstance(info.description, str)


def test_get_features_well_known_active_or_enabled(pool):
    """On a newly created pool, well-known features should not be DISABLED."""
    lz, p = pool
    features = p.get_features()
    for feat in WELL_KNOWN_FEATURES:
        assert features[feat].state in ("ENABLED", "ACTIVE"), (
            f"{feat} expected ENABLED or ACTIVE, got {features[feat].state}"
        )


def test_get_features_asdict(pool):
    lz, p = pool
    features = p.get_features(asdict=True)
    assert isinstance(features, dict)
    assert len(features) > 0
    for name, info in features.items():
        assert isinstance(name, str)
        assert isinstance(info, dict)
        assert info["state"] in VALID_STATES
        assert isinstance(info["guid"], str)
        assert isinstance(info["description"], str)


def test_get_features_asdict_false_returns_struct(pool):
    lz, p = pool
    features = p.get_features(asdict=False)
    for name, info in features.items():
        assert type(info).__name__ == "struct_zpool_feature"
