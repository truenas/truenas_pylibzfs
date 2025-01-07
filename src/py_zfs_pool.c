#include "pylibzfs2.h"

#define	ZFS_POOL_STR	"<libzfs2.ZFSpool name %s>"

static
PyObject *py_zfs_pool_str(PyObject *self) {
	py_zfs_pool_t *pool = (py_zfs_pool_t *)self;
	if (pool->zhp) {
		return (PyUnicode_FromFormat(ZFS_POOL_STR,
		    zpool_get_name(pool->zhp)));
	} else {
		return (PyUnicode_FromFormat(ZFS_POOL_STR, "<EMPTY>"));
	}
}

static
PyObject *py_zfs_pool_new(PyTypeObject *type, PyObject *args,
    PyObject *kwds) {
	py_zfs_pool_t *self = NULL;
	self = (py_zfs_pool_t *)type->tp_alloc(type, 0);
	return ((PyObject *)self);
}

static
int py_zfs_pool_init(PyObject *type, PyObject *args, PyObject *kwds) {
	return (0);
}

static
void py_zfs_pool_dealloc(py_zfs_pool_t *self) {
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static
PyObject *py_zfs_pool_asdict(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_reiterate_vdevs(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_unsup_features(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_wait(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_sync(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_create(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_attach_vdevs(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_sync_vdev_by_guid(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_delete(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_start_scrub(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_stop_scrub(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_clear(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_upgrade(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_ddt_prefetch(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_ddt_prune(PyObject *self, PyObject *args) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_root_dataset(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_root_vdev(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_data_vdevs(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_log_vdevs(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_cache_vdevs(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_spare_vdevs(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_special_vdevs(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_dedup_vdevs(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_groups(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_config(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_name(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_guid(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_hostname(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyObject *py_zfs_pool_get_state(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_status_code(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_healthy(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_warning(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_status_detail(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_error_count(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_properties(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_features(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_disks(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_scrub(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

static
PyObject *py_zfs_pool_get_expand(py_zfs_prop_t *self, void *extra) {
	Py_RETURN_NONE;
}

PyGetSetDef zfs_pool_getsetters[] = {
	{
		.name	= "root_dataset",
		.get	= (getter)py_zfs_pool_get_root_dataset
	},
	{
		.name	= "root_vdev",
		.get	= (getter)py_zfs_pool_get_root_vdev
	},
	{
		.name	= "data_vdevs",
		.get	= (getter)py_zfs_pool_get_data_vdevs
	},
	{
		.name	= "log_vdevs",
		.get	= (getter)py_zfs_pool_get_log_vdevs
	},
	{
		.name	= "cache_vdevs",
		.get	= (getter)py_zfs_pool_get_cache_vdevs
	},
	{
		.name	= "spare_vdevs",
		.get	= (getter)py_zfs_pool_get_spare_vdevs
	},
	{
		.name	= "special_vdevs",
		.get	= (getter)py_zfs_pool_get_special_vdevs
	},
	{
		.name	= "dedup_vdevs",
		.get	= (getter)py_zfs_pool_get_dedup_vdevs
	},
	{
		.name	= "groups",
		.get	= (getter)py_zfs_pool_get_groups
	},
	{
		.name	= "config",
		.get	= (getter)py_zfs_pool_get_config,
	},
	{
		.name	= "name",
		.get	= (getter)py_zfs_pool_get_name
	},
	{
		.name	= "guid",
		.get	= (getter)py_zfs_pool_get_guid
	},
	{
		.name	= "hostname",
		.get	= (getter)py_zfs_pool_get_hostname
	},
	{
		.name	= "state",
		.get	= (getter)py_zfs_pool_get_state
	},
	{
		.name	= "status_code",
		.get	= (getter)py_zfs_pool_get_status_code
	},
	{
		.name	= "healthy",
		.get	= (getter)py_zfs_pool_get_healthy
	},
	{
		.name	= "warning",
		.get	= (getter)py_zfs_pool_get_warning
	},
	{
		.name	= "status_detail",
		.get	= (getter)py_zfs_pool_get_status_detail
	},
	{
		.name	= "error_count",
		.get	= (getter)py_zfs_pool_get_error_count
	},
	{
		.name	= "properties",
		.get	= (getter)py_zfs_pool_get_properties
	},
	{
		.name	= "features",
		.get	= (getter)py_zfs_pool_get_features
	},
	{
		.name	= "disks",
		.get	= (getter)py_zfs_pool_get_disks
	},
	{
		.name	= "scrub",
		.get	= (getter)py_zfs_pool_get_scrub
	},
	{
		.name	= "expand",
		.get	= (getter)py_zfs_pool_get_expand
	},
	{ .name = NULL }
};

PyMethodDef zfs_pool_methods[] = {
	{
		.ml_name = "asdict",
		.ml_meth = py_zfs_pool_asdict,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "retrieve_vdevs",
		.ml_meth = py_zfs_pool_reiterate_vdevs,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "unsup_features",
		.ml_meth = py_zfs_pool_unsup_features,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "wait",
		.ml_meth = py_zfs_pool_wait,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "sync",
		.ml_meth = py_zfs_pool_sync,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "create",
		.ml_meth = py_zfs_pool_create,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "attach_vdevs",
		.ml_meth = py_zfs_pool_attach_vdevs,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "vdev_by_guid",
		.ml_meth = py_zfs_pool_sync_vdev_by_guid,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "delete",
		.ml_meth = py_zfs_pool_delete,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "start_scrub",
		.ml_meth = py_zfs_pool_start_scrub,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "stop_scrub",
		.ml_meth = py_zfs_pool_stop_scrub,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "clear",
		.ml_meth = py_zfs_pool_clear,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "upgrade",
		.ml_meth = py_zfs_pool_upgrade,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "ddt_prefetch",
		.ml_meth = py_zfs_pool_ddt_prefetch,
		.ml_flags = METH_VARARGS
	},
	{
		.ml_name = "ddt_prune",
		.ml_meth = py_zfs_pool_ddt_prune,
		.ml_flags = METH_VARARGS
	},
	{ NULL, NULL, 0, NULL }
};

PyTypeObject ZFSPool = {
	.tp_name = "ZFSPool",
	.tp_basicsize = sizeof (py_zfs_pool_t),
	.tp_methods = zfs_pool_methods,
	.tp_getset = zfs_pool_getsetters,
	.tp_new = py_zfs_pool_new,
	.tp_init = py_zfs_pool_init,
	.tp_doc = "ZFSPool",
	.tp_dealloc = (destructor)py_zfs_pool_dealloc,
	.tp_str = py_zfs_pool_str,
	.tp_repr = py_zfs_pool_str,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
};
