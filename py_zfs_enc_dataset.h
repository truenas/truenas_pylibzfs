#ifndef _PY_ZFS_ENC_DATASET_H
#define _PY_ZFS_ENC_DATASET_H

#include "zfs.h"
#include "py_zfs_dataset.h"

#define	ZFS_ENC_DATASET_STR	"<libzfs2.ZFSEncDataset name %s type %s>"

typedef struct {
	py_zfs_dataset_t ds;
	PyObject *encrypted;
	PyObject *key_location;
	PyObject *encryption_root;
	PyObject *key_loaded;
} py_zfs_enc_dataset_t;

PyObject *py_zfs_enc_dataset_str(PyObject *self) {
	py_zfs_enc_dataset_t *eds = (py_zfs_enc_dataset_t *)self;
	if (eds->ds.rsrc.obj.zhp) {
		return (PyUnicode_FromFormat(ZFS_ENC_DATASET_STR,
		    zfs_get_name(eds->ds.rsrc.obj.zhp),
		    get_dataset_type(zfs_get_type(eds->ds.rsrc.obj.zhp))));
    } else {
		return (PyUnicode_FromFormat(ZFS_ENC_DATASET_STR, "<EMPTY>",
		    "<EMPTY>"));
    }
}

PyObject *py_zfs_enc_dataset_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_enc_dataset_t *self = NULL;
	self = (py_zfs_enc_dataset_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

int py_zfs_enc_dataset_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

void py_zfs_enc_dataset_dealloc(py_zfs_enc_dataset_t *self) {
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *py_zfs_enc_dataset_load_key(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_enc_dataset_unload_key(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_enc_dataset_check_key(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_enc_dataset_change_key(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_enc_dataset_get_encrypted(py_zfs_enc_dataset_t *self,
    void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_enc_dataset_get_key_location(py_zfs_enc_dataset_t *self,
    void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_enc_dataset_get_encrypted_root(py_zfs_enc_dataset_t *self,
    void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_enc_dataset_get_key_loaded(py_zfs_enc_dataset_t *self,
    void *extra) {
	Py_RETURN_NONE;
}

PyGetSetDef zfs_enc_dataset_getsetters[] = {
	{
		.name	= "encrypted",
		.get	= (getter)py_zfs_enc_dataset_get_encrypted,
	},
	{
		.name	= "key_location",
		.get	= (getter)py_zfs_enc_dataset_get_key_location,
	},
	{
		.name	= "encrypted_root",
		.get	= (getter)py_zfs_enc_dataset_get_encrypted_root,
	},
	{
		.name	= "key_loaded",
		.get	= (getter)py_zfs_enc_dataset_get_key_loaded,
	},
	{ .name = NULL }
};

PyMethodDef zfs_enc_dataset_methods[] = {
	{
		.ml_name = "load_key",
		.ml_meth = py_zfs_enc_dataset_load_key,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "unload_key",
		.ml_meth = py_zfs_enc_dataset_unload_key,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "check_key",
		.ml_meth = py_zfs_enc_dataset_check_key,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "change_key",
		.ml_meth = py_zfs_enc_dataset_change_key,
		.ml_flags = METH_VARARGS
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSEncDataset = {
	.tp_name = "ZFSEncDataset",
	.tp_basicsize = sizeof (py_zfs_enc_dataset_t),
	.tp_methods = zfs_enc_dataset_methods,
	.tp_getset = zfs_enc_dataset_getsetters,
	.tp_new = py_zfs_enc_dataset_new,
	.tp_init = py_zfs_enc_dataset_init,
	.tp_doc = "ZFSEncDataset",
	.tp_dealloc = (destructor)py_zfs_enc_dataset_dealloc,
	.tp_str = py_zfs_enc_dataset_str,
	.tp_repr = py_zfs_enc_dataset_str,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_base = &ZFSDataset
};

#endif /* _PY_ZFS_ENC_DATASET_H */
