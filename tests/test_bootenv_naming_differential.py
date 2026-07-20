"""Differential test: naming.validate_component vs the real ZFS validator.

Compares our pure-Python name rules against zfs_name_valid (reached via
truenas_pylibzfs.name_is_valid) to guard against drift from the actual
rules in module/zcommon/zfs_namecheck.c. Unlike the other bootenv naming
tests this one imports the binding, but it needs no root and no pool:
name validation is pure string checking in libzfs.
"""

import pytest
import truenas_pylibzfs
from truenas_bootenv import naming

FST = truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM


def _zfs_accepts(component):
    return truenas_pylibzfs.name_is_valid(
        name=f"testpool/ROOT/{component}", type=FST
    )


AGREE_CASES = [
    # valid on both sides
    "a" * 240,            # long but within the limit for any short prefix
    "25.10.0",
    "25.10.0-1",
    "my_be",
    "a:b",
    "UPPER.case-1",
    # invalid on both sides
    "a" * 300,            # exceeds the dataset name limit
    "trailing ",
    "double  trail ",
    "a@b",
    "a#b",
    "a!b",
    "a+b",
    "tab\tname",
    "\xfcn\xefcode",      # 'unicode' with u-umlaut / i-diaeresis
]


@pytest.mark.parametrize("name", AGREE_CASES)
def test_agrees_with_zfs_name_valid(name):
    ours = naming.validate_component(name) is None
    assert ours == _zfs_accepts(name), (
        f"{name!r}: ours={ours}, zfs_name_valid={not ours} - "
        f"naming.py has drifted from zfs_namecheck.c"
    )


def test_generated_suffixes_accepted_by_real_zfs():
    s = naming.snapshot_suffix()
    for _ in range(3):
        assert truenas_pylibzfs.name_is_valid(
            name=f"testpool/ROOT/be@{s}",
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_SNAPSHOT,
        ), s
        s = naming.bump(s)


def test_space_divergence_is_intentional():
    """ZFS accepts interior spaces in components, but the sh-based grub
    menu generator (10_truenas_linux) breaks on unquoted space-bearing
    names, collapsing the boot menu. BE names therefore reject spaces
    entirely; see naming.py.
    """
    assert naming.validate_component("with space") is not None  # we reject
    assert _zfs_accepts("with space")                           # ZFS accepts


def test_percent_divergence_is_intentional():
    """ZFS's entity_namecheck() accepts '%' but reserves it for internal
    temporary clone names during online recv (zfs_namecheck.c:177). We
    are deliberately stricter: user BE names must never contain it. If
    ZFS ever stops accepting '%', this test documents that our rejection
    then simply agrees with ZFS and the divergence note in naming.py can
    be dropped.
    """
    assert naming.validate_component("a%b") is not None   # we reject
    assert _zfs_accepts("a%b")                            # ZFS (today) accepts
