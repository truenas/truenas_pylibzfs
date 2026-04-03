# src/libzfs_core

Python bindings for the `libzfs_core` C library, exposed as the
`truenas_pylibzfs.lzc` submodule.

## Overview

`libzfs_core` is a stable, handle-free ZFS API designed for use in
multithreaded applications. Unlike `libzfs`, its calls:

- **Do not require a `libzfs_handle_t`** - there is no shared handle to
  acquire or contend on.
- **Are thread-safe by design** - multiple threads may call `lzc_*`
  functions concurrently without external locking.

These properties make `lzc` the preferred interface for bulk or concurrent
operations (snapshot management, send/receive, holds, channel programs).

## Source files

| File | Purpose |
|---|---|
| `py_zfs_core_module.c` | Module entry point, method table, `ZFSCoreException`, `ZCPScript` enum, and docstrings (`PyDoc_STRVAR`) for all methods |
| `libzfs_core_replication.h/.c` | `send`, `send_space`, `send_progress`, `receive` wrapper functions (no docstrings here) |
| `lua_channel_programs.h` | Built-in ZCP (Lua channel program) script table |

## Exposed methods

| Python name | lzc call(s) | Mutating |
|---|---|---|
| `create_snapshots` | `lzc_snapshot` | yes |
| `destroy_snapshots` | `lzc_destroy_snaps` | yes |
| `create_holds` | `lzc_hold` | yes |
| `release_holds` | `lzc_release` | yes |
| `rollback` | `lzc_rollback` | yes |
| `run_channel_program` | `lzc_channel_program` | yes (unless readonly=True) |
| `send` | `lzc_send` / `lzc_send_resume` | no (data transfer) |
| `send_space` | `lzc_send_space` | no |
| `send_progress` | `lzc_send_progress` | no |
| `receive` | `lzc_receive` / `lzc_receive_resumable` | yes |
| `wait` | `lzc_wait` | no |

## Developer notes

### History logging

Because `libzfs_core` calls bypass the `libzfs` handle entirely, history
logging must be done manually by opening a **temporary `libzfs_handle_t`**
just for the `zpool_log_history()` call. Use the existing helpers:

```c
/* preferred: wraps libzfs_init/zpool_log_history/libzfs_fini */
py_log_history_impl(NULL, NULL, "zfs rollback %s -> %s", snap, fs);

/* for snapshot operations: builds a formatted message first */
py_zfs_core_log_snap_history("lzc_snapshot()", pool, py_snaps, user_props);
```

**Not every `lzc` call warrants a history entry.** Before adding history
logging to a new operation, verify that:

1. The operation is genuinely mutating (read-only calls such as `send_space`,
   `send_progress`, and `wait` do not belong in pool history).
2. The ZFS kernel module actually persists the history record for that
   operation - some `lzc` calls succeed silently without touching the history
   log (e.g. purely in-kernel or ioctl paths that skip `spa_history_log`).
   Confirm against the kernel source before assuming the record appears.

### Audit events

Every method must emit a `PySys_Audit` event at entry, following the pattern
used throughout the module:

```c
if (PySys_Audit(PYLIBZFS_MODULE_NAME ".lzc.<method>", "...", ...) < 0)
    return NULL;
```

### Docstrings

All `PyDoc_STRVAR` definitions belong in `py_zfs_core_module.c`.
Implementation files (`libzfs_core_replication.c`, etc.) must not contain
docstrings.
