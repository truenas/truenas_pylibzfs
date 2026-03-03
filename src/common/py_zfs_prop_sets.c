#include "../truenas_pylibzfs.h"
typedef struct {
	PyObject *zfs_space_props;
	PyObject *zfs_volume_props;
	PyObject *zfs_volume_readonly_props;
	PyObject *zfs_filesystem_props;
	PyObject *zfs_filesystem_readonly_props;
	PyObject *zfs_filesystem_snapshot_props;
	PyObject *zfs_filesystem_snapshot_readonly_props;
	PyObject *zfs_volume_snapshot_props;
	PyObject *zfs_volume_snapshot_readonly_props;
	PyObject *zpool_status_nonrecoverable;
	PyObject *zpool_status_recoverable;
	PyObject *zpool_readonly_properties;
	PyObject *zpool_properties;
	PyObject *zpool_class_space;
	PyObject *zpool_space;
} pylibzfs_propset_t;


static
pylibzfs_propset_t *get_propset_state(PyObject *module)
{
	pylibzfs_propset_t *state = NULL;

	state = (pylibzfs_propset_t *)PyModule_GetState(module);
	PYZFS_ASSERT(state, "Failed to get propset module state.");

	return state;
}

static int
py_zfs_propset_module_clear(PyObject *module)
{
	pylibzfs_propset_t *state = get_propset_state(module);
	Py_CLEAR(state->zfs_space_props);
	Py_CLEAR(state->zfs_volume_props);
	Py_CLEAR(state->zfs_volume_readonly_props);
	Py_CLEAR(state->zfs_filesystem_props);
	Py_CLEAR(state->zfs_filesystem_readonly_props);
	Py_CLEAR(state->zfs_filesystem_snapshot_props);
	Py_CLEAR(state->zfs_filesystem_snapshot_readonly_props);
	Py_CLEAR(state->zfs_volume_snapshot_props);
	Py_CLEAR(state->zfs_volume_snapshot_readonly_props);
	Py_CLEAR(state->zpool_status_nonrecoverable);
	Py_CLEAR(state->zpool_status_recoverable);
	Py_CLEAR(state->zpool_readonly_properties);
	Py_CLEAR(state->zpool_properties);
	Py_CLEAR(state->zpool_class_space);
	Py_CLEAR(state->zpool_space);
	return 0;
}

static void
py_zfs_propset_module_free(void *module)
{
	if (module)
		py_zfs_propset_module_clear((PyObject *)module);
}

static
boolean_t is_space_zfs_prop(zfs_prop_t prop)
{
	boolean_t is_space;

	switch (prop) {
	case ZFS_PROP_AVAILABLE:
	case ZFS_PROP_USEDSNAP:
	case ZFS_PROP_WRITTEN:
	case ZFS_PROP_USEDDS:
	case ZFS_PROP_USEDREFRESERV:
	case ZFS_PROP_USEDCHILD:
	case ZFS_PROP_USED:
		is_space = B_TRUE;
		break;
	default:
		is_space = B_FALSE;
		break;
	};

	return is_space;
}

