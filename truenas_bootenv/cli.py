"""Command line interface for TrueNAS boot environment management.

NOTE: argparse-manpage imports this module at Debian package build
time (in a chroot with no zfs.ko) to generate the manpage from
build_parser(), and importing truenas_pylibzfs calls libzfs_init()
immediately. Keep the module-level imports below free of the binding
and import everything ZFS-related lazily inside the command handlers.
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import TYPE_CHECKING
from . import naming
from .errors import BEError, BENotFound

if TYPE_CHECKING:
    # never imported at runtime: argparse-manpage loads this module in
    # the Debian build chroot, where the binding cannot initialize
    from truenas_pylibzfs import libzfs_types

DEFAULT_POOLS = ('boot-pool', 'freenas-boot')


def build_parser() -> argparse.ArgumentParser:
    """Build the argument parser (also consumed by argparse-manpage)."""
    parser = argparse.ArgumentParser(
        prog='truenas-bootenv',
        description=(
            'Manage TrueNAS boot environments. Replaces the zectl '
            'commands previously used by TrueNAS.'
        ),
        epilog=(
            'This tool is for interactive administration and '
            'debugging. TrueNAS middleware manages boot environments '
            'through the boot.environment API and serializes '
            'concurrent operations. Avoid running mutating commands '
            'here while middlewared is active.'
        ),
    )
    parser.add_argument(
        '--pool', metavar='POOL',
        help=(
            'boot pool to operate on (default: boot-pool, falling back '
            'to freenas-boot)'
        ),
    )
    sub = parser.add_subparsers(dest='command', required=True)

    p = sub.add_parser(
        'list', help='List boot environments',
        description=(
            'List boot environments. The Active column shows N for the '
            'environment the system is running now and R for the one '
            'selected for the next boot.'
        ),
    )
    p.add_argument(
        '-H', dest='no_header', action='store_true',
        help='Scripting mode: no header, tab-separated columns',
    )
    p.set_defaults(func=_cmd_list)

    p = sub.add_parser(
        'activate', help='Select the boot environment for the next boot',
        description=(
            'Make a boot environment the default for the next boot and '
            'regenerate the boot menu. The running environment is not '
            'affected until reboot. Re-activating the already-selected '
            'environment is allowed and regenerates the menu.'
        ),
    )
    p.add_argument('name', help='Name of the boot environment')
    p.set_defaults(func=_cmd_activate)

    p = sub.add_parser(
        'create', help='Clone a boot environment into a new one',
        description=(
            'Create a new boot environment as a recursive clone of an '
            'existing one, preserving its locally set and received '
            'native properties and kernel version. The new environment '
            'is not activated.'
        ),
    )
    p.add_argument(
        '-e', dest='source', metavar='SOURCE',
        help=(
            'name of the source boot environment (default: the '
            'running one, unlike zectl which used the next-boot one)'
        ),
    )
    p.add_argument('target', help='Name for the new boot environment')
    p.set_defaults(func=_cmd_create)

    p = sub.add_parser(
        'destroy', help='Destroy a boot environment',
        description=(
            'Destroy a boot environment and reclaim its space. The '
            'running and next-boot environments are refused. Clones '
            'depending on the destroyed environment are promoted first '
            'so they keep their data.'
        ),
    )
    p.add_argument('name', help='Name of the boot environment')
    p.set_defaults(func=_cmd_destroy)

    return parser


def _open_handle() -> libzfs_types.ZFS:
    import truenas_pylibzfs
    return truenas_pylibzfs.open_handle()


def _resolve_pool(lzh: libzfs_types.ZFS, pool_arg: str | None) -> str:
    """Return the boot pool name, honoring --pool when given."""
    from . import engine
    candidates = (pool_arg,) if pool_arg is not None else DEFAULT_POOLS
    for pool in candidates:
        if naming.validate_component(pool) is not None:
            raise BEError(f'{pool!r} is not a valid pool name')
        if engine.dataset_exists(lzh, f'{pool}/ROOT'):
            return pool
    tried = ', '.join(repr(p) for p in candidates)
    raise BEError(f'no boot environment root found on {tried}')


def _dataset(lzh: libzfs_types.ZFS, pool_arg: str | None, name: str) -> str:
    pool = _resolve_pool(lzh, pool_arg)
    try:
        return naming.be_dataset(pool, name)
    except ValueError as e:
        raise BEError(f'invalid boot environment name: {e}') from e


def _destroy_dataset(
    lzh: libzfs_types.ZFS, pool_arg: str | None, name: str,
) -> str:
    """Compose the dataset path for destroy without re-judging the name.

    list shows boot environments whose names create() would refuse (an
    interior space, say, inherited from a release that allowed them), so
    destroy has to be able to reach them or the CLI could list a boot
    environment it can never remove. The engine checks the position of
    the dataset rather than its characters, exactly for this.
    """
    return f'{_resolve_pool(lzh, pool_arg)}/ROOT/{name}'


def _cmd_list(args: argparse.Namespace) -> int:
    from . import engine

    lzh = _open_handle()
    pool = _resolve_pool(lzh, args.pool)
    try:
        running = engine.running_dataset()
    except BEError:
        running = None   # listing degrades; mutating verbs stay strict
    rows = []
    for be in engine.list_environments(
        lzh, pool_name=pool, running_ds=running,
    ):
        created = time.strftime(
            '%Y-%m-%d %H:%M', time.localtime(be.created),
        )
        flags = (('N' if be.active else '')
                 + ('R' if be.activated else ''))
        rows.append((be.name, flags or '-', created))

    if args.no_header:
        for row in rows:
            print('\t'.join(row))
        return 0
    widths = [max(len(r[i]) for r in rows + [('Name', 'Active', 'Created')])
              for i in range(3)]
    for row in [('Name', 'Active', 'Created')] + rows:
        print('  '.join(col.ljust(widths[i]) for i, col in enumerate(row)))
    return 0


def _cmd_activate(args: argparse.Namespace) -> int:
    from . import engine
    lzh = _open_handle()
    engine.activate(
        lzh, dataset=_dataset(lzh, args.pool, args.name), run_grub=True,
    )
    print(f'{args.name} will be used on the next boot')
    return 0


def _cmd_create(args: argparse.Namespace) -> int:
    from . import engine
    lzh = _open_handle()
    source: str | None
    if args.source is not None:
        source = _dataset(lzh, args.pool, args.source)
    else:
        source = engine.running_dataset()
        if source is None:
            raise BEError(
                'could not determine the running boot environment; '
                'use -e to name the source',
            )
    engine.create(
        lzh, source_dataset=source,
        target_dataset=_dataset(lzh, args.pool, args.target),
        run_grub=True,
    )
    print(f'{args.target} created from {source.split("/")[-1]}')
    return 0


def _cmd_destroy(args: argparse.Namespace) -> int:
    from . import engine
    lzh = _open_handle()
    dataset = _destroy_dataset(lzh, args.pool, args.name)
    # the engine counts an absent boot environment as success so the
    # updater's prune loop can retry after a crash. Someone typing a name
    # at a shell wants to be told they got it wrong, not that a boot
    # environment that never existed was destroyed.
    if not engine.dataset_exists(lzh, dataset):
        raise BENotFound(f'{args.name!r} not found')
    engine.destroy(
        lzh, dataset=dataset,
        running_ds=engine.running_dataset(), run_grub=True,
    )
    print(f'{args.name} destroyed')
    return 0


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        result: int = args.func(args)
        return result
    except BEError as e:
        print(f'truenas-bootenv: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
