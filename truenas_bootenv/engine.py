"""Boot environment operations implemented on truenas_pylibzfs.

The functions here replace the zectl commands middleware shells out to.
Functions that touch ZFS take an open libzfs handle as their first
argument (middleware passes its thread-local handle, the CLI opens its
own) and identify boot environments by full dataset path.

Only this module imports truenas_pylibzfs at module level (see the
package __init__).

Divergences from zectl, beyond those noted per function:
  - mountpoint-not-none preconditions dropped (the menu entry names
    the root dataset and the initrd mounts it by name, so the
    mountpoint property plays no part in booting)
  - no 'destroy be@snapshot' and no 'destroy -F' (middleware never
    used them; mounted datasets refuse loudly instead of being
    force-unmounted)
  - no create from an explicit snapshot (middleware never used it,
    and a recursive clone needs one consistent snapshot on every
    child; create takes its own atomic set instead)
  - volumes inside a boot environment are refused everywhere, never
    silently destroyed or skipped (zectl destroyed them with the
    environment, losing user data)
"""

from __future__ import annotations

import dataclasses
import errno
import functools
import string
import subprocess
import truenas_pylibzfs
from collections.abc import Callable, Collection
from truenas_pylibzfs import (
    PropertySource,
    ZFSError,
    ZFSProperty,
    libzfs_types,
    property_sets,
)
from typing import Any, ParamSpec, TypeVar
from . import naming
from .errors import (
    BEBusy,
    BEDestroyUnsafe,
    BEError,
    BEExists,
    BEGrubError,
    BENotFound,
)

P = ParamSpec('P')
R = TypeVar('R')

_COPY_SOURCES = (PropertySource.LOCAL, PropertySource.RECEIVED)

KERNEL_VERSION_PROP = 'truenas:kernel_version'
KEEP_PROP = 'zectl:keep'
GRUB_PENDING_PROP = 'truenas:grub_pending'

_PROC_MOUNTS = '/proc/mounts'


@dataclasses.dataclass(frozen=True, slots=True)
class BootEnvironment:
    """One boot environment, as reported by list_environments.

    keep mirrors the zectl:keep user property: True or False when it is
    set, None when it is absent.
    """

    name: str
    dataset: str
    created: int
    used_bytes: int
    keep: bool | None
    can_activate: bool
    active: bool
    activated: bool


# kernel versions end up in grub.cfg menuentry ids and kernel file
# paths, so keep them to the character set real kernels use
_KERNEL_VERSION_CHARS = frozenset(
    string.ascii_letters + string.digits + '._+-'
)


def _exists(lzh: libzfs_types.ZFS, name: str) -> bool:
    """Return whether a dataset/snapshot with this name exists."""
    try:
        lzh.open_resource(name=name)
    except truenas_pylibzfs.ZFSException as e:
        if e.code == ZFSError.EZFS_NOENT:
            return False
        raise
    return True


def _translating_zfs_errors(fn: Callable[P, R]) -> Callable[P, R]:
    """Translate ZFSException into typed BE errors at the API boundary."""
    @functools.wraps(fn)
    def wrapper(*args: P.args, **kwargs: P.kwargs) -> R:
        try:
            return fn(*args, **kwargs)
        except truenas_pylibzfs.ZFSException as e:
            if e.code == ZFSError.EZFS_BUSY:
                raise BEBusy(f'ZFS reports busy: {e}', errno.EBUSY) from e
            if e.code == ZFSError.EZFS_NOENT:
                raise BENotFound(str(e)) from e
            raise BEError(f'ZFS error: {e}') from e
    return wrapper


def _collect_topdown(hdl: Any, state: list[Any]) -> bool:
    """iter_filesystems() callback: append hdl to state, then recurse.

    iter_filesystems() only visits direct children, so the callback
    re-runs it on each child (zectl's C callbacks do the same).
    """
    state.append(hdl)
    hdl.iter_filesystems(callback=_collect_topdown, state=state)
    return True


def _walk_topdown(dataset: Any, collected: list[Any]) -> list[Any]:
    """Collect dataset and all descendant filesystems, parents first.

    create clones parents before children; destroy reverses the list
    for leaf-first teardown.
    """
    _collect_topdown(dataset, collected)
    return collected


def _collect_snap(hdl: Any, state: list[Any]) -> bool:
    """iter_snapshots() callback: append hdl to state."""
    state.append(hdl)
    return True


