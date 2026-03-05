#ifndef _LIBZFS_CORE_REPLICATION_H
#define _LIBZFS_CORE_REPLICATION_H
#include "../truenas_pylibzfs.h"

/*
 * Exposed from py_zfs_core_module.c (static keyword removed so this
 * translation unit can raise ZFSCoreException without duplicating logic).
 */
extern void set_zfscore_exc(PyObject *module, const char *msg, int code,
			    PyObject *errors_tuple);

/* Functions implemented in libzfs_core_replication.c */
extern PyObject *py_lzc_send(PyObject *self, PyObject *args, PyObject *kwds);
extern PyObject *py_lzc_send_space(PyObject *self, PyObject *args,
				   PyObject *kwds);
extern PyObject *py_lzc_send_progress(PyObject *self, PyObject *args,
				      PyObject *kwds);
extern PyObject *py_lzc_receive(PyObject *self, PyObject *args, PyObject *kwds);

#endif /* _LIBZFS_CORE_REPLICATION_H */
