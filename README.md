# truenas_pylibzfs

Python C extension providing bindings to libzfs and libzfs_core for TrueNAS. Exposes three modules:

- **`truenas_pylibzfs`** — high-level libzfs wrapper (pools, datasets, volumes, snapshots, encryption, properties, events)
- **`truenas_pylibzfs.lzc`** — low-level libzfs_core wrapper (atomic send/receive, snapshot batching, holds, rollback, channel programs, pool wait)
- **`truenas_pylibzfs.libzfs_types`** — all C extension types (`ZFSPool`, `ZFSDataset`, etc.) and enums (`ZFSType`, `ZPOOLProperty`, etc.); use for `isinstance` checks and enum access

This README covers a minimal set of usage examples. Where it conflicts with the inline docstrings or type stubs, treat those as authoritative. Docstrings are accessible at runtime via `help()`:

```python
import truenas_pylibzfs

lz = truenas_pylibzfs.open_handle()
rsrc = lz.open_resource(name="tank/data")

help(truenas_pylibzfs)              # module overview
help(lz)                            # ZFS handle methods
help(rsrc)                          # resource methods (dataset/volume/snapshot)
help(truenas_pylibzfs.libzfs_types.ZFSPool)   # pool methods
help(truenas_pylibzfs.libzfs_types)          # all types and enums
help(truenas_pylibzfs.lzc)          # lzc submodule
help(truenas_pylibzfs.lzc.send)     # individual lzc method
```

## Compatibility

This library is not designed to be backwards compatible with OpenZFS versions not distributed as part of TrueNAS. The `master` branch tracks the OpenZFS version shipped in TrueNAS master builds in lock-step; there is no guarantee of compatibility with upstream OpenZFS releases or other distributions.

## Requirements (Debian)

- `libnvpair3`
- `libuutil3`
- `libzfs7`
- `libzfs7-devel`
- `python3-all-dev`
- `libbsd-dev`
- `libssl-dev`

## Build

Build a Debian package:

```sh
debuild -us -uc -b
```

Build in-place for development:

```sh
python setup.py build_ext --inplace
```

Type stubs (`stubs/__init__.pyi`, `stubs/lzc.pyi`) are installed as part of the `truenas_pylibzfs` package for IDE support.

## Module Overview

### Opening a handle

```python
import truenas_pylibzfs

# History logging is on by default with prefix "truenas_pylibzfs"
lz = truenas_pylibzfs.open_handle()

# Custom history prefix, or disable logging entirely
lz = truenas_pylibzfs.open_handle(history_prefix="mymiddleware")
lz = truenas_pylibzfs.open_handle(history=False)
```

`open_handle()` returns a `ZFS` object. All other operations flow from it.

---

## Pool Operations

### List / open pools

```python
def print_pool(hdl, state):
    print(hdl.name)
    state["pools"].append(hdl)
    return True  # return False to stop iteration

state = {"pools": []}
lz.iter_pools(callback=print_pool, state=state)

pool = lz.open_pool(name="tank")
```

### Create / destroy pools

```python
mirror = truenas_pylibzfs.create_vdev_spec(
    vdev_type=truenas_pylibzfs.VDevType.MIRROR,
    children=[
        truenas_pylibzfs.create_vdev_spec(
            vdev_type=truenas_pylibzfs.VDevType.DISK,
            name="/dev/sdb"
        ),
        truenas_pylibzfs.create_vdev_spec(
            vdev_type=truenas_pylibzfs.VDevType.DISK,
            name="/dev/sdc"
        ),
    ]
)

lz.create_pool(
    name="tank",
    storage_vdevs=[mirror],
    properties={truenas_pylibzfs.ZPOOLProperty.ASHIFT: 12},
    filesystem_properties={truenas_pylibzfs.ZFSProperty.COMPRESSION: "lz4"},
)

lz.destroy_pool(name="tank", force=False)
```

### Pool status and health

```python
status = pool.status()
print(status.state)          # ZPOOLStatus enum
print(status.errata)
print(status.vdev_tree)      # struct_vdev (recursive)

# As a plain dict (for serialization)
d = pool.status(asdict=True, get_stats=True)
```

### Pool properties