def _snapshots(dataset: Any) -> list[Any]:
    """Return dataset's snapshots, oldest first."""
    found: list[Any] = []
    dataset.iter_snapshots(
        callback=_collect_snap, state=found, order_by_transaction_group=True,
    )
    return found


def _origin(dataset: Any) -> str | None:
    """Return the origin snapshot name if dataset is a clone, else None."""
    a = dataset.asdict(properties={ZFSProperty.ORIGIN})
    value = a['properties']['origin']['value']
    if not isinstance(value, str) or value in ('', '-'):
        return None
    return value


def _kernel_version(user_properties: dict[str, str]) -> str | None:
    """Return the recorded kernel version from a user-property dict.

    Returns None when the property is absent, holds a ZFS spelling of
    absence, or contains characters the boot menu generator cannot
    embed safely, so every caller runs the same bootability test.
    """
    value = user_properties.get(KERNEL_VERSION_PROP)
    if value in (None, '', '-'):
        return None
    if set(value) - _KERNEL_VERSION_CHARS:
        return None
    return value


def _pool_bootfs(lzh: libzfs_types.ZFS, pool_name: str) -> str | None:
    """Return the pool's bootfs dataset name, or None when unset.

    An unset bootfs reads back as '-'.
    """
    pool = lzh.open_pool(name=pool_name)
    props = pool.get_properties(
        properties={truenas_pylibzfs.ZPOOLProperty.BOOTFS},
    )
    value = props.bootfs.value if props.bootfs is not None else None
    if not isinstance(value, str) or value in ('', '-'):
        return None
    return value


def _refuse_volumes(subtree: list[Any], dataset: str) -> None:
    """Refuse boot environments that contain zvols.

    zectl silently destroys them with the environment. A volume here
    is user data in the wrong place, and clone cannot copy its
    volume-only properties anyway.
    """
    for child in subtree:
        if child.type == truenas_pylibzfs.ZFSType.ZFS_TYPE_VOLUME:
            raise BEError(
                f'{dataset!r} contains a volume ({child.name!r}); '
                f'volumes are not valid boot environment members',
                errno.ENOTSUP,
            )


def _refuse_if_mounted(subtree: list[Any], dataset: str) -> None:
    """Refuse the destroy while any dataset in the subtree is mounted.

    The leaf-first teardown would destroy the unmounted members before
    the mounted one refused, losing their data. zectl checked this up
    front; a boot environment an operator mounted to recover files must
    be left intact for them to unmount, not partially destroyed by the
    updater's space-pruning.
    """
    for fs in subtree:
        props = fs.asdict(properties={ZFSProperty.MOUNTED})['properties']
        if props['mounted']['value']:
            raise BEBusy(
                f'{dataset!r} has a mounted dataset ({fs.name!r}); '
                f'unmount it before destroying',
                errno.EBUSY,
            )


def _refuse_if_held(subtree: list[Any], dataset: str) -> None:
    """Refuse the destroy while any snapshot in the subtree is held.

    The leaf-first teardown destroys the deeper members first, so a held
    snapshot on a shallower dataset (the root's go last) would refuse only
    after those members are already gone, stranding a half-destroyed
    remnant. A user hold marks a snapshot someone means to keep, so refuse
    before the walk and leave the boot environment intact for them to
    release the hold.
    """
    for fs in subtree:
        for snap in _snapshots(fs):
            if snap.get_holds():
                raise BEBusy(
                    f'{dataset!r} has a held snapshot ({snap.name!r}); '
                    f'release the hold before destroying',
                    errno.EBUSY,
                )


@_translating_zfs_errors
def dataset_exists(lzh: libzfs_types.ZFS, name: str) -> bool:
    """Return whether a dataset or snapshot with this name exists."""
    return _exists(lzh, name)


@_translating_zfs_errors
def pool_bootfs(lzh: libzfs_types.ZFS, pool_name: str) -> str | None:
    """Return the pool's bootfs dataset name, or None when unset."""
    return _pool_bootfs(lzh, pool_name)


def _root_dataset_from_mounts() -> str | None:
    """Return the ZFS dataset mounted at '/' per the kernel mount table.

    zectl read the running dataset from the mount table; this recovers
    it the same way when statmount cannot name the source. Returns None
    if the table has no ZFS root entry.
    """
    try:
        with open(_PROC_MOUNTS) as mounts:
            for line in mounts:
                fields = line.split()
                if len(fields) >= 3 and fields[1] == '/' and fields[2] == 'zfs':
                    return fields[0]
    except OSError:
        pass
    return None


