#include "truenas_pylibzfs.h"
#include <sys/mntent.h>
#include <sys/mount.h>

static PyTypeObject *alltypes[] = {
	&ZFS,
	&ZFSCrypto,
	&ZFSDataset,
	&ZFSEventIterator,
	&ZFSHistoryIterator,
	&ZFSObject,
	&ZFSPool,
	&ZFSSnapshot,
	&ZFSVolume,
	NULL
};

static void add_constants(PyObject *m) {

#define ADD_INT_CONSTANT(val) PyModule_AddIntConstant(m, #val, val)
#define ADD_STR_CONSTANT(val) PyModule_AddStringConstant(m, #val, val);

	ADD_INT_CONSTANT(ZPL_VERSION);
	ADD_INT_CONSTANT(L2ARC_PERSISTENT_VERSION);
	ADD_INT_CONSTANT(ZFS_MAX_DATASET_NAME_LEN);
	ADD_INT_CONSTANT(ZFS_IOC_GETDOSFLAGS);
	ADD_INT_CONSTANT(ZFS_IOC_SETDOSFLAGS);

	ADD_STR_CONSTANT(ZPOOL_CACHE_BOOT);
	ADD_STR_CONSTANT(ZPOOL_CACHE);
	ADD_STR_CONSTANT(ZFS_DEV);
	ADD_STR_CONSTANT(MNTOPT_ATIME);
	ADD_STR_CONSTANT(MNTOPT_NOATIME);
	ADD_STR_CONSTANT(MNTOPT_EXEC);
	ADD_STR_CONSTANT(MNTOPT_NOEXEC);
	ADD_STR_CONSTANT(MNTOPT_SUID);
	ADD_STR_CONSTANT(MNTOPT_NOSUID);
	ADD_STR_CONSTANT(MNTOPT_DEVICES);
	ADD_STR_CONSTANT(MNTOPT_NODEVICES);
	ADD_STR_CONSTANT(MNTOPT_RO);
	ADD_STR_CONSTANT(MNTOPT_RW);
	ADD_STR_CONSTANT(MNTOPT_RELATIME);
	ADD_STR_CONSTANT(MNTOPT_NORELATIME);
	ADD_STR_CONSTANT(MNTOPT_XATTR);
	ADD_STR_CONSTANT(MNTOPT_NOXATTR);
	ADD_STR_CONSTANT(LIBZFS_NONE_VALUE);
	ADD_STR_CONSTANT(LIBZFS_INCONSISTENT_VALUE);
	ADD_STR_CONSTANT(LIBZFS_IOERROR_VALUE);
}

static int types_ready(PyObject *m) {
	int i, j;

	for (i = 0; alltypes[i]; i++) {
		if (PyType_Ready(alltypes[i]) < 0) {
			for (j = 0; j <= i; j++) {
				Py_DECREF(alltypes[j]);
			}
			return -1;
		}
	}

	return 0;
}

PyDoc_STRVAR(py_get_libzfs_handle__doc__,
"open_handle(*, history=True, history_prefix=\"truenas_pylibzfs:\", "
"mnttab_cache=True) -> bool\n\n"
"--------------------------------------------------------\n\n"
"Open a python libzfs handle. Arguments are keyword-only\n\n"
"Parameters\n"
"----------\n"
"history: bool, optional, default=True\n"
"    Optional boolean argument to generate history entries.\n"
"    Defaults to True (write zpool history)\n"
""
"history_prefix: str, optional, default=\"truenas_pylibzfs:\"\n"
"    Optional string to prefix to all history entries. This is useful\n"
"    for tracking origin of the history entry.\n"
""
"mnttab_cache: bool, optional, default=True\n"
"    Option boolean argument to determine whether to cache the mnttab\n"
"    within the libzfs handle. Defaults to True.\n\n"
""
"Returns\n"
"-------\n"
"new truenas_pylibzfs.ZFS object\n\n"
""
"Raises:\n"
"-------\n"
"truenas_pylibzfs.ZFSError:\n"
"    A libzfs error occurred while trying to open the underlying\n"
"    libzfs_handle_t handle.\n"
);
static PyObject *py_get_libzfs_handle(PyObject *self,
				      PyObject *args,
				      PyObject *kwargs)
{
	py_zfs_t *out = NULL;

	out = (py_zfs_t *)PyObject_Call((PyObject *)&ZFS, args, kwargs);
	if (out == NULL)
		return NULL;

	out->module = self;
	Py_INCREF(self);
	return (PyObject *)out;
}


