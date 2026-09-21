"""
Tests for ZFSBookmark, ZFSDataset.iter_bookmarks(), and opening a bookmark
by name.

Covers:
  - iter_bookmarks collects bookmarks, with 0, 1 and N of them
  - iter_bookmarks callback returning False stops iteration
  - iter_bookmarks state object passed through to callback
  - iter_bookmarks is not inherited by ZFSSnapshot
  - iter_bookmarks raises on an ioctl failure rather than returning False
  - open_resource returns a ZFSBookmark with the expected attributes
  - open_resource on a missing bookmark raises EZFS_NOENT, not EZFS_INVALIDNAME
  - get_properties over ZFS_BOOKMARK_PROPERTIES does not raise and reports
    absent values as None, while a property that is not bookmark-valid
    still raises ValueError
  - rename raises TypeError and does not fall through to ZFSObject.rename
  - destroy removes the bookmark, and destroying it twice succeeds
  - encrypted reflects the encryption state of the bookmarked dataset
  - the ZFSResource surface that is unsafe on a bookmark is absent

Bookmarks are created by shelling out to zfs(8): this binding has no
create-bookmark call yet.
"""

import contextlib
import subprocess

import pytest
import truenas_pylibzfs
from truenas_pylibzfs import lzc

ZFS_BOOKMARK_PROPERTIES = truenas_pylibzfs.property_sets.ZFS_BOOKMARK_PROPERTIES

POOL_NAME = 'testpool_bookmark'
PASSPHRASE = 'Cats1234'


def _make_bookmark(snap_name, bookmark_name):
    subprocess.run(
        ['zfs', 'bookmark', snap_name, bookmark_name],
        check=True, capture_output=True,
    )


def _list_bookmarks(resource_name):
    proc = subprocess.run(
        ['zfs', 'list', '-H', '-o', 'name', '-t', 'bookmark', '-r',
         resource_name],
        check=True, capture_output=True, text=True,
    )
    return [line for line in proc.stdout.splitlines() if line]


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


@pytest.fixture
def pool_with_bookmarks(pool):
    """Pool root with 2 snapshots, each with a bookmark."""
    lz, p, root = pool
    snaps = [f'{POOL_NAME}@snap1', f'{POOL_NAME}@snap2']
    bookmarks = [f'{POOL_NAME}#bm1', f'{POOL_NAME}#bm2']
    for snap, bookmark in zip(snaps, bookmarks):
        lzc.create_snapshots(snapshot_names=[snap])
        _make_bookmark(snap, bookmark)

    try:
        yield lz, p, root
    finally:
        for snap in reversed(snaps):
            try:
                lzc.destroy_snapshots(snapshot_names=[snap])
            except Exception:
                pass


# ---------------------------------------------------------------------------
# iter_bookmarks
# ---------------------------------------------------------------------------

