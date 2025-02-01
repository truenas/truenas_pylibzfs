#include "../truenas_pylibzfs.h"
#include "py_zfs_iter.h"

#define ZFS_VOLUME_STR "<" PYLIBZFS_MODULE_NAME \
    ".ZFSVolume(name=%U, pool=%U, type=%U)>"

static
PyObject *py_zfs_volume_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_volume_t *self = NULL;
	self = (py_zfs_volume_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_volume_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_volume_dealloc(py_zfs_volume_t *self) {
	free_py_zfs_obj(RSRC_TO_ZFS(self));
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_repr_zfs_volume(PyObject *self)
{
	py_zfs_volume_t *ds = (py_zfs_volume_t *)self;

	return py_repr_zfs_obj_impl(RSRC_TO_ZFS(ds), ZFS_VOLUME_STR);
}

static
PyGetSetDef zfs_volume_getsetters[] = {
	{ .name = NULL }
};

static
PyMethodDef zfs_volume_methods[] = {
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSVolume = {
	.tp_name = PYLIBZFS_MODULE_NAME ".ZFSVolume",
	.tp_basicsize = sizeof (py_zfs_volume_t),
	.tp_methods = zfs_volume_methods,
	.tp_getset = zfs_volume_getsetters,
	.tp_new = py_zfs_volume_new,
	.tp_init = py_zfs_volume_init,
	.tp_doc = "ZFSVolume",
	.tp_dealloc = (destructor)py_zfs_volume_dealloc,
	.tp_repr = py_repr_zfs_volume,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_base = &ZFSResource
};

py_zfs_volume_t *init_zfs_volume(py_zfs_t *lzp, zfs_handle_t *zfsp, boolean_t simple)
{
	py_zfs_volume_t *out = NULL;
	py_zfs_obj_t *obj = NULL;
	const char *ds_name;
	const char *pool_name;
	zfs_type_t zfs_type;
	uint64_t guid, createtxg;

	out = (py_zfs_volume_t *)PyObject_CallFunction((PyObject *)&ZFSVolume, NULL);
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

	PYZFS_ASSERT((zfs_type == ZFS_TYPE_VOLUME), "Incorrect ZFS dataset type");

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
