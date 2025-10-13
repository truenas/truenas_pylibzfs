#include "../truenas_pylibzfs.h"
#include "py_zfs_iter.h"

#define ZFS_SNAP_STR "<" PYLIBZFS_MODULE_NAME \
    ".ZFSSnapshot(name=%U, pool=%U, type=%U)>"

static
PyObject *py_zfs_snapshot_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_snapshot_t *self = NULL;
	self = (py_zfs_snapshot_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_snapshot_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_snapshot_dealloc(py_zfs_snapshot_t *self) {
	free_py_zfs_obj(RSRC_TO_ZFS(self));
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_repr_zfs_snapshot(PyObject *self)
{
	py_zfs_snapshot_t *ds = (py_zfs_snapshot_t *)self;

	return py_repr_zfs_obj_impl(RSRC_TO_ZFS(ds), ZFS_SNAP_STR);
}

PyDoc_STRVAR(py_zfs_snapshot_get_clones__doc__,
"get_clones() -> tuple\n"
"---------------------\n"
"Retrieve tuple of names of ZFS clones. See man 7 zfsconcepts.\n\n"
"A clone is a writable volume or file system whose initial contents are the\n"
"same as another dataset. Snapshot clones have an implicit dependency between\n"
"the parent and the child. This snapshot cannot be destroyed as long as one or\n"
"more clones of it exist. The \"origin\" property exposes this dependency.\n"
"\n"
"parameters:\n"
"-----------\n"
"None\n\n"
"returns:\n"
"--------\n"
"tuple: tuple containing names of ZFS datasets that are clones of this snapshot\n"
);
static
PyObject *py_zfs_snapshot_get_clones(PyObject *self, PyObject *args_unused)
{
	py_zfs_snapshot_t *ds = (py_zfs_snapshot_t *)self;
	nvlist_t *clones = NULL;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(ds->rsrc.obj.pylibzfsp);
	clones = zfs_get_clones_nvl(ds->rsrc.obj.zhp);
	PY_ZFS_UNLOCK(ds->rsrc.obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (clones == NULL)
		return PyTuple_New(0);

	return py_nvlist_names_tuple(clones);
}

PyDoc_STRVAR(py_zfs_snapshot_get_holds__doc__,
"get_holds() -> tuple\n"
"--------------------\n"
"Retrieve tuple of names of ZFS holds on snapshot.\n\n"
"parameters:\n"
"-----------\n"
"None\n\n"
"returns:\n"
"--------\n"
"tuple: tuple containing names of ZFS holds for this snapshot\n"
);
static
PyObject *py_zfs_snapshot_get_holds(PyObject *self, PyObject *args_unused)
{
	py_zfs_snapshot_t *ds = (py_zfs_snapshot_t *)self;
	nvlist_t *holds = NULL;
	py_zfs_error_t zfs_err;
	PyObject *out = NULL;
	int err;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(ds->rsrc.obj.pylibzfsp);
	err = zfs_get_holds(ds->rsrc.obj.zhp, &holds);
	if (err) {
		py_get_zfs_error(ds->rsrc.obj.pylibzfsp->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(ds->rsrc.obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_get_holds() failed");
		return NULL;
	}
	if (holds == NULL)
		return PyTuple_New(0);

	out = py_nvlist_names_tuple(holds);
	fnvlist_free(holds);
	return out;
}

PyDoc_STRVAR(py_zfs_snapshot_clone__doc__,
"clone(*, name, properties=None) -> None\n"
"---------------------------------------\n"
"Clone snapshot of ZFS resource. See the Clones section of zfsconcepts(7) for details.\n\n"
"Parameters:\n"
"-----------\n"
"name: str\n\n"
"    Name of the target resource to create based on this snapshot. NOTE: must be\n"
"    located on the same ZFS pool.\n"
"\n"
"properties: dict | truenas_pylibzfs.struct_zfs_props, optional\n"
"    Properties and values to set. This can be formatted as either\n\n"
"    a dictionary with the form: \n"
"    `{key: value}`\n"
"    or the form: \n"
"    `{key: {\"raw\": value, \"value\": value}}`\n"
"    in this case preference is given to the raw value\n"
"\n"
"    Alternatively the properties may also be provided in the form\n"
"    of a struct_zfs_props instance as returned by get_properties()\n"
"\n"
"Returns:\n"
"--------\n"
"None\n"
"\n"
"Raises:\n"
"-------\n"
"ZFSException:\n"
"    The clone operation failed. This can happen for a variety of reasons.\n"
"TypeError:\n"
"    Properties were specified and were not one of supported types documented above\n"
"ValueError:\n"
"    One of the specified properties is not supported for the ZFS type of the\n"
"    ZFSResource of which this snapshot was taken. For example, setting a zvol\n"
"    property on a clone of a snapshot of a dataset.\n"
);
static
PyObject *py_zfs_snapshot_clone(PyObject *self,	PyObject *args, PyObject *kwargs)
{
	py_zfs_snapshot_t *ds = (py_zfs_snapshot_t *)self;
	py_zfs_error_t zfs_err;
	int err;
	char *kwnames [] = {"name", "properties", NULL};
	nvlist_t *nvl = NULL;
	PyObject *pyprops = NULL;
	PyObject *conv_str = NULL;
	const char *cname = NULL;
	pylibzfs_state_t *state = NULL;


	if (!PyArg_ParseTupleAndKeywords(args, kwargs,
					 "|$sO",
					 kwnames,
					 &cname,
					 &pyprops)) {
					 return NULL;
	}

	if (cname == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"name keyword argument is required.");
		return NULL;
	}

	if (pyprops) {
		nvl = py_zfsprops_to_nvlist(state,
					    pyprops,
					    ds->rsrc.obj.ctype,
					    B_FALSE);
		if (nvl == NULL) {
			return NULL;
		}
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME
			".ZFSSnapshot.clone", "OO",
			ds->rsrc.obj.name, kwargs) < 0) {
		fnvlist_free(nvl);
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(ds->rsrc.obj.pylibzfsp);
	err = zfs_clone(ds->rsrc.obj.zhp, cname, nvl);
	if (err) {
		py_get_zfs_error(ds->rsrc.obj.pylibzfsp->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(ds->rsrc.obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_clone() failed");
		fnvlist_free(nvl);
		return NULL;
	}

	/* clone operation succeeded. Write history (including properties) */
	if (nvl) {
		conv_str = py_dump_nvlist(nvl, B_TRUE);
	}

	Py_BEGIN_ALLOW_THREADS
	fnvlist_free(nvl);
	Py_END_ALLOW_THREADS


	if (conv_str) {
		const char *json = PyUnicode_AsUTF8(conv_str);
		err = py_log_history_fmt(ds->rsrc.obj.pylibzfsp,
					 "zfs clone %s -> %s with properties: %s",
					 zfs_get_name(ds->rsrc.obj.zhp),
					 cname, json ? json : "UNKNOWN");
	} else {
		err = py_log_history_fmt(ds->rsrc.obj.pylibzfsp,
					 "zfs clone %s -> %s",
					 zfs_get_name(ds->rsrc.obj.zhp), cname);
	}

	Py_XDECREF(conv_str);

	// We may have encountered an error generating history message
	if (err)
		return NULL;

	Py_RETURN_NONE;
}

static
PyGetSetDef zfs_snapshot_getsetters[] = {
	{ .name = NULL }
};

static
PyMethodDef zfs_snapshot_methods[] = {
	{
		.ml_name = "get_holds",
		.ml_meth = (PyCFunction)py_zfs_snapshot_get_holds,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_snapshot_get_holds__doc__
	},
	{
		.ml_name = "get_clones",
		.ml_meth = (PyCFunction)py_zfs_snapshot_get_clones,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_snapshot_get_clones__doc__
	},
	{
		.ml_name = "clone",
		.ml_meth = (PyCFunction)py_zfs_snapshot_clone,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_snapshot_clone__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSSnapshot = {
	.tp_name = PYLIBZFS_MODULE_NAME ".ZFSSnapshot",
	.tp_basicsize = sizeof (py_zfs_snapshot_t),
	.tp_methods = zfs_snapshot_methods,
	.tp_getset = zfs_snapshot_getsetters,
	.tp_new = py_zfs_snapshot_new,
	.tp_init = py_zfs_snapshot_init,
	.tp_doc = "ZFSSnapshot",
	.tp_dealloc = (destructor)py_zfs_snapshot_dealloc,
	.tp_repr = py_repr_zfs_snapshot,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_base = &ZFSResource
};

py_zfs_snapshot_t *init_zfs_snapshot(py_zfs_t *lzp, zfs_handle_t *zfsp, boolean_t simple)
{
	py_zfs_snapshot_t *out = NULL;
	py_zfs_obj_t *obj = NULL;
	const char *ds_name;
	const char *pool_name;
	zfs_type_t zfs_type;
	uint64_t guid, createtxg;
	boolean_t is_encrypted = B_FALSE;

	out = (py_zfs_snapshot_t *)PyObject_CallFunction((PyObject *)&ZFSSnapshot, NULL);
	if (out == NULL) {
		return NULL;
	}
	out->rsrc.is_simple = simple;
	obj = RSRC_TO_ZFS(out);
	obj->pylibzfsp = lzp;
	Py_INCREF(lzp);

	Py_BEGIN_ALLOW_THREADS
	ds_name = zfs_get_name(zfsp);
	zfs_type = zfs_get_type(zfsp);
	pool_name = zfs_get_pool_name(zfsp);
	guid = zfs_prop_get_int(zfsp, ZFS_PROP_GUID);
	createtxg = zfs_prop_get_int(zfsp, ZFS_PROP_CREATETXG);
	is_encrypted = zfs_is_encrypted(zfsp);
	Py_END_ALLOW_THREADS

	PYZFS_ASSERT((zfs_type == ZFS_TYPE_SNAPSHOT), "Incorrect ZFS type");

	obj->name = PyUnicode_FromString(ds_name);
	if (obj->name == NULL)
		goto error;

	obj->pool_name = PyUnicode_FromString(pool_name);
	if (obj->pool_name == NULL)
		goto error;

	obj->ctype = zfs_type;
	obj->type_enum = py_get_zfs_type(lzp, zfs_type, &obj->type);
	obj->guid = Py_BuildValue("k", guid);
	if (obj->guid == NULL)
		goto error;

	obj->createtxg = Py_BuildValue("k", createtxg);
	if (obj->createtxg == NULL)
		goto error;

	obj->encrypted = Py_NewRef(is_encrypted ? Py_True : Py_False);
	obj->zhp = zfsp;
	return out;

error:
	// This deallocates the new object and decrements refcnt on pylibzfsp
	Py_DECREF(out);
	return NULL;
}
