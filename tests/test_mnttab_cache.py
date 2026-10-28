"""
Tests for the libzfs mnttab cache that open_handle(mnttab_cache=True) uses.

The cache only lives for the outermost iterator, or the outermost call that
walks child datasets (rename, recursive unmount, property changes that
remount children). These tests check that mount state is never stale from
one call to the next, that mounts made through the same handle are seen
inside a call, and that the cache is dropped on every way out of a call.

Changes made by another process use the zfs(8) and mount(8) commands.
"""

import subprocess
import threading
import time

import pytest
import truenas_pylibzfs

ZFSProperty = truenas_pylibzfs.ZFSProperty
PropertySource = truenas_pylibzfs.PropertySource
POOL_NAME = 'testpool_mnttab'
NCHILD = 8


def run(*args):
    subprocess.run(args, check=True, capture_output=True)


def kernel_mounts():
    """Return {dataset: mountpoint} for the ZFS mounts in /proc/self/mounts."""
    out = {}
    with open('/proc/self/mounts') as f:
        for line in f:
            dev, mp, fstype = line.split()[:3]
            if fstype == 'zfs':
                out[dev] = mp
    return out


def query(lz, parent, during=None):
    """One iteration over the children of parent: {name: (mounted, mp)}."""
    seen = {}

    def cb(hdl, state):
        if during is not None:
            during(hdl)
        props = hdl.asdict(properties={ZFSProperty.MOUNTED})['properties']
        seen[hdl.name] = (props['mounted']['value'], hdl.get_mountpoint())
        return True

    lz.open_resource(name=parent).iter_filesystems(callback=cb, state=None)
    return seen


@pytest.fixture
def tree(make_pool):
    """A mounted parent dataset with NCHILD mounted children."""
    lz, _, root = make_pool(POOL_NAME)
    root.mount()
    parent = f'{POOL_NAME}/parent'
    children = [f'{parent}/c{i}' for i in range(NCHILD)]
    for name in [parent] + children:
        lz.create_resource(
            name=name, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM
        )
        lz.open_resource(name=name).mount()
    return lz, parent, children


def test_external_unmount_seen_by_next_call(tree):
    lz, parent, children = tree
    target = children[3]
    assert all(mounted for mounted, _ in query(lz, parent).values())

    run('zfs', 'unmount', target)
    state = query(lz, parent)
    assert state[target] == (False, None)
    assert all(state[c][0] for c in children if c != target)

    run('zfs', 'mount', target)
    assert query(lz, parent)[target] == (True, kernel_mounts()[target])


def test_external_remount_reported_as_temporary(tree):
    lz, parent, children = tree
    target = children[1]
    mp = kernel_mounts()[target]

    def readonly():
        out = {}

        def cb(hdl, state):
            if hdl.name == target:
                prop = hdl.asdict(
                    properties={ZFSProperty.READONLY}, get_source=True
                )['properties']['readonly']
                out['ro'] = (prop['value'], prop['source']['type'])
            return True

        lz.open_resource(name=parent).iter_filesystems(callback=cb, state=None)
        return out['ro']

    # -i skips the mount.zfs helper, as another process would remount
    run('mount', '-i', '-o', 'remount,ro', mp)
    try:
        assert readonly() == ('on', PropertySource.TEMPORARY)
    finally:
        run('mount', '-i', '-o', 'remount,rw', mp)
    value, source = readonly()
    assert value == 'off' and source != PropertySource.TEMPORARY


def test_own_unmount_and_mount_seen_inside_call(tree):
    lz, parent, children = tree
    target = children[2]
    seen = {}

    def during(hdl):
        if hdl.name == target:
            hdl.unmount()
            seen['unmounted'] = hdl.get_mountpoint()
            hdl.mount()
            seen['mounted'] = hdl.get_mountpoint()

    state = query(lz, parent, during)
    assert seen['unmounted'] is None
    assert seen['mounted'] == kernel_mounts()[target]
    assert all(state[c][0] for c in children)


def test_external_change_seen_after_call(tree):
    lz, parent, children = tree
    target = children[-1]
    changed = []

    def during(hdl):
        # look something up first so that the table is cached, then change
        # the mount table behind the cache's back
        if not changed:
            hdl.get_mountpoint()
            run('zfs', 'unmount', target)
            changed.append(True)

    query(lz, parent, during)
    assert lz.open_resource(name=target).get_mountpoint() is None
    assert query(lz, parent)[target] == (False, None)


