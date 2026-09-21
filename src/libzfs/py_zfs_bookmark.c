#include "../truenas_pylibzfs.h"
#include "py_zfs_iter.h"

#define ZFS_BOOKMARK_STR "<" PYLIBZFS_TYPES_MODULE_NAME \
    ".ZFSBookmark(name=%U, pool=%U, type=%U)>"

static
void py_zfs_bookmark_dealloc(py_zfs_bookmark_t *self) {
	free_py_zfs_obj(&self->obj);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_repr_zfs_bookmark(PyObject *self)
{
	py_zfs_bookmark_t *bm = (py_zfs_bookmark_t *)self;

	return py_repr_zfs_obj_impl(&bm->obj, ZFS_BOOKMARK_STR);
}

PyDoc_STRVAR(py_zfs_bookmark_get_properties__doc__,
"get_properties(*, properties, get_source=False) -> "
"truenas_pylibzfs.struct_zfs_property\n\n"
"-----------------------------------------------------\n\n"
"Get the specified properties of this bookmark.\n\n"
"A bookmark carries only the values ZFS records in its on-disk entry.\n"
"Properties that are valid for a bookmark but were never populated are\n"
"returned as None rather than raising. This is always the case for\n"
"\"compressratio\", and is also the case for \"referenced\" and\n"
"\"logicalreferenced\" on a bookmark created while feature@bookmark_written\n"
"was disabled.\n\n"
""
"Parameters\n"
"----------\n"
"properties: set\n"
"    Set of truenas_pylibzfs.ZFSProperty values to retrieve. The set of\n"
"    properties that are valid for a bookmark is available as\n"
"    truenas_pylibzfs.property_sets.ZFS_BOOKMARK_PROPERTIES.\n\n"
"get_source: bool, optional, default=False\n"
"    Include the source of each property in the output.\n\n"
""
"Returns\n"
"-------\n"
"    truenas_pylibzfs.struct_zfs_property\n\n"
""
"Raises\n"
"------\n"
"ValueError:\n"
"    The properties keyword was omitted, or a property that is not valid\n"
"    for a bookmark was requested.\n"
"TypeError:\n"
"    The properties keyword is not a set.\n"
);
static
PyObject *py_zfs_bookmark_get_properties(PyObject *self,
					 PyObject *args_unused,
					 PyObject *kwargs)
{
	py_zfs_bookmark_t *bm = (py_zfs_bookmark_t *)self;
	PyObject *prop_set = NULL;
	boolean_t get_source = B_FALSE;
	char *kwnames [] = {
		"properties",
		"get_source",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$Op",
					 kwnames,
					 &prop_set,
					 &get_source)) {
					 return NULL;
	}
	if (prop_set == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"properties keyword is required.");
		return NULL;
	}
	if (!PyAnySet_Check(prop_set)) {
		PyErr_SetString(PyExc_TypeError,
				"properties must be a python set.");
		return NULL;
	}

	/*
	 * There is no is_simple check here because a bookmark handle can never
	 * be simple. make_bookmark_handle() always duplicates the full set of
	 * properties the iterator requested, and refreshing them would issue
	 * ZFS_IOC_OBJSET_STATS against a name containing a '#'.
	 */
	return py_zfs_get_properties(&bm->obj, prop_set, get_source);
}

