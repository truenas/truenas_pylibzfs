#ifndef	_PYZFS_STATE_H
#define _PYZFS_STATE_H

typedef struct { zfs_type_t type; PyObject *obj; PyObject *name;} pystate_zfstype_t;
typedef struct { zfs_prop_t type; PyObject *obj; PyObject *name;} pystate_zfsprop_t;
typedef struct { zprop_source_t type; PyObject *obj; PyObject *name;} pystate_zfspropsrc_t;

typedef struct {
	/*
	 * Per-module instance lookup tables for enum entries
	 * (obj, name) must be freed using Py_CLEAR() when freeing
	 * the python state.
	 */
	pystate_zfstype_t zfs_type_enum_tbl[ARRAY_SIZE(zfs_type_table)];
	pystate_zfspropsrc_t zfs_prop_src_enum_tbl[ARRAY_SIZE(zprop_source_table)];
	/*
	 * This is used by py_zfs_prop.c for fast lookup of ZFSProperty enum
	 * for determining whether to look up property for output.
	 */
	pystate_zfsprop_t zfs_prop_enum_tbl[ARRAY_SIZE(zfs_prop_table)];

	/*
	 * Fields necessary for implementing struct_zfs_props_type
	 * This array contains malloced strings (.name, .doc) that must be freed
	 * using PyMem_Free() when module state is freed.
	 * These are only used for dynamically creating the struct_zfs_props_type.
	 */
	PyStructSequence_Field struct_prop_fields[ARRAY_SIZE(zfs_prop_table) + 1];
	PyStructSequence_Desc struct_zfs_prop_desc;

	/*
	 * Named tuple containing all ZFS properties as attributes.
	 * this is used for generate get_properties() response.
	 */
	PyTypeObject *struct_zfs_props_type;

	/*
	 * Reference to named tuple for individual ZFS properties
	 * - value (parsed raw value)
	 * - raw (string)
	 * - source (struct_zfs_prop_src_type or None type)
	 */
	PyTypeObject *struct_zfs_prop_type;

	/*
	 * Reference to named tuple for ZFS property source
	 * - type (PropertySource enum for the source type)
	 * - value (str or None type)
	 */
	PyTypeObject *struct_zfs_prop_src_type;

	/*
	 * References to enums that are available in the base
	 * of module. We have them in state so that we can
	 * efficiently use them for validation purposes.
	 */
	PyObject *zfs_property_src_enum;
	PyObject *zfs_property_enum;
	PyObject *zfs_type_enum;
	PyObject *zfs_uquota_enum;
} pylibzfs_state_t;

extern int init_py_zfs_state(PyObject *module);
extern void init_py_struct_prop_state(pylibzfs_state_t *state);
extern void free_py_zfs_state(PyObject *module);
#endif /* _PYZFS_STATE_H */
