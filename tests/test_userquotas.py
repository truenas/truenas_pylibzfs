"""
Tests for ZFSDataset.iter_userspace() and ZFSDataset.set_userquotas().

Covers:
  - iter_userspace returns empty on a fresh filesystem
  - set_userquotas sets a USER_QUOTA for uid 0, verifiable via iter_userspace
  - set_userquotas quota=0 removes the quota
  - set_userquotas GROUP_QUOTA type
  - Missing quotas arg raises ValueError
  - keyword-only enforcement for both methods
"""

import pytest
import truenas_pylibzfs

POOL_NAME = 'testpool_quota'


@pytest.fixture
def pool(make_pool):
    return make_pool(POOL_NAME)


@pytest.fixture
def dataset(pool):
    lz, _, root = pool
    ds_name = f'{POOL_NAME}/quotads'
    lz.create_resource(name=ds_name, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)
    ds = lz.open_resource(name=ds_name)
    ds.mount()
    try:
        yield lz, ds
    finally:
        try:
            ds.unmount()
        except Exception:
            pass
        try:
            lz.destroy_resource(name=ds_name)
        except Exception:
            pass


def _collect_userspace(ds, quota_type):
    entries = []

    def cb(quota, state):
        state.append({'xid': quota.xid, 'value': quota.value})
        return True

    ds.iter_userspace(callback=cb, state=entries, quota_type=quota_type)
    return entries


# ---------------------------------------------------------------------------
# iter_userspace
# ---------------------------------------------------------------------------

def test_iter_userspace_empty_by_default(dataset):
    lz, ds = dataset
    entries = _collect_userspace(ds, truenas_pylibzfs.ZFSUserQuota.USER_QUOTA)
    assert entries == []


def test_iter_userspace_keyword_only(dataset):
    lz, ds = dataset

    def cb(q, s):
        return True

    with pytest.raises(TypeError):
        ds.iter_userspace(cb, None, truenas_pylibzfs.ZFSUserQuota.USER_QUOTA)


def test_iter_userspace_missing_quotatype_raises(dataset):
    lz, ds = dataset

    def cb(q, s):
        return True

    with pytest.raises((TypeError, ValueError)):
        ds.iter_userspace(callback=cb, state=None)


# ---------------------------------------------------------------------------
# set_userquotas
# ---------------------------------------------------------------------------

def test_set_user_quota_basic(dataset):
    lz, ds = dataset
    quota_size = 100 * 1024 * 1024  # 100 MiB

    ds.set_userquotas(quotas=[{
        'quota_type': truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
        'xid': 0,
        'value': quota_size,
    }])

    entries = _collect_userspace(ds, truenas_pylibzfs.ZFSUserQuota.USER_QUOTA)
    uid0 = [e for e in entries if e['xid'] == 0]
    assert len(uid0) == 1
    assert uid0[0]['value'] == quota_size


def test_set_user_quota_zero_removes(dataset):
    lz, ds = dataset
    quota_size = 50 * 1024 * 1024  # 50 MiB

    ds.set_userquotas(quotas=[{
        'quota_type': truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
        'xid': 0,
        'value': quota_size,
    }])

    ds.set_userquotas(quotas=[{
        'quota_type': truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
        'xid': 0,
        'value': 0,
    }])

    entries = _collect_userspace(ds, truenas_pylibzfs.ZFSUserQuota.USER_QUOTA)
    assert [e for e in entries if e['xid'] == 0] == []


def test_set_group_quota(dataset):
    lz, ds = dataset
    quota_size = 200 * 1024 * 1024  # 200 MiB

    ds.set_userquotas(quotas=[{
        'quota_type': truenas_pylibzfs.ZFSUserQuota.GROUP_QUOTA,
        'xid': 0,
        'value': quota_size,
    }])

    entries = _collect_userspace(ds, truenas_pylibzfs.ZFSUserQuota.GROUP_QUOTA)
    gid0 = [e for e in entries if e['xid'] == 0]
    assert len(gid0) == 1
    assert gid0[0]['value'] == quota_size


def test_set_userquotas_missing_arg_raises(dataset):
    lz, ds = dataset
    with pytest.raises((TypeError, ValueError)):
        ds.set_userquotas()


def test_set_userquotas_keyword_only(dataset):
    lz, ds = dataset
    with pytest.raises(TypeError):
        ds.set_userquotas([{
            'quota_type': truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
            'xid': 0,
            'value': 1024,
        }])


def test_set_userquotas_invalid_quota_type_raises(dataset):
    lz, ds = dataset
    with pytest.raises((TypeError, ValueError)):
        ds.set_userquotas(quotas=[{
            'quota_type': 'not_a_quota_type',
            'xid': 0,
            'value': 1024,
        }])


def test_set_userquotas_used_type_raises(dataset):
    """Setting a _USED counter (read-only) must raise."""
    lz, ds = dataset
    with pytest.raises((TypeError, ValueError)):
        ds.set_userquotas(quotas=[{
            'quota_type': truenas_pylibzfs.ZFSUserQuota.USER_USED,
            'xid': 0,
            'value': 1024,
        }])