```python
props = pool.get_properties(properties={
    truenas_pylibzfs.ZPOOLProperty.SIZE,
    truenas_pylibzfs.ZPOOLProperty.FREE,
    truenas_pylibzfs.ZPOOLProperty.HEALTH,
})
print(props.size.value, props.free.value, props.health.value)

pool.set_properties(properties={
    truenas_pylibzfs.ZPOOLProperty.COMMENT: "production"
})

# User-defined properties
pool.set_user_properties(user_properties={"org.myapp:owner": "alice"})
print(pool.get_user_properties())
```

### Scrub and scan

```python
pool.scan(func=truenas_pylibzfs.ScanFunction.SCRUB)
pool.scan(
    func=truenas_pylibzfs.ScanFunction.SCRUB,
    cmd=truenas_pylibzfs.ScanScrubCmd.PAUSE
)
info = pool.scrub_info()  # struct_zpool_scrub or None
```

### Vdev operations

```python
# Add a spare
spare = truenas_pylibzfs.create_vdev_spec(
    vdev_type=truenas_pylibzfs.VDevType.DISK,
    name="/dev/sdd"
)
pool.add_vdevs(spare_vdevs=[spare])

# Replace a faulted disk
replacement = truenas_pylibzfs.create_vdev_spec(
    vdev_type=truenas_pylibzfs.VDevType.DISK,
    name="/dev/sde"
)
pool.replace_vdev(device="/dev/sdb", new_device=replacement)

pool.offline_device(device="/dev/sdb", temporary=True)
pool.online_device(device="/dev/sdb")
pool.detach_vdev(device="/dev/sdd")
pool.remove_vdev(device="/dev/sdd")   # async removal
```

### Pool history

```python
for entry in pool.iter_history(skip_internal=True):
    print(entry)   # dict with timestamp, command, etc.
```

### Import / export

```python
lz.export_pool(name="tank")

# Find importable pools from a specific device
candidates = lz.import_pool_find(device="/dev/sdb")
for c in candidates:
    print(c.name, c.guid)

# Import by name or GUID
lz.import_pool(name="tank")
lz.import_pool(guid=12345678, altroot="/mnt", force=True)
```

---

## Dataset / Volume Operations

### Create and destroy

```python
lz.create_resource(
    name="tank/data",
    type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    properties={
        truenas_pylibzfs.ZFSProperty.ACLTYPE: "nfsv4",
        truenas_pylibzfs.ZFSProperty.COMPRESSION: "zstd",
    },
    user_properties={"org.myapp:purpose": "nfs-export"},
)

lz.create_resource(
    name="tank/zvol0",
    type=truenas_pylibzfs.ZFSType.ZFS_TYPE_VOLUME,
    properties={truenas_pylibzfs.ZFSProperty.VOLSIZE: 10 * 1024**3},
)

lz.destroy_resource(name="tank/data")
```

### Open and inspect

```python
rsrc = lz.open_resource(name="tank/data")
print(rsrc.name, rsrc.type, rsrc.guid)

# Get specific properties
props = rsrc.get_properties(properties={
    truenas_pylibzfs.ZFSProperty.USED,
    truenas_pylibzfs.ZFSProperty.AVAILABLE,
    truenas_pylibzfs.ZFSProperty.COMPRESSION,
})
print(props.used.value, props.available.value)
print(props.compression.value, props.compression.source)  # PropertySource enum

# Full dict representation
d = rsrc.asdict(properties={
    truenas_pylibzfs.ZFSProperty.USED,
    truenas_pylibzfs.ZFSProperty.COMPRESSION,
})
```

### Iterate children

```python
def visit(hdl, state):
    state.append(hdl.name)
    hdl.iter_filesystems(callback=visit, state=state)  # recurse
    return True

names = []
rsrc.iter_filesystems(callback=visit, state=names)
```

Snapshot iteration supports filtering by transaction group:

```python
def visit_snap(hdl, state):
    state.append(hdl.name)
    return True

rsrc.iter_snapshots(callback=visit_snap, state=[], min_transaction_group=0)
```

### Properties and inheritance

