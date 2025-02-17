#include "truenas_pylibzfs.h"

static
int setup_zfs_type(PyObject *module, pylibzfs_state_t *state)
{
	uint i, j;
	PyObject *pyenum = state->zfs_type_enum;

	for (i = 0; i < ARRAY_SIZE(state->zfs_type_enum_tbl); i++) {
		PyObject *enum_val, *enum_key, *name;

		enum_key = Py_BuildValue("i", zfs_type_table[i].type);
		if (enum_key == NULL) {
			goto fail;
		}
		enum_val = PyObject_CallOneArg(pyenum, enum_key);
		Py_DECREF(enum_key);
		if (enum_val == NULL) {
			goto fail;
		}

		name = PyObject_GetAttrString(enum_val, "name");
		if (name == NULL) {
			goto fail;
		}

		state->zfs_type_enum_tbl[i].obj = enum_val;
		state->zfs_type_enum_tbl[i].type = zfs_type_table[i].type;
		state->zfs_type_enum_tbl[i].name = name;
	}

	return 0;

fail:
	for (j = 0; j < i; j++) {
		Py_CLEAR(state->zfs_type_enum_tbl[j].obj);
		Py_CLEAR(state->zfs_type_enum_tbl[j].name);
	}

	return -1;
}

static
int setup_zfs_prop_type(PyObject *module, pylibzfs_state_t *state)
{
	uint i, j;
	PyObject *pyenum = state->zfs_property_enum;

	for (i = 0; i < ARRAY_SIZE(state->zfs_prop_enum_tbl); i++) {
		PyObject *enum_val, *enum_key, *name;
		zfs_prop_t prop = zfs_prop_table[i].prop;
		if (!zfs_prop_visible(prop))
			continue;

		enum_key = Py_BuildValue("i", zfs_prop_table[i].prop);
		if (enum_key == NULL) {
			goto fail;
		}

		enum_val = PyObject_CallOneArg(pyenum, enum_key);
		Py_DECREF(enum_key);
		if (enum_val == NULL) {
			goto fail;
		}

		name = PyObject_GetAttrString(enum_val, "name");
		if (name == NULL) {
			goto fail;
		}

		state->zfs_prop_enum_tbl[i].obj = enum_val;
		state->zfs_prop_enum_tbl[i].type = prop;
		state->zfs_prop_enum_tbl[i].name = name;
	}

	return 0;

fail:
	for (j = 0; j < i; j++) {
		Py_CLEAR(state->zfs_prop_enum_tbl[j].obj);
		Py_CLEAR(state->zfs_prop_enum_tbl[j].name);
	}

	return -1;
}

static
int setup_property_source_type(PyObject *module, pylibzfs_state_t *state)
{
	uint i, j;
	PyObject *pyenum = state->zfs_property_src_enum;

	for (i = 0; i < ARRAY_SIZE(state->zfs_prop_src_enum_tbl); i++) {
		PyObject *enum_val, *enum_key, *name;
		zprop_source_t st = zprop_source_table[i].sourcetype;

		enum_key = Py_BuildValue("i", zprop_source_table[i].sourcetype);
		if (enum_key == NULL) {
			goto fail;
		}
		enum_val = PyObject_CallOneArg(pyenum, enum_key);
		Py_DECREF(enum_key);
		if (enum_val == NULL) {
			goto fail;
		}

		name = PyObject_GetAttrString(enum_val, "name");
		if (name == NULL) {
			goto fail;
		}

		state->zfs_prop_src_enum_tbl[i].obj = enum_val;
		state->zfs_prop_src_enum_tbl[i].type = st;
		state->zfs_prop_src_enum_tbl[i].name = name;
	}

	return 0;

fail:
	for (j = 0; j < i; j++) {
		Py_CLEAR(state->zfs_prop_src_enum_tbl[j].obj);
		Py_CLEAR(state->zfs_prop_src_enum_tbl[j].name);
	}

	return -1;

}

pylibzfs_state_t *py_get_module_state(py_zfs_t *zfs)
{
	pylibzfs_state_t *state = NULL;

	state = (pylibzfs_state_t *)PyModule_GetState(zfs->module);
	PYZFS_ASSERT(state, "Failed to get module state.");

	return state;
}

