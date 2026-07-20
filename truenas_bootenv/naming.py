"""Pure string helpers for boot environment naming.

No ZFS imports allowed in this module: it must stay importable in
environments without the ZFS kernel module (the Debian build chroot,
where argparse-manpage imports the CLI at package build time).
"""

from __future__ import annotations

import string
import time

# Characters ZFS accepts in a dataset/snapshot name component, per
# valid_char() in module/zcommon/zfs_namecheck.c. '/', '@' and '#' are
# delimiters (child dataset / snapshot / bookmark), never valid inside
# a component.
#
# Edge rules beyond valid_char():
# - '%' is rejected: ZFS reserves it for internal temporary clones and
#   libzfs itself refuses it on modifying operations
#   (zfs_validate_name, libzfs_dataset.c).
# - ' ' is rejected although ZFS accepts interior spaces: the grub
#   menu generator (10_truenas_linux) expands names unquoted and a
#   space in the activated name breaks the whole menu. Also matches
#   name_is_valid() in truenas_pylibzfs.c for the trailing-space case.
_VALID_COMPONENT_CHARS = frozenset(
    string.ascii_letters + string.digits + '-_.:'
)


def snapshot_suffix(now_ns: int | None = None) -> str:
    """Timestamp-based snapshot suffix with microsecond resolution.

    zectl used strftime('%F-%T'), which collides under rapid
    successive creates. Example: 2026-07-01-14:03:05.123456

    now_ns overrides the clock for tests (a time.time_ns() value).
    """
    ns = time.time_ns() if now_ns is None else now_ns
    return (
        time.strftime('%Y-%m-%d-%H:%M:%S', time.localtime(ns // 1_000_000_000))
        + f'.{(ns // 1000) % 1_000_000:06d}'
    )


def bump(suffix: str) -> str:
    """Return the next variant of a snapshot suffix.

    Appends or increments a trailing '-N' counter:

        'S' -> 'S-1' -> 'S-2' -> ...

    A fresh suffix from snapshot_suffix() ends in the '.NNNNNN'
    microsecond field, never in '-N', so the increment cannot misfire.
    """
    base, sep, tail = suffix.rpartition('-')
    if sep and tail.isdigit():
        return f'{base}-{int(tail) + 1}'
    return f'{suffix}-1'


def rel_path(src_root: str, ds: str) -> str:
    """Path of ds relative to src_root ('' for src_root itself).

    Raises ValueError if ds is not src_root or a descendant of it.
    'p/ROOT/ab' is not under 'p/ROOT/a' even though it shares the
    string prefix.
    """
    if ds == src_root:
        return ''
    prefix = src_root + '/'
    if not ds.startswith(prefix):
        raise ValueError(f'{ds!r} is not under {src_root!r}')
    return ds[len(prefix):]


def map_target(src_root: str, tgt_root: str, ds: str) -> str:
    """Map a dataset in the source subtree onto the target subtree.

    map_target('p/ROOT/a', 'p/ROOT/b', 'p/ROOT/a')     -> 'p/ROOT/b'
    map_target('p/ROOT/a', 'p/ROOT/b', 'p/ROOT/a/var') -> 'p/ROOT/b/var'
    """
    rel = rel_path(src_root, ds)
    return tgt_root if rel == '' else f'{tgt_root}/{rel}'


def validate_be_dataset(dataset: str) -> str | None:
    """Validate that dataset names a boot environment root.

    TrueNAS boot environments live at exactly <pool>/ROOT/<name>.
    Returns None if valid, else a human-readable reason.
    """
    if len(dataset) > 255:
        return f'{dataset!r} exceeds the maximum dataset name length'
    parts = dataset.split('/')
    if len(parts) != 3:
        return f'{dataset!r} is not of the form <pool>/ROOT/<name>'
    pool, parent, name = parts
    if parent != 'ROOT':
        return f'{dataset!r} is not under a ROOT dataset'
    for component in (pool, name):
        reason = validate_component(component)
        if reason is not None:
            return reason
    return None


def be_dataset(pool_name: str, name: str) -> str:
    """Validate name and compose the <pool>/ROOT/<name> dataset path.

    Raises ValueError carrying the reason when name is not a valid
    boot environment name or the composed path is not a valid boot
    environment dataset.
    """
    reason = validate_component(name)
    if reason is None:
        reason = validate_be_dataset(f'{pool_name}/ROOT/{name}')
    if reason is not None:
        raise ValueError(reason)
    return f'{pool_name}/ROOT/{name}'


def validate_component(name: str) -> str | None:
    """Validate a single BE name component (no '/', '@', '#').

    Returns None if valid, else a human-readable reason. Mirrors the
    ZFS zfs_namecheck.c component rules.
    """
    if not name:
        return 'name is empty'
    if len(name) > 255:
        return 'name too long'
    bad = set(name) - _VALID_COMPONENT_CHARS
    if bad:
        # repr the characters: a bare space or tab would be invisible
        # in the error message
        return f'invalid character(s): {" ".join(repr(c) for c in sorted(bad))}'
    if name in ('.', '..'):
        return f'{name!r} is reserved'
    return None
