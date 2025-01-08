#include "pylibzfs2.h"

static char hello_docstring[] = "Hello from py-libzfs2!";

static PyTypeObject *types[] = {
	&ZFS
};

static PyTypeObject *alltypes[] = {
	&ZFS,
	&ZFSDataset,
	&ZFSObject,
	&ZFSPool,
	&ZFSProperty,
	NULL
};

static int num_types = sizeof(types) / sizeof(PyTypeObject *);

static PyObject *hello (PyObject *self, PyObject *args) {
	return Py_BuildValue("s", hello_docstring);
}

static void add_constants(PyObject *m) {
	uint i;

#define ADD_CONSTANT(val)  PyModule_AddIntConstant(m, #val, val)

	ADD_CONSTANT(ZFS_TYPE_FILESYSTEM);
	ADD_CONSTANT(ZFS_TYPE_VOLUME);
	ADD_CONSTANT(ZFS_TYPE_SNAPSHOT);
	ADD_CONSTANT(ZFS_TYPE_BOOKMARK);

	for (i=0; i < ARRAY_SIZE(zfserr_table); i++) {
		PyModule_AddIntConstant(m, zfserr_table[i].name,
					zfserr_table[i].error);
	}

	for (i=0; i < ARRAY_SIZE(zpool_status_table); i++) {
		PyModule_AddIntConstant(m, zpool_status_table[i].name,
					zpool_status_table[i].status);
	}
}

static int add_types(PyObject * m) {
	for (int i = 0; i < num_types; ++i) {
		Py_INCREF(types[i]);
		if (PyModule_AddObject(m, types[i]->tp_name,
		    (PyObject *)types[i]) < 0) {
			for (int j = 0; j <= i; ++j)
				Py_DECREF(types[j]);
			return (-1);
		}
	}
	return (0);
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

static int init_types() {
	for (int i = 0; i < num_types; ++i) {
		if (PyType_Ready(types[i]) < 0)
			return (-1);
		}
	return (0);
}

/* Module method table */
static PyMethodDef pylibzfs2Methods[] = {
	{"hello", hello, METH_NOARGS, hello_docstring},
	{NULL}
};

/* Module structure */
static struct PyModuleDef pylibzfs2 = {
	PyModuleDef_HEAD_INIT,
	"pylibzfs2",
	"pylibzfs2 provides python bindings for libzfs",
	-1,
	pylibzfs2Methods
};

/* Module initialization */
PyMODINIT_FUNC
PyInit_libzfs2(void)
{
	if (init_types() < 0)
		return (NULL);

	PyObject *mlibzfs2 = PyModule_Create(&pylibzfs2);
	if (mlibzfs2 == NULL)
		return (NULL);

	if (types_ready(mlibzfs2) < 0) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	if (add_types(mlibzfs2) < 0) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	add_constants(mlibzfs2);

	PyExc_ZFSError = PyErr_NewException("libzfs2.ZFSError",
					    PyExc_RuntimeError,
					    NULL);

	if (PyExc_ZFSError == NULL) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	if (PyModule_AddObject(mlibzfs2, "ZFSError", PyExc_ZFSError) < 0) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	return (mlibzfs2);
}