int init_py_zfs_state(PyObject *module)
{
	int err;
	pylibzfs_state_t *state = NULL;

	state = (pylibzfs_state_t *)PyModule_GetState(module);
	PYZFS_ASSERT(state, "Failed to get module state.");

	/* Populate state lookup tables with pointers to enums
	 * allocated during enum init
	 */
	err = setup_zfs_type(module, state);
	PYZFS_ASSERT(err == 0, "Failed to setup ZFS type in module state.");

	err = setup_property_source_type(module, state);
	PYZFS_ASSERT(err == 0, "Failed to setup Property Source type in module state.");

	err = setup_zfs_prop_type(module, state);
	PYZFS_ASSERT(err == 0, "Failed to setup Property type in module state.");

	init_py_struct_prop_state(state);
	init_py_struct_userquota_state(state);
	module_init_zfs_crypto(module);

	return 0;
}

PyObject *py_get_zfs_type(py_zfs_t *zfs, zfs_type_t type, PyObject **name_out)
{
	PyObject *out = NULL, *name = NULL;
	pylibzfs_state_t *state = NULL;
	uint i;

	state = py_get_module_state(zfs);

	for (i = 0; i < ARRAY_SIZE(state->zfs_type_enum_tbl); i++) {
		if (zfs_type_table[i].type == type) {
			out = state->zfs_type_enum_tbl[i].obj;

			if (name_out != NULL) {
				name = state->zfs_type_enum_tbl[i].name;
				PYZFS_ASSERT(name, "Failed to get name ref.");
			}
			break;
		}
	}

	PYZFS_ASSERT(out, "Failed to get reference for zfs_type_t enum");
	if (name_out != NULL)
		*name_out = Py_NewRef(name);

	return Py_NewRef(out);
}

PyObject *py_get_property_source(py_zfs_t *zfs, zprop_source_t sourcetype)
{
	PyObject *out = NULL;
	pylibzfs_state_t *state = NULL;

	uint i;

	state = py_get_module_state(zfs);

	for (i = 0; i < ARRAY_SIZE(state->zfs_prop_src_enum_tbl); i++) {
		if (zprop_source_table[i].sourcetype == sourcetype) {
			out = state->zfs_prop_src_enum_tbl[i].obj;
			break;
		}
	}

	PYZFS_ASSERT(out, "Failed to get reference for zprop_source_t enum");

	return Py_NewRef(out);
}

/* WARNING: this should only be called from m_clear for module */
void free_py_zfs_state(PyObject *module)
{
	pylibzfs_state_t *state = NULL;
	size_t idx;

	state = (pylibzfs_state_t *)PyModule_GetState(module);
	if (state == NULL)
		return;

	for (idx = 0; idx < ARRAY_SIZE(state->zfs_type_enum_tbl); idx++) {
		Py_CLEAR(state->zfs_type_enum_tbl[idx].name);
		Py_CLEAR(state->zfs_type_enum_tbl[idx].obj);
	}

	for (idx = 0; idx < ARRAY_SIZE(state->zfs_prop_src_enum_tbl); idx++) {
		Py_CLEAR(state->zfs_prop_src_enum_tbl[idx].name);
		Py_CLEAR(state->zfs_prop_src_enum_tbl[idx].obj);
	}

	for (idx = 0; idx < ARRAY_SIZE(state->zfs_prop_enum_tbl); idx++) {
		Py_CLEAR(state->zfs_prop_enum_tbl[idx].name);
		Py_CLEAR(state->zfs_prop_enum_tbl[idx].obj);
	}

	/* doc and name were allocated using python memory interface */
	for (idx = 0; idx < ARRAY_SIZE(state->struct_prop_fields); idx++) {
		PyMem_Free((void *)state->struct_prop_fields[idx].name);
		state->struct_prop_fields[idx].name = NULL;

		PyMem_Free((void *)state->struct_prop_fields[idx].doc);
		state->struct_prop_fields[idx].doc = NULL;
	}

	Py_CLEAR(state->struct_zfs_props_type);
	Py_CLEAR(state->struct_zfs_prop_type);
	Py_CLEAR(state->struct_zfs_prop_src_type);
	Py_CLEAR(state->struct_zfs_userquota_type);
	Py_CLEAR(state->struct_zfs_crytpo_info_type);

	Py_CLEAR(state->zfs_property_src_enum);
	Py_CLEAR(state->zfs_property_enum);
	Py_CLEAR(state->zfs_type_enum);
	Py_CLEAR(state->zfs_uquota_enum);
}
