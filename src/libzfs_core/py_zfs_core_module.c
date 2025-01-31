#include "../truenas_pylibzfs.h"

static PyObject *py_lzc_create_snaps(PyObject *self,
				     PyObject *args,
				     PyObject *kwargs)
{
	Py_RETURN_NONE;
}


static PyObject *py_lzc_destroy_snaps(PyObject *self,
				      PyObject *args,
				      PyObject *kwargs)
{
	Py_RETURN_NONE;
}


/* Module method table */
static PyMethodDef TruenasPylibzfsCoreMethods[] = {
	{
		.ml_name = "create_snapshots",
		.ml_meth = (PyCFunction)py_lzc_create_snaps,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = "TODO: Write some docs"
	},
	{
		.ml_name = "destroy_snapshots",
		.ml_meth = (PyCFunction)py_lzc_destroy_snaps,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = "TODO: Write some docs"
	},
	{NULL}
};

/* Module structure */
static struct PyModuleDef truenas_pylibzfs_core = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = PYLIBZFS_MODULE_NAME,
	.m_doc = PYLIBZFS_MODULE_NAME " provides python bindings for libzfs_core for TrueNAS",
	.m_size = sizeof(pylibzfs_state_t),
	.m_methods = TruenasPylibzfsCoreMethods,
};



PyObject *py_setup_lzc_module(void)
{
	PyObject *mlzc = PyModule_Create(&truenas_pylibzfs_core);
	if (mlzc == NULL)
		return NULL;

	return mlzc;
}