```python
rsrc.set_properties(properties={
    truenas_pylibzfs.ZFSProperty.QUOTA: 10 * 1024**3,
})
rsrc.inherit_property(property_name=truenas_pylibzfs.ZFSProperty.COMPRESSION)
rsrc.set_user_properties(user_properties={"org.myapp:tag": "v2"})
```

### Mount / unmount

```python
rsrc.mount()
rsrc.mount(mountpoint="/alt/path", options=["ro"])
print(rsrc.get_mountpoint())
rsrc.unmount(force=False)
```

### Rename

```python
rsrc.rename(newname="tank/data-renamed")
```

---

## Snapshots

### Create, destroy, holds

```python
# Atomic batch creation via lzc
truenas_pylibzfs.lzc.create_snapshots(
    snapshot_names={"tank/data@snap1", "tank/vol@snap1"},
    user_properties={"org.myapp:reason": "nightly"},
)

truenas_pylibzfs.lzc.destroy_snapshots(
    snapshot_names={"tank/data@snap1"},
    defer_destroy=False,
)

# Holds
truenas_pylibzfs.lzc.create_holds(holds={"tank/data@snap1": "myjob"})
truenas_pylibzfs.lzc.release_holds(holds={"tank/data@snap1": {"myjob"}})

snap = lz.open_resource(name="tank/data@snap1")
print(snap.get_holds())
```

### Clone and promote

```python
snap = lz.open_resource(name="tank/data@snap1")
snap.clone(name="tank/clone", properties={
    truenas_pylibzfs.ZFSProperty.COMPRESSION: "lz4",
})

clone = lz.open_resource(name="tank/clone")
clone.promote()  # clone becomes independent; original dataset gets the snapshot
```

### Rollback

```python
# Roll back to most recent snapshot
name = truenas_pylibzfs.lzc.rollback(resource_name="tank/data")

# Roll back to a specific snapshot
name = truenas_pylibzfs.lzc.rollback(
    resource_name="tank/data",
    snapshot_name="tank/data@snap1",
)
```

---

## Send / Receive (Replication)

`truenas_pylibzfs.lzc` wraps libzfs_core send/receive directly. The caller is responsible for managing the file descriptor (pipe, socket, file).

### Full send

```python
import os

r_fd, w_fd = os.pipe()
truenas_pylibzfs.lzc.send(
    snapname="tank/data@snap1",
    fd=w_fd,
    flags=truenas_pylibzfs.lzc.SendFlags.COMPRESS,
)
os.close(w_fd)
```

### Incremental send

```python
truenas_pylibzfs.lzc.send(
    snapname="tank/data@snap2",
    fd=w_fd,
    fromsnap="tank/data@snap1",
)
```

### Estimate send size

```python
size = truenas_pylibzfs.lzc.send_space(
    snapname="tank/data@snap2",
    fromsnap="tank/data@snap1",
)
print(f"estimated bytes: {size}")
```

### Send progress

```python
# From another thread while send is in progress
bytes_sent, bytes_total = truenas_pylibzfs.lzc.send_progress(
    snapshot_name="tank/data@snap2",
    fd=w_fd,
)
```

### Receive

```python
truenas_pylibzfs.lzc.receive(
    snapname="backup/data@snap1",
    fd=r_fd,
    force=False,
    resumable=True,  # enables resume token on partial transfer
)
```

### Resumable receive

```python
# First attempt — interrupted
try:
    truenas_pylibzfs.lzc.receive(
        snapname="backup/data@snap1",
        fd=r_fd,
        resumable=True,
    )
except truenas_pylibzfs.ZFSCoreException:
    pass

# Get resume token from the partially-received dataset
snap_rsrc = lz.open_resource(name="backup/data")
token = snap_rsrc.get_properties(
    properties={truenas_pylibzfs.ZFSProperty.RECEIVE_RESUME_TOKEN}
).receive_resume_token.value

# Resume send using token
truenas_pylibzfs.lzc.send(
    snapname="tank/data@snap1",
    fd=w_fd,
    resume_token=token,
)
```

---

## Encryption

### Create encrypted dataset

```python
crypto_cfg = truenas_pylibzfs.resource_cryptography_config(
    keyformat="passphrase",
    keylocation="prompt",
)

lz.create_resource(
    name="tank/secret",
    type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
    crypto=crypto_cfg,
)
```

### Key management

