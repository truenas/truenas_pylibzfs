# This code example shows how to create a ZFS dataset

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