static
boolean_t py_add_zfs_propset(pylibzfs_state_t *pstate,
			     PyObject *module,
			     pylibzfs_propset_t *state)
{
	PyObject *iterator = PyObject_GetIter(pstate->zfs_property_enum);
	PyObject *item = NULL;
	if (iterator == NULL)
		return B_FALSE;

	state->zfs_volume_props = PyFrozenSet_New(NULL);
	if (state->zfs_volume_props == NULL)
		goto error;

	state->zfs_volume_readonly_props = PyFrozenSet_New(NULL);
	if (state->zfs_volume_readonly_props == NULL)
		goto error;

	state->zfs_filesystem_props = PyFrozenSet_New(NULL);
	if (state->zfs_filesystem_props == NULL)
		goto error;

	state->zfs_filesystem_readonly_props = PyFrozenSet_New(NULL);
	if (state->zfs_filesystem_readonly_props == NULL)
		goto error;

	state->zfs_filesystem_snapshot_props = PyFrozenSet_New(NULL);
	if (state->zfs_filesystem_snapshot_props == NULL)
		goto error;

	state->zfs_filesystem_snapshot_readonly_props = PyFrozenSet_New(NULL);
	if (state->zfs_filesystem_snapshot_readonly_props == NULL)
		goto error;

	state->zfs_volume_snapshot_props = PyFrozenSet_New(NULL);
	if (state->zfs_volume_snapshot_props == NULL)
		goto error;

	state->zfs_volume_snapshot_readonly_props = PyFrozenSet_New(NULL);
	if (state->zfs_volume_snapshot_readonly_props == NULL)
		goto error;

	state->zfs_space_props = PyFrozenSet_New(NULL);
	if (state->zfs_space_props == NULL)
		goto error;

	/*
	 * Iterate the ZFS propset enum and build out frozenset
	 * based on the properties
	 */
	while ((item = PyIter_Next(iterator))) {
		long val = PyLong_AsLong(item);
		PYZFS_ASSERT((val != -1), "Unexpected value for ZFS property");
		PYZFS_ASSERT((val < ZFS_NUM_PROPS), "Value exceeds known ZFS props");

		if (zfs_prop_valid_for_type(val, ZFS_TYPE_VOLUME, B_FALSE)) {
			if (PySet_Add(state->zfs_volume_props, item))
				goto error;
			if (zfs_prop_readonly(val) &&
			    (PySet_Add(state->zfs_volume_readonly_props, item)))
				goto error;

			if (zfs_prop_valid_for_type(val, ZFS_TYPE_SNAPSHOT, B_FALSE)) {
				if (PySet_Add(state->zfs_volume_snapshot_props, item))
					goto error;
				if (zfs_prop_readonly(val) &&
				    (PySet_Add(state->zfs_volume_snapshot_readonly_props, item)))
					goto error;
			}
		}

		if (zfs_prop_valid_for_type(val, ZFS_TYPE_FILESYSTEM, B_FALSE)) {
			if (PySet_Add(state->zfs_filesystem_props, item))
				goto error;
			if (zfs_prop_readonly(val) &&
			    (PySet_Add(state->zfs_filesystem_readonly_props, item)))
				goto error;

			if (zfs_prop_valid_for_type(val, ZFS_TYPE_SNAPSHOT, B_FALSE)) {
				if (PySet_Add(state->zfs_filesystem_snapshot_props, item))
					goto error;
				if (zfs_prop_readonly(val) &&
				    (PySet_Add(state->zfs_filesystem_snapshot_readonly_props, item)))
					goto error;
			}
		}

		if (is_space_zfs_prop(val) &&
		    (PySet_Add(state->zfs_space_props, item)))
			goto error;

		Py_DECREF(item);
	}

	if (PyModule_AddObjectRef(module, "ZFS_VOLUME_PROPERTIES",
	    state->zfs_volume_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_VOLUME_READONLY_PROPERTIES",
	    state->zfs_volume_readonly_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_FILESYSTEM_PROPERTIES",
	    state->zfs_filesystem_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_FILESYSTEM_READONLY_PROPERTIES",
	    state->zfs_filesystem_readonly_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_FILESYSTEM_SNAPSHOT_PROPERTIES",
	    state->zfs_filesystem_snapshot_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_FILESYSTEM_SNAPSHOT_READONLY_PROPERTIES",
	    state->zfs_filesystem_snapshot_readonly_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_VOLUME_SNAPSHOT_PROPERTIES",
	    state->zfs_volume_snapshot_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_VOLUME_SNAPSHOT_READONLY_PROPERTIES",
	    state->zfs_volume_snapshot_readonly_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_SPACE_PROPERTIES",
	    state->zfs_space_props) < 0)
		goto error;

	return B_TRUE;
error:
	// NOTE: allocated frozen sets within the state struct are freed
	// when the module is freed on error

	Py_DECREF(iterator);
	Py_XDECREF(item);
	return B_FALSE;
}

