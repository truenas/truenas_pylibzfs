import truenas_pylibzfs

DATASET = 'dozer/parent'
SNAPSHOT_NAME = 'canary_delme'
DSSNAP = f'{DATASET}@{SNAPSHOT_NAME}'
CLONE = 'dozer/clone'


lz = truenas_pylibzfs.open_handle()
truenas_pylibzfs.lzc.create_snapshots(snapshot_names={DSSNAP})
snap_rsrc = lz.open_resource(name=DSSNAP)
snap_rsrc.clone(name=CLONE)

clone_rsrc = lz.open_resource(name=CLONE)
origin = clone_rsrc.get_properties(properties={truenas_pylibzfs.ZFSProperty.ORIGIN}).origin
assert origin.value == DSSNAP, origin.value
clone_rsrc.promote()

origin = clone_rsrc.get_properties(properties={truenas_pylibzfs.ZFSProperty.ORIGIN}).origin
assert origin.value is None, origin.value

rsrc = lz.open_resource(name=DATASET)
origin = rsrc.get_properties(properties={truenas_pylibzfs.ZFSProperty.ORIGIN}).origin
assert origin.value == f'{CLONE}@{SNAPSHOT_NAME}', origin.value
