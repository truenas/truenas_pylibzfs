#include "../truenas_pylibzfs.h"

/* Create a dictionary for enum spec for the ZFSType enum */
static
PyObject *zfs_type_table_to_dict(void)
{
	PyObject *dict_out = NULL;
	int err;
	uint i;

	dict_out = PyDict_New();
	if (dict_out == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zfs_type_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zfs_type_table[i].type);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(dict_out,
					   zfs_type_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return dict_out;
fail:
	Py_XDECREF(dict_out);
	return NULL;
}

/* Create a dictionary for enum spec for the ZFSProperty enum */
static
PyObject *zfs_prop_table_to_dict(void)
{
	PyObject *dict_out = NULL;
	int err;
	uint i;

	dict_out = PyDict_New();
	if (dict_out == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zfs_prop_table); i++) {
		PyObject *val = NULL;
		PyObject *key = NULL;

		/*
		 * For now exclude properties that aren't
		 * marked as "visible". We can adjust in
		 * future if there is some reason to expose
		 * these properties to consumers
		 */
		if (!zfs_prop_visible(zfs_prop_table[i].prop))
			continue;

		val = PyLong_FromLong(zfs_prop_table[i].prop);
		if (val == NULL)
			goto fail;

		key = PyUnicode_FromString(zfs_prop_to_name(zfs_prop_table[i].prop));
		if (key == NULL) {
			Py_DECREF(val);
			goto fail;
		}

		PyObject *uppercase_key = PyObject_CallMethod(key, "upper", NULL);
		Py_DECREF(key);
		if (uppercase_key == NULL) {
			Py_DECREF(val);
			goto fail;
		}

		err = PyDict_SetItem(dict_out, uppercase_key, val);
		Py_DECREF(val);
		Py_DECREF(uppercase_key);
		if (err)
			goto fail;
	}

	return dict_out;
fail:
	Py_XDECREF(dict_out);
	return NULL;
}

