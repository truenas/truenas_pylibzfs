#include "../truenas_pylibzfs.h"

#define ZFS_OBJECT_STR "<" PYLIBZFS_MODULE_NAME \
    ".ZFSObject(name=%U, pool=%U, type=%U)>"

static
PyObject *py_zfs_obj_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_obj_t *self = NULL;
	self = (py_zfs_obj_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_obj_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

/*
 * This needs to be public so that zfs dataset, volume, etc
 * types can properly clean up in their destructor functions.
 */
void free_py_zfs_obj(py_zfs_obj_t *self)
{
	if (self->zhp != NULL) {
		Py_BEGIN_ALLOW_THREADS
		zfs_close(self->zhp);
		Py_END_ALLOW_THREADS
		self->zhp = NULL;
	}
	Py_CLEAR(self->name);
	Py_CLEAR(self->pool_name);
	Py_CLEAR(self->type);
	Py_CLEAR(self->type_enum);
	Py_CLEAR(self->pylibzfsp);
	Py_CLEAR(self->guid);
	Py_CLEAR(self->createtxg);
}

static
void py_zfs_obj_dealloc(py_zfs_obj_t *self) {
	free_py_zfs_obj(self);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_repr_zfs_object(PyObject *self)
{
	py_zfs_obj_t *obj = (py_zfs_obj_t *)self;

	return py_repr_zfs_obj_impl(obj, ZFS_OBJECT_STR);
}

PyDoc_STRVAR(py_zfs_obj_rename__doc__,
"rename(*, new_name, recursive=False, no_unmount=False, force_unmount=False) -> None\n"
"-----------------------------------------------------------------------------------\n"
"Rename the ZFS object.\n\n"
"Parameters\n"
"----------\n"
"new_name: str\n"
"    New name for ZFS object. The new name may not change the pool name component\n"
"    of the original name and contain alphanumeric characters and the following\n"
"    special characters:\n\n"
"    * Underscore (_)\n"
"    * Hyphen (-)\n"
"    * Colon (:)\n"
"    * Period (.)\n"
"\n"
"    The name length may not exceed 255 bytes, but it is generally advisable\n"
"    to limit the length to something significantly less than the absolute\n"
"    name length limit.\n\n"
""
"recursive: bool, optional, default=False\n"
"    Recursively rename the snapshots of all descendent datasets. Snapshots\n"
"    are the only dataset that can be renamed recursively.\n\n"
""
"no_unmount: bool, optional, default=False\n"
"    Do not remount file systems during rename. If a file system's mountpoint\n"
"    property is set to legacy or none, the file system is not unmounted even\n"
"    if this option is False (default).\n\n"
""
"force_unmount: bool, optional, default=False\n"
"    Force unmount any file systems that need to be unmounted in the process.\n\n"
""
"Returns\n"
"-------\n"
"    None\n\n"
""
"Raises\n"
"------\n"
"ValueError:\n"
"    The ZFS name is for the underlying ZFS type.\n\n"
"ValueError:\n"
"    Recursive specified when ZFS type is not a snapshot.\n\n"
"ValueError:\n"
"    The combination of parameters supplied to this method are invalid.\n\n"
"ZFSError:\n"
"    The rename operation failed.\n\n"
);
static
PyObject *py_zfs_obj_rename(PyObject *self,
			    PyObject *args_unused,
			    PyObject *kwargs)
{
	py_zfs_obj_t *obj = (py_zfs_obj_t *)self;
	int err, recursive, nounmount, forceunmount;
	char *new_name = NULL;
	char orig_name[ZFS_MAX_DATASET_NAME_LEN];
	py_zfs_error_t zfs_err;
	renameflags_t flags;

	char *kwnames [] = {
		"new_name",
		"recursive",
		"no_unmount",
		"force_unmount",
		NULL
	};

	recursive = nounmount = forceunmount = 0;

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$sppp",
					 kwnames,
					 &new_name,
					 &recursive,
					 &nounmount,
					 &forceunmount)) {
		return NULL;
	}

	if (new_name == NULL) {
		PyErr_SetString(PyExc_ValueError,
			"new_name keyword argument is required.");
		return (NULL);
	} else if (strcmp(zfs_get_name(obj->zhp), new_name) == 0) {
		PyErr_SetString(PyExc_ValueError,
			"new_name must differ from current name.");
		return NULL;
	} else if (!zfs_name_valid(new_name, obj->ctype)) {
		PyErr_SetString(PyExc_ValueError,
			"new_name is not valid for the ZFS type.");
		return NULL;
	}

	if (nounmount && forceunmount) {
		PyErr_SetString(PyExc_ValueError,
				"Force unmount and no unmount options "
				"may not be specified simultaneosly.");
		return NULL;
	}

	if (recursive && (obj->ctype != ZFS_TYPE_SNAPSHOT)) {
		PyErr_SetString(PyExc_ValueError,
				"Recursive is only valid for snapshot "
				"renames.");
		return NULL;
	}

	flags = (renameflags_t) {
		.recursive = recursive,
		.nounmount = nounmount,
		.forceunmount = forceunmount
	};

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSObject.rename", "OO",
			obj->name, kwargs) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(obj->pylibzfsp);

	// Make a copy of the original name for logging history
	strlcpy(orig_name, zfs_get_name(obj->zhp), sizeof(orig_name));

	err = zfs_rename(obj->zhp, new_name, flags);
	if (err) {
		py_get_zfs_error(obj->pylibzfsp->lzh, &zfs_err);
	}

	PY_ZFS_UNLOCK(obj->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_rename() failed");
		return NULL;
	} else {
		err = py_log_history_fmt(obj->pylibzfsp,
					 "zfs rename %s%s%s%s -> %s",
					 forceunmount ? "-f ": "",
					 nounmount ? "-u ": "",
					 recursive ? "-r ": "",
					 orig_name, new_name);
	}

	// swap out our name with new one even if we failed to write history
	Py_CLEAR(obj->name);
	obj->name = PyUnicode_FromString(new_name);

	if (err) {
		// writing the zpool history failed which means
		// we have an exception set
		return NULL;
	}

	Py_RETURN_NONE;
}

