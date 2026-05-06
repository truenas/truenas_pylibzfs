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
| `libzfs_core_replication.h/.c` | `send`, `send_space`, `send_progress`, `receive`, `local_replicate` wrapper functions (no docstrings here) |
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
| `local_replicate` | `lzc_send` + `lzc_receive` (paired in one call via an internal pipe and pthread) | yes |
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

### `local_replicate` design notes

`local_replicate` is a single-call wrapper that drives `lzc_send` and
`lzc_receive` against opposite ends of an anonymous pipe inside the
process. The send side runs in a `pthread_create`'d thread so the
receive ioctl can block on the read end concurrently, and both ioctls
run with the GIL released.

A few details worth knowing before touching the implementation:

- **Pipe sizing.** The pipe is enlarged to 1 MiB via
  `F_SETPIPE_SZ` immediately after `pipe2()`, before any writer
  exists. The resize is best-effort: failures (`EPERM` over
  `/proc/sys/fs/pipe-max-size` for non-root, `EINVAL` on rejected
  sizes) are tolerated and the kernel default takes over. The
  resize is intentionally done while the pipe is empty to avoid
  Linux kernel bug 212295, which can deadlock `F_SETPIPE_SZ` on
  a pipe that already has partial-unit data buffered.
- **`SIGPIPE` masking.** The send thread blocks `SIGPIPE` so a
  failed receive (which closes the read end) surfaces as `EPIPE`
  from `lzc_send` rather than terminating the process.
- **Error precedence.** If both sides fail, the receive error is
  reported and the send error is dropped; a failed receive is
  almost always the underlying cause and the downstream `EPIPE`
  on the send side would mask it.
- **`SendFlags.RAW` handling.** `RAW` is rejected when set in
  `send_flags` and must instead be passed via `raw=True`, which
  drives both the send and receive sides together so they cannot
  disagree. `SendFlags.SAVED` is rejected entirely; resume flows
  must use `lzc.send` + `lzc.receive` directly.
- **History entry.** The receive ioctl sets the `allow_log` TSD
  on the calling thread, so `py_log_history_impl` can record a
  synthesized `zfs send | zfs receive` line. Send/recv flags are
  rendered as their CLI mnemonics (`-L`, `-e`, `-c`, `-w`, `-F`)
  for grep-ability; `-o` property overrides are noted with a
  fixed marker rather than rendered inline.
- **`props` semantics.** User-supplied `props` are passed to
  `lzc_receive_with_cmdprops` as `cmdprops`, applied to the
  destination with source `LOCAL` - the `zfs receive -o` slot.
  This matches the mental model most callers have for "set
  this property on the destination": LOCAL is what `zfs get`
  reports without `-s received`, and `zfs inherit <prop>`
  clears it the way users expect.
- **`props` value conversion.** The kernel's
  `zfs_set_prop_nvlist` rejects `DATA_TYPE_STRING` values for
  non-string properties (compression, readonly, etc.). The
  caller-supplied dict arrives as a string-valued nvlist via
  `py_dict_to_nvlist`, so a temporary `libzfs` handle is
  opened (via `lzc_repl_open_temp_handle`) just to run
  `zfs_valid_proplist` and convert strings to the native
  types the ioctl expects. Bad values are reported as
  `ZFSCoreException` before the transfer starts.
- **Cancellation.** The call is not interruptible from Python.
  `SIGINT` is queued during the underlying ioctls; depending on
  where the kernel is when the signal arrives, the ioctl may
  return `EINTR` (surfaced as `ZFSCoreException`) or run to
  completion with `KeyboardInterrupt` raised on the next bytecode
  after return.