PyDoc_STRVAR(py_fzfs_rewrite__doc__,
"fzfs_rewrite(fd, *, offset=0, length=0, physical=True) -> None\n"
"---------------------------------------------------------------\n\n"
"Rewrite a range of file as-is without modification.\n"
"----------\n"
"Parameters\n"
"----------\n"
"fd: int, required\n"
"    open file descriptor of file to rewrite.\n"
""
"offset: int, optional, default=0\n"
"    Offset of the range to rewrite.\n"
""
"length: int, optional, default=0\n"
"    Data length to rewrite. 0 means to end of file.\n"
""
"physical: bool, optional, default=True\n"
"    Perform physical rewrite, preserving logical birth time of blocks.\n"
"    This avoids unnecessary inclusion in incremental streams. Physical\n"
"    rewrite requires the physical_rewrite feature to be enabled on the pool\n"
""
"Returns\n"
"-------\n"
"    None\n\n"
""
"NOTE:\n"
"Rewrite works by replacing an existing block with a new block of the same\n"
"logical size. Changed dataset properties that operate on the data or\n"
"metadata without changing the logical size will be applied. These include\n"
"\"checksum\", \"compression\", \"dedup\", and \"copies\". Changes to\n"
"properties that affect the size of a logical block, like \"recordsize\",\n"
"will have no effect.\n\n"
"Rewrite of cloned blocks and blocks that are part of any snapshots, same\n"
"as some property changes may increase pool space usage. Holes that were\n"
"never written or were previously zero-compressed are not rewritten and\n"
"will remain holes even if compression is disabled.\n"
);
static PyObject *py_fzfs_rewrite(PyObject *self,
				PyObject *args,
				PyObject *kwargs)
{
	zfs_rewrite_args_t rewrite_args = {0};
	boolean_t physical = B_TRUE;
	int fd, err, async_err = 0;
	char *kwlist[] = {"fd", "offset", "length", "physical", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs,
					 "i|$KKp",
					 kwlist,
					 &fd,
					 &rewrite_args.off,
					 &rewrite_args.len,
					 &physical)) {
		return NULL;
	}

	if (physical)
		rewrite_args.flags = ZFS_REWRITE_PHYSICAL;

	do {
		Py_BEGIN_ALLOW_THREADS
		err = ioctl(fd, ZFS_IOC_REWRITE, &rewrite_args);
		Py_END_ALLOW_THREADS
	} while (((err == -1) && errno == EINTR) && !(async_err = PyErr_CheckSignals()));

	if (err) {
		if (!async_err) {
			PyErr_SetFromErrno(PyExc_OSError);
		}
		return NULL;
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_create_vdev_spec__doc__,
"create_vdev_spec(*, vdev_type, name=None, children=None)"
" -> struct_vdev_create_spec\n"
"---------------------------------------------------------\n\n"
"Build an immutable vdev specification record for use with\n"
"ZFS.create_pool().  All arguments are keyword-only.\n\n"
"Parameters\n"
"----------\n"
"vdev_type: str | " PYLIBZFS_MODULE_NAME ".VDevType, required\n"
"    The type of vdev to create.  Must be one of:\n"
"    \"disk\", \"file\", \"mirror\",\n"
"    \"raidz1\", \"raidz2\", \"raidz3\",\n"
"    \"draid1\", \"draid2\", \"draid3\".\n\n"
"name: str | None, optional\n"
"    Device path for leaf vdevs (disk/file), e.g. \"/dev/sda\".\n"
"    For dRAID vdevs, a config string of the form \"<ndata>d:<nspares>s\",\n"
"    e.g. \"3d:1s\" for 3 data disks and 1 distributed spare.\n"
"    Must be None for all other virtual vdev types (mirror, raidz*).\n\n"
"children: sequence of struct_vdev_create_spec | None, optional\n"
"    Child vdev specs for virtual vdev types (mirror, raidz*, draid*).\n"
"    Must be None for leaf vdev types (disk, file).\n\n"
"Returns\n"
"-------\n"
PYLIBZFS_MODULE_NAME ".struct_vdev_create_spec\n"
"    Immutable named-tuple-like record with fields:\n"
"    (name, vdev_type, children).\n\n"
"Raises\n"
"------\n"
"ValueError:\n"
"    vdev_type is missing, unrecognised, or the name/children combination\n"
"    is inconsistent with the requested type (e.g. leaf vdev with children,\n"
"    or dRAID with a malformed config string).\n"
"TypeError:\n"
"    vdev_type is not a string, name is not a string or None, or children\n"
"    is not a sequence.\n"
);
static PyObject *
py_create_vdev_spec(PyObject *self, PyObject *args, PyObject *kwargs)
{
	pylibzfs_state_t *state = NULL;
	PyObject *py_vtype = NULL;
	PyObject *py_name = Py_None;
	PyObject *py_children = Py_None;
	char *kwnames[] = {"vdev_type", "name", "children", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$OOO",
	    kwnames, &py_vtype, &py_name, &py_children))
		return NULL;

	if (py_vtype == NULL || py_vtype == Py_None) {
		PyErr_SetString(PyExc_ValueError,
		    "\"vdev_type\" keyword argument is required");
		return NULL;
	}

	if (!PyUnicode_Check(py_vtype)) {
		PyErr_SetString(PyExc_TypeError, "vdev_type must be a string");
		return NULL;
	}

	state = (pylibzfs_state_t *)PyModule_GetState(self);
	PYZFS_ASSERT(state, "Failed to get module state");

	return py_zfs_pool_create_vdev_spec(state, py_vtype, py_name,
	    py_children);
}

