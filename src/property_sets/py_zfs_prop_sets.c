#include "../truenas_pylibzfs.h"
typedef struct {
	PyObject *zfs_space_props;
	PyObject *zfs_volume_props;
	PyObject *zfs_filesystem_props;
	PyObject *readonly_zfs_props;
} pylibzfs_propset_t;


static
pylibzfs_propset_t *get_propset_state(PyObject *module)
{
	pylibzfs_propset_t *state = NULL;

	state = (pylibzfs_propset_t *)PyModule_GetState(module);
	PYZFS_ASSERT(state, "Failed to get propeset module state.");

	return state;
}

static int
py_zfs_propset_module_clear(PyObject *module)
{
	pylibzfs_propset_t *state = get_propset_state(module);
	Py_CLEAR(state->zfs_space_props);
	Py_CLEAR(state->zfs_volume_props);
	Py_CLEAR(state->zfs_filesystem_props);
	Py_CLEAR(state->readonly_zfs_props);
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

	state->readonly_zfs_props = PyFrozenSet_New(NULL);
	if (state->readonly_zfs_props == NULL)
		goto error;

	state->zfs_volume_props = PyFrozenSet_New(NULL);
	if (state->zfs_volume_props == NULL)
		goto error;

	state->zfs_filesystem_props = PyFrozenSet_New(NULL);
	if (state->zfs_filesystem_props == NULL)
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

		if (zfs_prop_readonly(val) &&
		    (PySet_Add(state->readonly_zfs_props, item)))
			goto error;

		if (zfs_prop_valid_for_type(val, ZFS_TYPE_VOLUME, B_FALSE) &&
		    (PySet_Add(state->zfs_volume_props, item)))
			goto error;

		if (zfs_prop_valid_for_type(val, ZFS_TYPE_FILESYSTEM, B_FALSE) &&
		    (PySet_Add(state->zfs_filesystem_props, item)))
			goto error;

		if (is_space_zfs_prop(val) &&
		    (PySet_Add(state->zfs_space_props, item)))
			goto error;

		Py_DECREF(item);
	}

	if (PyModule_AddObjectRef(module, "READONLY_ZFS_PROPERTIES",
	    state->readonly_zfs_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_VOLUME_PROPERTIES",
	    state->zfs_volume_props) < 0)
		goto error;

	if (PyModule_AddObjectRef(module, "ZFS_FILESYSTEM_PROPERTIES",
	    state->zfs_filesystem_props) < 0)
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
boolean_t py_init_propset_state(pylibzfs_state_t *pstate,
				PyObject *module,
				pylibzfs_propset_t *state)
{

	if (!py_add_zfs_propset(pstate, module, state))
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
