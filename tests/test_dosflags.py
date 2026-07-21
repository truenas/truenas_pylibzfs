"""ZFSDOSFlag enum values, checked against the DOS attribute bits in
sys/fs/zfs.h."""
import truenas_pylibzfs

ZFSDOSFlag = truenas_pylibzfs.libzfs_types.ZFSDOSFlag

# Ground-truth bit values from sys/fs/zfs.h
EXPECTED = {
    "ZFS_READONLY": 0x0000000100000000,
    "ZFS_HIDDEN": 0x0000000200000000,
    "ZFS_SYSTEM": 0x0000000400000000,
    "ZFS_ARCHIVE": 0x0000000800000000,
    "ZFS_IMMUTABLE": 0x0000001000000000,
    "ZFS_NOUNLINK": 0x0000002000000000,
    "ZFS_APPENDONLY": 0x0000004000000000,
    "ZFS_NODUMP": 0x0000008000000000,
    "ZFS_OFFLINE": 0x0000100000000000,
    "ZFS_SPARSE": 0x0000200000000000,
}


def test_dosflag_values_match_zfs_bits():
    for name, bit in EXPECTED.items():
        member = getattr(ZFSDOSFlag, name)
        assert member.value == bit, f"{name} should be {bit:#x}, got {member.value:#x}"


def test_offline_is_distinct_from_sparse():
    assert ZFSDOSFlag.ZFS_OFFLINE.value != ZFSDOSFlag.ZFS_SPARSE.value
    assert ZFSDOSFlag.ZFS_OFFLINE.value == 0x0000100000000000
    assert ZFSDOSFlag.ZFS_SPARSE.value == 0x0000200000000000


def test_offline_member_exists():
    assert "ZFS_OFFLINE" in ZFSDOSFlag.__members__
