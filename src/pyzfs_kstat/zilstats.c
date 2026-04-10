#include "pyzfs_kstat.h"

#include <stdio.h>

/*
 * get_zilstats() implementation.
 *
 * Reads ZILSTATS_PATH and populates a ZilStats struct sequence whose type
 * was created at module init by init_zilstats_type() in pyzfs_kstat.c.
 *
 * File format (from module/zfs/zil.c in the ZFS source tree):
 *   Line 1: metadata header -- skip
 *   Line 2: column names "name  type  data" -- skip
 *   Line 3+: "<name>  <type>  <value>"
 *     All ZIL fields are KSTAT_DATA_UINT64 (type 4).
 */
PyObject *
py_get_zilstats(PyObject *module, PyObject *args)
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

    PySys_Audit("truenas_pylibzfs.kstat.get_zilstats", NULL);

    state = (pyzfs_kstat_state_t *)PyModule_GetState(module);

    Py_BEGIN_ALLOW_THREADS
    fp = fopen(ZILSTATS_PATH, "r");
    Py_END_ALLOW_THREADS

    if (fp == NULL) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, ZILSTATS_PATH);
        return NULL;
    }

    /* Skip the two header lines. */
    if (fgets(line, sizeof(line), fp) == NULL ||
        fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        PyErr_SetString(PyExc_ValueError,
            "Unexpected format in " ZILSTATS_PATH
            ": missing header lines");
        return NULL;
    }

    result = PyStructSequence_New(state->zilstats_type);
    if (result == NULL) {
        fclose(fp);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        /*
         * %*u discards the type column (always KSTAT_DATA_UINT64 = 4
         * for ZIL stats). PyLong_FromString handles the conversion.
         */
        nscanned = sscanf(line, "%63s %*u %23s", name, valstr);
        if (nscanned != 2)
            continue;

        if (idx >= ZILSTATS_N_FIELDS) {
            fclose(fp);
            PyErr_Format(PyExc_ValueError,
                ZILSTATS_PATH " has more than %d data fields. "
                "Update zilstats.c and stubs to match the installed "
                "ZFS version.",
                ZILSTATS_N_FIELDS);
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
                ZILSTATS_PATH " field %d (%s): "
                "trailing garbage in value \"%s\"",
                idx, name, valstr);
            Py_DECREF(result);
            return NULL;
        }

        PyStructSequence_SetItem(result, idx, val); /* steals ref */
        idx++;
    }

    fclose(fp);

    if (idx != ZILSTATS_N_FIELDS) {
        PyErr_Format(PyExc_ValueError,
            ZILSTATS_PATH " has %d data fields; expected %d. "
            "Update zilstats.c and stubs to match the installed "
            "ZFS version.",
            idx, ZILSTATS_N_FIELDS);
        Py_DECREF(result);
        return NULL;
    }

    return result;
}
