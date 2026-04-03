# src/libzfs

Python bindings for the `libzfs` C library, forming the core of the
`truenas_pylibzfs` namespace.

## Overview

`libzfs` is the primary ZFS management API. Unlike `libzfs_core`, every
operation requires an open `libzfs_handle_t`. In this binding:

- **A handle is mandatory** - obtain one with `truenas_pylibzfs.open_handle()`,
  which returns a `ZFS` object wrapping the handle.
- **The handle carries a mutex** - all callers must hold `PY_ZFS_LOCK` while
  calling into `libzfs`, and must drop the GIL first to avoid deadlocks (see
  *Locking* below).
- **Prefer per-thread handles** - each thread should open its own `ZFS` handle
  via `open_handle()` and operate only on objects created from that handle.
  This avoids lock contention entirely and is the recommended usage pattern.

Sharing a single `ZFS` handle across threads is supported but serialises all
`libzfs` operations through the mutex, which is a **performance concern**, not
a stability concern - the locking ensures correctness regardless. All new code
must follow the existing locking standard using `PY_ZFS_LOCK` /
`PY_ZFS_UNLOCK` as described in the *Locking* section below. Many
`libzfs` types (`zpool_handle_t`, `zfs_handle_t`, etc.) hold an internal
back-reference to the `libzfs_handle_t` that created them and use it for
operations including error reporting, so the mutex must be held for ALL
`libzfs` operations on any object that originated from a given handle, not
just operations that touch the handle directly. `PY_ZFS_LOCK(plz)` /
`PY_ZFS_UNLOCK(plz)` (defined in `truenas_pylibzfs.h`) are used consistently
for this purpose throughout the module.

## Type hierarchy

```
ZFS                        - libzfs handle; pool/dataset factory
+-- ZFSPool                - zpool_handle_t wrapper
+-- ZFSObject              - base class (name, type, guid, ...)
    +-- ZFSResource        - adds property get/set, is_simple flag
        +-- ZFSDataset     - filesystem
        +-- ZFSVolume      - zvol
        +-- ZFSSnapshot    - snapshot
```

`ZFSEventIterator` and `ZFSHistoryIterator` are standalone iterator types
that hold their own fds and do not inherit from `ZFSObject`.

All types are exported through the `truenas_pylibzfs.libzfs_types` submodule
(built by `py_libzfs_types_module.c`) and also re-exported at the top-level
namespace.

## Source files