PyDoc_STRVAR(py_read_label__doc__,
"read_label(*, fd: int) -> dict | None\n"
"-------------------------------------\n\n"
"Read the ZFS label from a block device and return its configuration\n"
"as a dictionary, or None if no valid ZFS label is found.\n\n"
"All arguments are keyword-only.\n\n"
"Parameters\n"
"----------\n"
"fd: int, required\n"
"    Open file descriptor for the block device. May be opened O_RDONLY.\n\n"
"Returns\n"
"-------\n"
"dict | None\n"
"    The nvlist config from the device's ZFS label, or None if no valid\n"
"    label is found on the device.\n\n"
"Raises\n"
"------\n"
"RuntimeError:\n"
"    zpool_read_label() failed (out of memory).\n"
);
static PyObject *py_read_label(PyObject *self, PyObject *args, PyObject *kwargs)
{
	int fd = -1, ret = 0, num_labels = 0;
	nvlist_t *config = NULL;
	PyObject *nvldump = NULL, *dict_out = NULL;
	pylibzfs_state_t *state = NULL;
	char *kwnames[] = {"fd", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$i", kwnames, &fd))
		return NULL;

	if (fd < 0) {
		PyErr_SetString(PyExc_ValueError, "fd must be a non-negative integer");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".read_label", "i", fd) < 0)
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	ret = zpool_read_label(fd, &config, &num_labels);
	Py_END_ALLOW_THREADS

	if (ret != 0) {
		PyErr_SetString(PyExc_RuntimeError, "zpool_read_label() failed");
		return NULL;
	}

	if (num_labels == 0 || config == NULL) {
		fnvlist_free(config);
		Py_RETURN_NONE;
	}

	state = (pylibzfs_state_t *)PyModule_GetState(self);
	PYZFS_ASSERT(state, "Failed to get module state");

	nvldump = py_dump_nvlist(config, B_TRUE);
	fnvlist_free(config);
	if (nvldump == NULL)
		return NULL;

	dict_out = PyObject_CallFunction(state->loads_fn, "O", nvldump);
	Py_DECREF(nvldump);
	return dict_out;
}