PyDoc_STRVAR(py_zfs_bookmark_destroy__doc__,
"destroy() -> None\n"
"-----------------\n"
"Destroy this bookmark. This is irreversible.\n\n"
"Destroying a bookmark that no longer exists succeeds silently, because\n"
"lzc_destroy_bookmarks() ignores entries that are already gone. The python\n"
"object also keeps referring to the destroyed bookmark until it is dropped;\n"
"it is not invalidated by this call.\n\n"
""
"Parameters\n"
"----------\n"
"    None\n\n"
""
"Returns\n"
"-------\n"
"    None\n\n"
""
"Raises\n"
"------\n"
"ZFSException:\n"
"    The bookmark could not be destroyed.\n"
"RuntimeError:\n"
"    The bookmark was destroyed but writing the zpool history entry failed.\n"
);
static
PyObject *py_zfs_bookmark_destroy(PyObject *self, PyObject *args_unused)
{
	py_zfs_bookmark_t *bm = (py_zfs_bookmark_t *)self;
	py_zfs_error_t zfs_err;
	int err;

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSBookmark.destroy", "O",
			bm->obj.name) < 0) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(bm->obj.pylibzfsp);
	/*
	 * zfs_destroy() special-cases ZFS_TYPE_BOOKMARK, routing it to
	 * lzc_destroy_bookmarks() and setting the libzfs error state itself on
	 * failure, so the usual py_get_zfs_error() pattern applies. The defer
	 * argument must be B_FALSE: zfs_destroy() returns EINVAL for a deferred
	 * destroy of anything that is not a snapshot.
	 */
	err = zfs_destroy(bm->obj.zhp, B_FALSE);
	if (err) {
		py_get_zfs_error(bm->obj.pylibzfsp->lzh, &zfs_err);
	}
	PY_ZFS_UNLOCK(bm->obj.pylibzfsp);
	Py_END_ALLOW_THREADS

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zfs_destroy() failed");
		return NULL;
	}

	err = py_log_history_fmt(bm->obj.pylibzfsp, "zfs destroy %s",
				 zfs_get_name(bm->obj.zhp));
	if (err)
		return NULL;

	Py_RETURN_NONE;
}

PyDoc_STRVAR(py_zfs_bookmark_rename__doc__,
"rename(*, new_name, recursive=False, no_unmount=False,\n"
"       force_unmount=False) -> NoReturn\n"
"------------------------------------------------------\n"
"Always raises TypeError. ZFS bookmarks cannot be renamed.\n\n"
"This overrides ZFSObject.rename(). Without the override the inherited\n"
"method would accept a bookmark name - zfs_name_valid() passes \"pool/fs#new\"\n"
"for ZFS_TYPE_BOOKMARK and the recursive guard only rejects non-snapshots -\n"
"and drive zfs_rename() through its non-snapshot branch, which gathers a\n"
"changelist and issues ZFS_IOC_RENAME against a name containing a '#'.\n\n"
""
"Parameters\n"
"----------\n"
"    Accepted for signature compatibility with ZFSObject.rename(), and\n"
"    ignored.\n\n"
""
"Returns\n"
"-------\n"
"    Never returns.\n\n"
""
"Raises\n"
"------\n"
"TypeError:\n"
"    Always.\n"
);
static
PyObject *py_zfs_bookmark_rename(PyObject *self,
				 PyObject *args_unused,
				 PyObject *kwargs_unused)
{
	PyErr_SetString(PyExc_TypeError,
			"ZFS bookmarks cannot be renamed.");
	return NULL;
}

/*
 * Shared implementation of iter_bookmarks() for ZFSDataset and ZFSVolume.
 * Not on ZFSResource: ZFSSnapshot derives from it and
 * zfs_iter_bookmarks_v2() succeeds without iterating for a snapshot handle,
 * which would report "no bookmarks" rather than an error.
 */