class TestIterBookmarks:
    def test_collects_bookmarks(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        seen = []

        def cb(bookmark, state):
            state.append(bookmark.name)
            return True

        result = root.iter_bookmarks(callback=cb, state=seen)
        assert result is True
        assert f'{POOL_NAME}#bm1' in seen
        assert f'{POOL_NAME}#bm2' in seen

    def test_yields_bookmark_objects(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        seen = []

        def cb(bookmark, state):
            state.append(bookmark)
            return True

        root.iter_bookmarks(callback=cb, state=seen)
        for bookmark in seen:
            assert isinstance(bookmark, truenas_pylibzfs.libzfs_types.ZFSBookmark)
            assert bookmark.type == truenas_pylibzfs.ZFSType.ZFS_TYPE_BOOKMARK
            assert bookmark.pool_name == POOL_NAME

    def test_callback_stop(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        seen = []

        def cb(bookmark, state):
            state.append(bookmark.name)
            return False

        result = root.iter_bookmarks(callback=cb, state=seen)
        assert result is False
        assert len(seen) == 1

    def test_state_passed_to_callback(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        collector = []

        def cb(bookmark, state):
            state.append(bookmark.name)
            return True

        root.iter_bookmarks(callback=cb, state=collector)
        assert len(collector) == 2

    def test_no_bookmarks_returns_true(self, pool):
        lz, p, root = pool
        seen = []

        def cb(bookmark, state):
            state.append(bookmark.name)
            return True

        result = root.iter_bookmarks(callback=cb, state=seen)
        assert result is True
        assert seen == []

    def test_callback_required(self, pool):
        lz, p, root = pool
        with pytest.raises(ValueError):
            root.iter_bookmarks(state=None)

    def test_callback_must_be_callable(self, pool):
        lz, p, root = pool
        with pytest.raises(TypeError):
            root.iter_bookmarks(callback='not callable', state=None)

    def test_keyword_only(self, pool):
        lz, p, root = pool

        def cb(bookmark, state):
            return True

        with pytest.raises(TypeError):
            root.iter_bookmarks(cb, None)

    def test_exception_in_callback_propagates(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks

        def cb(bookmark, state):
            raise RuntimeError('from callback')

        with pytest.raises(RuntimeError, match='from callback'):
            root.iter_bookmarks(callback=cb, state=None)

    def test_ioctl_error_raises_rather_than_returning_false(self, pool):
        """
        Regression test for the zfs_iter_bookmarks_v2() error contract.

        That iterator reports failure as a raw positive errno and never
        populates the libzfs error state, unlike every other libzfs iterator,
        which returns -1. Without the translation in py_iter_bookmarks() the
        positive value is neither ITER_RESULT_IOCTL_ERROR nor
        ITER_RESULT_STOP, so a genuine EPERM or ENOENT is reported to python
        as a benign False with no exception set.

        Destroying the dataset out from under an open handle drives
        lzc_get_bookmarks() to ENOENT. The call must raise, not return False.
        """
        lz, p, root = pool
        ds_name = f'{POOL_NAME}/doomed'
        lz.create_resource(
            name=ds_name,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )
        ds = lz.open_resource(name=ds_name)

        # Destroy it behind the handle's back, so the handle stays valid but
        # the ioctl fails.
        subprocess.run(['zfs', 'destroy', '-r', ds_name],
                       check=True, capture_output=True)

        def cb(bookmark, state):
            return True

        with pytest.raises(truenas_pylibzfs.ZFSException) as exc:
            ds.iter_bookmarks(callback=cb, state=None)

        assert exc.value.code == truenas_pylibzfs.ZFSError.EZFS_NOENT

    def test_not_inherited_by_snapshot(self, pool_with_bookmarks):
        """
        zfs_iter_bookmarks_v2() returns success without iterating anything
        when handed a snapshot handle, so an inherited iter_bookmarks would
        answer "no bookmarks" instead of "wrong type". It must not exist on
        ZFSSnapshot at all.
        """
        lz, p, root = pool_with_bookmarks
        snap = lz.open_resource(name=f'{POOL_NAME}@snap1')
        assert not hasattr(snap, 'iter_bookmarks')
        assert not hasattr(truenas_pylibzfs.libzfs_types.ZFSResource,
                           'iter_bookmarks')


# ---------------------------------------------------------------------------
# open_resource
# ---------------------------------------------------------------------------

class TestOpenBookmark:
    def test_open_by_name(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        assert isinstance(bookmark, truenas_pylibzfs.libzfs_types.ZFSBookmark)
        assert bookmark.name == f'{POOL_NAME}#bm1'
        assert bookmark.pool_name == POOL_NAME
        assert bookmark.type == truenas_pylibzfs.ZFSType.ZFS_TYPE_BOOKMARK
        assert bookmark.guid > 0
        assert bookmark.createtxg > 0

    def test_guid_matches_snapshot(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        snap = lz.open_resource(name=f'{POOL_NAME}@snap1')
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        assert bookmark.guid == snap.guid
        assert bookmark.createtxg == snap.createtxg

    def test_repr(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        assert 'ZFSBookmark' in repr(bookmark)
        assert f'{POOL_NAME}#bm1' in repr(bookmark)

    def test_missing_bookmark_is_noent(self, pool_with_bookmarks):
        """
        Widening SUPPORTED_RESOURCES changes the failure mode for any name
        containing a '#'. It used to fail in zfs_validate_name() with
        EZFS_INVALIDNAME; it now reaches the bookmark lookup and returns
        EZFS_NOENT.
        """
        lz, p, root = pool_with_bookmarks
        with pytest.raises(truenas_pylibzfs.ZFSException) as exc:
            lz.open_resource(name=f'{POOL_NAME}#nosuchbookmark')

        assert exc.value.code == truenas_pylibzfs.ZFSError.EZFS_NOENT

    def test_bookmark_of_missing_dataset(self, pool):
        lz, p, root = pool
        with pytest.raises(truenas_pylibzfs.ZFSException):
            lz.open_resource(name=f'{POOL_NAME}/nosuchds#bm')


# ---------------------------------------------------------------------------
# get_properties
# ---------------------------------------------------------------------------

class TestBookmarkProperties:
    def test_property_set_is_not_empty(self):
        assert len(ZFS_BOOKMARK_PROPERTIES) > 0
        assert truenas_pylibzfs.ZFSProperty.GUID in ZFS_BOOKMARK_PROPERTIES

    def test_full_property_set_does_not_raise(self, pool_with_bookmarks):
        """
        Regression test: compressratio is registered valid for a bookmark but
        is never populated by the kernel, so zfs_prop_get() reports it as -1
        without setting errno. That used to surface as a spurious
        RuntimeError.
        """
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        props = bookmark.get_properties(properties=ZFS_BOOKMARK_PROPERTIES)
        assert props.compressratio.value is None
        assert props.compressratio.raw == 'none'

    def test_populated_properties(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        props = bookmark.get_properties(properties={
            truenas_pylibzfs.ZFSProperty.GUID,
            truenas_pylibzfs.ZFSProperty.CREATETXG,
        })
        assert props.guid.value == bookmark.guid
        assert props.createtxg.value == bookmark.createtxg

    def test_properties_keyword_required(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        with pytest.raises(ValueError):
            bookmark.get_properties()

    def test_properties_must_be_a_set(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        with pytest.raises(TypeError):
            bookmark.get_properties(properties=['guid'])

    @pytest.mark.parametrize('prop', [
        'MOUNTPOINT',
        'COMPRESSION',
        'VOLSIZE',
    ])
    def test_invalid_property_for_bookmark_raises(self, pool_with_bookmarks,
                                                  prop):
        """
        A property that is not valid for ZFS_TYPE_BOOKMARK must still raise
        ValueError. zfs_prop_get() returns -1 both for a bookmark property
        the kernel did not populate and for a property that does not apply to
        the handle type at all, so the mapping of absent values to None must
        not swallow the second case.
        """
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        with pytest.raises(ValueError, match='invalid for zfs type'):
            bookmark.get_properties(properties={
                getattr(truenas_pylibzfs.ZFSProperty, prop),
            })


# ---------------------------------------------------------------------------
# rename, destroy, and the absent ZFSResource surface
# ---------------------------------------------------------------------------

class TestBookmarkOperations:
    def test_rename_raises(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        with pytest.raises(TypeError):
            bookmark.rename(new_name=f'{POOL_NAME}#renamed')

        assert f'{POOL_NAME}#bm1' in _list_bookmarks(POOL_NAME)

    def test_rename_override_shadows_base(self):
        """
        The override is what keeps zfs_rename() away from a '#' name, so pin
        the shadowing itself rather than only its observable effect.
        """
        types = truenas_pylibzfs.libzfs_types
        assert types.ZFSBookmark.rename is not types.ZFSObject.rename

    def test_destroy(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        bookmark.destroy()
        assert f'{POOL_NAME}#bm1' not in _list_bookmarks(POOL_NAME)
        assert f'{POOL_NAME}#bm2' in _list_bookmarks(POOL_NAME)

    def test_destroy_twice_succeeds(self, pool_with_bookmarks):
        """
        lzc_destroy_bookmarks() ignores bookmarks that do not exist, and the
        handle is not invalidated by a successful destroy. Assert the
        behaviour so it is not quietly turned into an exception later.
        """
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        bookmark.destroy()
        bookmark.destroy()

    def test_destroyed_bookmark_cannot_be_reopened(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        bookmark.destroy()
        with pytest.raises(truenas_pylibzfs.ZFSException) as exc:
            lz.open_resource(name=f'{POOL_NAME}#bm1')

        assert exc.value.code == truenas_pylibzfs.ZFSError.EZFS_NOENT

    def test_resource_surface_absent(self, pool_with_bookmarks):
        """
        ZFSBookmark derives from ZFSObject, not ZFSResource: every method
        below would either be meaningless on a bookmark or would drive a
        dataset ioctl against a '#' name.
        """
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        for name in (
            'mount',
            'unmount',
            'get_mountpoint',
            'set_properties',
            'get_user_properties',
            'set_user_properties',
            'refresh_properties',
            'inherit_property',
            'asdict',
            'open_pool',
            'iter_filesystems',
            'iter_snapshots',
            'iter_bookmarks',
        ):
            assert not hasattr(bookmark, name), name

    def test_not_constructible(self):
        with pytest.raises(TypeError):
            truenas_pylibzfs.libzfs_types.ZFSBookmark()


# ---------------------------------------------------------------------------
# encrypted
# ---------------------------------------------------------------------------

class TestBookmarkEncrypted:
    def test_unencrypted(self, pool_with_bookmarks):
        lz, p, root = pool_with_bookmarks
        bookmark = lz.open_resource(name=f'{POOL_NAME}#bm1')
        assert bookmark.encrypted is False

    def test_encrypted(self, pool):
        """
        zfs_is_encrypted() would answer False here, because a bookmark handle
        carries no dmu stats. The value is derived from the IVset GUID
        instead, which ZFS records only for bookmarks of encrypted datasets.
        """
        lz, p, root = pool
        ds_name = f'{POOL_NAME}/enc'
        crypto = lz.resource_cryptography_config(
            keyformat='passphrase', key=PASSPHRASE
        )
        lz.create_resource(
            name=ds_name,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
            crypto=crypto,
        )
        try:
            lzc.create_snapshots(snapshot_names=[f'{ds_name}@snap1'])
            _make_bookmark(f'{ds_name}@snap1', f'{ds_name}#bm1')
            bookmark = lz.open_resource(name=f'{ds_name}#bm1')
            assert bookmark.encrypted is True
            bookmark.destroy()
        finally:
            with contextlib.suppress(Exception):
                lzc.destroy_snapshots(snapshot_names=[f'{ds_name}@snap1'])
            lz.destroy_resource(name=ds_name)
