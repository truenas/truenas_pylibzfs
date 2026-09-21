import subprocess

import truenas_pylibzfs

DATASET = 'dozer/DS'
SNAPSHOT = 's1'
BOOKMARK = 'bm1'

lz = truenas_pylibzfs.open_handle()

truenas_pylibzfs.lzc.create_snapshots(snapshot_names={f'{DATASET}@{SNAPSHOT}'})

# There is no binding for creating a bookmark yet.
subprocess.run(
    ['zfs', 'bookmark', f'{DATASET}@{SNAPSHOT}', f'{DATASET}#{BOOKMARK}'],
    check=True,
)

# The snapshot may now be destroyed. The bookmark still records its
# creation txg, which is enough to use it as an incremental send source.
truenas_pylibzfs.lzc.destroy_snapshots(snapshot_names={f'{DATASET}@{SNAPSHOT}'})


def collect(bookmark, state):
    state.append(bookmark.name)
    return True


names = []
ds = lz.open_resource(name=DATASET)
ds.iter_bookmarks(callback=collect, state=names)
assert names == [f'{DATASET}#{BOOKMARK}'], names

bookmark = lz.open_resource(name=f'{DATASET}#{BOOKMARK}')
print(bookmark.name, bookmark.guid, bookmark.createtxg)

props = bookmark.get_properties(
    properties=truenas_pylibzfs.property_sets.ZFS_BOOKMARK_PROPERTIES
)
# The kernel does not record a compressratio for a bookmark. Absent values
# are reported as None rather than raising.
assert props.compressratio.value is None, props.compressratio

bookmark.destroy()
