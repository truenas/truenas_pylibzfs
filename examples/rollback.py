import truenas_pylibzfs

DATASET = 'dozer/DS'
SNAPSHOT1 = 's1'
SNAPSHOT2 = 's2'

def create_snapshots():
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={f'{DATASET}@{SNAPSHOT1}'})
    truenas_pylibzfs.lzc.create_snapshots(snapshot_names={f'{DATASET}@{SNAPSHOT2}'})

create_snapshots()
target = truenas_pylibzfs.lzc.rollback(resource_name=DATASET)
assert target == f'{DATASET}@{SNAPSHOT2}', target

try:
    truenas_pylibzfs.lzc.rollback(resource_name=DATASET, snapshot_name=SNAPSHOT1)
except FileExistsError:
    pass
else:
    raise RuntimeError('failed to raise exception')

truenas_pylibzfs.lzc.destroy_snapshots(snapshot_names={f'{DATASET}@{SNAPSHOT2}'})

target = truenas_pylibzfs.lzc.rollback(resource_name=DATASET, snapshot_name=SNAPSHOT1)
assert target == f'{DATASET}@{SNAPSHOT1}', target
