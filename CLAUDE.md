# truenas_pylibzfs

Python C extension providing bindings for libzfs and libzfs_core for TrueNAS.

## ZFS Source — MANDATORY

When reading ZFS headers or source, **never** use `/usr/include/libzfs` or other
system paths. Always resolve the ZFS source root using the following priority:

1. **Environment variable**: if `ZFS_SRC` is set, use that path as the ZFS source
   root (e.g. `export ZFS_SRC=/CODE/claudedir/zfs`).
2. **Ask the user**: if `ZFS_SRC` is not set, ask the user where their local ZFS
   source tree is before proceeding with any ZFS source lookups.
3. **GitHub fallback**: if no local copy is available, fetch source directly from
   `https://github.com/truenas/zfs`, branch `truenas/zfs-2.4-release`.

Once the root is known (call it `$ZFS_SRC`), the relevant subtrees are:

- Headers: `$ZFS_SRC/include/`
- Internal headers: `$ZFS_SRC/lib/libzfs/` (e.g. `libzfs_impl.h`)
- libzfs: `$ZFS_SRC/lib/libzfs/`
- libzutil: `$ZFS_SRC/lib/libzutil/`
- libzfs_core: `$ZFS_SRC/lib/libzfs_core/`
- cmd/zpool: `$ZFS_SRC/cmd/zpool/`
- cmd/zfs: `$ZFS_SRC/cmd/zfs/`

## Build

```
python setup.py build_ext --inplace
```

`setup.py` lists all source files and link libraries (`zfs`, `zfs_core`,
`nvpair`, `uutil`, `zutil`). Add new `.c` files there when creating them.

A ZFS version compatible with `$ZFS_SRC` must be installed on the build host
(kernel module + userland libraries). The installed version must match the source
tree — mismatches will cause build or runtime failures.

## Source Layout

```
src/
  truenas_pylibzfs.c        — module entry point
  truenas_pylibzfs.h        — main header (structs, macros, extern declarations)
  truenas_pylibzfs_state.h/c — per-module-instance state (enum refs, etc.)
  truenas_pylibzfs_enums.h
  truenas_pylibzfs_core.h
  truenas_pylibzfs_crypto.h
  zfs.h                     — aggregated ZFS includes
  common/
    error.c                 — py_get_zfs_error(), set_exc_from_libzfs()
    utils.c                 — get_dataset_type(), py_repr_zfs_obj_impl(), etc.
    nvlist_utils.c          — nvlist_to_json_str(), py_dump_nvlist()
    nvlist_utils_nvl_to_dict.c — py_nvlist_to_dict(), user_props_nvlist_to_py_dict()
    nvlist_utils_dict_to_nvl.c — py_dict_to_nvlist(), py_zfsprops_to_nvlist(), etc.
    py_zfs_prop_sets.c      — frozenset property set module
  libzfs/
    py_zfs.c                — ZFS handle object (open_handle, create_resource, etc.)
    py_zfs_pool.c           — ZFSPool object
    py_zfs_dataset.c        — ZFSDataset object
    py_zfs_volume.c         — ZFSVolume object
    py_zfs_snapshot.c       — ZFSSnapshot object
    py_zfs_resource.c       — shared resource methods
    py_zfs_object.c         — ZFSObject (bookmark support)
    py_zfs_prop.c           — property get/set
    py_zfs_pool_prop.c      — pool property get/set
    py_zfs_pool_create.c    — pool creation / vdev spec building
    py_zfs_pool_expand.c    — pool expand
    py_zfs_pool_scrub.c     — pool scrub info
    py_zfs_pool_status.c    — pool status
    py_zfs_iter.c/h         — iterators
    py_zfs_events.c/h       — ZFS event iterator
    py_zfs_history.c        — pool history iterator
    py_zfs_mount.c          — mount/umount
    py_zfs_crypto.c         — encryption helpers
    py_zfs_userquota.c      — user/group quota
    py_zfs_enum.c           — enum construction helpers
    py_zfs_common.c         — shared methods (promote, etc.)
    py_libzfs_types_module.c — libzfs_types submodule (exposes types + enums)
  libzfs_core/
    py_zfs_core_module.c    — lzc module entry + docstrings (PyDoc_STRVAR here)
    libzfs_core_replication.h/c — send/receive wrapper functions (no docstrings)
    lua_channel_programs.h
stubs/                      — Python type stubs (.pyi) for IDE support
tests/                      — pytest test suite
examples/                   — usage examples
```

## C Code Style

