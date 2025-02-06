# This code snippet provides examples of how to set ZFS properties
# to retrieve space information on ZFS resources

import truenas_pylibzfs

DS1 = 'dozer/test1'
DS2 = 'dozer/test2'

propset = {
    truenas_pylibzfs.ZFSProperty.READONLY,
    truenas_pylibzfs.ZFSProperty.ACLMODE,
    truenas_pylibzfs.ZFSProperty.ACLTYPE,
}

lz = truenas_pylibzfs.open_handle()

# Example 1:
# API users can get properties from one dataset and use
# them as a payload for setting properties on another dataset
props = lz.open_resource(name=DS1).get_properties(properties=propset)
lz.open_resource(name=DS2).set_properties(properties=props)

# Example 2:
# If API users get properties as a dictionary, that also works
props = lz.open_resource(name=DS1).asdict(properties=propset)['properties']
lz.open_resource(name=DS2).set_properties(properties=props)

# Example 3:
# Users can also simplify the dictionary to just have key: value
props = {
   truenas_pylibzfs.ZFSProperty.READONLY: "on",
   truenas_pylibzfs.ZFSProperty.ACLMODE: "discard",
   truenas_pylibzfs.ZFSProperty.ACLTYPE: "posix",
}
lz.open_resource(name=DS2).set_properties(properties=props)
