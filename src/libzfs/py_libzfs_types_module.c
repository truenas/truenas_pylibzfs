#include "../truenas_pylibzfs.h"
#include "py_zfs_events.h"

static struct PyModuleDef truenas_pylibzfs_types = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = PYLIBZFS_MODULE_NAME ".libzfs_types",
	.m_doc = "Types and enums for " PYLIBZFS_MODULE_NAME,
	.m_size = -1,
};

static int
types_add_to_submodule(PyObject *m)
{
	int i;
	static const struct {
		const char *name;
		PyTypeObject *type;
	} type_exports[] = {
		{ "ZFS", &ZFS },
		{ "ZFSCrypto", &ZFSCrypto },
		{ "ZFSDataset", &ZFSDataset },
		{ "ZFSEventIterator", &ZFSEventIterator },
		{ "ZFSHistoryIterator", &ZFSHistoryIterator },
		{ "ZFSObject", &ZFSObject },
		{ "ZFSPool", &ZFSPool },
		{ "ZFSResource", &ZFSResource },
		{ "ZFSSnapshot", &ZFSSnapshot },
		{ "ZFSVolume", &ZFSVolume },
		{ NULL, NULL }
	};

	for (i = 0; type_exports[i].name != NULL; i++) {
		if (PyModule_AddObjectRef(m, type_exports[i].name,
		    (PyObject *)type_exports[i].type) < 0)
			return -1;
	}
	return 0;
}

PyObject *
py_setup_libzfs_types_module(PyObject *parent)
{
	PyObject *m = NULL;

	m = PyModule_Create(&truenas_pylibzfs_types);
	if (m == NULL)
		return NULL;

	if (types_add_to_submodule(m) < 0) {
		Py_DECREF(m);
		return NULL;
	}

	if (py_add_zfs_enums(parent, m) != 0) {
		Py_DECREF(m);
		return NULL;
	}

	return m;
}