def running_dataset() -> str | None:
    """Return the dataset mounted at '/', or None when / is not ZFS.

    The source comes from statmount; when the kernel reports a ZFS
    root without naming it, the mount table is read as a fallback (as
    zectl did) so a listing keeps working. Raises BEError only when the
    answer cannot be determined at all: the destroy guards derive from
    it, so a detection failure must not read as there being no running
    boot environment. truenas_os_pyutils is imported lazily so the
    engine stays importable where it is not installed.
    """
    try:
        from truenas_os_pyutils.mount import statmount
        entry = statmount(path='/')
        fs_type = entry['fs_type']
        source = entry['mount_source']
    except Exception as e:
        raise BEError(
            f'cannot determine the running boot environment: {e}',
        ) from e
    if fs_type != 'zfs':
        return None
    if not source:
        # kernels without STATMOUNT_SB_SOURCE report the source as None;
        # recover it from the mount table the way zectl did, so listing
        # keeps working and the destroy guard still has the real dataset
        source = _root_dataset_from_mounts()
    if not isinstance(source, str) or not source:
        # a ZFS root whose dataset cannot be named must fail closed, not
        # read as there being no running boot environment
        raise BEError(
            'cannot determine the running boot environment: neither '
            'statmount nor the mount table named the dataset mounted at /',
        )
    return source


def _collect_child(hdl: Any, state: list[Any]) -> bool:
    """iter_filesystems() callback: append hdl to state."""
    state.append(hdl)
    return True


@_translating_zfs_errors
def list_environments(
    lzh: libzfs_types.ZFS, *, pool_name: str, running_ds: str | None,
) -> list[BootEnvironment]:
    """Describe the boot environments on a pool.

    Returns one BootEnvironment per entry: name, dataset, created
    (unix time), used_bytes, keep (True/False, or None when the
    property is absent), can_activate, active (equals running_ds) and
    activated (equals the pool bootfs). Raises BENotFound when
    <pool>/ROOT does not exist.
    """
    root_name = f'{pool_name}/ROOT'
    if not _exists(lzh, root_name):
        raise BENotFound(f'{root_name!r} not found')
    bootfs = _pool_bootfs(lzh, pool_name)
    children: list[Any] = []
    lzh.open_resource(name=root_name).iter_filesystems(
        callback=_collect_child, state=children,
    )
    entries: list[BootEnvironment] = []
    for child in children:
        if child.type == truenas_pylibzfs.ZFSType.ZFS_TYPE_VOLUME:
            # volumes are never boot environment members; a stray
            # zvol under <pool>/ROOT must not pollute the listing
            continue
        try:
            a = child.asdict(
                properties={ZFSProperty.CREATION, ZFSProperty.USED},
                get_user_properties=True,
            )
        except truenas_pylibzfs.ZFSException as e:
            if e.code == ZFSError.EZFS_NOENT:
                # destroyed while we were listing
                continue
            raise
        props = a['properties']
        user_properties = a['user_properties'] or {}
        keep = user_properties.get(KEEP_PROP)
        entries.append(BootEnvironment(
            name=child.name.split('/')[-1],
            dataset=child.name,
            created=props['creation']['value'],
            used_bytes=props['used']['value'],
            keep=None if keep is None else keep == 'True',
            can_activate=_kernel_version(user_properties) is not None,
            active=child.name == running_ds,
            activated=child.name == bootfs,
        ))
    return entries


@_translating_zfs_errors
def sync_boot_pool(lzh: libzfs_types.ZFS, pool_name: str) -> None:
    """Force a transaction group commit on the pool.

    A renamed grub.cfg is not on disk until its transaction group
    commits, so without this a power cut right after a menu
    regeneration boots the previous default (verified by sysrq-b
    testing). Callers run it after every menu change.
    """
    lzh.open_pool(name=pool_name).sync_pool()


@_translating_zfs_errors
def set_grub_pending(
    lzh: libzfs_types.ZFS, pool_name: str, pending: bool,
) -> None:
    """Set or clear the pending menu-regeneration marker on <pool>/ROOT.

    The marker records that ZFS state changed while the boot menu may
    not have been regenerated yet. Recovery policy belongs to the
    caller; middleware reconciles the marker at startup.
    """
    root = lzh.open_resource(name=f'{pool_name}/ROOT')
    if pending:
        root.set_user_properties(user_properties={GRUB_PENDING_PROP: '1'})
    else:
        root.inherit_property(property=GRUB_PENDING_PROP)


