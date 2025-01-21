#ifndef	_PYZFS_STATE_H
#define _PYZFS_STATE_H

typedef struct { zfs_type_t type; PyObject *obj; PyObject *name;} pystate_zfstype_t;

typedef struct {
	/* Per-module instance lookup tables for enum entries */
	pystate_zfstype_t zfs_type_enum[ARRAY_SIZE(zfs_type_table)];
} pylibzfs_state_t;

extern int init_py_zfs_state(PyObject *module);
#endif /* _PYZFS_STATE_H */
