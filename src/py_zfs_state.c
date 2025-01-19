#include "pylibzfs2.h"

static
int setup_zfs_type(PyObject *module, pylibzfs_state_t *state)
{
	uint i, j;
	PyObject *pyenum;

	pyenum = PyObject_GetAttrString(module, "ZFSType");
	if (pyenum == NULL)
		return -1;

	for (i = 0; i < ARRAY_SIZE(state->zfs_type_enum); i++) {
		PyObject *enum_val, *enum_key;

		enum_key = Py_BuildValue("i", zfs_type_table[i].type);
		if (enum_key == NULL) {
			goto fail;
		}
		enum_val = PyObject_CallOneArg(pyenum, enum_key);
		Py_DECREF(enum_key);
		if (enum_val == NULL) {
			goto fail;
		}

		state->zfs_type_enum[i].obj = enum_val;
		state->zfs_type_enum[i].type = zfs_type_table[i].type;
	}

	return 0;

fail:
	for (j = 0; j < i; j++)
		Py_CLEAR(state->zfs_type_enum[j].obj);

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

	return 0;
}

PyObject *py_get_zfs_type(py_zfs_t *zfs, zfs_type_t type)
{
	PyObject *out = NULL;
	pylibzfs_state_t *state = NULL;
	uint i;

	state = py_get_module_state(zfs);

	for (i = 0; i < ARRAY_SIZE(state->zfs_type_enum); i++) {
		if (zfs_type_table[i].type == type) {
			out = state->zfs_type_enum[i].obj;
		}
	}

	PYZFS_ASSERT(out, "Failed to get reference for zfs_type_t enum");
	return Py_NewRef(out);
}
