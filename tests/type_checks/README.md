# tests/type_checks

Fully-annotated Python files that verify the `truenas_pylibzfs` stubs are
correct and that common call patterns type-check cleanly.

## Purpose

These files are not pytest tests and are never executed at runtime. They exist
to be checked by mypy. A failure here means a stub signature is wrong or an
expected call pattern has become incompatible with the stubs.

This catches a class of bug that stubtest cannot: stubtest checks that stubs
match the runtime, but it cannot detect when the declared types are internally
inconsistent or reject valid call patterns (e.g. wrong container type for an
argument).

## Requirements

`truenas_pylibzfs` must be installed so mypy can resolve the package. In CI
this is satisfied by the build step that precedes stub checking. For local use,
install the package first (`pip install -e .`).

## Running

```
python3 -m mypy tests/type_checks/
```

No extra flags. The files are fully annotated and run under the `strict = true`
setting in `mypy.ini`.

## CI

Run in `qemu-4-test.sh` after `mypy stubs/` and before stubtest. A non-zero
exit from any of the three checks fails the stub-checks section.