def test_nested_iterators_drop_cache(tree):
    lz, parent, children = tree
    target = children[4]

    def walk(hdl, state):
        if hdl.name.split('/')[0] != POOL_NAME:
            return True
        hdl.get_mountpoint()
        hdl.iter_filesystems(callback=walk, state=None)
        return True

    lz.iter_root_filesystems(callback=walk, state=None)
    run('zfs', 'unmount', target)
    assert lz.open_resource(name=target).get_mountpoint() is None


@pytest.mark.parametrize('how', ['raise', 'stop'])
def test_cache_dropped_when_callback_ends_early(tree, how):
    lz, parent, children = tree
    target = children[5]

    def cb(hdl, state):
        hdl.get_mountpoint()
        if how == 'raise':
            raise RuntimeError('callback failed')
        return False

    rsrc = lz.open_resource(name=parent)
    if how == 'raise':
        with pytest.raises(RuntimeError):
            rsrc.iter_filesystems(callback=cb, state=None)
    else:
        rsrc.iter_filesystems(callback=cb, state=None)

    run('zfs', 'unmount', target)
    assert lz.open_resource(name=target).get_mountpoint() is None
    assert query(lz, parent)[target] == (False, None)


def test_cache_disabled_reads_mount_table_every_time(tree):
    _, parent, children = tree
    lz = truenas_pylibzfs.open_handle(mnttab_cache=False)
    order = list(query(lz, parent))
    first, last = order[0], order[-1]

    def during(hdl):
        if hdl.name == first:
            hdl.get_mountpoint()
            run('zfs', 'unmount', last)

    assert query(lz, parent, during)[last] == (False, None)


def test_threads_share_handle_with_mount_churn(tree):
    lz, parent, children = tree
    target = children[7]
    stop = time.monotonic() + 8
    errors = []

    def worker():
        try:
            while time.monotonic() < stop:
                query(lz, parent)
                lz.open_resource(name=children[0]).get_mountpoint()
        except Exception as e:
            errors.append(e)

    def churn():
        while time.monotonic() < stop:
            subprocess.run(['zfs', 'unmount', target], capture_output=True)
            subprocess.run(['zfs', 'mount', target], capture_output=True)

    threads = [threading.Thread(target=worker) for _ in range(4)]
    threads.append(threading.Thread(target=churn))
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    subprocess.run(['zfs', 'mount', target], capture_output=True)

    assert not errors
    mounts = kernel_mounts()
    state = query(lz, parent)
    for c in children:
        assert state[c] == (True, mounts[c])


def test_calls_that_walk_children(tree):
    lz, parent, children = tree
    names = [c.rsplit('/', 1)[1] for c in children]

    def check_mounted_under(ds):
        mounts = kernel_mounts()
        for n in names:
            assert mounts[f'{ds}/{n}'] == f'{mounts[ds]}/{n}'

    renamed = f'{POOL_NAME}/renamed'
    lz.open_resource(name=parent).rename(new_name=renamed)
    check_mounted_under(renamed)

    lz.open_resource(name=renamed).unmount(recursive=True)
    mounts = kernel_mounts()
    assert not [ds for ds in mounts if ds.startswith(renamed)]

    def mount_cb(hdl, state):
        hdl.mount()
        return True

    rsrc = lz.open_resource(name=renamed)
    rsrc.mount()
    rsrc.iter_filesystems(callback=mount_cb, state=None)
    check_mounted_under(renamed)

    alt = f'{kernel_mounts()[POOL_NAME]}/alt'
    lz.open_resource(name=renamed).set_properties(
        properties={ZFSProperty.MOUNTPOINT: alt}
    )
    assert kernel_mounts()[renamed] == alt
    check_mounted_under(renamed)

    lz.open_resource(name=renamed).inherit_property(
        property=ZFSProperty.MOUNTPOINT
    )
    assert kernel_mounts()[renamed] == f'{kernel_mounts()[POOL_NAME]}/renamed'
    check_mounted_under(renamed)
