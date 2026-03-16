"""
Type tests for ZFS handle, pool, and dataset operations.

Functions accept typed parameters rather than constructing instances (which is
intentionally blocked by tp_new = py_no_new_impl on all C extension types).
A type error here means a stub signature is broken.
"""
from typing import Any

from truenas_pylibzfs import VDevType, ZFSProperty, ZFSType, ZPOOLProperty, create_vdev_spec
from truenas_pylibzfs import libzfs_types


# ---------------------------------------------------------------------------
# ZFS handle
# ---------------------------------------------------------------------------

def check_open_pool(zfs: libzfs_types.ZFS) -> None:
    pool: libzfs_types.ZFSPool = zfs.open_pool(name="tank")
    _ = pool


def check_create_pool_stripe(zfs: libzfs_types.ZFS) -> None:
    disk: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.DISK, name="/dev/sda"
    )
    zfs.create_pool(name="tank", storage_vdevs=[disk])


def check_create_pool_mirror(zfs: libzfs_types.ZFS) -> None:
    disk1: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.DISK, name="/dev/sda"
    )
    disk2: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.DISK, name="/dev/sdb"
    )
    mirror: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.MIRROR, children=[disk1, disk2]
    )
    zfs.create_pool(
        name="tank",
        storage_vdevs=[mirror],
        properties={ZPOOLProperty.ASHIFT: "12"},
        filesystem_properties={ZFSProperty.COMPRESSION: "lz4"},
    )


def check_create_filesystem(zfs: libzfs_types.ZFS) -> None:
    zfs.create_resource(
        name="tank/dataset",
        type=ZFSType.ZFS_TYPE_FILESYSTEM,
    )


def check_create_volume(zfs: libzfs_types.ZFS) -> None:
    zfs.create_resource(
        name="tank/vol",
        type=ZFSType.ZFS_TYPE_VOLUME,
        properties={"volsize": str(1024 ** 3)},
    )


def check_destroy_pool(zfs: libzfs_types.ZFS) -> None:
    zfs.destroy_pool(name="tank")


def check_destroy_resource(zfs: libzfs_types.ZFS) -> None:
    destroyed: bool = zfs.destroy_resource(name="tank/dataset")
    _ = destroyed


def check_import_pool_find(zfs: libzfs_types.ZFS) -> None:
    results: list[libzfs_types.struct_zpool_status] = zfs.import_pool_find()
    _ = results


def check_import_pool(zfs: libzfs_types.ZFS) -> None:
    pool: libzfs_types.ZFSPool = zfs.import_pool(name="tank")
    _ = pool


# ---------------------------------------------------------------------------
# ZFSPool
# ---------------------------------------------------------------------------

def check_pool_name(pool: libzfs_types.ZFSPool) -> None:
    name: str = pool.name
    _ = name


def check_pool_status(pool: libzfs_types.ZFSPool) -> None:
    status: libzfs_types.struct_zpool_status | dict[str, Any] = pool.status()
    _ = status


def check_pool_scrub_info(pool: libzfs_types.ZFSPool) -> None:
    info: libzfs_types.struct_zpool_scrub | None = pool.scrub_info()
    _ = info


def check_pool_expand_info(pool: libzfs_types.ZFSPool) -> None:
    info: libzfs_types.struct_zpool_expand | None = pool.expand_info()
    _ = info


def check_pool_get_properties(pool: libzfs_types.ZFSPool) -> None:
    props: libzfs_types.struct_zpool_property = pool.get_properties(
        properties={ZPOOLProperty.HEALTH, ZPOOLProperty.SIZE}
    )
    _ = props


def check_pool_get_user_properties(pool: libzfs_types.ZFSPool) -> None:
    user_props: dict[str, str] = pool.get_user_properties()
    _ = user_props


def check_pool_set_user_properties(pool: libzfs_types.ZFSPool) -> None:
    pool.set_user_properties(user_properties={"org:owner": "alice"})


