#ifndef _PY_ZFS_DATASET_H
#define _PY_ZFS_DATASET_H

#include "zfs.h"
#include "py_zfs_resource.h"

#define	ZFS_DATASET_STR	"<libzfs2.ZFSDataset name %s type %s>"

typedef struct {
	py_zfs_resource_t rsrc;
	PyObject *children;
	PyObject *children_recursive;
	PyObject *snapshots;
	PyObject *bookmarks;
	PyObject *snapshots_recursive;
	PyObject *dependents;
	PyObject *mountpoint;
} py_zfs_dataset_t;

PyObject *py_zfs_dataset_str(PyObject *self) {
	py_zfs_dataset_t *ds = (py_zfs_dataset_t *)self;
	if (ds->rsrc.obj.zhp) {
		return (PyUnicode_FromFormat(ZFS_DATASET_STR,
		    zfs_get_name(ds->rsrc.obj.zhp),
		    get_dataset_type(zfs_get_type(ds->rsrc.obj.zhp))));
	} else {
		return (PyUnicode_FromFormat(ZFS_DATASET_STR, "<EMPTY>",
		    "<EMPTY>"));
	}
}

PyObject *py_zfs_dataset_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_dataset_t *self = NULL;
	self = (py_zfs_dataset_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

int py_zfs_dataset_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

void py_zfs_dataset_dealloc(py_zfs_dataset_t *self) {
	Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *py_zfs_dataset_as_dict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_gather_snapshots(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_delete_snapshots(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_destroy_snapshot(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_mount(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_mount_recursive(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_umount(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_umount_recursive(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_send(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_promote(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_snapshot(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_recieve(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_diff(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_get_children(py_zfs_dataset_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_get_children_recursive(py_zfs_dataset_t *self,
    void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_get_snapshots(py_zfs_dataset_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_get_bookmarks(py_zfs_dataset_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_get_snapshots_recursive(py_zfs_dataset_t *self,
    void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_get_dependents(py_zfs_dataset_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_dataset_get_mountpoint(py_zfs_dataset_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyGetSetDef zfs_dataset_getsetters[] = {
	{
		.name	= "children",
		.get	= (getter)py_zfs_dataset_get_children
	},
	{
		.name	= "children_recursive",
		.get	= (getter)py_zfs_dataset_get_children_recursive
	},
	{
		.name	= "snapshots",
		.get	= (getter)py_zfs_dataset_get_snapshots
	},
	{
		.name	= "bookmarks",
		.get	= (getter)py_zfs_dataset_get_bookmarks
	},
	{
		.name	= "snapshots_recursive",
		.get	= (getter)py_zfs_dataset_get_snapshots_recursive
	},
	{
		.name	= "dependents",
		.get	= (getter)py_zfs_dataset_get_dependents
	},
	{
		.name	= "mountpoint",
		.get	= (getter)py_zfs_dataset_get_mountpoint
	},
	{ .name = NULL }
};

PyMethodDef zfs_dataset_methods[] = {
	{
		.ml_name = "asdict",
		.ml_meth = py_zfs_dataset_as_dict,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "__gather_snapshots",
		.ml_meth = py_zfs_dataset_gather_snapshots,
		.ml_flags = METH_VARARGS | METH_CLASS
	},
	{
		.ml_name = "delete_snapshots",
		.ml_meth = py_zfs_dataset_delete_snapshots,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "destroy_snapshot",
		.ml_meth = py_zfs_dataset_destroy_snapshot,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "mount",
		.ml_meth = py_zfs_dataset_mount,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "mount_recursive",
		.ml_meth = py_zfs_dataset_mount_recursive,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "umount",
		.ml_meth = py_zfs_dataset_umount,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "umount_recursive",
		.ml_meth = py_zfs_dataset_umount_recursive,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "send",
		.ml_meth = py_zfs_dataset_send,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "promote",
		.ml_meth = py_zfs_dataset_promote,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "snapshot",
		.ml_meth = py_zfs_dataset_snapshot,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "recieve",
		.ml_meth = py_zfs_dataset_recieve,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "diff",
		.ml_meth = py_zfs_dataset_diff,
		.ml_flags = METH_VARARGS
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSDataset = {
	.tp_name = "ZFSDataset",
	.tp_basicsize = sizeof (py_zfs_dataset_t),
	.tp_methods = zfs_dataset_methods,
	.tp_getset = zfs_dataset_getsetters,
	.tp_new = py_zfs_dataset_new,
	.tp_init = py_zfs_dataset_init,
	.tp_doc = "ZFSDataset",
	.tp_dealloc = (destructor)py_zfs_dataset_dealloc,
	.tp_str = py_zfs_dataset_str,
	.tp_repr = py_zfs_dataset_str,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_base = &ZFSResource
};

#endif /* _PY_ZFS_DATASET_H */