static
boolean_t py_add_zpool_status_sets(pylibzfs_state_t *pstate,
				    PyObject *module,
				    pylibzfs_propset_t *state)
{
	static const zpool_status_t nonrecoverable[] = {
		ZPOOL_STATUS_MISSING_DEV_NR,
		ZPOOL_STATUS_CORRUPT_LABEL_NR,
		ZPOOL_STATUS_CORRUPT_POOL,
		ZPOOL_STATUS_VERSION_NEWER,
		ZPOOL_STATUS_UNSUP_FEAT_READ,
		ZPOOL_STATUS_FAULTED_DEV_NR,
		ZPOOL_STATUS_IO_FAILURE_MMP,
		ZPOOL_STATUS_BAD_GUID_SUM,
	};
	static const zpool_status_t recoverable[] = {
		ZPOOL_STATUS_MISSING_DEV_R,
		ZPOOL_STATUS_CORRUPT_LABEL_R,
		ZPOOL_STATUS_FAULTED_DEV_R,
		ZPOOL_STATUS_CORRUPT_DATA,
		ZPOOL_STATUS_BAD_LOG,
		ZPOOL_STATUS_IO_FAILURE_WAIT,
		ZPOOL_STATUS_IO_FAILURE_CONTINUE,
		ZPOOL_STATUS_FAILING_DEV,
	};
	PyObject *item = NULL;
	int rv;

	state->zpool_status_nonrecoverable = PyFrozenSet_New(NULL);
	if (state->zpool_status_nonrecoverable == NULL)
		return B_FALSE;

	for (size_t i = 0; i < ARRAY_SIZE(nonrecoverable); i++) {
		item = PyObject_CallFunction(
		    pstate->zpool_status_enum, "i", nonrecoverable[i]);
		if (item == NULL)
			return B_FALSE;

		rv = PySet_Add(state->zpool_status_nonrecoverable, item);
		Py_DECREF(item);
		if (rv < 0)
			return B_FALSE;
	}

	if (PyModule_AddObjectRef(module, "ZPOOL_STATUS_NONRECOVERABLE",
	    state->zpool_status_nonrecoverable) < 0)
		return B_FALSE;

	state->zpool_status_recoverable = PyFrozenSet_New(NULL);
	if (state->zpool_status_recoverable == NULL)
		return B_FALSE;

	for (size_t i = 0; i < ARRAY_SIZE(recoverable); i++) {
		item = PyObject_CallFunction(
		    pstate->zpool_status_enum, "i", recoverable[i]);
		if (item == NULL)
			return B_FALSE;

		rv = PySet_Add(state->zpool_status_recoverable, item);
		Py_DECREF(item);
		if (rv < 0)
			return B_FALSE;
	}

	if (PyModule_AddObjectRef(module, "ZPOOL_STATUS_RECOVERABLE",
	    state->zpool_status_recoverable) < 0)
		return B_FALSE;

	return B_TRUE;
}

/*
 * Returns true for the per-allocation-class space counters, i.e. the
 * class_normal_*, class_special_*, class_dedup_*, class_log_*,
 * class_elog_*, and class_special_elog_* properties.
 */
static boolean_t
is_class_zpool_prop(zpool_prop_t prop)
{
	return (prop >= ZPOOL_PROP_NORMAL_SIZE &&
	    prop <= ZPOOL_PROP_SELOG_FRAGMENTATION);
}

/*
 * Returns true for all pool properties that report space consumption or
 * capacity figures, including the per-class counters.
 */