```python
rsrc = lz.open_resource(name="tank/secret")
enc = rsrc.crypto()     # returns ZFSCrypto or None if not encrypted

info = enc.info()
print(info.is_root, info.encryption_root, info.key_location, info.key_is_loaded)

enc.load_key(key="mysecretpassphrase")
enc.unload_key()
print(enc.check_key(key="mysecretpassphrase"))  # True/False, does not load

enc.change_key(info=truenas_pylibzfs.resource_cryptography_config(
    keyformat="passphrase",
    keylocation="prompt",
    key="newpassphrase",
))
enc.inherit_key()   # remove as encryption root, inherit from parent
```

---

## User Quotas

```python
# Set quotas
rsrc.set_userquotas(quotas=[
    {"quota_type": truenas_pylibzfs.ZFSUserQuota.USER_QUOTA, "domain": "", "rid": 1001, "quota": 5 * 1024**3},
    {"quota_type": truenas_pylibzfs.ZFSUserQuota.GROUP_QUOTA, "domain": "", "rid": 100, "quota": 0},  # 0 = remove
])

# Iterate current quota usage
def print_quota(entry, state):
    state.append(entry)
    return True

rsrc.iter_userspace(
    callback=print_quota,
    state=[],
    quota_type=truenas_pylibzfs.ZFSUserQuota.USER_USED,
)
```

---

## Pool Events

```python
# Non-blocking: returns all existing events then stops
for event in lz.zpool_events():
    print(event)

# Skip existing events, wait for new ones (blocking)
for event in lz.zpool_events(blocking=True, skip_existing_events=True):
    print(event)
```

---

## Label Operations

`read_label` and `clear_label` operate on file descriptors directly, without needing to import or open a pool.

```python
import os

fd = os.open("/dev/sdb", os.O_RDONLY)
label = truenas_pylibzfs.read_label(fd=fd)
if label:
    print(label["name"], label["guid"])
os.close(fd)

# Wipe ZFS label from a disk
fd = os.open("/dev/sdb", os.O_RDWR)
truenas_pylibzfs.clear_label(fd=fd)
os.close(fd)
```

---

## Name Validation

```python
ok = truenas_pylibzfs.name_is_valid(
    name="tank/data@snap1",
    type=truenas_pylibzfs.ZFSType.ZFS_TYPE_SNAPSHOT,
)
```

---

## Channel Programs (Lua)

```python
script = """
args = ...
fs = args["fs"]
-- do ZFS operations atomically
return zfs.get_prop(fs, "used")
"""

result = truenas_pylibzfs.lzc.run_channel_program(
    pool_name="tank",
    script=script,
    script_arguments={"fs": "tank/data"},
    instruction_limit=10_000_000,
    memory_limit=10 * 1024 * 1024,
    readonly=True,
)
```

---

## Pool Wait

```python
# Block until a scrub finishes
truenas_pylibzfs.lzc.wait(
    pool_name="tank",
    activity=truenas_pylibzfs.lzc.ZpoolWaitActivity.ZPOOL_WAIT_SCRUB,
)
```

---

## Error Handling

All libzfs errors raise `truenas_pylibzfs.ZFSException`. libzfs_core errors raise `truenas_pylibzfs.ZFSCoreException`.

```python
try:
    lz.open_pool(name="nonexistent")
except truenas_pylibzfs.ZFSException as e:
    print(e.code)         # ZFSError enum member
    print(e.description)
    print(e.action)       # suggested remedy (may be empty)
```

---

## Source Layout

