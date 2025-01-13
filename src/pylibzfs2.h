#ifndef _PYLIBZFS2_H
#define _PYLIBZFS2_H
#include "zfs.h"
#include "pylibzfs2_enums.h"

#define PYLIBZFS_MODULE_NAME "truenas_pylibzfs"
#define SUPPORTED_RESOURCES ZFS_TYPE_VOLUME | ZFS_TYPE_FILESYSTEM

/*
 * Macro to handle extreme error case in module. This should only be invoked
 * if an error condition is detected that would make it dangerous to continue.
 * This will call abort() and generate a corefile.
 */
#define __PYZFS_ASSERT_IMPL(test, message, location) do {\
        if (!test) {\
                Py_FatalError(message " [" location "]");\
        }\
} while (0);
#define PYZFS_ASSERT(test, message)\
        __PYZFS_ASSERT_IMPL(test, message, __location__);

/*
 * Wrapper around libzfs_handle_t
 * lzh: libzfs handle
 * zfs_lock: pthread_mutex for protecting libzfs handle
 *
 * NOTE: the libzfs_handle_t is potentially shared by multiple python objects.
 * The `zfs_lock` should be taken prior to any ZFS operation (e.g. zfs_rename)
 * and any error information should be retrieved while the lock is held.
 */
typedef struct {
	PyObject_HEAD
	libzfs_handle_t *lzh;
	pthread_mutex_t zfs_lock;
	boolean_t mnttab_cache_enable;
	int history;
	const char *history_prefix;
	PyObject *proptypes;
} py_zfs_t;

/*
 * The following macros are to simplify code that locks and unlocks
 * py_zfs_t objects for operations using libzfs_handle_t.
 */
#define PY_ZFS_LOCK(obj) do { \
	pthread_mutex_lock(&obj->zfs_lock); \
} while (0);

#define PY_ZFS_UNLOCK(obj) do { \
	pthread_mutex_unlock(&obj->zfs_lock); \
} while (0);

/*
 * Common struct for resource objects and bookmarks.
 *
 * Contains the following items:
 *
 * pylibzfsp: reference to libzfs handle
 * zhp: open zfs handle
 * ctype: type of ZFS object (C enum)
 * type: Unicode object for ZFS type
 * name: Unicode object of dataset name
 * guid: Python Int representing ZFS object GUID
 * createtxg: Python Int TXG in which object created
 * pool_name: Unicode object of pool name
 */
typedef struct {
	PyObject_HEAD
	py_zfs_t *pylibzfsp;
	zfs_handle_t *zhp;
	zfs_type_t ctype;
	PyObject *type;
	PyObject *name;
	PyObject *guid;
	PyObject *createtxg;
	PyObject *pool_name;
} py_zfs_obj_t;

/*
 * Common struct for datasets, volumes, snapshots, etc
 *
 * is_simple: c boolean indicating that the handle contains limited info.
 *    this can happen if we're using an optimized iterator. In this case
 *    get_properties-style methods will fail with ValueError
 */
typedef struct {
	py_zfs_obj_t obj;
	boolean_t is_simple;
} py_zfs_resource_t;

typedef struct {
	py_zfs_resource_t rsrc;
} py_zfs_dataset_t;

typedef struct {
	py_zfs_resource_t rsrc;
} py_zfs_volume_t;

typedef struct {
	py_zfs_resource_t rsrc;
	PyObject *snapshot_name;
} py_zfs_snapshot_t;

/* Macro to get pointer to base ZFS object from various ZFS resource types */
#define RSRC_TO_ZFS(x) (&x->rsrc.obj)

typedef struct {
	PyObject_HEAD
	py_zfs_t *pylibzfsp;
	zpool_handle_t *zhp;
	PyObject *name;
} py_zfs_pool_t;

typedef struct {
	PyObject_HEAD
	int propid;
	const char *cname;
	char cvalue[ZFS_MAXPROPLEN + 1];
	char crawvalue[ZFS_MAXPROPLEN + 1];
	char csrcstr[ZFS_MAXPROPLEN + 1];
	zprop_source_t csource;
} py_zfs_prop_t;

typedef struct {
	py_zfs_prop_t super;
	PyObject *values;
	const char *name;
} py_zfs_user_prop_t;