static boolean_t
is_space_zpool_prop(zpool_prop_t prop)
{
	if (is_class_zpool_prop(prop))
		return (B_TRUE);

	switch (prop) {
	case ZPOOL_PROP_SIZE:
	case ZPOOL_PROP_FREE:
	case ZPOOL_PROP_ALLOCATED:
	case ZPOOL_PROP_FREEING:
	case ZPOOL_PROP_LEAKED:
	case ZPOOL_PROP_FRAGMENTATION:
	case ZPOOL_PROP_CAPACITY:
	case ZPOOL_PROP_EXPANDSZ:
	case ZPOOL_PROP_CHECKPOINT:
	case ZPOOL_PROP_AVAILABLE:
	case ZPOOL_PROP_USABLE:
	case ZPOOL_PROP_USED:
	case ZPOOL_PROP_BCLONEUSED:
	case ZPOOL_PROP_BCLONESAVED:
	case ZPOOL_PROP_BCLONERATIO:
	case ZPOOL_PROP_DEDUPRATIO:
	case ZPOOL_PROP_DEDUPUSED:
	case ZPOOL_PROP_DEDUPSAVED:
	case ZPOOL_PROP_DEDUP_TABLE_SIZE:
	case ZPOOL_PROP_DEDUPCACHED:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

typedef struct {
	pylibzfs_state_t   *pstate;
	pylibzfs_propset_t *state;
	boolean_t           error;
} zpool_propset_cb_t;

/*
 * zprop_iter() callback — called once per visible pool property.
 * Hidden properties (registered via zprop_register_hidden) are skipped
 * by zprop_iter() itself when show_all=B_FALSE.
 */
static int
zpool_propset_cb(int prop_num, void *arg)
{
	zpool_propset_cb_t *cb = arg;
	zpool_prop_t zprop = (zpool_prop_t)prop_num;
	PyObject *item;

	item = cb->pstate->zpool_prop_enum_tbl[zprop].obj;
	if (item == NULL)
		return (ZPROP_CONT);

	if (zpool_prop_readonly(zprop)) {
		if (PySet_Add(cb->state->zpool_readonly_properties, item)) {
			cb->error = B_TRUE;
			return (ZPROP_INVAL);
		}
	} else if (!zpool_prop_setonce(zprop)) {
		if (PySet_Add(cb->state->zpool_properties, item)) {
			cb->error = B_TRUE;
			return (ZPROP_INVAL);
		}
	}

	if (is_class_zpool_prop(zprop)) {
		if (PySet_Add(cb->state->zpool_class_space, item)) {
			cb->error = B_TRUE;
			return (ZPROP_INVAL);
		}
	}

	if (is_space_zpool_prop(zprop)) {
		if (PySet_Add(cb->state->zpool_space, item)) {
			cb->error = B_TRUE;
			return (ZPROP_INVAL);
		}
	}

	return (ZPROP_CONT);
}

static
boolean_t py_add_zpool_propsets(pylibzfs_state_t *pstate,
				  PyObject *module,
				  pylibzfs_propset_t *state)
{
	zpool_propset_cb_t cb = {
		.pstate = pstate,
		.state  = state,
		.error  = B_FALSE,
	};

	state->zpool_readonly_properties = PyFrozenSet_New(NULL);
	if (state->zpool_readonly_properties == NULL)
		return B_FALSE;

	state->zpool_properties = PyFrozenSet_New(NULL);
	if (state->zpool_properties == NULL)
		return B_FALSE;

	state->zpool_class_space = PyFrozenSet_New(NULL);
	if (state->zpool_class_space == NULL)
		return B_FALSE;

	state->zpool_space = PyFrozenSet_New(NULL);
	if (state->zpool_space == NULL)
		return B_FALSE;

	/*
	 * zprop_iter() with show_all=B_FALSE automatically skips properties
	 * registered with zprop_register_hidden(), giving us the same
	 * visible-only filtering that the zpool command uses.
	 */
	zprop_iter(zpool_propset_cb, &cb, B_FALSE, B_FALSE, ZFS_TYPE_POOL);
	if (cb.error)
		return B_FALSE;

	if (PyModule_AddObjectRef(module, "ZPOOL_READONLY_PROPERTIES",
	    state->zpool_readonly_properties) < 0)
		return B_FALSE;

	if (PyModule_AddObjectRef(module, "ZPOOL_PROPERTIES",
	    state->zpool_properties) < 0)
		return B_FALSE;

	if (PyModule_AddObjectRef(module, "ZPOOL_CLASS_SPACE",
	    state->zpool_class_space) < 0)
		return B_FALSE;

	if (PyModule_AddObjectRef(module, "ZPOOL_SPACE",
	    state->zpool_space) < 0)
		return B_FALSE;

	return B_TRUE;
}

static
boolean_t py_init_propset_state(pylibzfs_state_t *pstate,
				PyObject *module,
				pylibzfs_propset_t *state)
{

	if (!py_add_zfs_propset(pstate, module, state))
		return B_FALSE;

	if (!py_add_zpool_status_sets(pstate, module, state))
		return B_FALSE;

	if (!py_add_zpool_propsets(pstate, module, state))
		return B_FALSE;

	return B_TRUE;
}

PyDoc_STRVAR(py_zfs_propset_module__doc__,
PYLIBZFS_MODULE_NAME ".propset provides various frozen sets for ZFS and zpool\n"
"properties for the convenience of API consumers.\n"
"\n"
"The following frozen sets are provided\n"
"- ZFS_READONLY_PROPERTIES: these properties are not valid for setting once the\n"
"    dataset or volume is created\n"
"\n"
"- ZFS_VOLUME_PROPERTIES: these properties are valid for ZFS_TYPE_VOLUME.\n"
"\n"
"- ZFS_FILESYSTEM_PROPERTIES: these properties are valid for ZFS_TYPE_FILESYSTEM.\n"
"\n"
"- ZFS_SPACE_PROPERTIES: these properties provide the equivalent of the property\n"
"   set returned by the command \"zfs get space\".\n"
"\n"
"- ZPOOL_STATUS_NONRECOVERABLE: ZPOOLStatus values indicating the pool cannot\n"
"   recover without administrative intervention (e.g. restore from backup).\n"
"\n"
"- ZPOOL_STATUS_RECOVERABLE: ZPOOLStatus values indicating the pool has errors\n"
"   that may be resolved through administrative action (e.g. replacing a\n"
"   failed device, clearing I/O errors, or scrubbing).\n"
"\n"
"- ZPOOL_CLASS_SPACE: ZPOOLProperty members for per-allocation-class space\n"
"   counters (class_normal_*, class_special_*, class_dedup_*, class_log_*,\n"
"   class_elog_*, class_special_elog_*).\n"
"\n"
"- ZPOOL_SPACE: ZPOOLProperty members for all pool space and capacity\n"
"   properties, including the per-class counters in ZPOOL_CLASS_SPACE.\n"
);
/* Module structure */
static struct PyModuleDef truenas_pypropset = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = PYLIBZFS_MODULE_NAME ".property_sets",
	.m_doc = py_zfs_propset_module__doc__,
	.m_size = sizeof(pylibzfs_propset_t),
	.m_clear = py_zfs_propset_module_clear,
	.m_free = py_zfs_propset_module_free,
};

PyObject *py_setup_propset_module(PyObject *p)
{
	pylibzfs_propset_t *state = NULL;
	pylibzfs_state_t *pstate = (pylibzfs_state_t *)PyModule_GetState(p);
	if (pstate == NULL)
		return NULL;

	PyObject *m = PyModule_Create(&truenas_pypropset);
	if (m == NULL)
		return NULL;

	state = get_propset_state(m);
	if (!py_init_propset_state(pstate, m, state)) {
		Py_DECREF(m);
		return NULL;
	}

	return m;
}
