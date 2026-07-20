# This code example shows how to create a ZFS dataset

import pytest
import truenas_pylibzfs

props = {
   truenas_pylibzfs.ZFSProperty.ACLMODE: "restricted",
   truenas_pylibzfs.ZFSProperty.ACLTYPE: "nfsv4",
}

def test_create_dataset_with_properties(root_dataset):
    # creating a dataset with specific property set
    lz, root = root_dataset
    rsrc_name = f'{root.name}/nfs_export'
    lz.create_resource(
        name=rsrc_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        properties=props
    )

    rsrc = lz.open_resource(name=rsrc_name)
    rprops = rsrc.asdict(properties={truenas_pylibzfs.ZFSProperty.ACLMODE, truenas_pylibzfs.ZFSProperty.ACLTYPE})
    for key, value in props.items():
        assert rprops['properties'][key.name.lower()]['value'] == value, str(rprops)

    lz.destroy_resource(name=rsrc_name)


def test_create_dataset_with_properties_from_other_dataset(dataset):
    propset = {
        truenas_pylibzfs.ZFSProperty.ACLMODE,
        truenas_pylibzfs.ZFSProperty.ACLTYPE
    }

    lz, root, ds = dataset
    rsrc_name = f'{root.name}/test_write'
    props = ds.asdict(properties=propset)['properties']
    lz.create_resource(
        name=rsrc_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        properties=props
    )

    lz.destroy_resource(name=rsrc_name)


def test_create_dataset_with_user_properties(root_dataset):
    lz, root = root_dataset
    rsrc_name = f'{root.name}/canary'
    lz.create_resource(
        name=rsrc_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        properties=props,
        user_properties={"org.truenas:canary": "test"}
    )

    rsrc = lz.open_resource(name=rsrc_name)
    rprops = rsrc.asdict(properties={truenas_pylibzfs.ZFSProperty.ACLMODE, truenas_pylibzfs.ZFSProperty.ACLTYPE})
    for key, value in props.items():
        assert rprops['properties'][key.name.lower()]['value'] == value, str(rprops)

    user_props = rsrc.get_user_properties()
    assert user_props['org.truenas:canary'] == 'test'
    lz.destroy_resource(name=rsrc_name)


def test_create_dataset_properties_struct_bad_slot(dataset):
    """
    A struct_zfs_property slot holds an arbitrary object. A hand-rolled
    struct carrying something other than struct_zfs_property_data must be
    rejected rather than indexed as one.
    """
    lz, root, ds = dataset
    props = ds.get_properties(properties={truenas_pylibzfs.ZFSProperty.ATIME})
    bad = props.__replace__(atime=42)

    with pytest.raises(TypeError):
        lz.create_resource(
            name=f'{root.name}/bad_prop_struct',
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
            properties=bad,
        )
