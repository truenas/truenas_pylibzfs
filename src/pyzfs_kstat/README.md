# src/pyzfs_kstat

ZFS kernel module statistics reader, exposed as the `truenas_pylibzfs.kstat`
submodule.

## Overview

Unlike the `libzfs` and `libzfs_core` bindings, this submodule has **no
dependency on libzfs or any ZFS userland library**. It reads kstat files
exposed by the ZFS kernel module under `/proc/spl/kstat/zfs/` via standard
C stdio, making it lightweight and usable without opening a `ZFS` handle.

Currently provides ARC (Adaptive Replacement Cache) statistics via
`get_arcstats()`. ZIL statistics will follow.

## Source files

| File | Purpose |
|---|---|
| `pyzfs_kstat.h` | Module state struct, path/field-count constants, extern declarations |
| `pyzfs_kstat.c` | Module init, `ArcStats` `PyStructSequence` type registration, `PyDoc_STRVAR` field docstrings, method table |
| `arcstats.c` | `get_arcstats()` implementation -- reads and parses `/proc/spl/kstat/zfs/arcstats` |

## Exposed methods

| Python name | Source | Description |
|---|---|---|
| `get_arcstats()` | `arcstats.c` | Returns an `ArcStats` struct sequence with 147 integer fields |

## Exposed types

| Python name | Kind | Description |
|---|---|---|
| `ArcStats` | `PyStructSequence` | Named tuple-like object with one field per ARC kstat counter |

## Developer notes

### Adding a new kstat

To add a new kstat (e.g. ZIL stats):

1. Create a new `.c` file (e.g. `zilstats.c`) with the parsing function.
2. Add `PyDoc_STRVAR` field docstrings and the `PyStructSequence_Desc` to
   `pyzfs_kstat.c`.
3. Extend `pyzfs_kstat_state_t` in `pyzfs_kstat.h` with the new type pointer
   and update the field count constant.
4. Register the new type in `py_setup_kstat_module()`.
5. Add the new `.c` file to `setup.py`.
6. Add a corresponding `.pyi` stub entry in `stubs/kstat.pyi`.

### Field ordering

Field names and order in the `PyStructSequence_Field` array must match the
kstat initializer in the ZFS source tree (`module/zfs/arc.c` for ARC stats).
The CI test `tests/test_kstat_arcstats.py` verifies this invariant against the
live `/proc/spl/kstat/zfs/arcstats` file.

### Docstrings

All `PyDoc_STRVAR` definitions for field and method docstrings belong in
`pyzfs_kstat.c`. Implementation files (e.g. `arcstats.c`) contain only
parsing logic and no docstrings.
