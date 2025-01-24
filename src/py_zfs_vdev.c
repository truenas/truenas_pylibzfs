#include "truenas_pylibzfs.h"

#define ZFS_VDEV_STR_PATH "<" PYLIBZFS_MODULE_NAME ".ZFSVdev(type=%U, path=%U)>"
#define ZFS_VDEV_STR "<" PYLIBZFS_MODULE_NAME ".ZFSVdev(type=%U)"

static
PyObject *py_repr_zfs_vdev(PyObject *self) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *) self;
	PyObject *out;
	if (v->path)
		out = PyUnicode_FromFormat(ZFS_VDEV_STR_PATH, v->type, v->path);
	else
		out = PyUnicode_FromFormat(ZFS_VDEV_STR, v->type);
	return (out);
}

static
PyObject *py_zfs_vdev_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_vdev_t *self = NULL;
	self = (py_zfs_vdev_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_vdev_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_vdev_dealloc(py_zfs_vdev_t *self) {
	Py_CLEAR(self->pool);
	Py_CLEAR(self->parent);
	Py_CLEAR(self->path);
	Py_CLEAR(self->type);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_zfs_vdev_asdict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_vdev_type__doc__,
"Returns the type of VDEV in string format.\n\n"
"This can be either of \"root\", \"mirror\", \"replacing\", \"raidz\", \n"
"\"draid\", \"disk\", \"file\", \"missing\",\"hole\", \"spare\", \"log\", \n"
"\"l2cache\", \"indirect\" or \"dspare\".\n"
);
static
PyObject *py_zfs_vdev_get_type(py_zfs_vdev_t *self, void *extra) {
	if (self->type == NULL)
		Py_RETURN_NONE;
	return (Py_NewRef(self->type));
}

PyDoc_STRVAR(py_zfs_vdev_path__doc__,
"Returns the path of VDEV if it exists in nvlist config of VDEV.\n\n"
"If path does not exist, for example in case of root, mirror, raidz VDEVs,\n"
"None is returned. Otherwise path of VDEV is returned in string format."
);
static
PyObject *py_zfs_vdev_get_path(py_zfs_vdev_t *self, void *extra) {
	if (self->path == NULL)
		Py_RETURN_NONE;
	return (Py_NewRef(self->path));
}

PyDoc_STRVAR(py_zfs_vdev_get_name__doc__,
"name(*) -> string\n\n"
"-----------------\n\n"
"Returns the name of the VDEV.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"String\n"
"    Value indicates the name of VDEV used in VDEV nvlist config.\n\n"
"Raises:\n"
"-------\n"
"PyErr_NoMemory:\n"
"    If libzfs fails to allocate memory for returning the path,\n"
"    PyErr_NoMemory will be raised.\n"
);
static
PyObject *py_zfs_vdev_get_name(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	PyObject *out = NULL;
	char *name = NULL;

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(v->pool->pylibzfsp);
	name = zpool_vdev_name(v->pool->pylibzfsp->lzh, v->pool->zhp,
	    v->vdev_tree, VDEV_NAME_TYPE_ID);
	PY_ZFS_UNLOCK(v->pool->pylibzfsp);
	Py_END_ALLOW_THREADS

	if (name == NULL)
		return PyErr_NoMemory();

	out = PyUnicode_FromString(name);
	free(name);
	return (out);
}

PyDoc_STRVAR(py_zfs_vdev_get_guid__doc__,
"guid(*) -> Int\n\n"
"--------------\n\n"
"Returns current VDEV GUID.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"Int\n"
"    Value indicates the GUID of VDEV.\n\n"
);
static
PyObject *py_zfs_vdev_get_guid(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	uint64_t guid;

	Py_BEGIN_ALLOW_THREADS
	guid = fnvlist_lookup_uint64(v->vdev_tree, ZPOOL_CONFIG_GUID);
	Py_END_ALLOW_THREADS

	return (PyLong_FromUnsignedLongLong(guid));
}

static
const char *vdev_get_status_impl(nvlist_t *tree) {
	vdev_stat_t *vs;
	uint_t c;
	const char *state = NULL;
	Py_BEGIN_ALLOW_THREADS
	if (nvlist_lookup_uint64_array(tree, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0) {
		state = zpool_state_to_name(vs->vs_state, vs->vs_aux);
	}
	Py_END_ALLOW_THREADS
	return (state);
}

PyDoc_STRVAR(py_zfs_vdev_get_status__doc__,
"status(*) -> String\n\n"
"-------------------\n\n"
"Returns the status of the VDEV.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"String\n"
"    Value indicates the state of VDEV used in VDEV nvlist config. If state\n"
"    of VDEV is not found in nvlist config for VDEV, None is returned.\n\n"
);
static
PyObject *py_zfs_vdev_get_status(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	const char *state = vdev_get_status_impl(v->vdev_tree);
	if (state == NULL)
		Py_RETURN_NONE;
	return (PyUnicode_FromString(state));
}

PyDoc_STRVAR(py_zfs_vdev_get_size__doc__,
"size(*) -> Int\n\n"
"--------------\n\n"
"Returns the size of the VDEV.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"Int\n"
"    Value indicates the size of VDEV.\n\n"
);
static
PyObject *py_zfs_vdev_get_size(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	uint64_t asize = 0;
	uint64_t ashift = 0;
	uint64_t size;
	boolean_t found;

	Py_BEGIN_ALLOW_THREADS
	found = (nvlist_lookup_uint64(v->vdev_tree, ZPOOL_CONFIG_ASIZE,
	    &asize) == 0 && nvlist_lookup_uint64(v->vdev_tree,
	    ZPOOL_CONFIG_ASHIFT, &ashift) == 0);
	Py_END_ALLOW_THREADS

	if (!found)
		Py_RETURN_NONE;
	size = asize << ashift;
	return (PyLong_FromUnsignedLongLong(size));
}

PyDoc_STRVAR(py_zfs_vdev_get_children__doc__,
"children(*) -> Tuple\n\n"
"--------------------\n\n"
"Returns ZFSVdev objects for child VDEVs in a Tuple.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"Tuple\n"
"    Tuple contains ZFSVDev objects for child VDEVs under current VDEV.\n\n"
);
static
PyObject *py_zfs_vdev_get_children(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	nvlist_t  **child;
	uint_t children;
	boolean_t found;
	PyObject *list, *obj;

	Py_BEGIN_ALLOW_THREADS
	found = (nvlist_lookup_nvlist_array(v->vdev_tree, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0);
	Py_END_ALLOW_THREADS

	if (!found)
		Py_RETURN_NONE;

	list = PyTuple_New(children);
	PYZFS_ASSERT(list, "Failed to create Python list object.");
	for (uint_t i = 0; i < children; ++i) {
		obj = (PyObject *)init_zfs_vdev(v->pool, child[i], self);
		PYZFS_ASSERT(obj, "Failed to create ZFSVdev child object.");
		PyTuple_SET_ITEM(list, i, obj);
	}

	return (list);
}

static
void vdev_get_disks_impl(PyObject *list, nvlist_t *tree) {
	const char *ctype;
	const char *cstate;
	const char *cpath;
	PyObject *path;
	nvlist_t **child;
	uint_t children;
	boolean_t found;

	cstate = vdev_get_status_impl(tree);
	if (strcmp(cstate, "UNAVAIL") == 0 || strcmp(cstate, "OFFLINE") == 0)
		return;
	Py_BEGIN_ALLOW_THREADS
	ctype = fnvlist_lookup_string(tree, ZPOOL_CONFIG_TYPE);
	Py_END_ALLOW_THREADS
	if (strcmp(ctype, VDEV_TYPE_FILE) == 0)
		return;
	else if (strcmp(ctype, VDEV_TYPE_DISK) == 0) {
		Py_BEGIN_ALLOW_THREADS
		cpath = fnvlist_lookup_string(tree, ZPOOL_CONFIG_PATH);
		Py_END_ALLOW_THREADS
		path = PyUnicode_FromString(cpath);
		PYZFS_ASSERT((PyList_Append(list, path) == 0),
		    "Failed to add disk path to Python list.");
		return;
	} else {
		Py_BEGIN_ALLOW_THREADS
		found = (nvlist_lookup_nvlist_array(tree, ZPOOL_CONFIG_CHILDREN,
		    &child, &children) == 0);
		Py_END_ALLOW_THREADS
		if (found) {
			for (uint_t i = 0; i < children; ++i) {
				vdev_get_disks_impl(list, child[i]);
			}
		}
	}
	return;
}

PyDoc_STRVAR(py_zfs_vdev_get_disks__doc__,
"disks(*) -> Tuple\n\n"
"-----------------\n\n"
"Returns a Tuple containing path for all DISK type VDEVs.\n\n"
"Parameters\n"
"----------\n"
"None\n\n"
"Returns\n"
"-------\n"
"Tuple\n"
"    Tuple contains the path of all DISK VDEVs in String format.\n\n"
);
static
PyObject *py_zfs_vdev_get_disks(PyObject *self, PyObject *args) {
	py_zfs_vdev_t *v = (py_zfs_vdev_t *)self;
	PyObject *list;
	PyObject *tuple;

	list = PyList_New(0);
	PYZFS_ASSERT(list, "Failed to create Python list object.");
	vdev_get_disks_impl(list, v->vdev_tree);
	tuple = PyList_AsTuple(list);
	Py_DECREF(list);

	return (tuple);
}

static
PyGetSetDef zfs_vdev_getsetters[] = {
	{
		.name	= "type",
		.get	= (getter)py_zfs_vdev_get_type,
		.doc	= py_zfs_vdev_type__doc__
	},
	{
		.name	= "path",
		.get	= (getter)py_zfs_vdev_get_path,
		.doc	= py_zfs_vdev_path__doc__
	},
	{ .name = NULL }
};

static
PyMethodDef zfs_vdev_methods[] = {
	{
		.ml_name = "asdict",
		.ml_meth = py_zfs_vdev_asdict,
		.ml_flags = METH_VARARGS | METH_KEYWORDS
	},
	{
		.ml_name = "name",
		.ml_meth = py_zfs_vdev_get_name,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_name__doc__
	},
	{
		.ml_name = "guid",
		.ml_meth = py_zfs_vdev_get_guid,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_guid__doc__
	},
	{
		.ml_name = "status",
		.ml_meth = py_zfs_vdev_get_status,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_status__doc__
	},
	{
		.ml_name = "size",
		.ml_meth = py_zfs_vdev_get_size,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_size__doc__
	},
	{
		.ml_name = "children",
		.ml_meth = py_zfs_vdev_get_children,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_children__doc__
	},
	{
		.ml_name = "disks",
		.ml_meth = py_zfs_vdev_get_disks,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_vdev_get_disks__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSVdev = {
	.tp_name = "ZFSVdev",
	.tp_basicsize = sizeof (py_zfs_vdev_t),
	.tp_methods = zfs_vdev_methods,
	.tp_getset = zfs_vdev_getsetters,
	.tp_new = py_zfs_vdev_new,
	.tp_init = py_zfs_vdev_init,
	.tp_doc = "ZFSVdev",
	.tp_dealloc = (destructor)py_zfs_vdev_dealloc,
	.tp_repr = py_repr_zfs_vdev,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE
};

py_zfs_vdev_t *init_zfs_vdev(py_zfs_pool_t *pool, nvlist_t *tree,
    PyObject *parent)
{
	py_zfs_vdev_t *out = NULL;
	const char *type;
	const char *path;
	boolean_t path_found;

	out = (py_zfs_vdev_t *)PyObject_CallFunction((PyObject *)&ZFSVdev, NULL);
	if (out == NULL) {
		return (NULL);
	}

	out->pool = pool;
	Py_INCREF(pool);
	out->parent = parent;
	Py_XINCREF(parent);
	out->vdev_tree = tree;

	Py_BEGIN_ALLOW_THREADS
	/*
	 * force nvlist wrappers wrap nvlist_* functions with asserions that
	 * operation was successful. We can avoid checking for success with
	 * these functions, since an assert would be hit in case of failure.
	 */
	type = fnvlist_lookup_string(tree, ZPOOL_CONFIG_TYPE);
	path_found = nvlist_lookup_string(tree, ZPOOL_CONFIG_PATH, &path) == 0;
	Py_END_ALLOW_THREADS

	if (strncmp(type, VDEV_TYPE_RAIDZ, strlen(VDEV_TYPE_RAIDZ)) == 0) {
		int parity;
		Py_BEGIN_ALLOW_THREADS
		parity = fnvlist_lookup_uint64(tree, ZPOOL_CONFIG_NPARITY);
		Py_END_ALLOW_THREADS
		out->type = PyUnicode_FromFormat("%s%d", type, parity);
	} else
		out->type = PyUnicode_FromString(type);
	if (out->type == NULL) {
		Py_DECREF(out);
		return (NULL);
	}

	if (path_found)
		out->path = PyUnicode_FromString(path);
	else
		out->path = NULL;

	return (out);
}