PyObject *py_zfs_iter_bookmarks(py_zfs_resource_t *res,
				PyObject *args_unused,
				PyObject *kwargs)
{
	int err;
	py_zfs_obj_t *obj = &res->obj;

	py_iter_state_t iter_state = (py_iter_state_t){
		.pylibzfsp = obj->pylibzfsp,
		.target = obj->zhp
	};

	char *kwnames [] = {
		"callback",
		"state",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args_unused, kwargs,
					 "|$OO",
					 kwnames,
					 &iter_state.callback_fn,
					 &iter_state.private_data)) {
		return NULL;
	}

	if (!iter_state.callback_fn) {
		PyErr_SetString(PyExc_ValueError,
				"`callback` keyword argument is required.");
		return NULL;
	}

	if (!PyCallable_Check(iter_state.callback_fn)) {
		PyErr_SetString(PyExc_TypeError,
				"callback function must be callable.");
		return NULL;
	}

	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".ZFSResource.iter_bookmarks",
			"OO", obj->name, kwargs) < 0) {
		return NULL;
	}

	/*
	 * No iterator configuration is set here. zfs_iter_bookmarks_v2()
	 * declares its flags argument __maybe_unused and ignores it, so
	 * iter_config.bookmark is left zeroed by the initializer above.
	 */
	err = py_iter_bookmarks(&iter_state);
	if ((err == ITER_RESULT_ERROR) || (err == ITER_RESULT_IOCTL_ERROR)) {
		// Exception is set by callback function
		return NULL;
	}

	if (err == ITER_RESULT_SUCCESS) {
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

static
PyGetSetDef zfs_bookmark_getsetters[] = {
	{ .name = NULL }
};

static
PyMethodDef zfs_bookmark_methods[] = {
	{
		.ml_name = "get_properties",
		.ml_meth = (PyCFunction)py_zfs_bookmark_get_properties,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_bookmark_get_properties__doc__
	},
	{
		.ml_name = "destroy",
		.ml_meth = py_zfs_bookmark_destroy,
		.ml_flags = METH_NOARGS,
		.ml_doc = py_zfs_bookmark_destroy__doc__
	},
	{
		.ml_name = "rename",
		.ml_meth = (PyCFunction)py_zfs_bookmark_rename,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc = py_zfs_bookmark_rename__doc__
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSBookmark = {
	.tp_name = PYLIBZFS_TYPES_MODULE_NAME ".ZFSBookmark",
	.tp_basicsize = sizeof (py_zfs_bookmark_t),
	.tp_methods = zfs_bookmark_methods,
	.tp_getset = zfs_bookmark_getsetters,
	.tp_new = py_no_new_impl,
	.tp_doc = "ZFSBookmark",
	.tp_dealloc = (destructor)py_zfs_bookmark_dealloc,
	.tp_repr = py_repr_zfs_bookmark,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_base = &ZFSObject
};

py_zfs_bookmark_t *init_zfs_bookmark(py_zfs_t *lzp, zfs_handle_t *zfsp)
{
	py_zfs_bookmark_t *out = NULL;
	py_zfs_obj_t *obj = NULL;
	const char *bm_name;
	const char *pool_name;
	zfs_type_t zfs_type;
	uint64_t guid, createtxg, ivset_guid;

	out = (py_zfs_bookmark_t *)ZFSBookmark.tp_alloc(&ZFSBookmark, 0);
	if (out == NULL)
		return NULL;

	obj = &out->obj;
	obj->pylibzfsp = lzp;
	Py_INCREF(lzp);

	Py_BEGIN_ALLOW_THREADS
	bm_name = zfs_get_name(zfsp);
	zfs_type = zfs_get_type(zfsp);
	pool_name = zfs_get_pool_name(zfsp);
	guid = zfs_prop_get_int(zfsp, ZFS_PROP_GUID);
	createtxg = zfs_prop_get_int(zfsp, ZFS_PROP_CREATETXG);
	/*
	 * zfs_is_encrypted() reads dds_flags, which make_bookmark_handle()
	 * leaves zeroed, so it answers B_FALSE for the bookmark of an
	 * encrypted dataset. Use the IVset guid, which the kernel records
	 * only when the bookmarked snapshot's dataset has a crypto object.
	 * A bookmark predating IVset guid recording reports B_FALSE.
	 */
	ivset_guid = zfs_prop_get_int(zfsp, ZFS_PROP_IVSET_GUID);
	Py_END_ALLOW_THREADS

	PYZFS_ASSERT((zfs_type == ZFS_TYPE_BOOKMARK), "Incorrect ZFS type");

	obj->name = PyUnicode_FromString(bm_name);
	if (obj->name == NULL)
		goto error;

	obj->pool_name = PyUnicode_FromString(pool_name);
	if (obj->pool_name == NULL)
		goto error;

	obj->ctype = zfs_type;
	obj->type_enum = py_get_zfs_type(lzp, zfs_type, &obj->type);
	obj->guid = Py_BuildValue("k", guid);
	if (obj->guid == NULL)
		goto error;

	obj->createtxg = Py_BuildValue("k", createtxg);
	if (obj->createtxg == NULL)
		goto error;

	obj->encrypted = Py_NewRef(ivset_guid ? Py_True : Py_False);
	obj->zhp = zfsp;
	return out;

error:
	// This deallocates the new object and decrements refcnt on pylibzfsp
	Py_DECREF(out);
	return NULL;
}
