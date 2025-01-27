#include "truenas_pylibzfs.h"
#include "py_zfs_iter.h"

#define ZFS_DATASET_STR "<" PYLIBZFS_MODULE_NAME \
    ".ZFSDataset(name=%U, pool=%U, type=%U)>"

static
PyObject *py_zfs_dataset_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_dataset_t *self = NULL;
	self = (py_zfs_dataset_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_dataset_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_dataset_dealloc(py_zfs_dataset_t *self) {
	free_py_zfs_obj(RSRC_TO_ZFS(self));
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_repr_zfs_dataset(PyObject *self)
{
	py_zfs_dataset_t *ds = (py_zfs_dataset_t *)self;

	return py_repr_zfs_obj_impl(RSRC_TO_ZFS(ds), ZFS_DATASET_STR);
}

static
PyGetSetDef zfs_dataset_getsetters[] = {
	{ .name = NULL }
};

static
PyMethodDef zfs_dataset_methods[] = {
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSDataset = {
	.tp_name = PYLIBZFS_MODULE_NAME ".ZFSDataset",
	.tp_basicsize = sizeof (py_zfs_dataset_t),
	.tp_methods = zfs_dataset_methods,
	.tp_getset = zfs_dataset_getsetters,
	.tp_new = py_zfs_dataset_new,
	.tp_init = py_zfs_dataset_init,
	.tp_doc = "ZFSDataset",
	.tp_dealloc = (destructor)py_zfs_dataset_dealloc,
	.tp_repr = py_repr_zfs_dataset,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_base = &ZFSResource
};

py_zfs_dataset_t *init_zfs_dataset(py_zfs_t *lzp, zfs_handle_t *zfsp, boolean_t simple)
{
	py_zfs_dataset_t *out = NULL;
	py_zfs_obj_t *obj = NULL;
	const char *ds_name;
	const char *pool_name;
	zfs_type_t zfs_type;
	uint64_t guid, createtxg;

	out = (py_zfs_dataset_t *)PyObject_CallFunction((PyObject *)&ZFSDataset, NULL);
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