@_translating_zfs_errors
def grub_pending(lzh: libzfs_types.ZFS, pool_name: str) -> bool:
    """Return whether the pending menu-regeneration marker is set.

    A pool without a ROOT dataset has no marker, so callers probing at
    startup do not need their own missing-dataset handling.
    """
    try:
        root = lzh.open_resource(name=f'{pool_name}/ROOT')
    except truenas_pylibzfs.ZFSException as e:
        if e.code == ZFSError.EZFS_NOENT:
            return False
        raise
    pending: bool = root.get_user_properties().get(GRUB_PENDING_PROP) == '1'
    return pending


def _validated(dataset: str) -> None:
    """Raise BEError unless dataset has the <pool>/ROOT/<name> shape."""
    reason = naming.validate_be_dataset(dataset)
    if reason is not None:
        raise BEError(f'Not a boot environment: {reason}')


def _validated_position(dataset: str) -> None:
    """Raise BEError unless dataset sits at <pool>/ROOT/<name>.

    Unlike _validated this does not re-judge the name's characters:
    an existing dataset whose name create() would refuse (say one
    with an interior space) is still listed by list_environments, so
    destroy must accept it or the API could never remove it.
    """
    parts = dataset.split('/')
    if (len(parts) != 3 or parts[1] != 'ROOT' or not all(parts)
            or '@' in dataset or '#' in dataset):
        raise BEError(f'Not a boot environment: {dataset!r}')


def _lzc_error(e: Exception, context: str, creating: bool = False) -> BEError:
    """Translate a ZFSCoreException into a typed BE error.

    lzc functions raise ZFSCoreException (a sibling of ZFSException),
    whose .errors is a tuple of (name, errno) pairs. EEXIST means a
    name collision when creating snapshots but surviving dependent
    clones when destroying them.
    """
    for name, err in getattr(e, 'errors', None) or ():
        if err == errno.EBUSY:
            return BEBusy(
                f'{context}: {name!r} is busy or held; release holds first',
                errno.EBUSY,
            )
        if err == errno.EEXIST:
            if creating:
                # a snapshot name that was free a moment ago is now taken:
                # system state, not the caller's input, so this stays a
                # plain BEError (middleware maps it to CallError)
                return BEError(
                    f'{context}: {name!r} already exists; retry the operation',
                    errno.EEXIST,
                )
            return BEDestroyUnsafe(
                f'{context}: {name!r} still has dependent clones', errno.EEXIST,
            )
    return BEError(f'{context}: {e}')


def _lzc_create_snapshots(names: Collection[str]) -> None:
    """Create the whole snapshot name-set atomically (one TXG)."""
    try:
        truenas_pylibzfs.lzc.create_snapshots(snapshot_names=set(names))
    except truenas_pylibzfs.lzc.ZFSCoreException as e:
        raise _lzc_error(e, 'snapshot creation failed', creating=True) from e


def _lzc_destroy_snapshots(names: Collection[str]) -> None:
    names = set(names)
    if not names:
        return
    try:
        truenas_pylibzfs.lzc.destroy_snapshots(snapshot_names=names)
    except truenas_pylibzfs.lzc.ZFSCoreException as e:
        raise _lzc_error(e, 'snapshot destruction failed') from e