static
PyObject *py_get_prop(PyObject *obj)
{
	if (obj == NULL) {
		Py_RETURN_NONE;
	}

	return Py_NewRef(obj);
}

PyDoc_STRVAR(py_zfs_obj_name__doc__,
"Name of the ZFS object\n"
);
static
PyObject *py_zfs_obj_get_name(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->name);
}

PyDoc_STRVAR(py_zfs_obj_type__doc__,
"Dataset type underlying the ZFS object\n\n"
"This will be one of \"FILESYSTEM\", \"VOLUME\", \"SNAPSHOT\", "
"or \"BOOKMARK\".\n"
);
static
PyObject *py_zfs_obj_get_type(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->type_enum);
}

PyDoc_STRVAR(py_zfs_obj_guid__doc__,
"GUID of the ZFS object\n\n"
"The 64 bit GUID of this dataset or bookmark which does not change over its\n"
"entire lifetime. When a snapshot is sent to another pool, the received\n"
"snapshot has the same GUID.  Thus, the guid is suitable to identify a\n"
"snapshot across pools.\n"
);
static
PyObject *py_zfs_obj_get_guid(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->guid);
}

PyDoc_STRVAR(py_zfs_obj_createtxg__doc__,
"Transaction group in which ZFS object was created\n\n"
"Bookmarks have the same createtxg as the snapshot they are initially tied to.\n"
"This property is suitable for ordering a list of snapshots, e.g. for\n"
"incremental send and receive, and for slicing ranges of snapshots for\n"
"iteration.\n"
);
static
PyObject *py_zfs_obj_get_createtxg(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->createtxg);
}

PyDoc_STRVAR(py_zfs_obj_pool_name__doc__,
"Name of the ZFS pool of which this ZFS object is a member.\n"
);
static
PyObject *py_zfs_obj_get_pool(py_zfs_obj_t *self, void *extra) {
	return py_get_prop(self->pool_name);
}

static
PyGetSetDef zfs_obj_getsetters[] = {
	{
		.name	= "name",
		.get	= (getter)py_zfs_obj_get_name,
		.doc	= py_zfs_obj_name__doc__,
	},
	{
		.name	= "type",
		.get	= (getter)py_zfs_obj_get_type,
		.doc	= py_zfs_obj_type__doc__,
	},
	{
		.name	= "guid",
		.get	= (getter)py_zfs_obj_get_guid,
		.doc	= py_zfs_obj_guid__doc__,
	},
	{
		.name	= "createtxg",
		.get	= (getter)py_zfs_obj_get_createtxg,
		.doc	= py_zfs_obj_createtxg__doc__,
	},
	{
		.name	= "pool_name",
		.get	= (getter)py_zfs_obj_get_pool,
		.doc	= py_zfs_obj_pool_name__doc__,
	},
	{ .name = NULL }
};

static
PyMethodDef zfs_obj_methods[] = {
	{
		.ml_name = "rename",
		.ml_meth = (PyCFunction)py_zfs_obj_rename,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_obj_rename__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSObject = {
	.tp_name = "ZFSObject",
	.tp_basicsize = sizeof (py_zfs_obj_t),
	.tp_methods = zfs_obj_methods,
	.tp_getset = zfs_obj_getsetters,
	.tp_new = py_zfs_obj_new,
	.tp_init = py_zfs_obj_init,
	.tp_doc = "ZFSObject",
	.tp_dealloc = (destructor)py_zfs_obj_dealloc,
	.tp_repr = py_repr_zfs_object,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};
