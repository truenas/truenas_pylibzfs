import truenas_pylibzfs
import errno
import os

destroy_rsrc = truenas_pylibzfs.lzc.ChannelProgramEnum.DESTROY_RESOURCES
destroy_snap = truenas_pylibzfs.lzc.ChannelProgramEnum.DESTROY_SNAPSHOTS
take_snap = truenas_pylibzfs.lzc.ChannelProgramEnum.TAKE_SNAPSHOTS
rollback_snap = truenas_pylibzfs.lzc.ChannelProgramEnum.ROLLBACK_TO_TXG

lz = truenas_pylibzfs.open_handle()

def create_datasets():
    for ds in ('dozer/foo', 'dozer/foo/bar', 'dozer/foo/bar/tar'):
        lz.create_resource(name=ds, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)

create_datasets()
rsrc = lz.open_resource(name='dozer/foo')

# We need to recursively unmount the dataset first
rsrc.unmount(recursive=True)

# execute channel program explicitly to destroy datasets
truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_rsrc,
    script_arguments_dict={"recursive": True, "defer": True, "target": "dozer/foo"},
    readonly=False
)

# Recreate to do our channel program tests
create_datasets()

res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=take_snap,
    script_arguments=["dozer/foo", "now"],
    readonly=False
)

# All the callback stuff to get a snapshot count
def add_to_snap_cnt(hdl, state):
    state['cnt'] += 1
    return True


def iter_filesystems_for_snaps(hdl, state):
    hdl.iter_snapshots(callback=add_to_snap_cnt, state=state, fast=True)
    hdl.iter_filesystems(
        callback=iter_filesystems_for_snaps,
        state=state,
        fast=True
    )
    return True


def count_snapshots(lz_hdl, datasets):
    count_state = {'cnt': 0}

    for dataset_name in datasets:
        rsrc = lz_hdl.open_resource(name=dataset_name)
        rsrc.iter_filesystems(
            callback=iter_filesystems_for_snaps,
            state=count_state,
            fast=True
        )

    return count_state['cnt']

count = count_snapshots(lz, ['dozer'])

assert count == 3, f'count: {count}, channel_program_result: {res}'

# succeeded in snapshotting. Now let's nuke them

res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_snap,
    script_arguments_dict={"recursive": True, "target": "dozer/foo", "defer": False,},
    readonly=False
)

count = count_snapshots(lz, ['dozer'])
assert count == 0, f'count: {count}, channel_program_result: {res}'


# Test pattern matching - direct

res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=take_snap,
    script_arguments=["dozer/foo", "now"],
    readonly=False
)

count = count_snapshots(lz, ['dozer'])
assert count == 3, f'count: {count}, channel_program_result: {res}'


res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=take_snap,
    script_arguments=["dozer/foo", "now2"],
    readonly=False
)

count = count_snapshots(lz, ['dozer'])
assert count == 6, f'count: {count}, channel_program_result: {res}'


res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_snap,
    script_arguments_dict={"recursive": True, "target": "dozer/foo", "defer": False, "pattern": "now"},
    readonly=False
)

count = count_snapshots(lz, ['dozer'])
assert count == 3, f'count: {count}, channel_program_result: {res}'

res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_snap,
    script_arguments_dict={"recursive": True, "target": "dozer/foo", "defer": False, "pattern": "now2"},
    readonly=False
)

count = count_snapshots(lz, ['dozer'])
assert count == 0, f'count: {count}, channel_program_result: {res}'

# Test pattern matching - wildcard

res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=take_snap,
    script_arguments=["dozer/foo", "now"],
    readonly=False
)

count = count_snapshots(lz, ['dozer'])
assert count == 3, f'count: {count}, channel_program_result: {res}'


res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=take_snap,
    script_arguments=["dozer/foo", "now2"],
    readonly=False
)

count = count_snapshots(lz, ['dozer'])
assert count == 6, f'count: {count}, channel_program_result: {res}'

