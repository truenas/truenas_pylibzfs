# src/

C source for the `truenas_pylibzfs` Python extension module.

## Subdirectories

- [libzfs/README.md](libzfs/README.md) - bindings for the `libzfs` API;
  handle-based, requires a mutex for thread safety
- [libzfs_core/README.md](libzfs_core/README.md) - bindings for the
  `libzfs_core` API; handle-free and thread-safe by design
