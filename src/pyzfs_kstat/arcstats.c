#include "pyzfs_kstat.h"

#include <stdio.h>

/*
 * get_arcstats() implementation.
 *
 * Reads ARCSTATS_PATH and populates an ArcStats struct sequence whose type
 * was created at module init by init_arcstats_type() in pyzfs_kstat.c.
 *
 * File format (from module/zfs/arc.c in the ZFS source tree):
 *   Line 1: metadata header -- skip
 *   Line 2: column names "name  type  data" -- skip
 *   Line 3+: "<name>  <type>  <value>"
 *     type 3 = KSTAT_DATA_INT64  (memory_available_bytes only, signed)
 *     type 4 = KSTAT_DATA_UINT64 (all other fields, unsigned)
 */
PyObject *
py_get_arcstats(PyObject *module, PyObject *args)
{
    pyzfs_kstat_state_t *state = NULL;
    FILE *fp = NULL;
    PyObject *result = NULL;
    PyObject *val = NULL;
    char line[256];
    char name[64];
    char valstr[24];
    char *endptr = NULL;
    int nscanned = 0;
    int idx = 0;

    PySys_Audit("truenas_pylibzfs.kstat.get_arcstats", NULL);

    state = (pyzfs_kstat_state_t *)PyModule_GetState(module);

    Py_BEGIN_ALLOW_THREADS
    fp = fopen(ARCSTATS_PATH, "r");
    Py_END_ALLOW_THREADS

    if (fp == NULL) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, ARCSTATS_PATH);
        return NULL;
    }

    /* Skip the two header lines. */
    if (fgets(line, sizeof(line), fp) == NULL ||
        fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        PyErr_SetString(PyExc_ValueError,
            "Unexpected format in " ARCSTATS_PATH
            ": missing header lines");
        return NULL;
    }

    result = PyStructSequence_New(state->arcstats_type);
    if (result == NULL) {
        fclose(fp);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        /*
         * %*u discards the type column (KSTAT_DATA_INT64 = 3,
         * KSTAT_DATA_UINT64 = 4). We do not need it: PyLong_FromString
         * handles signed, unsigned, and arbitrary-precision values
         * without requiring us to know the underlying C type.
         */
        nscanned = sscanf(line, "%63s %*u %23s", name, valstr);
        if (nscanned != 2)
            continue;

        if (idx >= ARCSTATS_N_FIELDS) {
            fclose(fp);
            PyErr_Format(PyExc_ValueError,
                ARCSTATS_PATH " has more than %d data fields. "
                "Update arcstats.c and stubs to match the installed "
                "ZFS version.",
                ARCSTATS_N_FIELDS);
            Py_DECREF(result);
            return NULL;
        }

        val = PyLong_FromString(valstr, &endptr, 10);
        if (val == NULL) {
            fclose(fp);
            Py_DECREF(result);
            return NULL;
        }
        if (*endptr != '\0') {
            fclose(fp);
            Py_DECREF(val);
            PyErr_Format(PyExc_ValueError,
                ARCSTATS_PATH " field %d (%s): "
                "trailing garbage in value \"%s\"",
                idx, name, valstr);
            Py_DECREF(result);
            return NULL;
        }

        PyStructSequence_SetItem(result, idx, val); /* steals ref */
        idx++;
    }

    fclose(fp);

    if (idx != ARCSTATS_N_FIELDS) {
        PyErr_Format(PyExc_ValueError,
            ARCSTATS_PATH " has %d data fields; expected %d. "
            "Update arcstats.c and stubs to match the installed "
            "ZFS version.",
            idx, ARCSTATS_N_FIELDS);
        Py_DECREF(result);
        return NULL;
    }

    return result;
}