PyDoc_STRVAR(py_clear_label__doc__,
"clear_label(*, fd: int) -> None\n"
"--------------------------------\n\n"
"Clear (zero) all ZFS label information on a block device.\n\n"
"All arguments are keyword-only.\n\n"
"Parameters\n"
"----------\n"
"fd: int, required\n"
"    Open file descriptor for the block device. Must be opened O_RDWR.\n\n"
"Returns\n"
"-------\n"
"None\n\n"
"Raises\n"
"------\n"
"OSError:\n"
"    A write error occurred while clearing the labels (errno is set).\n"
);
static PyObject *py_clear_label(PyObject *self, PyObject *args, PyObject *kwargs)
{
	int fd = -1, ret;
	char *kwnames[] = {"fd", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$i", kwnames, &fd))
		return NULL;

	if (fd < 0) {
		PyErr_SetString(PyExc_ValueError, "fd must be a non-negative integer");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".clear_label", "i", fd) < 0)
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	ret = zpool_clear_label(fd);
	Py_END_ALLOW_THREADS

	if (ret != 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_name_is_valid__doc__,
"name_is_valid(*, name, type) -> bool\n\n"
"-------------------------------------\n\n"
"Check whether the given name is a valid ZFS name for the specified type.\n\n"
"Parameters\n"
"----------\n"
"name: str, required\n"
"    The name to validate.\n"
"type: " PYLIBZFS_MODULE_NAME ".ZFSType, required\n"
"    The ZFS type to validate the name against.\n\n"
"Returns\n"
"-------\n"
"bool\n"
"    True if the name is valid for the given type, False otherwise.\n"
);
static PyObject *
py_name_is_valid(PyObject *self, PyObject *args, PyObject *kwargs)
{
	pylibzfs_state_t *state = NULL;
	const char *name = NULL;
	PyObject *pyzfstype = NULL;
	long ztype;
	int valid;
	char *kwnames[] = {"name", "type", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$sO",
					 kwnames, &name, &pyzfstype)) {
		return NULL;
	}

	if (name == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"\"name\" keyword argument is required.");
		return NULL;
	}

	if (pyzfstype == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"\"type\" keyword argument is required.");
		return NULL;
	}

	state = (pylibzfs_state_t *)PyModule_GetState(self);
	PYZFS_ASSERT(state, "Failed to get module state");

	if (!PyObject_IsInstance(pyzfstype, state->zfs_type_enum)) {
		PyObject *repr = PyObject_Repr(pyzfstype);
		PyErr_Format(PyExc_TypeError,
			     "%V: not a valid ZFSType",
			     repr, "UNKNOWN");
		Py_XDECREF(repr);
		return NULL;
	}

	ztype = PyLong_AsLong(pyzfstype);
	if (ztype == -1 && PyErr_Occurred())
		return NULL;

	valid = zfs_name_valid(name, (zfs_type_t)ztype);
	if (valid && (name[strlen(name) - 1] != ' ')) {
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

/* Module method table */
static PyMethodDef TruenasPylibzfsMethods[] = {
	{
		.ml_name = "open_handle",
		.ml_meth = (PyCFunction)py_get_libzfs_handle,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_get_libzfs_handle__doc__
	},
	{
		.ml_name = "fzfs_rewrite",
		.ml_meth = (PyCFunction)py_fzfs_rewrite,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_fzfs_rewrite__doc__
	},
	{
		.ml_name = "create_vdev_spec",
		.ml_meth = (PyCFunction)py_create_vdev_spec,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_create_vdev_spec__doc__
	},
	{
		.ml_name = "read_label",
		.ml_meth = (PyCFunction)py_read_label,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_read_label__doc__
	},
	{
		.ml_name = "clear_label",
		.ml_meth = (PyCFunction)py_clear_label,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_clear_label__doc__
	},
	{
		.ml_name = "name_is_valid",
		.ml_meth = (PyCFunction)py_name_is_valid,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_name_is_valid__doc__
	},
	{NULL}
};

static int
pylibzfs_module_clear(PyObject *module)
{
	free_py_zfs_state(module);
	return 0;
}

static void
pylibzfs_module_free(void *module)
{
	if (module)
		pylibzfs_module_clear((PyObject *)module);
}

/* Module structure */
static struct PyModuleDef truenas_pylibzfs = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = PYLIBZFS_MODULE_NAME,
	.m_doc = PYLIBZFS_MODULE_NAME " provides python bindings for libzfs for TrueNAS",
	.m_size = sizeof(pylibzfs_state_t),
	.m_clear = pylibzfs_module_clear,
	.m_free = pylibzfs_module_free,
	.m_methods = TruenasPylibzfsMethods,
};