extern PyTypeObject ZFS;
extern PyTypeObject ZFSDataset;
extern PyTypeObject ZFSObject;
extern PyTypeObject ZFSPool;
extern PyTypeObject ZFSProperty;
extern PyTypeObject ZFSResource;

/*
 * Provided by error.c
 *
 * Note on getting ZFS errors:
 * ZFS errors should be retrieved while the libzfs_handle_t lock is held
 *
 * example pseudo-code:
 *
 * py_zfs_error_t zfs_err;
 *
 * Py_BEGIN_ALLOW_THREADS // drop GIL
 * pthread_mutex_lock(&<zfs_lock>);  // take libzfs_handle_t lock
 * <Do ZFS operation>
 * if <error>
 *   py_get_zfs_error(<libzfs_handle_t>, &zfs_err);
 *
 * pthread_mutex_unlock(&<zfs_lock>);  / unlock
 * Py_END_ALLOW_THREADS  // re-acquire GIL
 *
 * if <error> {
 *   set_exc_from_libzfs(&zfs_err, "Some Error");
 *   return NULL;
 * }
 *
 */
typedef struct {
	zfs_error_t code;
	char description[1024];
	char action[1024];
} py_zfs_error_t;

/*
 * @brief extract error information from libzfs handle
 *
 * This function extracts ZFS error information from a given libzfs
 * handle (error description, error action, and code). It should be
 * called under PY_ZFS_LOCK to ensure that other threads using the
 * libzfs dataset handle do not corrupt error information.
 *
 * @param[in]	lz - pointer to libzfs handle from which to get error info
 * @param[out]	out - pointer to py_zfs_error_t in which to write error info
 *
 * @return	void - should always succeed
 *
 * NOTE: this may be called without GIL
 */
extern void py_get_zfs_error(libzfs_handle_t *lz, py_zfs_error_t *out);

extern PyObject *setup_zfs_exception(void);

/*
 * @brief set a ZFSError exception based on given parameters
 *
 * This function sets a ZFSError python exception based on
 * libzfs error information that is contained in the py_zfs_error_t
 * struct. Additional information may be specified. This should
 * only be used to handle ZFS errors. Requires GIL but does not
 * require PY_ZFS_LOCK.
 *
 * @param[in]	err - pointer to libzfs error information extracted
 *		from libzfs_handle_t after a ZFS error.
 *
 * @param[in]	additional_information - pointer to a NULL-terminated
 *		string containing additional information to put in
 *		the ZFSError. May be NULL.
 *
 * NOTE: this should be called through macro `set_ex_from_libzfs()`
 * to ensure that location (file and line number) is properly set.
 *
 * GIL must be held when this is called.
 */
extern void _set_exc_from_libzfs(py_zfs_error_t *err,
				 const char *additional_info,
				 const char *location);
#define set_exc_from_libzfs(err, additional_info) \
	_set_exc_from_libzfs(err, additional_info, __location__)

/* Provided by py_zfs_dataset.c */
/*
 * @brief create a new ZFSDataset object based on parameters
 *
 * This function creates a new ZFSDataset object based on a py_zfs_t object
 * and a zfs_handle_t handle. Some example consumers are open_resource() method
 * for ZFSObject and iter_filesystems() method in ZFSDataset.
 *
 * @param[in]	lzp - pointer to py_zfs_t object. On success reference count
 *		for the py_zfs_t object will be incremented.
 *
 * @param[in]	zfsp - pointer to open zfs_handle_t handle. On success the
 *		resulting ZFSDataset object owns the handle and it must not
 *		be closed or manipulated in other ways.
 *
 * @return	returns pointer to new ZFSDataset object. NULL on failure.
 *
 * NOTE: if this call fails, onus is on caller to zfs_close() zfsp if required.
 *
 * GIL must be held when this is called.
 */
extern py_zfs_dataset_t *init_zfs_dataset(py_zfs_t *lzp, zfs_handle_t *zfsp);

/* Provided by py_zfs_pool.c */
extern py_zfs_pool_t *init_zfs_pool(py_zfs_t *lzp, zpool_handle_t *zhp);

/* Provided by utils.c */
extern const char *get_dataset_type(zfs_type_t type);
extern PyObject *py_repr_zfs_obj_impl(py_zfs_obj_t *obj, const char *fmt);

/* Provided by py_zfs_enum.c */
extern int py_add_zfs_enums(PyObject *module);

#endif  /* _PYLIBZFS2_H */