/* Create a dictionary for enum spec for the ZPOOLProperty enum */
static
PyObject *zpool_prop_table_to_dict(void)
{
	PyObject *dict_out = NULL;
	int err;
	uint i;

	dict_out = PyDict_New();
	if (dict_out == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zpool_prop_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zpool_prop_table[i].prop);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(dict_out,
					   zpool_prop_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return dict_out;
fail:
	Py_XDECREF(dict_out);
	return NULL;
}

/* Create a dictionary for enum spec for the ZFSDOSFlag enum */
static
PyObject *zfs_dosflag_table_to_dict(void)
{
	PyObject *dict_out = NULL;
	int err;
	uint i;

	dict_out = PyDict_New();
	if (dict_out == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zfs_dosflag_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zfs_dosflag_table[i].flag);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(dict_out,
					   zfs_dosflag_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return dict_out;
fail:
	Py_XDECREF(dict_out);
	return NULL;
}

/* Create a dictionary for enum spec for the PropertySource enum */
static
PyObject *zfs_prop_src_table_to_dict(void)
{
	PyObject *dict_out = NULL;
	int err;
	uint i;

	dict_out = PyDict_New();
	if (dict_out == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zprop_source_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zprop_source_table[i].sourcetype);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(dict_out,
					   zprop_source_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return dict_out;
fail:
	Py_XDECREF(dict_out);
	return NULL;
}

/* Create a dictionary for enum spec for the ZFSError enum */
static
PyObject *zfs_err_table_to_dict(void)
{
	PyObject *zfserr_dict = NULL;
	int err;
	uint i;

	zfserr_dict = PyDict_New();
	if (zfserr_dict == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zfserr_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zfserr_table[i].error);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(zfserr_dict,
					   zfserr_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return zfserr_dict;
fail:
	Py_XDECREF(zfserr_dict);
	return NULL;
}

/* Create a dictionary for enum spec for the ZPOOLStatus enum */
static
PyObject *zpool_status_table_to_dict(void)
{
	PyObject *status_dict = NULL;
	int err;
	uint i;

	status_dict = PyDict_New();
	if (status_dict == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zpool_status_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zpool_status_table[i].status);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(status_dict,
					   zpool_status_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return status_dict;
fail:
	Py_XDECREF(status_dict);
	return NULL;
}

/* Create a dictionary for enum spec for the VDevState enum */
static
PyObject *vdev_state_table_to_dict(void)
{
	PyObject *state_dict = NULL;
	int err;
	uint i;

	state_dict = PyDict_New();
	if (state_dict == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(vdev_state_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(vdev_state_table[i].state);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(state_dict,
					   vdev_state_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return state_dict;
fail:
	Py_XDECREF(state_dict);
	return NULL;
}

/* Create a dictionary for enum spec for the VDevAuxState enum */
static
PyObject *zfs_vdev_aux_table_to_dict(void)
{
	PyObject *zfsaux_dict = NULL;
	int err;
	uint i;

	zfsaux_dict = PyDict_New();
	if (zfsaux_dict == NULL)
		return (NULL);

	for (i=0; i < ARRAY_SIZE(zfs_vdev_aux_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zfs_vdev_aux_table[i].aux);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(zfsaux_dict,
					   zfs_vdev_aux_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return (zfsaux_dict);
fail:
	Py_XDECREF(zfsaux_dict);
	return (NULL);
}

/* Create a dictionary for enum spec for the USERQuota enum */
static
PyObject *uquota_table_to_dict(void)
{
	PyObject *uquota_dict = NULL;
	int err;
	uint i;

	uquota_dict = PyDict_New();
	if (uquota_dict == NULL)
		return NULL;

	for (i=0; i < ARRAY_SIZE(zfs_uquota_table); i++) {
		PyObject *val = NULL;

		val = PyLong_FromLong(zfs_uquota_table[i].uquota_type);
		if (val == NULL)
			goto fail;

		err = PyDict_SetItemString(uquota_dict,
					   zfs_uquota_table[i].name,
					   val);
		Py_DECREF(val);
		if (err)
			goto fail;
	}

	return uquota_dict;
fail:
	Py_XDECREF(uquota_dict);
	return NULL;
}

static
PyObject *build_args_tuple_enum(const char *class_name,
				PyObject *(*get_dict)(void))
{
	PyObject *name = NULL;
	PyObject *attrs = NULL;
	PyObject *args_out = NULL;

	name = PyUnicode_FromString(class_name);
	if (name == NULL)
		goto out;

	attrs = get_dict();
	if (attrs == NULL)
		goto out;

	args_out = PyTuple_Pack(2, name, attrs);

out:
	Py_XDECREF(name);
	Py_XDECREF(attrs);
	return args_out;
}

/*
 * Add a single enum to module (the primary home, truenas_pylibzfs.enums) and
 * optionally also to parent_module (truenas_pylibzfs root) for backwards
 * compatibility.  Pass parent_module=NULL to register in the enums submodule
 * only.
 *
 * If penum_out is non-NULL, a new reference to the created enum object is
 * stored there for the caller to save in module state.
 */
int add_enum(PyObject *module,
	     PyObject *parent_module,
	     PyObject *enum_type,
	     const char *class_name,
	     PyObject *(*get_dict)(void),
	     PyObject *kwargs,
	     PyObject **penum_out)
{
	PyObject *args = NULL;
	PyObject *enum_obj = NULL;
	int err;

	args = build_args_tuple_enum(class_name, get_dict);
	if (args == NULL)
		return -1;

	// Create the enum via the functional API for python enums
	// https://docs.python.org/3/howto/enum.html#functional-api
	enum_obj = PyObject_Call(enum_type, args, kwargs);
	Py_DECREF(args);

	// Always register in the enums submodule
	err = PyModule_AddObjectRef(module, class_name, enum_obj);
	if (err) {
		Py_XDECREF(enum_obj);
		return -1;
	}

	// Optionally also register in the root truenas_pylibzfs module
	// for backwards compatibility with callers that import from there
	if (parent_module != NULL) {
		err = PyModule_AddObjectRef(parent_module, class_name, enum_obj);
		if (err) {
			Py_XDECREF(enum_obj);
			return -1;
		}
	}

	if (penum_out == NULL) {
		Py_XDECREF(enum_obj);
	} else {
		*penum_out = enum_obj;
	}
	return 0;
}

/*
 * Add all ZFS enum lookup tables from truenas_pylibzfs_enums.h as
 * enum.IntEnum / enum.IntFlag types.
 *
 * module - the root truenas_pylibzfs module (used for backwards-compatible
 *          re-export of selected enums and as the source of module state)
 * emod   - the truenas_pylibzfs.enums submodule (primary registration target
 *          for all enums)
 */
int
py_add_zfs_enums(PyObject *module, PyObject *emod)
{
	int err = -1;
	PyObject *enum_mod = NULL;
	PyObject *int_enum = NULL;
	PyObject *intflag_enum = NULL;
	PyObject *kwargs = NULL;
	pylibzfs_state_t *state = NULL;

	state = (pylibzfs_state_t *)PyModule_GetState(module);
	if (state == NULL)
		goto out;

	kwargs = Py_BuildValue("{s:s}", "module", PYLIBZFS_MODULE_NAME);
	if (kwargs == NULL)
		goto out;

	enum_mod = PyImport_ImportModule("enum");
	if (enum_mod == NULL)
		goto out;

	int_enum = PyObject_GetAttrString(enum_mod, "IntEnum");
	if (int_enum == NULL)
		goto out;

	intflag_enum = PyObject_GetAttrString(enum_mod, "IntFlag");
	if (intflag_enum == NULL)
		goto out;

	err = add_enum(emod, module, int_enum, "ZFSError",
		       zfs_err_table_to_dict, kwargs, NULL);
	if (err)
		goto out;

	err = add_enum(emod, NULL, int_enum, "ZPOOLStatus",
		       zpool_status_table_to_dict, kwargs,
		       &state->zpool_status_enum);
	if (err)
		goto out;

	err = add_enum(emod, module, int_enum, "ZFSType",
		       zfs_type_table_to_dict, kwargs,
		       &state->zfs_type_enum);
	if (err)
		goto out;

	err = add_enum(emod, NULL, intflag_enum, "ZFSDOSFlag",
		       zfs_dosflag_table_to_dict, kwargs, NULL);
	if (err)
		goto out;

	err = add_enum(emod, module, int_enum, "ZFSProperty",
		       zfs_prop_table_to_dict, kwargs,
		       &state->zfs_property_enum);
	if (err)
		goto out;

	err = add_enum(emod, NULL, int_enum, "ZPOOLProperty",
		       zpool_prop_table_to_dict, kwargs, NULL);
	if (err)
		goto out;

	err = add_enum(emod, module, intflag_enum, "PropertySource",
		       zfs_prop_src_table_to_dict, kwargs,
		       &state->zfs_property_src_enum);
	if (err)
		goto out;

	err = add_enum(emod, NULL, int_enum, "VDevAuxState",
			zfs_vdev_aux_table_to_dict, kwargs, NULL);
	if (err)
		goto out;

	err = add_enum(emod, module, int_enum, "ZFSUserQuota",
		       uquota_table_to_dict, kwargs,
		       &state->zfs_uquota_enum);
	if (err)
		goto out;

	err = add_enum(emod, NULL, int_enum, "VDevState",
		       vdev_state_table_to_dict, kwargs,
		       &state->vdev_state_enum);
	if (err)
		goto out;
out:
	Py_XDECREF(kwargs);
	Py_XDECREF(int_enum);
	Py_XDECREF(intflag_enum);
	Py_XDECREF(enum_mod);
	return err;
}
