"""
tests/test_feature_properties.py — test suite for create_pool(feature_properties=...)

Tests that individual ZFS pool features can be selectively disabled (or
explicitly enabled) at pool creation time via the feature_properties kwarg.
"""

import os
import pytest
import shutil
import tempfile
import truenas_pylibzfs

VDevType = truenas_pylibzfs.VDevType

POOL_NAME = "test_feat_props"
DISK_SZ = 128 * 1024 * 1024  # 128 MiB


# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def make_disks():
    dirs = []

    def _make(n):
        d = tempfile.mkdtemp(prefix="pylibzfs_feat_")
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


def _destroy():
    try:
        lz = truenas_pylibzfs.open_handle()
        lz.destroy_pool(name=POOL_NAME, force=True)
    except Exception:
        pass


def _spec(path):
    return truenas_pylibzfs.create_vdev_spec(
        vdev_type=VDevType.FILE, name=path
    )


# ---------------------------------------------------------------------------
# Tests — feature_properties validation (no pool creation)
# ---------------------------------------------------------------------------

class TestFeaturePropertiesValidation:
    def test_invalid_feature_name_raises(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        with pytest.raises(ValueError, match="not a valid ZFS feature name"):
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties={"not_a_real_feature": True},
                force=True,
            )
        _destroy()

    def test_non_bool_value_raises(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        with pytest.raises(TypeError, match="value must be a boolean"):
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties={"async_destroy": "yes"},
                force=True,
            )
        _destroy()

    def test_non_string_key_raises(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        with pytest.raises(TypeError, match="keys must be strings"):
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties={123: True},
                force=True,
            )
        _destroy()

    def test_non_dict_raises(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        with pytest.raises(TypeError, match="must be a dict"):
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties=["async_destroy"],
                force=True,
            )
        _destroy()


# ---------------------------------------------------------------------------
# Tests — feature_properties functional (pool creation + verification)
# ---------------------------------------------------------------------------

class TestFeaturePropertiesCreate:
    def test_disable_single_feature(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        try:
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties={"head_errlog": False},
                force=True,
            )
            pool = lz.open_pool(name=POOL_NAME)
            features = pool.get_features(asdict=True)
            assert features["head_errlog"]["state"] == "DISABLED"
        finally:
            _destroy()

    def test_disable_multiple_features(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        disabled_feats = {"head_errlog": False, "edonr": False}
        try:
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties=disabled_feats,
                force=True,
            )
            pool = lz.open_pool(name=POOL_NAME)
            features = pool.get_features(asdict=True)
            for fname in disabled_feats:
                assert features[fname]["state"] == "DISABLED", \
                    f"{fname} should be DISABLED"
        finally:
            _destroy()

    def test_explicit_enable_is_noop(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        try:
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties={"async_destroy": True},
                force=True,
            )
            pool = lz.open_pool(name=POOL_NAME)
            features = pool.get_features(asdict=True)
            assert features["async_destroy"]["state"] != "DISABLED"
        finally:
            _destroy()

    def test_other_features_remain_enabled(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        disabled_feats = {"head_errlog", "edonr"}
        try:
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties={f: False for f in disabled_feats},
                force=True,
            )
            pool = lz.open_pool(name=POOL_NAME)
            features = pool.get_features(asdict=True)
            unexpected = [
                k for k, v in features.items()
                if k not in disabled_feats and v["state"] == "DISABLED"
            ]
            assert len(unexpected) == 0, \
                f"Unexpected disabled features: {unexpected}"
        finally:
            _destroy()

    def test_none_is_default(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        try:
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties=None,
                force=True,
            )
            pool = lz.open_pool(name=POOL_NAME)
            features = pool.get_features(asdict=True)
            disabled = [
                k for k, v in features.items()
                if v["state"] == "DISABLED"
            ]
            assert len(disabled) == 0
        finally:
            _destroy()

    def test_empty_dict_is_default(self, make_disks):
        lz = truenas_pylibzfs.open_handle()
        disks = make_disks(1)
        try:
            lz.create_pool(
                name=POOL_NAME,
                storage_vdevs=[_spec(disks[0])],
                feature_properties={},
                force=True,
            )
            pool = lz.open_pool(name=POOL_NAME)
            features = pool.get_features(asdict=True)
            disabled = [
                k for k, v in features.items()
                if v["state"] == "DISABLED"
            ]
            assert len(disabled) == 0
        finally:
            _destroy()
