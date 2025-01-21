#include "pylibzfs2.h"

static PyTypeObject *alltypes[] = {
	&ZFS,
	&ZFSDataset,
	&ZFSObject,
	&ZFSPool,
	&ZFSProperty,
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
"libzfs2.ZFSError:\n"
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
static PyMethodDef pylibzfs2Methods[] = {
	{
		.ml_name = "open_handle",
		.ml_meth = (PyCFunction)py_get_libzfs_handle,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_get_libzfs_handle__doc__
	},
	{NULL}
};

/* Module structure */
static struct PyModuleDef pylibzfs2 = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = PYLIBZFS_MODULE_NAME,
	.m_doc = PYLIBZFS_MODULE_NAME " provides python bindings for libzfs for TrueNAS",
	.m_size = sizeof(pylibzfs_state_t),
	.m_methods = pylibzfs2Methods,
};


/* Module initialization */
PyMODINIT_FUNC
PyInit_truenas_pylibzfs(void)
{
	PyObject *zfs_exc;

	PyObject *mlibzfs2 = PyModule_Create(&pylibzfs2);
	if (mlibzfs2 == NULL)
		return (NULL);

	if (types_ready(mlibzfs2) < 0) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	add_constants(mlibzfs2);

	zfs_exc = setup_zfs_exception();
	if (zfs_exc == NULL) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	if (PyModule_AddObject(mlibzfs2, "ZFSException", zfs_exc) < 0) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	if (py_add_zfs_enums(mlibzfs2)) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	if (init_py_zfs_state(mlibzfs2) < 0) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	return (mlibzfs2);
}
