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

	obj->zhp = zfsp;
	return out;

error:
	// This deallocates the new object and decrements refcnt on pylibzfsp
	Py_DECREF(out);
	return NULL;
}