def _update_grub(lzh: libzfs_types.ZFS, pool_name: str) -> None:
    """Run update-grub and sync the pool, raising BEGrubError on failure.

    Only the CLI path uses this. Middleware regenerates the menu via
    etc.generate('grub') instead, which writes grub.cfg atomically.
    """
    try:
        subprocess.run(
            ['update-grub'], check=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
    except subprocess.CalledProcessError as e:
        out = (e.stdout or b'').decode(errors='replace')
        raise BEGrubError(f'update-grub failed: {out!r}') from e
    except OSError as e:
        raise BEGrubError(f'update-grub could not be run: {e}') from e
    sync_boot_pool(lzh, pool_name)


@_translating_zfs_errors
def activate(
    lzh: libzfs_types.ZFS, *, dataset: str, run_grub: bool = False,
) -> None:
    """Make a boot environment the default for the next boot.

    Sets canmount=noauto on every filesystem in the subtree and
    promotes any that is still a clone, so the boot environment owns its
    data independently of the environment it was cloned from; then points
    the pool's bootfs property at the dataset. One call promotes one
    level, as in zectl, so an environment cloned from a clone still
    depends on the older ancestor afterwards.

    bootfs is only read by grub-mkconfig, so the next boot does not
    change until the menu is regenerated. Middleware does this via
    etc.generate('grub'); the standalone CLI passes run_grub=True.

    Re-activating the already-activated boot environment is allowed,
    which is how a caller retries after a failed menu regeneration. It
    settles on the same bootfs and the same menu, but it is not a no-op
    on disk: an environment that is still a clone is promoted another
    level, as zectl's activate also does, until it owns its data
    outright. A boot environment without truenas:kernel_version is
    refused because the menu generator would skip it and the default
    would silently fall elsewhere.
    """
    _validated(dataset)
    if not _exists(lzh, dataset):
        raise BENotFound(f'{dataset!r} not found')

    subtree = _walk_topdown(lzh.open_resource(name=dataset), [])
    _refuse_volumes(subtree, dataset)
    if _kernel_version(subtree[0].get_user_properties()) is None:
        raise BEError(
            f'{dataset!r} has no usable truenas:kernel_version; the '
            f'boot menu would not show it',
        )
    for fs in subtree:
        fs.set_properties(
            properties={'canmount': 'noauto'}, remount=False,
        )
        if _origin(fs) is not None:
            fs.promote()

    pool = lzh.open_pool(name=dataset.split('/')[0])
    pool.set_properties(properties={'bootfs': dataset})

    if run_grub:
        try:
            _update_grub(lzh, dataset.split('/')[0])
        except BEGrubError as e:
            raise BEGrubError(
                f'{dataset!r} was activated successfully, but {e}; '
                f'run update-grub or activate again to refresh the menu',
            ) from e


def _clone_props(dataset: Any) -> dict[str, str]:
    """Collect the properties a clone must carry over from its source.

    Only locally-set and received properties are copied, so inherited
    and default values keep inheriting on the clone. Readonly
    properties are excluded up front: they cannot go in a create
    property list, and two of them (filesystem_count, snapshot_count)
    really do turn up with a local source once a filesystem or
    snapshot limit has ever activated count tracking. canmount is
    forced to noauto so an inactive boot environment can never
    auto-mount over the running one. zectl's clone_prop_cb made both
    of these same choices.
    """
    a = dataset.asdict(
        properties=(
            property_sets.ZFS_FILESYSTEM_PROPERTIES
            - property_sets.ZFS_FILESYSTEM_READONLY_PROPERTIES
        ),
        get_source=True,
    )
    props = {}
    for name, entry in a['properties'].items():
        if not entry:
            continue
        source = entry.get('source')
        if source is None or source['type'] not in _COPY_SOURCES:
            continue
        if name == 'canmount':
            continue
        raw = entry['raw']
        if name in ('quota', 'refquota') and raw == '0':
            # a quota locally set to none is stored as the value zero,
            # but libzfs refuses a numeric zero in a clone property
            # list and demands the string 'none'
            raw = 'none'
        props[name] = raw
    props['canmount'] = 'noauto'
    return props


def _cleanup_partial_create(
    lzh: libzfs_types.ZFS,
    cloned: list[tuple[str, str]],
    snapshots: set[str],
    snapshots_created: bool,
) -> None:
    """Best-effort removal of a half-built create().

    cloned holds the (dataset, origin) pairs this create() meant to make,
    recorded before each clone so one that failed after ZFS had already
    created the dataset is still undone. A dataset is only destroyed when
    its origin is the snapshot we cloned from, which is what separates our
    own half-built clone from a stranger's dataset that appeared under the
    target while we raced it: theirs has a different origin, or none, so it
    survives. Cleanup must never mask the original error, so every step is
    guarded individually. The clones go first because they pin the source
    snapshots, which are removed last in a finally so they are attempted
    even if the clone removal breaks.
    """
    try:
        for name, origin in reversed(cloned):
            try:
                if not _exists(lzh, name):
                    continue
                fs = lzh.open_resource(name=name)
                if _origin(fs) != origin:
                    continue        # not ours; leave it alone
                _lzc_destroy_snapshots({s.name for s in _snapshots(fs)})
                lzh.destroy_resource(name=name)
            except Exception:
                pass
    except Exception:
        pass
    finally:
        # nothing in here may escape: an exception raised from a finally
        # replaces the failure that brought us here, and the caller needs
        # to hear why create() actually failed, not how the cleanup went
        if snapshots_created:
            try:
                live = {s for s in snapshots if _exists(lzh, s)}
                try:
                    _lzc_destroy_snapshots(live)
                except Exception:
                    # one snapshot still pinned by a clone the walk above
                    # could not remove fails the whole batch, so reclaim
                    # the rest one at a time rather than leaking them all
                    for snapshot in live:
                        try:
                            _lzc_destroy_snapshots({snapshot})
                        except Exception:
                            pass
            except Exception:
                pass


@_translating_zfs_errors
def create(
    lzh: libzfs_types.ZFS,
    *,
    source_dataset: str,
    target_dataset: str,
    run_grub: bool = False,
) -> None:
    """Create a new boot environment as a clone of an existing one.

    Mirrors zectl's 'create -r -e <source> <target>': take one atomic
    recursive snapshot of the source subtree, clone every dataset onto
    the target preserving its locally-set and received native
    properties, and copy the source's truenas:kernel_version onto the
    new boot environment.
    Other user properties (zectl:keep and the like) are not copied,
    matching zectl's native-only property iteration.

    A source without truenas:kernel_version is refused because the
    menu generator skips kernel-less boot environments and the clone
    could never be booted.

    On any in-process failure the half-built target and the source
    snapshots are removed again (best effort) and the original error
    re-raised. After a hard crash mid-create, destroy the leftover
    target to retry; destroy reclaims the snapshots its clones pin.
    Snapshots from a crash before or midway through cloning have no
    clones pinning them and must be removed manually
    (zfs destroy <source>@<suffix>).
    """
    _validated(source_dataset)
    _validated(target_dataset)
    if source_dataset.split('/')[0] != target_dataset.split('/')[0]:
        raise BEError(
            f'{source_dataset!r} and {target_dataset!r} are on '
            f'different pools',
        )
    if not _exists(lzh, source_dataset):
        raise BENotFound(f'{source_dataset!r} not found')
    if _exists(lzh, target_dataset):
        raise BEExists(f'{target_dataset!r} already exists', errno.EEXIST)

    source_root = lzh.open_resource(name=source_dataset)
    kernel_version = source_root.get_user_properties().get(
        KERNEL_VERSION_PROP,
    )
    if kernel_version in (None, '', '-'):
        raise BEError(
            f'{source_dataset!r} has no truenas:kernel_version; refusing to '
            f'create a boot environment the boot menu would not show',
        )
    if set(kernel_version) - _KERNEL_VERSION_CHARS:
        raise BEError(
            f'{source_dataset!r} has a truenas:kernel_version with '
            f'characters the boot menu generator cannot embed safely: '
            f'{kernel_version!r}',
        )

    subtree = _walk_topdown(source_root, [])
    _refuse_volumes(subtree, source_dataset)

    suffix = naming.snapshot_suffix()
    while any(_exists(lzh, f'{fs.name}@{suffix}') for fs in subtree):
        suffix = naming.bump(suffix)
    snapshots = {f'{fs.name}@{suffix}' for fs in subtree}

    snapshots_created = False
    # (dataset, origin) pairs this call means to clone, recorded BEFORE the
    # clone so one that fails after ZFS already made the dataset is still
    # undone. The origin is what tells our own clone apart from a stranger's
    # dataset at the same name, so the rollback undoes exactly ours.
    cloned = []
    try:
        try:
            _lzc_create_snapshots(snapshots)
        except BEError as e:
            if e.errno == errno.EEXIST:
                # the names were taken between the bump loop above and
                # this call, so those snapshots are someone else's, not
                # ours to reclaim: leave snapshots_created False
                raise
            # lzc writes the pool history only after the snapshots exist,
            # so this can raise with them already made; they are ours
            snapshots_created = True
            raise
        except BaseException:
            # anything else the binding throws is also a possible
            # raised-after-creating (including the KeyboardInterrupt its
            # history-retry loop can raise), so treat the snapshots as ours
            snapshots_created = True
            raise
        snapshots_created = True
        for fs in subtree:
            target = naming.map_target(source_dataset, target_dataset, fs.name)
            origin = f'{fs.name}@{suffix}'
            cloned.append((target, origin))
            snap = lzh.open_resource(name=origin)
            snap.clone(name=target, properties=_clone_props(fs))
        lzh.open_resource(name=target_dataset).set_user_properties(
            user_properties={KERNEL_VERSION_PROP: kernel_version},
        )
    except BaseException:
        # BaseException, not Exception: the binding's history write runs
        # after the ZFS action commits and can raise KeyboardInterrupt, so
        # a Ctrl-C in the clone loop must still roll back. destroy()'s
        # teardown guards itself the same way. _cleanup_partial_create is
        # fully guarded internally, so it cannot mask what brought us here.
        _cleanup_partial_create(lzh, cloned, snapshots, snapshots_created)
        raise

    if run_grub:
        try:
            _update_grub(lzh, target_dataset.split('/')[0])
        except BEGrubError as e:
            raise BEGrubError(
                f'{target_dataset!r} was created successfully, but {e}; '
                f'run update-grub or activate it to refresh the menu',
            ) from e


def _promote_externals(
    lzh: libzfs_types.ZFS,
    subtree: list[Any],
    external_clones: Callable[[Any], list[str]],
) -> None:
    """Promote every clone outside the subtree, oldest first.

    Promotion moves snapshots between datasets, which can expose new
    external clones on other subtree members, so the scan repeats
    until a full pass finds nothing left to promote.
    """
    changed = True
    while changed:
        changed = False
        for fs in subtree:
            for snap in _snapshots(fs):
                ext = external_clones(snap)
                if not ext:
                    continue
                ext.sort(
                    key=lambda name: lzh.open_resource(name=name).createtxg,
                )
                lzh.open_resource(name=ext[0]).promote()
                changed = True


@_translating_zfs_errors
def destroy(
    lzh: libzfs_types.ZFS,
    *,
    dataset: str,
    running_ds: str | None,
    run_grub: bool = False,
) -> None:
    """Destroy a boot environment, preserving external dependents.

    Refuses the running boot environment (running_ds, as reported by
    statmount('/') on the caller's side) and the activated one (pool
    bootfs, re-read here so a concurrent activate cannot race the
    check). A boot environment with any mounted dataset is refused
    intact, so one an operator mounted to recover files is not
    partially destroyed by the updater's space-pruning. A boot
    environment that is already gone counts as success, so the
    updater's prune loop can retry after a crash.

    Before anything is destroyed, every dependent clone outside the
    subtree is promoted (oldest first, repeated to a fixpoint), so a
    sibling boot environment cloned from this one survives with the
    data it shares. A final re-scan refuses the destroy if any
    external clone somehow remains. zectl instead promoted only one
    dependent per level (and, by a comment/code mismatch, the newest);
    here every one is promoted. A snapshot still held after that
    promotion refuses the destroy while the subtree is whole, so a
    pinned snapshot leaves the boot environment intact for the operator
    to release the hold.

    The subtree is then destroyed leaf-first, and origin snapshots
    this boot environment was cloned from are removed from their
    source datasets when nothing else uses them (zectl's hardcoded
    destroy_origin behavior). truenas:kernel_version is cleared on
    the root before the walk, so a crash partway through leaves an
    inert remnant (never a bootable-looking half boot environment);
    re-running destroy removes it. Should the leaf-first walk fail
    before it destroys anything (leaving the subtree fully intact), the
    marker is restored, since the boot environment is still whole.
    """
    _validated_position(dataset)
    if dataset == running_ds:
        raise BEBusy(f'{dataset!r} is the running boot environment',
                     errno.EBUSY)
    if dataset == _pool_bootfs(lzh, dataset.split('/')[0]):
        raise BEBusy(f'{dataset!r} is the activated boot environment',
                     errno.EBUSY)

    if not _exists(lzh, dataset):
        if run_grub:
            try:
                _update_grub(lzh, dataset.split('/')[0])
            except BEGrubError as e:
                raise BEGrubError(
                    f'{dataset!r} was already removed, but {e}; '
                    f'run update-grub to refresh the menu',
                ) from e
        return

    subtree = _walk_topdown(lzh.open_resource(name=dataset), [])
    _refuse_volumes(subtree, dataset)
    _refuse_if_mounted(subtree, dataset)
    prefix = dataset + '/'

    # an origin on another member of this same subtree cannot be
    # promoted away, and the leaf-first walk would fail midway on it.
    # The TrueNAS layout never produces this shape, so refuse up front.
    for fs in subtree:
        origin = _origin(fs)
        if origin is not None and origin.startswith(prefix):
            raise BEDestroyUnsafe(
                f'{fs.name!r} is a clone of {origin!r} inside the same '
                f'boot environment; destroy the internal clones first',
                errno.EEXIST,
            )

    def external_clones(snap: Any) -> list[str]:
        return [
            c for c in snap.get_clones()
            if c != dataset and not c.startswith(prefix)
        ]

    _promote_externals(lzh, subtree, external_clones)

    for fs in subtree:
        for snap in _snapshots(fs):
            if external_clones(snap):
                raise BEDestroyUnsafe(
                    f'{snap.name!r} still has external clones after '
                    f'promotion; refusing to destroy', errno.EEXIST,
                )

    # after promotion has evacuated any shared history to the survivors,
    # a snapshot still held here would fail the leaf-first walk partway
    # and strand a half-destroyed remnant. Refuse now, while intact.
    _refuse_if_held(subtree, dataset)

    origins = set()
    for fs in subtree:
        # promotion just rewrote origins, so reopen before deciding
        # what to reclaim
        origin = _origin(lzh.open_resource(name=fs.name))
        if (origin is not None and not origin.startswith(prefix)
                and not origin.startswith(dataset + '@')):
            origins.add(origin)

    # cleared before anything is destroyed: a crash partway through
    # the leaf-first walk below must leave an inert remnant, not a
    # half boot environment that still advertises itself as bootable
    root = lzh.open_resource(name=dataset)
    kernel_version = root.get_user_properties().get(KERNEL_VERSION_PROP)

    # the clear itself is inside the guard: the binding writes the pool
    # history only after the property is committed, so it can raise with
    # the marker already on disk, and the restore below has to be able to
    # put it back rather than strand an intact boot environment as inert
    try:
        if kernel_version not in (None, '', '-'):
            root.set_user_properties(
                user_properties={KERNEL_VERSION_PROP: '-'},
            )
        for fs in reversed(subtree):
            _lzc_destroy_snapshots({s.name for s in _snapshots(fs)})
            lzh.destroy_resource(name=fs.name)
    except BaseException:
        # an unexpected failure that left the subtree fully intact
        # restores the marker: the boot environment is still whole, so it
        # must stay activatable. A partially destroyed subtree keeps the
        # marker cleared on purpose, because booting it would come up
        # missing member datasets. The restore is best effort and must
        # never mask the failure that brought us here.
        if kernel_version not in (None, '', '-'):
            try:
                if all(_exists(lzh, fs.name) for fs in subtree):
                    lzh.open_resource(name=dataset).set_user_properties(
                        user_properties={
                            KERNEL_VERSION_PROP: kernel_version,
                        },
                    )
            except Exception:
                pass
        raise

    for origin in origins:
        # the destroy already succeeded, so a failure here (say a user
        # hold on the origin) must not report it as failed. The
        # snapshot just stays until whatever pins it lets go.
        try:
            if not _exists(lzh, origin):
                continue
            if not lzh.open_resource(name=origin).get_clones():
                _lzc_destroy_snapshots({origin})
        except (BEError, truenas_pylibzfs.ZFSException):
            continue

    if run_grub:
        try:
            _update_grub(lzh, dataset.split('/')[0])
        except BEGrubError as e:
            raise BEGrubError(
                f'{dataset!r} was destroyed successfully, but {e}; '
                f'run update-grub to refresh the menu',
            ) from e


@_translating_zfs_errors
def set_keep(lzh: libzfs_types.ZFS, dataset: str, keep: bool) -> None:
    """Set the zectl:keep user property to 'True' or 'False'.

    The updater reads this exact spelling when deciding which boot
    environments it may prune for space.
    """
    lzh.open_resource(name=dataset).set_user_properties(
        user_properties={KEEP_PROP: 'True' if keep else 'False'},
    )


@_translating_zfs_errors
def promote_children(
    lzh: libzfs_types.ZFS, dataset: str,
) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    """Promote every clone among dataset's descendants.

    The installer clones the previous install's /var/log into a new
    boot environment, which pins the old one until the clone is
    promoted at boot. Returns (promoted, failures): promoted holds
    (child, origin) pairs, failures holds (child, error) pairs for the
    caller to log. A failed child does not stop the remaining ones.
    """
    promoted = []
    failures = []
    for fs in _walk_topdown(lzh.open_resource(name=dataset), [])[1:]:
        try:
            origin = _origin(fs)
            if origin is None:
                continue
            fs.promote()
            promoted.append((fs.name, origin))
        except Exception as e:
            # one stuck child must not stop the remaining promotions
            failures.append((fs.name, str(e)))
    return promoted, failures
