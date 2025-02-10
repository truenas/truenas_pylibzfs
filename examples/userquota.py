# This code snippet provides an example of querying
# and setting a ZFS userquota

import truenas_pylibzfs


def userspace_callback(qt, state):
    state.append(qt)
    return True


q1 = {
    "quota_type": truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
    "xid": 1000,
    "value": 8675309
}

q2 = {
    "quota_type": truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
    "xid": 1001,
    "value": 8675309
}

lz = truenas_pylibzfs.open_handle()

rsrc = lz.open_resource(name='dozer')
rsrc.set_userquotas(quotas=[q1])

quotas = []
rsrc.iter_userspace(
    quota_type=truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
    callback=userspace_callback,
    state=quotas
)

assert len(quotas) == 1
assert quotas[0].quota_type == truenas_pylibzfs.ZFSUserQuota.USER_QUOTA
assert quotas[0].xid == 1000
assert quotas[0].value == 8675309

# remove quota
rsrc.set_userquotas(quotas=[{
    "quota_type": truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
    "xid": 1000,
    "value": None
}])

quotas = []
rsrc.iter_userspace(
    quota_type=truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
    callback=userspace_callback,
    state=quotas
)

assert len(quotas) == 0

# Add multiple quotas back
rsrc.set_userquotas(quotas=(q1, q2))

quotas = []
rsrc.iter_userspace(
    quota_type=truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
    callback=userspace_callback,
    state=quotas
)
assert len(quotas) == 2

# now remove multiple
rsrc.set_userquotas(quotas=(
    {"quota_type": q1["quota_type"], "xid": q1["xid"], "value": None},
    {"quota_type": q2["quota_type"], "xid": q2["xid"], "value": None},
))

quotas = []
rsrc.iter_userspace(
    quota_type=truenas_pylibzfs.ZFSUserQuota.USER_QUOTA,
    callback=userspace_callback,
    state=quotas
)
assert len(quotas) == 0