```
src/
  truenas_pylibzfs.h          # shared type definitions and macros
  truenas_pylibzfs.c          # module init
  truenas_pylibzfs_state.c    # per-interpreter module state
  common/
    error.c                   # ZFSException / ZFSCoreException construction
    nvlist_utils.c            # nvlist ↔ Python dict dispatch
    nvlist_utils_nvl_to_dict.c
    nvlist_utils_dict_to_nvl.c
    py_zfs_prop_sets.c        # property set helpers
    utils.c
  libzfs/
    py_zfs.c                  # ZFS handle class, module-level functions
    py_zfs_pool.c             # ZFSPool class
    py_zfs_pool_create.c      # pool/vdev creation helpers
    py_zfs_pool_prop.c        # pool property get/set
    py_zfs_pool_status.c      # pool status structs
    py_zfs_pool_scrub.c       # scrub/scan
    py_zfs_pool_expand.c      # RAIDZ expansion
    py_zfs_resource.c         # ZFSResource base class
    py_zfs_dataset.c          # ZFSDataset subclass
    py_zfs_volume.c           # ZFSVolume subclass
    py_zfs_snapshot.c         # ZFSSnapshot subclass
    py_zfs_crypto.c           # ZFSCrypto class
    py_zfs_prop.c             # dataset property get/set
    py_zfs_iter.c             # iter_pools, iter_filesystems, iter_snapshots, iter_userspace
    py_zfs_events.c           # zpool_events generator
    py_zfs_history.c          # iter_history
    py_zfs_mount.c            # mount/unmount
    py_zfs_userquota.c        # user/group/project quota iteration and set
    py_zfs_enum.c             # Python enum registration
    py_zfs_object.c           # ZFSObject base
    py_zfs_common.c
    py_libzfs_types_module.c  # libzfs_types submodule (types + enums)
  libzfs_core/
    py_zfs_core_module.c      # lzc submodule init, docstrings, method table
    libzfs_core_replication.c # send/receive/send_space/send_progress wrappers
stubs/
  __init__.pyi                # type stubs for truenas_pylibzfs
  lzc.pyi                     # type stubs for truenas_pylibzfs.lzc
  libzfs_types.pyi            # type stubs for truenas_pylibzfs.libzfs_types (types + enums)
  property_sets.pyi           # convenience frozensets of related properties
tests/                        # pytest test suite
examples/                     # runnable usage examples
setup.py
```

## Property Sets

`truenas_pylibzfs.property_sets` exposes frozensets of related `ZFSProperty` / `ZPOOLProperty` members as convenience guides for callers building property queries:

| Name | Contents |
|---|---|
| `ZFS_FILESYSTEM_PROPERTIES` | Valid properties for `ZFS_TYPE_FILESYSTEM` |
| `ZFS_VOLUME_PROPERTIES` | Valid properties for `ZFS_TYPE_VOLUME` |
| `ZFS_SPACE_PROPERTIES` | Equivalent of `zfs get space` output |
| `ZFS_FILESYSTEM_SNAPSHOT_PROPERTIES` | Valid properties for filesystem snapshots |
| `ZFS_VOLUME_SNAPSHOT_PROPERTIES` | Valid properties for volume snapshots |
| `ZPOOL_PROPERTIES` | All settable pool properties |
| `ZPOOL_SPACE` | All pool space and capacity properties |
| `ZPOOL_CLASS_SPACE` | Per-allocation-class space counters |
| `ZPOOL_STATUS_RECOVERABLE` | `ZPOOLStatus` values resolvable by admin action |
| `ZPOOL_STATUS_NONRECOVERABLE` | `ZPOOLStatus` values requiring restore from backup |

```python
from truenas_pylibzfs.property_sets import ZFS_SPACE_PROPERTIES

props = rsrc.get_properties(properties=ZFS_SPACE_PROPERTIES)
```

---

## Key Implementation Notes

- **Thread safety**: libzfs calls are wrapped in `PY_ZFS_LOCK` / `PY_ZFS_UNLOCK` within `Py_BEGIN_ALLOW_THREADS` / `Py_END_ALLOW_THREADS` blocks. ZFS resource objects (datasets, volumes, snapshots, pools) share a reference and lock with the `ZFS` handle under which they were created. This means that using multiple resource objects from the same handle concurrently across threads will contend on that handle's lock. The preferred design for multi-threaded use is one `open_handle()` per thread with resource objects created from that thread-local handle.
- **History logging**: on by default; mutating operations are logged to the ZFS history of the affected pool. Disable with `open_handle(history=False)` or customize the prefix with `history_prefix=`.
- **Audit**: methods emit `PySys_Audit("truenas_pylibzfs.<method>", ...)` events for system-level auditing.
- **nvlist conversion**: `py_nvlist_to_dict()` converts nvlists directly to Python dicts without JSON roundtrip.
- **Iterators return bool**: iterator callbacks return `True` to continue, `False` to stop early.
