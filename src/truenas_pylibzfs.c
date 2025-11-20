#include "truenas_pylibzfs.h"
#include <sys/mntent.h>
#include <sys/mount.h>

static PyTypeObject *alltypes[] = {
	&ZFS,
	&ZFSCrypto,
	&ZFSDataset,
	&ZFSObject,
	&ZFSPool,
	&ZFSSnapshot,
	&ZFSVdev,
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

/* Module method table */
static PyMethodDef TruenasPylibzfsMethods[] = {
	{
		.ml_name = "open_handle",
		.ml_meth = (PyCFunction)py_get_libzfs_handle,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_get_libzfs_handle__doc__
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

	if (py_add_zfs_enums(mpylibzfs)) {
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