| File | Purpose |
|---|---|
| `py_zfs.c` | `ZFS` handle object - `open_handle`, `create_resource`, `open_resource`, `destroy_resource`, `iter_root_filesystems`, `iter_pools`, `open_pool`, `destroy_pool`, `export_pool`, `create_pool`, `import_pool_find`, `import_pool`, `resource_cryptography_config`, `zpool_events` |
| `py_zfs_pool.c` | `ZFSPool` - all pool-level operations: status, properties, device management (`add_vdevs`, `attach_vdev`, `replace_vdev`, `detach_vdev`, `remove_vdev`, `online_device`, `offline_device`), `scan`, `sync_pool`, `upgrade`, `expand_info`, `scrub_info`, `iter_history` |
| `py_zfs_resource.c` | Shared methods on `ZFSResource`: property get/set, rename, promote, mount/unmount, snapshot, clone, destroy, iter_filesystems/snapshots/bookmarks |
| `py_zfs_dataset.c` | `ZFSDataset`-specific additions: `iter_userspace`, `set_userquotas`, `crypto` property accessor |
| `py_zfs_volume.c` | `ZFSVolume`-specific additions: `crypto` property accessor, `promote` |
| `py_zfs_snapshot.c` | `ZFSSnapshot`-specific additions: `get_holds`, `get_clones`, `clone` |
| `py_zfs_object.c` | `ZFSObject` base - `rename`; read-only properties `name`, `type`, `guid`, `createtxg`, `pool_name`, `encrypted` |
| `py_zfs_common.c` | `py_zfs_promote()` shared helper used by dataset, volume, and resource |
| `py_zfs_prop.c` | ZFS dataset property get/set - `py_zfs_get_properties`, `py_object_to_zfs_prop_t`; `ZFSProperty` struct-sequence types |
| `py_zfs_pool_prop.c` | Pool property get/set - `py_zpool_get_properties`, `py_zpool_set_properties`, `py_zpool_get_user_properties`, `py_zpool_set_user_properties`; `ZPOOLProperty` struct-sequence types |
| `py_zfs_pool_create.c` | Pool creation vdev-spec builder and `zpool_create` / `zpool_import_props` wrappers |
| `py_zfs_pool_expand.c` | RAIDZ expansion status - `ZFSPoolExpand` struct-sequence (state, vdev, timing, bytes) |
| `py_zfs_pool_scrub.c` | Scan/scrub statistics - `ZFSPoolScrub` struct-sequence (23 fields: state, timing, bytes examined/processed/issued/errors, pass stats) |
| `py_zfs_pool_status.c` | Pool status - `ZFSPoolStatus` struct-sequence built from `zpool_get_status` |
| `py_zfs_iter.c/.h` | Iterator engine - `py_iter_state_t`, callbacks for filesystems, snapshots, userspace, and pools; manages GIL/lock interleaving around callbacks |
| `py_zfs_events.c/.h` | `ZFSEventIterator` - iterator over `zpool_events_next` records; holds its own `zevent_fd` |
| `py_zfs_history.c` | `ZFSHistoryIterator` - iterator over `zpool_get_history` records with `since`/`until` timestamp filtering |
| `py_zfs_mount.c` | `zfs_mount_at` / `zfs_umount` wrappers |
| `py_zfs_crypto.c` | `ZFSCrypto` object - key load/unload/change/rewrap, `keyformat`, `keylocation`, `keystatus` |
| `py_zfs_userquota.c` | `ZFSUserQuota` struct-sequence and `py_userquotas_to_nvlist` conversion |
| `py_zfs_enum.c` | Constructs all `IntEnum` / `IntFlag` / `StrEnum` types (`ZFSType`, `ZFSProperty`, `ZPOOLProperty`, `ZFSError`, `ZPOOLStatus`, `PropertySource`, `VDevState`, `VDevType`, `ZFSUserQuota`, `ScanFunction`, `ScanState`, `ScanScrubCmd`, ...) |
| `py_libzfs_types_module.c` | Builds the `truenas_pylibzfs.libzfs_types` submodule; exports all C types and enums |

## Developer notes

### Locking

Every call into `libzfs` must follow this pattern:

```c
py_zfs_error_t zfs_err;
bool err = false;

Py_BEGIN_ALLOW_THREADS        /* drop GIL */
PY_ZFS_LOCK(plz);             /* acquire handle mutex */
/* ... libzfs operation ... */
if (failure) {
    py_get_zfs_error(plz->lzh, &zfs_err);  /* capture while lock held */
    err = true;
}
PY_ZFS_UNLOCK(plz);
Py_END_ALLOW_THREADS          /* re-acquire GIL */

if (err) {
    set_exc_from_libzfs(&zfs_err, "context message");
    return NULL;
}
```

Retrieve error info (`py_get_zfs_error`) **while the lock is held**. Set the
Python exception (`set_exc_from_libzfs`) **after** releasing the lock, because
setting exceptions requires the GIL.

### History logging

Use `py_log_history_fmt` with the `py_zfs_t` handle:

```c
py_log_history_fmt(plz, "zpool <command> %s", pool_name);
py_log_history_fmt(plz, "zfs <command> %s", dataset_name);
```

This is different from `libzfs_core`, which has no handle and must open a
temporary one. Here the caller always has `plz` available.

### Audit events

Every public method must emit a `PySys_Audit` event before any mutating work:

```c
if (PySys_Audit(PYLIBZFS_MODULE_NAME ".<Type>.<method>", "...", ...) < 0)
    return NULL;
```

### Type instantiation

All extension types set `.tp_new = py_no_new_impl`, blocking direct Python
construction. Internal C factory functions allocate via `Type.tp_alloc(&Type, 0)`
directly. When adding a new type, follow the same pattern - no `py_xxx_new` or
`py_xxx_init` boilerplate.
