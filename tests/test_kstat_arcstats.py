"""
Verify that the hard-coded ArcStats field list stays in sync with
/proc/spl/kstat/zfs/arcstats.

This test must run on a host with the ZFS kernel module loaded and must be
built against the same ZFS source tree used to compile truenas_pylibzfs.
It is intended to run in the CI environment (GitHub Actions) where the build
VM has the target ZFS version installed.

If this test fails, the installed ZFS version has added, removed, or
reordered arcstats fields relative to the hard-coded list in
src/pyzfs_kstat/arcstats.c and stubs/. Update both files to match.
"""

import sys

from truenas_pylibzfs import kstat

ARCSTATS_PATH = "/proc/spl/kstat/zfs/arcstats"


def _read_proc_fields():
    """Return the ordered list of field names from ARCSTATS_PATH."""
    with open(ARCSTATS_PATH) as f:
        lines = f.readlines()
    # Line 0: metadata header; line 1: column header; lines 2+: data.
    return [line.split()[0] for line in lines[2:] if line.strip()]


def _arcstats_fields():
    """
    Return the ordered field names of the ArcStats struct sequence type.

    PyStructSequence types gained an automatic _fields tuple in Python 3.13.
    For earlier versions, extract field names from the member descriptors that
    PyStructSequence_NewType adds to the type dict.
    """
    t = type(kstat.get_arcstats())
    if hasattr(t, '_fields'):
        return list(t._fields)
    # Identify the member_descriptor C type via a known PyStructSequence
    # (sys.version_info), then collect all such descriptors from our type.
    _mdt = type(type(sys.version_info).__dict__['major'])
    return [k for k, v in t.__dict__.items() if isinstance(v, _mdt)]


def test_arcstats_fields_in_sync():
    """
    Field names and order in ArcStats must exactly match ARCSTATS_PATH.

    Checks both the set of names (catches additions and removals) and the
    order (catches reorderings that would silently mis-assign values).
    """
    proc_fields = _read_proc_fields()
    ext_fields = _arcstats_fields()

    assert ext_fields == proc_fields, (
        "ArcStats field list is out of sync with {}.\n"
        "  Only in {}: {}\n"
        "  Only in extension: {}\n"
        "  Update src/pyzfs_kstat/arcstats.c (ARCSTATS_N_FIELDS and "
        "arcstats_fields[]) and stubs/ to match.".format(
            ARCSTATS_PATH,
            ARCSTATS_PATH,
            sorted(set(proc_fields) - set(ext_fields)),
            sorted(set(ext_fields) - set(proc_fields)),
        )
    )


def test_arcstats_returns_ints():
    """All ArcStats field values must be integers."""
    stats = kstat.get_arcstats()
    for i, value in enumerate(stats):
        assert isinstance(value, int), (
            "ArcStats field {} expected int, got {}".format(i, type(value).__name__)
        )


def test_arcstats_field_count():
    """ArcStats field count must equal ARCSTATS_N_FIELDS (147)."""
    stats = kstat.get_arcstats()
    assert len(stats) == 147


def test_arcstats_successive_calls_are_independent():
    """Two successive get_arcstats() calls must return distinct objects."""
    a = kstat.get_arcstats()
    b = kstat.get_arcstats()
    assert a is not b
