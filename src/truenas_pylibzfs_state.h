#ifndef	_PYZFS_STATE_H
#define _PYZFS_STATE_H

typedef struct { zfs_type_t type; PyObject *obj; PyObject *name;} pystate_zfstype_t;
typedef struct { zfs_prop_t type; PyObject *obj; PyObject *name;} pystate_zfsprop_t;
typedef struct { zprop_source_t type; PyObject *obj; PyObject *name;} pystate_zfspropsrc_t;
typedef struct { zpool_prop_t type; PyObject *obj; PyObject *name;} pystate_zpool_prop_t;

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

	/* Reference to named tuple for ZFS user quota */
	PyTypeObject *struct_zfs_userquota_type;

	/* Reference to named tuple for crypto info */
	PyTypeObject *struct_zfs_crypto_info_type;

	/* Reference to named tuple for crypto config (for creation / update) */
	PyTypeObject *struct_zfs_crypto_change_type;

	/*
	 * References to enums that are available in the base
	 * of module. We have them in state so that we can
	 * efficiently use them for validation purposes.
	 */
	PyObject *zfs_property_src_enum;
	PyObject *zfs_property_enum;
	PyObject *zfs_type_enum;
	PyObject *zfs_uquota_enum;

	/*
	 * References to json module dumps, loads fuctions
	 */
	PyObject *dumps_fn;
	PyObject *loads_fn;

	/* Reference to zpool-related enums */
	PyObject *zpool_status_enum;
	PyObject *zpool_property_enum;
	PyTypeObject *struct_zpool_status_type;
	PyTypeObject *struct_vdev_status_type;
	PyTypeObject *struct_vdev_stats_type;
	PyTypeObject *struct_support_vdev_type;
	PyObject *vdev_state_enum;

	PyTypeObject *struct_zpool_feature_type;

	/* Reference to struct_vdev_create_spec type (py_zfs_pool_create.c) */
	PyTypeObject *struct_vdev_create_spec_type;

	/* Reference to VDevType StrEnum */
	PyObject *vdev_type_enum;

	/* References for scan/scrub enums and struct type */
	PyObject *scan_function_enum;
	PyObject *scan_state_enum;
	PyTypeObject *struct_zpool_scrub_type;

	/*
	 * Per-property lookup table for ZPOOLProperty enum members.
	 * Indexed 0..ZPOOL_NUM_PROPS-1 (ZPOOL_PROP_INVAL is excluded).
	 * (obj, name) must be Py_CLEAR()-ed when freeing the module state.
	 */
	pystate_zpool_prop_t zpool_prop_enum_tbl[ZPOOL_NUM_PROPS];

	/*
	 * Fields for the dynamically-created struct_zpool_property type.
	 * name/doc strings are heap-allocated via PyMem_* and must be freed
	 * with PyMem_Free() when the module state is freed.
	 * The +1 slot is the required NULL sentinel.
	 */
	PyStructSequence_Field struct_zpool_prop_fields[ZPOOL_NUM_PROPS + 1];
	PyStructSequence_Desc  struct_zpool_prop_desc;

	/*
	 * Named tuple containing all pool properties as attributes.
	 * Used as the return type of ZFSPool.get_properties().
	 */
	PyTypeObject *struct_zpool_props_type;
} pylibzfs_state_t;

extern int init_py_zfs_state(PyObject *module);
extern void init_py_struct_prop_state(pylibzfs_state_t *state);
extern void init_py_struct_zpool_prop_state(pylibzfs_state_t *state);
extern void free_py_zfs_state(PyObject *module);
#endif /* _PYZFS_STATE_H */