/* Constants module */
static struct PyModuleDef truenas_pylibzfs_constants = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = PYLIBZFS_MODULE_NAME,
	.m_doc = PYLIBZFS_MODULE_NAME ".constants" " provides constants related to libzfs.",
};

/* Enums module */
static struct PyModuleDef truenas_pylibzfs_enums = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = PYLIBZFS_MODULE_NAME ".enums",
	.m_doc = PYLIBZFS_MODULE_NAME ".enums provides enums related to libzfs.",
};

static
int py_init_libzfs(void)
{
	libzfs_handle_t *tmplz = NULL;

	// We need to initialize libzfs handle temporarily so that
	// zfs and zpool properties get properly initialized so that
	// we can build our Struct Sequences with correct values

	tmplz = libzfs_init();
	if (!tmplz) {
		PyErr_Format(PyExc_ImportError,
			     "libzfs_init() failed: %s",
			     strerror(errno));
		return B_FALSE;
	}

	libzfs_fini(tmplz);
	return B_TRUE;
}

/* Module initialization */
PyMODINIT_FUNC
PyInit_truenas_pylibzfs(void)
{
	PyObject *zfs_exc;
	PyObject *constants = NULL;
	PyObject *enums = NULL;
	PyObject *lzc = NULL;
	PyObject *propsets = NULL;
	int err;

	PyObject *mpylibzfs = PyModule_Create(&truenas_pylibzfs);
	if (mpylibzfs == NULL)
		return NULL;

	if (types_ready(mpylibzfs) < 0) {
		Py_DECREF(mpylibzfs);
		return NULL;
	}

	err = PyModule_AddObjectRef(mpylibzfs, "ZFS", (PyObject *)&ZFS);
	if (err) {
		Py_DECREF(mpylibzfs);
		return NULL;
	}

	constants = PyModule_Create(&truenas_pylibzfs_constants);
	if (constants != NULL)
		add_constants(constants);

	err = PyModule_AddObjectRef(mpylibzfs, "constants", constants);
	Py_XDECREF(constants);
	if (err) {
		Py_DECREF(mpylibzfs);
		return NULL;
	}

	if (!py_init_libzfs()) {
		Py_DECREF(mpylibzfs);
		return NULL;
	}

	lzc = py_setup_lzc_module(mpylibzfs);
	err = PyModule_AddObjectRef(mpylibzfs, "lzc", lzc);
	Py_XDECREF(lzc);
	if (err) {
		Py_DECREF(mpylibzfs);
		return NULL;
	}

	zfs_exc = setup_zfs_exception();
	err = PyModule_AddObjectRef(mpylibzfs, "ZFSException", zfs_exc);
	Py_XDECREF(zfs_exc);
	if (err) {
		Py_DECREF(mpylibzfs);
		return NULL;
	}

	enums = PyModule_Create(&truenas_pylibzfs_enums);
	if (enums != NULL) {
		if (py_add_zfs_enums(mpylibzfs, enums)) {
			Py_DECREF(enums);
			Py_DECREF(mpylibzfs);
			return NULL;
		}
	}

	err = PyModule_AddObjectRef(mpylibzfs, "enums", enums);
	Py_XDECREF(enums);
	if (err) {
		Py_DECREF(mpylibzfs);
		return NULL;
	}

	if (init_py_zfs_state(mpylibzfs) < 0) {
		Py_DECREF(mpylibzfs);
		return NULL;
	}

	propsets = py_setup_propset_module(mpylibzfs);
	err = PyModule_AddObjectRef(mpylibzfs, "property_sets", propsets);
	Py_XDECREF(propsets);
	if (err) {
		Py_DECREF(mpylibzfs);
		return NULL;
	}

	return (mpylibzfs);
}