- Comments, docstrings, and README files must be ASCII only — no Unicode punctuation (em dashes, curly quotes, ellipsis, box-drawing characters, etc.).
- Declare variables at the head of functions (C89 style).
- NULL-initialize pointers at declaration: `nvlist_t *nvl = NULL;`
- Use `Py_SETREF(op, newval)` instead of `Py_DECREF(op); op = newval;`.
  Use `Py_XSETREF` when `op` may be NULL. Use `Py_CLEAR(op)` instead of
  `Py_DECREF(op); op = NULL;`.
- No column-alignment spacing for struct members or other constructs.
- Header guards use `#ifndef _FOO_H` / `#define _FOO_H` / `#endif` — not `#pragma once`.
- Docstrings belong in `py_zfs_core_module.c` as `PyDoc_STRVAR()`, not in implementation files.
- Expand ZFS-specific acronyms on first use in a comment block, e.g. `TSD (Thread-Specific Data)`.

## Key Patterns

### Thread safety and locking

```c
py_zfs_error_t zfs_err;
bool err = false;

Py_BEGIN_ALLOW_THREADS        // drop GIL
PY_ZFS_LOCK(plz);             // take libzfs handle lock
// ... ZFS operation ...
if (failure) {
    py_get_zfs_error(plz->lzh, &zfs_err);
    err = true;
}
PY_ZFS_UNLOCK(plz);
Py_END_ALLOW_THREADS          // re-acquire GIL

if (err) {
    set_exc_from_libzfs(&zfs_err, "Context message");
    return NULL;
}
```

### Error retrieval

- Retrieve error info via `py_get_zfs_error()` **while the lock is held**.
- Set the Python exception via `set_exc_from_libzfs()` **after** releasing the lock (GIL required).

### nvlist ↔ Python dict

- nvlist → Python dict: `py_nvlist_to_dict(nvl)`
- nvlist → JSON string (for history logging): `nvlist_to_json_str(nvl)` — returns
  `PyMem_RawMalloc`'d `char *`, free with `PyMem_RawFree()` (safe to call on NULL).
- Python dict → nvlist: `py_dict_to_nvlist(dict)`

### History logging

```c
py_log_history_fmt(plz, "zpool <command> %s", pool_name);
```

For libzfs_core (no `py_zfs_t`): `py_log_history_impl(NULL, NULL, fmt, ...)` opens
a temporary libzfs handle internally.

### Audit

Each method should emit an audit event at entry:

```c
PySys_Audit("truenas_pylibzfs.<method>", "...", ...);
```

### Key structs

| Struct | Purpose |
|---|---|
| `py_zfs_t` | Wraps `libzfs_handle_t`; holds mutex, history settings |
| `py_zfs_obj_t` | Base for all ZFS resource objects |
| `py_zfs_resource_t` | Extends `py_zfs_obj_t`; adds `is_simple` flag |
| `py_zfs_pool_t` | Wraps `zpool_handle_t` |

Macro `RSRC_TO_ZFS(x)` gets `py_zfs_obj_t *` from a resource pointer.

### Type instantiation

All C extension types (`ZFSPool`, `ZFSDataset`, etc.) set `.tp_new = py_no_new_impl`
(defined in `common/utils.c`), which blocks direct Python-level construction and raises
`TypeError`. Internal C factories allocate instances via `Type.tp_alloc(&Type, 0)`
directly, bypassing `tp_new`.

When adding a new type, follow the same pattern: no `py_xxx_new` / `py_xxx_init`
boilerplate — set `.tp_new = py_no_new_impl` and write a named C factory function.

### Module state

`py_get_module_state(zfs)` returns `pylibzfs_state_t *` with cached enum refs and
other per-instance state. Use this rather than calling into Python enum classes directly.

## README files (`src/`)

`src/libzfs/README.md` and `src/libzfs_core/README.md` document the purpose
and contents of their respective directories.

**When adding a new source file or making a functional change to an existing
one, update the relevant README.md** - add the file to the source files table
and note any new methods, types, or behavioural changes.

## Stubs (`stubs/`)

The `stubs/` directory contains Python type stubs (`.pyi`) that describe the
public API of the C extension for IDEs and type checkers.

**When updating stubs, do NOT modify any C source or header files.** Stub
updates are purely documentation of the existing C API — if the C API needs to
change, that is a separate task and must be done explicitly.

## Testing

Tests use pytest and require a running system with ZFS support. Fixtures in
`tests/conftest.py` create temporary pools backed by sparse image files.

```
pytest tests/
```

Tests that need root / ZFS access must run as root on a system with ZFS kernel support.