def check_pool_root_dataset(pool: libzfs_types.ZFSPool) -> None:
    ds: libzfs_types.ZFSDataset = pool.root_dataset()
    _ = ds


def check_pool_add_vdevs(pool: libzfs_types.ZFSPool) -> None:
    disk: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.DISK, name="/dev/sdc"
    )
    pool.add_vdevs(spare_vdevs=[disk])


def check_pool_attach_vdev(pool: libzfs_types.ZFSPool) -> None:
    new_dev: libzfs_types.struct_vdev_create_spec = create_vdev_spec(
        vdev_type=VDevType.DISK, name="/dev/sdb"
    )
    pool.attach_vdev(device="/dev/sda", new_device=new_dev)


def check_pool_asdict(pool: libzfs_types.ZFSPool) -> None:
    d: dict[str, Any] = pool.asdict()
    _ = d


# ---------------------------------------------------------------------------
# ZFSResource / ZFSDataset
# ---------------------------------------------------------------------------

def check_dataset_name(ds: libzfs_types.ZFSDataset) -> None:
    name: str = ds.name
    _ = name


def check_dataset_get_properties(ds: libzfs_types.ZFSDataset) -> None:
    props: libzfs_types.struct_zfs_property = ds.get_properties(properties=set())
    _ = props


def check_dataset_get_user_properties(ds: libzfs_types.ZFSDataset) -> None:
    user_props: dict[str, str] = ds.get_user_properties()
    _ = user_props


def check_dataset_set_user_properties(ds: libzfs_types.ZFSDataset) -> None:
    ds.set_user_properties(user_properties={"custom:tag": "v1"})


def check_dataset_crypto(ds: libzfs_types.ZFSDataset) -> None:
    crypto: libzfs_types.ZFSCrypto | None = ds.crypto()
    _ = crypto


def check_crypto_info(crypto: libzfs_types.ZFSCrypto) -> None:
    info: libzfs_types.struct_zfs_crypto_info = crypto.info()
    is_root: bool = info.is_root
    root: str | None = info.encryption_root
    loc: str = info.key_location
    loaded: bool = info.key_is_loaded
    _ = (is_root, root, loc, loaded)


# ---------------------------------------------------------------------------
# ZFSSnapshot
# ---------------------------------------------------------------------------

def check_snapshot_get_holds(snap: libzfs_types.ZFSSnapshot) -> None:
    holds: tuple[str, ...] = snap.get_holds()
    _ = holds


def check_snapshot_get_clones(snap: libzfs_types.ZFSSnapshot) -> None:
    clones: tuple[str, ...] = snap.get_clones()
    _ = clones


def check_snapshot_clone(snap: libzfs_types.ZFSSnapshot) -> None:
    snap.clone(name="tank/cloned")


# ---------------------------------------------------------------------------
# struct field types
# ---------------------------------------------------------------------------

def check_vdev_create_spec_fields(
    spec: libzfs_types.struct_vdev_create_spec,
) -> None:
    name: str | None = spec.name
    vtype: libzfs_types.VDevType | str = spec.vdev_type
    children: tuple[libzfs_types.struct_vdev_create_spec, ...] | None = spec.children
    _ = (name, vtype, children)


def check_vdev_stats_fields(stats: libzfs_types.struct_vdev_stats) -> None:
    read_errs: int = stats.read_errors
    write_errs: int = stats.write_errors
    cksum_errs: int = stats.checksum_errors
    _ = (read_errs, write_errs, cksum_errs)


def check_zpool_status_fields(
    status: libzfs_types.struct_zpool_status,
) -> None:
    s: libzfs_types.ZPOOLStatus = status.status
    name: str = status.name
    guid: int = status.guid
    vdevs: tuple[libzfs_types.struct_vdev, ...] = status.storage_vdevs
    _ = (s, name, guid, vdevs)