res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_snap,
    script_arguments_dict={"recursive": True, "target": "dozer/foo", "defer": False, "pattern": "now.*"},
    readonly=False
)

count = count_snapshots(lz, ['dozer'])
assert count == 0, f'count: {count}, channel_program_result: {res}'

# CLEANUP
rsrc = lz.open_resource(name='dozer/foo')

# We need to recursively unmount the dataset first
rsrc.unmount(recursive=True)

truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_rsrc,
    script_arguments_dict={"recursive": True, "defer": True, "target": "dozer/foo"},
    readonly=False
)

# Now to test rollback to targeted snapshot
lz.create_resource(name='dozer/foo', type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)

truenas_pylibzfs.lzc.create_snapshots(snapshot_names={'dozer/foo@s1'})

rsrc = lz.open_resource(name='dozer/foo')
rsrc.mount()

# Create a file
with open('/mnt/dozer/foo/canary', 'w') as f:
    f.write("DONUTS!")
    f.flush()

rsrc.unmount()
truenas_pylibzfs.lzc.create_snapshots(snapshot_names={'dozer/foo@s2'})
s = lz.open_resource(name='dozer/foo@s1')
res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=rollback_snap,
    script_arguments_dict={"target": "dozer/foo", "txg": s.createtxg},
    readonly=False
)

print(res)

# remount before checking for file existence
rsrc.mount()

assert not os.path.exists('/mnt/dozer/foo/canary')
HOLD = ('dozer/foo@s1', 'canary')

# HOLDS testing
truenas_pylibzfs.lzc.create_holds(holds={HOLD})

# Unmount first
rsrc.unmount(recursive=True)

res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_rsrc,
    script_arguments_dict={"recursive": True, "defer": True, "target": "dozer/foo"},
    readonly=False
)

# {'return': {'holds': {'dozer/foo@s1': 'canary'}, 'failed': {'dozer/foo': 16}}}
print(res)
assert res['return']['holds'] == {'dozer/foo@s1': 'canary'}
assert 'dozer/foo' in res['return']['failed']

# EBUSY in this case means that it failed due to hold
assert res['return']['failed']['dozer/foo'] == errno.EBUSY

# Destroy was deferred due to the hold
truenas_pylibzfs.lzc.release_holds(holds={HOLD})

try:
    lz.open_resource(name='dozer/foo@s1')
except truenas_pylibzfs.ZFSException as exc:
    assert exc.code == truenas_pylibzfs.ZFSError.EZFS_NOENT
else:
    raise RuntimeError('snapshot exists after deferred deletion')

# now we have to re-destroy because deferred destroy is a snapshot thing
res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_rsrc,
    script_arguments_dict={"recursive": True, "defer": True, "target": "dozer/foo"},
    readonly=False
)

# {'return': {'holds': {}, 'failed': {}}
print(res)

assert not res['return']['holds']
assert not res['return']['failed']


# now test clone detection
SNAP = 'dozer/foo@s1'
CLONE = 'dozer/clone'
lz.create_resource(name='dozer/foo', type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
truenas_pylibzfs.lzc.create_snapshots(snapshot_names={SNAP})

snap_rsrc = lz.open_resource(name=SNAP)
snap_rsrc.clone(name=CLONE)

clone = lz.open_resource(name=CLONE)
clone.mount()

res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_rsrc,
    script_arguments_dict={"recursive": True, "defer": True, "target": "dozer/foo"},
    readonly=False
)

# {'return': {'clones': {'dozer/clone': 16}, 'failed': {'dozer/foo': 16}, 'holds': {}}}
print(res)

# failed because clone mounted
assert 'dozer/foo' in res['return']['failed']
assert CLONE in res['return']['clones']
assert res['return']['clones'][CLONE] == errno.EBUSY

clone.unmount()

res = truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=destroy_rsrc,
    script_arguments_dict={"recursive": True, "defer": True, "target": "dozer/foo"},
    readonly=False
)

assert not res['return']['clones']
assert not res['return']['failed']
