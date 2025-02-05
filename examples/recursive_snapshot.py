# This code snippet provides an example of how to use ZFS iterators
# to perform recursive operations. This example has two scenarios:
#
# 1. Provided a list of datasets, take recursive snapshots of all of
#    them with a specified name.
#
# 2. Recursively count all snapshots under the provided datasets
#
# WARNING: this just provides examples of how to use iterators.
# Counting snapshots after the create_snapshots() call is neither
# required nor desirable.
#
# NOTE: callback functions must return a boolean value indicating
# whether libzfs should halt iteration. In the following examples
# True is always returned because we're performing operations on
# all filesystems and snapshots.

import truenas_pylibzfs

DATASETS = ('dozer/MANY', 'dozer/share')
SNAPSHOT_NAME = 'canary_delme'


def add_to_snapset(hdl, snap_spec):
    to_add = f'{hdl.name}@{snap_spec["snapshot_name"]}'
    snap_spec['snapshots'].add(to_add)
    hdl.iter_filesystems(callback=add_to_snapset, state=snap_spec, fast=True)
    return True


def take_recursive_snapshots(lz_hdl, datasets, snapshot_name):
    snap_spec = {
        'snapshot_name': snapshot_name,
        'snapshots': set()
    }
    for dataset_name in datasets:
        rsrc = lz_hdl.open_resource(name=dataset_name)
        rsrc.iter_filesystems(
            callback=add_to_snapset,
            state=snap_spec,
            fast=True
        )

    truenas_pylibzfs.lzc.create_snapshots(snapshot_names=snap_spec['snapshots'])
    return snap_spec['snapshots']


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


lz = truenas_pylibzfs.open_handle()

count = count_snapshots(lz, DATASETS)
print(f'snapshot_count before snapshots: {count}')

snaps = take_recursive_snapshots(lz, DATASETS, SNAPSHOT_NAME)
print(f'took {len(snaps)} snapshots')

count = count_snapshots(lz, DATASETS)
print(f'snapshot_count after snapshots: {count}')

truenas_pylibzfs.lzc.destroy_snapshots(snapshot_names=snaps)

count = count_snapshots(lz, DATASETS)
print(f'snapshot_count after snapshot deletion: {count}')
