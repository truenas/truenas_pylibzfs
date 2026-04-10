#ifndef _PYZFS_KSTAT_H
#define _PYZFS_KSTAT_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

/*
 * Path to the ARC (Adaptive Replacement Cache) kstat file exposed by
 * the SPL (Solaris Porting Layer) kernel module.
 */
#define ARCSTATS_PATH "/proc/spl/kstat/zfs/arcstats"

/*
 * Number of data fields in ARCSTATS_PATH.
 *
 * Derived from arc_stats_t in include/sys/arc_impl.h in the ZFS source tree,
 * excluding the COMPAT_FREEBSD11-only "other_size" field which is not present
 * on Linux.
 *
 * This constant must stay in sync with ARCSTATS_PATH. The test
 * tests/test_kstat_arcstats.py enforces that invariant in CI.
 */
#define ARCSTATS_N_FIELDS 147

typedef struct {
    PyTypeObject *arcstats_type;
} pyzfs_kstat_state_t;

extern PyObject *py_get_arcstats(PyObject *module, PyObject *args);

#endif /* _PYZFS_KSTAT_H */
