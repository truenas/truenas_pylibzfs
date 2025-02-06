# This code example shows how to create a ZFS dataset

import truenas_pylibzfs

props = {
   truenas_pylibzfs.ZFSProperty.ACLMODE: "restricted",
   truenas_pylibzfs.ZFSProperty.ACLTYPE: "nfsv4",
}

lz = truenas_pylibzfs.open_handle()

# Example 1:
# creating a dataset with specific property set
lz.create_resource(
    name='dozer/nfs_export',
    type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    properties=props
)

# Example 2:
# creating dataset with certain properties mirroring another dataset's
propset = {
    truenas_pylibzfs.ZFSProperty.ACLMODE,
    truenas_pylibzfs.ZFSProperty.ACLTYPE
}

props = lz.open_resource(name='dozer/test1').asdict(properties=propset)['properties']
lz.create_resource(
    name='dozer/test_write',
    type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    properties=props
)


# Example 3:
# creating dataset with certain props and a user property
lz.create_resource(
    name='dozer/test_userprops',
    type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    properties=props,
    user_properties={"org.truenas:canary": "test"}
)
