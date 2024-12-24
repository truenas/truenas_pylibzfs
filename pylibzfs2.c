#include "pylibzfs2.h"

static char hello_docstring[] = "Hello from py-libzfs2!";

static PyTypeObject *types[] = {
	&ZFSProperty,
	&ZFSObject,
	&ZFSResource,
	&ZFSDataset,
	&ZFSEncDataset,
	&ZFSPool,
	&ZFS
};

static int num_types = sizeof(types) / sizeof(PyTypeObject *);

static PyObject *hello (PyObject *self, PyObject *args) {
	return Py_BuildValue("s", hello_docstring);
}

static void add_constants(PyObject *m) {
	PyModule_AddIntConstant(m, "ZFS_TYPE_FILESYSTEM", ZFS_TYPE_FILESYSTEM);
	PyModule_AddIntConstant(m, "ZFS_TYPE_VOLUME", ZFS_TYPE_VOLUME);
	PyModule_AddIntConstant(m, "ZFS_TYPE_SNAPSHOT", ZFS_TYPE_SNAPSHOT);
	PyModule_AddIntConstant(m, "ZFS_TYPE_BOOKMARK", ZFS_TYPE_BOOKMARK);
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

	if (add_types(mlibzfs2) < 0) {
		Py_DECREF(mlibzfs2);
		return (NULL);
	}

	add_constants(mlibzfs2);
	return (mlibzfs2);
}
