#include "../truenas_pylibzfs.h"
#include <errno.h>
#include <libzutil.h>
#include <stdlib.h>

/*
 * py_zfs_pool_create.c — pool creation API
 *
 * Implements:
 *   - struct_vdev_create_spec  (PyStructSequence)
 *   - create_vdev_spec()       (module-level factory)
 *   - ZFS.create_pool()        (method on ZFS class)
 *
 * Usage pattern:
 *
 *   sda = create_vdev_spec(vdev_type="disk", name="/dev/sda")
 *   sdb = create_vdev_spec(vdev_type="disk", name="/dev/sdb")
 *   m   = create_vdev_spec(vdev_type="mirror", children=[sda, sdb])
 *   lz.create_pool(name="tank", storage_vdevs=[m])
 */

/*
 * Maximum number of dRAID distributed spares.  There is no named constant
 * for this in the ZFS headers; the value matches the hardcoded limit in
 * draid_config_by_type() in zpool_vdev.c.
 */
#define	VDEV_DRAID_MAX_SPARES	100

/* Field indices for struct_vdev_create_spec */
#define VCSPEC_NAME_IDX     0
#define VCSPEC_TYPE_IDX     1
#define VCSPEC_CHILDREN_IDX 2

static PyStructSequence_Field vdev_create_spec_fields[] = {
	{
		"name",
		"Device path for leaf vdevs (disk/file), or dRAID config string "
		"(e.g. \"3d:1s\") for draid types. None for other virtual vdevs."
	},
	{
		"vdev_type",
		"Vdev type string. One of: \"disk\", \"file\", \"mirror\", "
		"\"raidz1\", \"raidz2\", \"raidz3\", \"draid1\", \"draid2\", \"draid3\"."
	},
	{
		"children",
		"Tuple of child struct_vdev_create_spec objects for virtual vdevs. "
		"None for leaf vdevs (disk/file)."
	},
	{0}
};

static PyStructSequence_Desc vdev_create_spec_desc = {
	.name = PYLIBZFS_MODULE_NAME ".struct_vdev_create_spec",
	.doc = "Vdev creation specification for use with ZFS.create_pool().",
	.fields = vdev_create_spec_fields,
	.n_in_sequence = 3
};

/* --------------------------------------------------------------------------
 * Type predicates — match against the accepted vdev_type strings.
 * -------------------------------------------------------------------------- */

static boolean_t
is_leaf_type(PyObject *py_type)
{
	return (PyUnicode_CompareWithASCIIString(py_type, "disk") == 0 ||
	    PyUnicode_CompareWithASCIIString(py_type, "file") == 0);
}

static boolean_t
is_mirror_type(PyObject *py_type)
{
	return (PyUnicode_CompareWithASCIIString(py_type, "mirror") == 0);
}

/*
 * Matches "raidz1", "raidz2", "raidz3" — explicitly enumerated for clarity.
 */
static boolean_t
is_raidz_type(PyObject *py_type)
{
	return (PyUnicode_CompareWithASCIIString(py_type, "raidz1") == 0 ||
	    PyUnicode_CompareWithASCIIString(py_type, "raidz2") == 0 ||
	    PyUnicode_CompareWithASCIIString(py_type, "raidz3") == 0);
}

/*
 * Matches "draid1", "draid2", "draid3" — explicitly enumerated for clarity.
 */
static boolean_t
is_draid_type(PyObject *py_type)
{
	return (PyUnicode_CompareWithASCIIString(py_type, "draid1") == 0 ||
	    PyUnicode_CompareWithASCIIString(py_type, "draid2") == 0 ||
	    PyUnicode_CompareWithASCIIString(py_type, "draid3") == 0);
}

static boolean_t
is_virtual_type(PyObject *py_type)
{
	return (is_mirror_type(py_type) || is_raidz_type(py_type) ||
	    is_draid_type(py_type));
}

static boolean_t
is_valid_vdev_type(PyObject *py_type)
{
	return (is_leaf_type(py_type) || is_virtual_type(py_type));
}

/*
 * Return the redundancy parity level for a vdev type:
 *   disk/file (leaf)  → 0
 *   mirror            → 1
 *   raidz1 / draid1   → 1
 *   raidz2 / draid2   → 2
 *   raidz3 / draid3   → 3
 * Returns -1 for unrecognised types (should never occur after validation).
 */
static int
vdev_parity_level(PyObject *py_type)
{
	if (is_leaf_type(py_type))
		return (0);
	if (is_mirror_type(py_type))
		return (1);
	if (PyUnicode_CompareWithASCIIString(py_type, "raidz1") == 0 ||
	    PyUnicode_CompareWithASCIIString(py_type, "draid1") == 0)
		return (1);
	if (PyUnicode_CompareWithASCIIString(py_type, "raidz2") == 0 ||
	    PyUnicode_CompareWithASCIIString(py_type, "draid2") == 0)
		return (2);
	if (PyUnicode_CompareWithASCIIString(py_type, "raidz3") == 0 ||
	    PyUnicode_CompareWithASCIIString(py_type, "draid3") == 0)
		return (3);
	return (-1);
}

/* --------------------------------------------------------------------------
 * dRAID configuration parsing — format "<ndata>d:<nspares>s"
 * -------------------------------------------------------------------------- */

typedef struct {
	uint64_t ndata;
	uint64_t nspares;
} draid_config_t;

/*
 * Parse a dRAID configuration string of the form
 * "<ndata>d:<nspares>s" (e.g. "3d:1s").
 * nchildren is not encoded in the name — it is derived from len(children).
 * Returns B_TRUE on success, B_FALSE if the string is malformed.
 */
static boolean_t
parse_draid_config(const char *str, draid_config_t *out)
{
	unsigned long ndata, nspares;
	char *endp;

	if (str == NULL || *str == '\0')
		return B_FALSE;

	/* Parse <ndata>d */
	errno = 0;
	ndata = strtoul(str, &endp, 10);
	if (endp == str || *endp != 'd' || errno == ERANGE)
		return B_FALSE;
	str = endp + 1;
	if (*str != ':')
		return B_FALSE;
	str++;

	/* Parse <nspares>s */
	errno = 0;
	nspares = strtoul(str, &endp, 10);
	if (endp == str || *endp != 's' || errno == ERANGE)
		return B_FALSE;
	/* Must be end of string */
	if (*(endp + 1) != '\0')
		return B_FALSE;

	out->ndata = (uint64_t)ndata;
	out->nspares = (uint64_t)nspares;
	return B_TRUE;
}

/* --------------------------------------------------------------------------
 * Single-spec validation — leaf vs virtual invariants, dRAID name syntax.
 * -------------------------------------------------------------------------- */

/*
 * Validate a single struct_vdev_create_spec for internal consistency.
 * Recurses into children.
 * Returns B_TRUE if valid, B_FALSE with exception set if not.
 */
static boolean_t
validate_single_spec(pylibzfs_state_t *state, PyObject *spec,
    const char *context)
{
	PyObject *py_name = NULL;
	PyObject *py_type = NULL;
	PyObject *py_children = NULL;
	PyObject *child = NULL;
	const char *name_str = NULL;
	draid_config_t cfg;
	uint64_t n_uint;
	uint64_t parity_level;
	Py_ssize_t n, i;

	if (!PyObject_TypeCheck(spec, state->struct_vdev_create_spec_type)) {
		PyErr_Format(PyExc_TypeError,
		    "%s: expected struct_vdev_create_spec, got %s",
		    context, Py_TYPE(spec)->tp_name);
		return B_FALSE;
	}

	py_name = PyStructSequence_GET_ITEM(spec, VCSPEC_NAME_IDX);
	py_type = PyStructSequence_GET_ITEM(spec, VCSPEC_TYPE_IDX);
	py_children = PyStructSequence_GET_ITEM(spec, VCSPEC_CHILDREN_IDX);

	if (!PyUnicode_Check(py_type)) {
		PyErr_Format(PyExc_TypeError,
		    "%s: vdev_type must be a string", context);
		return B_FALSE;
	}

	/* ---- Leaf vdevs ---- */
	if (is_leaf_type(py_type)) {
		if (py_name == Py_None || !PyUnicode_Check(py_name)) {
			PyErr_Format(PyExc_ValueError,
			    "%s: leaf vdev type \"%U\" requires a non-None "
			    "\"name\" (device path)",
			    context, py_type);
			return B_FALSE;
		}
		if (py_children != Py_None) {
			PyErr_Format(PyExc_ValueError,
			    "%s: leaf vdev type \"%U\" must not have children",
			    context, py_type);
			return B_FALSE;
		}
		return B_TRUE;
	}

	/* ---- Virtual vdevs (mirror / raidz / draid) ---- */
	if (is_virtual_type(py_type)) {
		if (py_children == Py_None || !PyTuple_Check(py_children)) {
			PyErr_Format(PyExc_ValueError,
			    "%s: virtual vdev type \"%U\" requires children "
			    "(got None or non-tuple)",
			    context, py_type);
			return B_FALSE;
		}

		n = PyTuple_Size(py_children);

		if (is_draid_type(py_type)) {
			/*
			 * dRAID: name must be a parseable config string of
			 * the form "<ndata>d:<nspares>s" (e.g. "3d:1s").
			 * nchildren is implicit from len(children).
			 */
			if (py_name == Py_None || !PyUnicode_Check(py_name)) {
				PyErr_Format(PyExc_ValueError,
				    "%s: dRAID vdev requires a name of the form "
				    "\"<ndata>d:<nspares>s\"",
				    context);
				return B_FALSE;
			}
			name_str = PyUnicode_AsUTF8(py_name);
			if (name_str == NULL)
				return B_FALSE;

			if (!parse_draid_config(name_str, &cfg)) {
				PyErr_Format(PyExc_ValueError,
				    "%s: dRAID name \"%s\" is malformed; "
				    "expected \"<ndata>d:<nspares>s\"",
				    context, name_str);
				return B_FALSE;
			}

			n_uint = (uint64_t)n;
			parity_level = (uint64_t)vdev_parity_level(py_type);

			if (cfg.ndata == 0) {
				PyErr_Format(PyExc_ValueError,
				    "%s: dRAID ndata must be > 0", context);
				return B_FALSE;
			}
			if (n_uint > VDEV_DRAID_MAX_CHILDREN) {
				PyErr_Format(PyExc_ValueError,
				    "%s: dRAID supports at most %u children, "
				    "got %llu",
				    context, VDEV_DRAID_MAX_CHILDREN,
				    (unsigned long long)n_uint);
				return B_FALSE;
			}
			if (cfg.nspares > VDEV_DRAID_MAX_SPARES) {
				PyErr_Format(PyExc_ValueError,
				    "%s: dRAID nspares %llu exceeds maximum "
				    "of %u",
				    context,
				    (unsigned long long)cfg.nspares,
				    VDEV_DRAID_MAX_SPARES);
				return B_FALSE;
			}
			if (n_uint < cfg.ndata + parity_level + cfg.nspares) {
				PyErr_Format(PyExc_ValueError,
				    "%s: dRAID requires at least %llu children "
				    "(ndata=%llu + parity=%llu + nspares=%llu),"
				    " got %llu",
				    context,
				    (unsigned long long)(cfg.ndata +
				    parity_level + cfg.nspares),
				    (unsigned long long)cfg.ndata,
				    (unsigned long long)parity_level,
				    (unsigned long long)cfg.nspares,
				    (unsigned long long)n_uint);
				return B_FALSE;
			}
		} else {
			/*
			 * mirror / raidz: name must be None.
			 */
			if (py_name != Py_None) {
				PyErr_Format(PyExc_ValueError,
				    "%s: vdev type \"%U\" must have name=None",
				    context, py_type);
				return B_FALSE;
			}
		}

		/* Recursively validate children */
		for (i = 0; i < n; i++) {
			child = PyTuple_GET_ITEM(py_children, i);
			if (!validate_single_spec(state, child, context))
				return B_FALSE;
		}

		return B_TRUE;
	}

	/* Unknown type */
	PyErr_Format(PyExc_ValueError,
	    "%s: unknown vdev_type \"%U\". Must be one of: "
	    "disk, file, mirror, raidz1, raidz2, raidz3, "
	    "draid1, draid2, draid3",
	    context, py_type);
	return B_FALSE;
}

/* --------------------------------------------------------------------------
 * Per-category topology constraints.
 * -------------------------------------------------------------------------- */

/*
 * All vdevs in seq must be leaf type.  Used for cache and spare lists.
 */
static boolean_t
validate_all_leaf(PyObject *seq, const char *context)
{
	PyObject *iterator = NULL;
	PyObject *spec = NULL;
	PyObject *py_type = NULL;

	iterator = PyObject_GetIter(seq);
	if (iterator == NULL)
		return B_FALSE;

	while ((spec = PyIter_Next(iterator))) {
		py_type = PyStructSequence_GET_ITEM(spec, VCSPEC_TYPE_IDX);
		if (!is_leaf_type(py_type)) {
			PyErr_Format(PyExc_ValueError,
			    "%s: vdev must be leaf type (disk or file), "
			    "got \"%U\"",
			    context, py_type);
			Py_DECREF(spec);
			Py_DECREF(iterator);
			return B_FALSE;
		}
		Py_DECREF(spec);
	}
	Py_DECREF(iterator);
	return B_TRUE;
}

/*
 * Log vdevs must be leaf or mirror.
 */
static boolean_t
validate_log_vdevs(PyObject *seq)
{
	PyObject *iterator = NULL;
	PyObject *spec = NULL;
	PyObject *py_type = NULL;

	iterator = PyObject_GetIter(seq);
	if (iterator == NULL)
		return B_FALSE;

	while ((spec = PyIter_Next(iterator))) {
		py_type = PyStructSequence_GET_ITEM(spec, VCSPEC_TYPE_IDX);
		if (!is_leaf_type(py_type) && !is_mirror_type(py_type)) {
			PyErr_Format(PyExc_ValueError,
			    "log_vdevs: log vdev must be leaf or mirror, "
			    "got \"%U\"",
			    py_type);
			Py_DECREF(spec);
			Py_DECREF(iterator);
			return B_FALSE;
		}
		Py_DECREF(spec);
	}
	Py_DECREF(iterator);
	return B_TRUE;
}

/*
 * Special and dedup vdevs must not be dRAID, and must have a parity level
 * (redundancy) at least equal to that of the storage vdevs.
 * Allowed types: leaf (only when storage is also stripe), mirror, raidz1/2/3.
 */
static boolean_t
validate_special_dedup_vdevs(PyObject *seq,
    const char *context, PyObject *storage_py_type, int storage_parity)
{
	PyObject *iterator = NULL;
	PyObject *spec = NULL;
	PyObject *py_type = NULL;
	int parity;

	iterator = PyObject_GetIter(seq);
	if (iterator == NULL)
		return B_FALSE;

	while ((spec = PyIter_Next(iterator))) {
		py_type = PyStructSequence_GET_ITEM(spec, VCSPEC_TYPE_IDX);

		if (is_draid_type(py_type)) {
			PyErr_Format(PyExc_ValueError,
			    "%s: dRAID is not permitted for special or "
			    "dedup vdevs",
			    context);
			Py_DECREF(spec);
			Py_DECREF(iterator);
			return B_FALSE;
		}

		if (!is_leaf_type(py_type) && !is_mirror_type(py_type) &&
		    !is_raidz_type(py_type)) {
			PyErr_Format(PyExc_ValueError,
			    "%s: vdev must be leaf, mirror, or raidz type, "
			    "got \"%U\"",
			    context, py_type);
			Py_DECREF(spec);
			Py_DECREF(iterator);
			return B_FALSE;
		}

		parity = vdev_parity_level(py_type);
		if (parity < storage_parity) {
			PyErr_Format(PyExc_ValueError,
			    "%s: vdev type \"%U\" (parity %d) provides less "
			    "redundancy than storage type \"%U\" (parity %d)",
			    context, py_type, parity,
			    storage_py_type, storage_parity);
			Py_DECREF(spec);
			Py_DECREF(iterator);
			return B_FALSE;
		}

		Py_DECREF(spec);
	}
	Py_DECREF(iterator);
	return B_TRUE;
}

/*
 * Check the minimum child count for a storage vdev by type.
 */
static boolean_t
validate_storage_min_children(PyObject *spec, PyObject *py_type)
{
	PyObject *py_children = NULL;
	Py_ssize_t nch;

	py_children = PyStructSequence_GET_ITEM(spec, VCSPEC_CHILDREN_IDX);
	nch = (py_children != Py_None) ? PyTuple_Size(py_children) : 0;

	if (is_mirror_type(py_type) && nch < 2) {
		PyErr_Format(PyExc_ValueError,
		    "mirror vdev requires at least 2 children, got %zd", nch);
		return B_FALSE;
	}
	if (PyUnicode_CompareWithASCIIString(py_type, "raidz1") == 0 &&
	    nch < 2) {
		PyErr_Format(PyExc_ValueError,
		    "raidz1 vdev requires at least 2 children, got %zd", nch);
		return B_FALSE;
	}
	if (PyUnicode_CompareWithASCIIString(py_type, "raidz2") == 0 &&
	    nch < 3) {
		PyErr_Format(PyExc_ValueError,
		    "raidz2 vdev requires at least 3 children, got %zd", nch);
		return B_FALSE;
	}
	if (PyUnicode_CompareWithASCIIString(py_type, "raidz3") == 0 &&
	    nch < 4) {
		PyErr_Format(PyExc_ValueError,
		    "raidz3 vdev requires at least 4 children, got %zd", nch);
		return B_FALSE;
	}
	return B_TRUE;
}

/* --------------------------------------------------------------------------
 * Full pool topology validation.
 * -------------------------------------------------------------------------- */

/*
 * Enforce all topology rules from the plan.  All seq arguments are
 * PySequence_Fast sequences (already validated per-spec); optional
 * categories (cache, log, special, dedup, spare) are NULL when absent.
 */
static boolean_t
validate_pool_topology(
    PyObject *storage_seq,
    PyObject *cache_seq,
    PyObject *log_seq,
    PyObject *special_seq,
    PyObject *dedup_seq,
    PyObject *spare_seq)
{
	PyObject *iterator = NULL;
	PyObject *spec = NULL;
	PyObject *py_type = NULL;
	PyObject *py_children = NULL;
	/*
	 * first_py_type is a borrowed reference from the first spec's type
	 * field.  It remains valid for the duration of the loop because
	 * storage_seq (a PySequence_Fast sequence) keeps all items alive.
	 */
	PyObject *first_py_type = NULL;
	Py_ssize_t nch = 0;
	Py_ssize_t first_child_count = -1;
	int storage_parity = 0;
	boolean_t first = B_TRUE;

	/* storage_vdevs must be non-empty */
	{
		Py_ssize_t storage_len = PyObject_Length(storage_seq);
		if (storage_len < 0)
			return B_FALSE;
		if (storage_len == 0) {
			PyErr_SetString(PyExc_ValueError,
			    "storage_vdevs must be non-empty");
			return B_FALSE;
		}
	}

	/* Per-spec storage checks */
	iterator = PyObject_GetIter(storage_seq);
	if (iterator == NULL)
		return B_FALSE;

	while ((spec = PyIter_Next(iterator))) {
		py_type = PyStructSequence_GET_ITEM(spec, VCSPEC_TYPE_IDX);

		/* Min children for this vdev type */
		if (!validate_storage_min_children(spec, py_type)) {
			Py_DECREF(spec);
			Py_DECREF(iterator);
			return B_FALSE;
		}

		/* All storage vdevs must have the same child count */
		if (is_virtual_type(py_type)) {
			py_children =
			    PyStructSequence_GET_ITEM(spec, VCSPEC_CHILDREN_IDX);
			nch = PyTuple_Size(py_children);
		}

		/* All storage vdevs must share the same type */
		if (first) {
			first_py_type = py_type;
			first = B_FALSE;
			Py_DECREF(spec);
		} else {
			int cmp = PyObject_RichCompareBool(py_type,
			    first_py_type, Py_EQ);
			if (cmp < 0) {
				Py_DECREF(spec);
				Py_DECREF(iterator);
				return B_FALSE;
			}
			if (cmp == 0) {
				PyErr_Format(PyExc_ValueError,
				    "storage_vdevs: all vdevs must share the "
				    "same type; got \"%U\" and \"%U\"",
				    first_py_type, py_type);
				Py_DECREF(spec);
				Py_DECREF(iterator);
				return B_FALSE;
			}
			Py_DECREF(spec);
		}

		if (is_virtual_type(py_type)) {
			if (first_child_count == -1) {
				first_child_count = nch;
			} else if (nch != first_child_count) {
				PyErr_Format(PyExc_ValueError,
				    "storage_vdevs: all \"%U\" vdevs must have "
				    "the same number of children; "
				    "got %zd and %zd",
				    py_type, first_child_count, nch);
				Py_DECREF(iterator);
				return B_FALSE;
			}
		}
	}
	Py_DECREF(iterator);

	/*
	 * Storage parity level, used to enforce that special/dedup vdevs
	 * carry at least equivalent redundancy.  first_py_type is always set
	 * here because an empty storage_seq is already rejected above.
	 */
	storage_parity = vdev_parity_level(first_py_type);

	/* cache vdevs must be leaf */
	if (cache_seq != NULL && !validate_all_leaf(cache_seq, "cache_vdevs"))
		return B_FALSE;

	/* spare vdevs must be leaf */
	if (spare_seq != NULL && !validate_all_leaf(spare_seq, "spare_vdevs"))
		return B_FALSE;

	/* log vdevs must be leaf or mirror */
	if (log_seq != NULL && !validate_log_vdevs(log_seq))
		return B_FALSE;

	/*
	 * Special and dedup vdevs must not be dRAID and must carry at least
	 * as much redundancy as the storage tier.
	 */
	if (special_seq != NULL &&
	    !validate_special_dedup_vdevs(special_seq, "special_vdevs",
	    first_py_type, storage_parity))
		return B_FALSE;

	if (dedup_seq != NULL &&
	    !validate_special_dedup_vdevs(dedup_seq, "dedup_vdevs",
	    first_py_type, storage_parity))
		return B_FALSE;

	return B_TRUE;
}

/* --------------------------------------------------------------------------
 * nvlist construction.
 * -------------------------------------------------------------------------- */

/*
 * Build an nvlist from a single struct_vdev_create_spec, recursively.
 * Caller must fnvlist_free() the returned nvlist.
 * Returns NULL with exception set on error.
 */
static nvlist_t *
build_vdev_spec_nvlist(PyObject *spec)
{
	PyObject *py_name = PyStructSequence_GET_ITEM(spec, VCSPEC_NAME_IDX);
	PyObject *py_type = PyStructSequence_GET_ITEM(spec, VCSPEC_TYPE_IDX);
	PyObject *py_children = PyStructSequence_GET_ITEM(spec, VCSPEC_CHILDREN_IDX);
	const char *path = NULL;
	const char *name_str = NULL;
	nvlist_t *nvl = NULL;
	nvlist_t **child_nvls = NULL;
	draid_config_t cfg;
	uint64_t parity;
	uint64_t ngroups;
	uint64_t stripe_w;
	uint64_t data_drives;
	PyObject *child = NULL;
	Py_ssize_t n, i, j;

	nvl = fnvlist_alloc();

	/* ---- Leaf vdevs ---- */
	if (is_leaf_type(py_type)) {
		path = PyUnicode_AsUTF8(py_name);
		if (path == NULL) {
			fnvlist_free(nvl);
			return NULL;
		}
		fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE,
		    PyUnicode_CompareWithASCIIString(py_type,
		    VDEV_TYPE_DISK) == 0 ? VDEV_TYPE_DISK : VDEV_TYPE_FILE);
		fnvlist_add_string(nvl, ZPOOL_CONFIG_PATH, path);
		/*
		 * For disk vdevs, determine whether the path refers to a
		 * whole disk (one that can be EFI-labelled by libzfs) or a
		 * partition.  zfs_dev_is_whole_disk() opens the device and
		 * tries efi_alloc_and_init(); it returns B_FALSE for files,
		 * partitions, or anything it cannot open.
		 */
		if (PyUnicode_CompareWithASCIIString(py_type,
		    VDEV_TYPE_DISK) == 0) {
			boolean_t whole_disk;
			Py_BEGIN_ALLOW_THREADS
			whole_disk = zfs_dev_is_whole_disk(path);
			Py_END_ALLOW_THREADS
			fnvlist_add_uint64(nvl, ZPOOL_CONFIG_WHOLE_DISK,
			    (uint64_t)whole_disk);
		}
		return nvl;
	}

	/* For all virtual vdevs, get the child count now */
	n = PyTuple_Size(py_children);

	/* ---- mirror ---- */
	if (is_mirror_type(py_type)) {
		fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_MIRROR);

	/* ---- raidz{1,2,3} ---- */
	} else if (is_raidz_type(py_type)) {
		parity = (uint64_t)vdev_parity_level(py_type);
		fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_RAIDZ);
		fnvlist_add_uint64(nvl, ZPOOL_CONFIG_NPARITY, parity);

	/* ---- draid{1,2,3} ---- */
	} else if (is_draid_type(py_type)) {
		parity = (uint64_t)vdev_parity_level(py_type);
		name_str = PyUnicode_AsUTF8(py_name);

		if (name_str == NULL) {
			fnvlist_free(nvl);
			return NULL;
		}
		if (!parse_draid_config(name_str, &cfg)) {
			PyErr_Format(PyExc_RuntimeError,
			    "Internal error: dRAID config parse failed "
			    "for \"%s\"", name_str);
			fnvlist_free(nvl);
			return NULL;
		}
		/*
		 * Calculate the minimum number of groups required to fill a
		 * slice.  This is the LCM of the stripe width (ndata + parity)
		 * and the number of data drives (children - nspares).
		 * Validation in validate_single_spec() guarantees
		 * children > nspares, so data_drives > 0 and the loop
		 * terminates.
		 */
		stripe_w = cfg.ndata + parity;
		data_drives = (uint64_t)n - cfg.nspares;
		ngroups = 1;
		while ((ngroups * stripe_w) % data_drives != 0)
			ngroups++;

		fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_DRAID);
		fnvlist_add_uint64(nvl, ZPOOL_CONFIG_NPARITY, parity);
		fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAID_NDATA, cfg.ndata);
		fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAID_NSPARES, cfg.nspares);
		fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAID_NGROUPS, ngroups);

	} else {
		PyErr_Format(PyExc_ValueError,
		    "Internal error: unknown vdev type \"%U\"", py_type);
		fnvlist_free(nvl);
		return NULL;
	}

	/* ---- Build children array for virtual vdevs ---- */
	if (n > 0) {
		child_nvls = PyMem_RawCalloc(n, sizeof (nvlist_t *));
		if (child_nvls == NULL) {
			PyErr_NoMemory();
			fnvlist_free(nvl);
			return NULL;
		}

		for (i = 0; i < n; i++) {
			child = PyTuple_GET_ITEM(py_children, i);
			child_nvls[i] = build_vdev_spec_nvlist(child);
			if (child_nvls[i] == NULL) {
				for (j = 0; j < i; j++)
					fnvlist_free(child_nvls[j]);
				PyMem_RawFree(child_nvls);
				fnvlist_free(nvl);
				return NULL;
			}
		}

		fnvlist_add_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
		    (const nvlist_t * const *)child_nvls, (uint_t)n);

		for (i = 0; i < n; i++)
			fnvlist_free(child_nvls[i]);
		PyMem_RawFree(child_nvls);
	}

	return nvl;
}

/*
 * Build an array of nvlists from a PySequence_Fast sequence of specs.
 * Caller must free with free_nvlist_array().
 * Returns NULL (with exception set) on error.
 */
static nvlist_t **
build_nvlist_array(PyObject *seq, Py_ssize_t n)
{
	nvlist_t **arr = NULL;
	PyObject *iterator = NULL;
	PyObject *spec = NULL;
	Py_ssize_t i = 0, j;

	arr = PyMem_RawCalloc(n, sizeof (nvlist_t *));
	if (arr == NULL) {
		PyErr_NoMemory();
		return NULL;
	}

	iterator = PyObject_GetIter(seq);
	if (iterator == NULL) {
		PyMem_RawFree(arr);
		return NULL;
	}

	while ((spec = PyIter_Next(iterator))) {
		arr[i] = build_vdev_spec_nvlist(spec);
		Py_DECREF(spec);
		if (arr[i] == NULL) {
			for (j = 0; j < i; j++)
				fnvlist_free(arr[j]);
			PyMem_RawFree(arr);
			Py_DECREF(iterator);
			return NULL;
		}
		i++;
	}
	Py_DECREF(iterator);

	return arr;
}

static void
free_nvlist_array(nvlist_t **arr, Py_ssize_t n)
{
	Py_ssize_t i;

	if (arr == NULL)
		return;
	for (i = 0; i < n; i++)
		fnvlist_free(arr[i]);
	PyMem_RawFree(arr);
}

/*
 * Convert a pool-properties Python dict to an nvlist.
 * Keys may be str (looked up via zpool_name_to_prop()) or any integer
 * compatible with zpool_prop_t (e.g. ZPoolProperty IntEnum values).
 * Values are stringified.
 * Returns NULL with exception set on error.
 */
static nvlist_t *
py_zpoolprops_to_nvlist(PyObject *pyprops)
{
	nvlist_t *nvl = NULL;
	PyObject *key = NULL;
	PyObject *value = NULL;
	PyObject *pystrval = NULL;
	zpool_prop_t zprop;
	const char *kstr = NULL;
	const char *prop_val = NULL;
	long lval;
	Py_ssize_t pos = 0;

	if (!PyDict_Check(pyprops)) {
		PyErr_SetString(PyExc_TypeError,
		    "Pool properties must be a dictionary");
		return NULL;
	}

	nvl = fnvlist_alloc();

	while (PyDict_Next(pyprops, &pos, &key, &value)) {
		if (PyUnicode_Check(key)) {
			kstr = PyUnicode_AsUTF8(key);
			if (kstr == NULL) {
				fnvlist_free(nvl);
				return NULL;
			}
			zprop = zpool_name_to_prop(kstr);
			if (zprop == ZPOOL_PROP_INVAL) {
				PyErr_Format(PyExc_ValueError,
				    "\"%s\": not a valid zpool property", kstr);
				fnvlist_free(nvl);
				return NULL;
			}
		} else if (PyLong_Check(key)) {
			lval = PyLong_AsLong(key);
			if (lval == -1 && PyErr_Occurred()) {
				fnvlist_free(nvl);
				return NULL;
			}
			if (lval < 0 || lval >= ZPOOL_NUM_PROPS) {
				PyErr_Format(PyExc_ValueError,
				    "%ld: not a valid zpool property value",
				    lval);
				fnvlist_free(nvl);
				return NULL;
			}
			zprop = (zpool_prop_t)lval;
		} else {
			PyErr_SetString(PyExc_TypeError,
			    "Pool property keys must be str or ZPoolProperty");
			fnvlist_free(nvl);
			return NULL;
		}

		pystrval = PyObject_Str(value);
		if (pystrval == NULL) {
			fnvlist_free(nvl);
			return NULL;
		}

		prop_val = PyUnicode_AsUTF8(pystrval);
		if (prop_val == NULL) {
			Py_DECREF(pystrval);
			fnvlist_free(nvl);
			return NULL;
		}

		fnvlist_add_string(nvl, zpool_prop_to_name(zprop), prop_val);
		Py_DECREF(pystrval);
	}

	return nvl;
}

/* --------------------------------------------------------------------------
 * Shared helper: validate + convert a Python vdev iterable.
 * -------------------------------------------------------------------------- */

/*
 * Convert pyobj (an iterable of struct_vdev_create_spec, or NULL/None) into a
 * PySequence_Fast sequence and validate each element.
 *
 * On success: *out_seq holds a new reference (caller must Py_DECREF).
 * If pyobj is NULL or Py_None: *out_seq = NULL.
 * On error: returns B_FALSE with exception set.
 */
static boolean_t
validate_vdev_list(pylibzfs_state_t *state, PyObject *pyobj,
    const char *argname, PyObject **out_seq)
{
	PyObject *seq = NULL;
	PyObject *iterator = NULL;
	PyObject *item = NULL;

	if (pyobj == NULL || pyobj == Py_None) {
		*out_seq = NULL;
		return B_TRUE;
	}

	seq = PySequence_Fast(pyobj, "vdev list must be a sequence");
	if (seq == NULL)
		return B_FALSE;

	iterator = PyObject_GetIter(seq);
	if (iterator == NULL) {
		Py_DECREF(seq);
		return B_FALSE;
	}

	while ((item = PyIter_Next(iterator))) {
		boolean_t ok = validate_single_spec(state, item, argname);
		Py_DECREF(item);
		if (!ok) {
			Py_DECREF(iterator);
			Py_DECREF(seq);
			return B_FALSE;
		}
	}
	Py_DECREF(iterator);

	*out_seq = seq;
	return B_TRUE;
}

/* --------------------------------------------------------------------------
 * create_vdev_spec() — module-level factory function.
 * -------------------------------------------------------------------------- */

PyObject *
py_create_vdev_spec(PyObject *self, PyObject *args, PyObject *kwargs)
{
	pylibzfs_state_t *state = NULL;
	PyObject *py_vtype = NULL;
	PyObject *py_name = Py_None;
	PyObject *py_children = Py_None;
	PyObject *children_tuple = NULL;
	PyObject *fast = NULL;
	PyObject *item = NULL;
	PyObject *out = NULL;
	Py_ssize_t n, i;
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

	if (!is_valid_vdev_type(py_vtype)) {
		PyErr_Format(PyExc_ValueError,
		    "Invalid vdev_type \"%U\". Must be one of: "
		    "disk, file, mirror, raidz1, raidz2, raidz3, "
		    "draid1, draid2, draid3",
		    py_vtype);
		return NULL;
	}

	state = (pylibzfs_state_t *)PyModule_GetState(self);
	PYZFS_ASSERT(state, "Failed to get module state");

	/* Normalize children: any sequence → tuple, or None → None */
	if (py_children != NULL && py_children != Py_None) {
		fast = PySequence_Fast(py_children,
		    "children must be a sequence");
		if (fast == NULL)
			return NULL;

		n = PySequence_Fast_GET_SIZE(fast);
		children_tuple = PyTuple_New(n);
		if (children_tuple == NULL) {
			Py_DECREF(fast);
			return NULL;
		}

		for (i = 0; i < n; i++) {
			item = PySequence_Fast_GET_ITEM(fast, i);
			Py_INCREF(item);
			PyTuple_SET_ITEM(children_tuple, i, item);
		}
		Py_DECREF(fast);
	} else {
		children_tuple = Py_NewRef(Py_None);
	}

	/* Allocate the struct sequence */
	out = PyStructSequence_New(state->struct_vdev_create_spec_type);
	if (out == NULL) {
		Py_DECREF(children_tuple);
		return NULL;
	}

	/* name field */
	if (py_name == NULL || py_name == Py_None) {
		PyStructSequence_SetItem(out, VCSPEC_NAME_IDX,
		    Py_NewRef(Py_None));
	} else if (PyUnicode_Check(py_name)) {
		PyStructSequence_SetItem(out, VCSPEC_NAME_IDX,
		    Py_NewRef(py_name));
	} else {
		PyErr_SetString(PyExc_TypeError, "name must be a string or None");
		Py_DECREF(out);
		Py_DECREF(children_tuple);
		return NULL;
	}

	PyStructSequence_SetItem(out, VCSPEC_TYPE_IDX, Py_NewRef(py_vtype));
	/* children_tuple ref is stolen by SetItem */
	PyStructSequence_SetItem(out, VCSPEC_CHILDREN_IDX, children_tuple);

	/* Full validation of the constructed spec */
	if (!validate_single_spec(state, out, "create_vdev_spec")) {
		Py_DECREF(out);
		return NULL;
	}

	return out;
}

/* --------------------------------------------------------------------------
 * ZFS.create_pool() — method on the ZFS class.
 * -------------------------------------------------------------------------- */

/*
 * Build the root vdev nvlist tree suitable for passing to zpool_create().
 *
 * All seq arguments are validated PySequence_Fast sequences; optional
 * categories (cache, log, special, dedup, spare) are NULL when absent.
 * Per-category nvlist arrays are allocated, tagged with the appropriate
 * allocation bias, composed into the root nvlist (which copies them via
 * fnvlist_add_nvlist_array), then freed before returning.  Only the root
 * nvlist is returned to the caller.
 *
 * Returns a new nvlist_t on success (caller must fnvlist_free it),
 * or NULL with a Python exception set on error.
 */
static nvlist_t *
build_pool_root_nvlist(
    PyObject *storage_seq,
    PyObject *cache_seq,
    PyObject *log_seq,
    PyObject *special_seq,
    PyObject *dedup_seq,
    PyObject *spare_seq)
{
	nvlist_t **storage_nvls = NULL;
	nvlist_t **cache_nvls = NULL;
	nvlist_t **log_nvls = NULL;
	nvlist_t **special_nvls = NULL;
	nvlist_t **dedup_nvls = NULL;
	nvlist_t **spare_nvls = NULL;
	nvlist_t **children = NULL;
	nvlist_t *root_nvl = NULL;
	Py_ssize_t storage_n, cache_n, log_n, special_n, dedup_n, spare_n;
	Py_ssize_t total;
	Py_ssize_t idx;
	Py_ssize_t i;

	storage_n = PyObject_Length(storage_seq);
	cache_n = cache_seq != NULL ? PyObject_Length(cache_seq) : 0;
	log_n = log_seq != NULL ? PyObject_Length(log_seq) : 0;
	special_n = special_seq != NULL ? PyObject_Length(special_seq) : 0;
	dedup_n = dedup_seq != NULL ? PyObject_Length(dedup_seq) : 0;
	spare_n = spare_seq != NULL ? PyObject_Length(spare_seq) : 0;

	if (storage_n < 0 || cache_n < 0 || log_n < 0 ||
	    special_n < 0 || dedup_n < 0 || spare_n < 0)
		return NULL;

	if (storage_n > 0) {
		storage_nvls = build_nvlist_array(storage_seq, storage_n);
		if (storage_nvls == NULL)
			goto fail;
	}

	if (log_n > 0) {
		log_nvls = build_nvlist_array(log_seq, log_n);
		if (log_nvls == NULL)
			goto fail;
		for (i = 0; i < log_n; i++) {
			fnvlist_add_uint64(log_nvls[i], ZPOOL_CONFIG_IS_LOG, 1);
			fnvlist_add_string(log_nvls[i],
			    ZPOOL_CONFIG_ALLOCATION_BIAS, VDEV_ALLOC_BIAS_LOG);
		}
	}

	if (special_n > 0) {
		special_nvls = build_nvlist_array(special_seq, special_n);
		if (special_nvls == NULL)
			goto fail;
		for (i = 0; i < special_n; i++) {
			fnvlist_add_string(special_nvls[i],
			    ZPOOL_CONFIG_ALLOCATION_BIAS, VDEV_ALLOC_BIAS_SPECIAL);
		}
	}

	if (dedup_n > 0) {
		dedup_nvls = build_nvlist_array(dedup_seq, dedup_n);
		if (dedup_nvls == NULL)
			goto fail;
		for (i = 0; i < dedup_n; i++) {
			fnvlist_add_string(dedup_nvls[i],
			    ZPOOL_CONFIG_ALLOCATION_BIAS, VDEV_ALLOC_BIAS_DEDUP);
		}
	}

	if (spare_n > 0) {
		spare_nvls = build_nvlist_array(spare_seq, spare_n);
		if (spare_nvls == NULL)
			goto fail;
	}

	if (cache_n > 0) {
		cache_nvls = build_nvlist_array(cache_seq, cache_n);
		if (cache_nvls == NULL)
			goto fail;
	}

	root_nvl = fnvlist_alloc();
	fnvlist_add_string(root_nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);

	/*
	 * The root vdev's children array contains storage, log, special, and
	 * dedup vdevs in that order.  Spares and cache are top-level keys on
	 * the root nvlist rather than children.
	 */
	total = storage_n + log_n + special_n + dedup_n;
	if (total > 0) {
		idx = 0;
		children = PyMem_RawCalloc(total, sizeof (nvlist_t *));
		if (children == NULL) {
			PyErr_NoMemory();
			goto fail;
		}

		for (i = 0; i < storage_n; i++)
			children[idx++] = storage_nvls[i];
		for (i = 0; i < log_n; i++)
			children[idx++] = log_nvls[i];
		for (i = 0; i < special_n; i++)
			children[idx++] = special_nvls[i];
		for (i = 0; i < dedup_n; i++)
			children[idx++] = dedup_nvls[i];

		fnvlist_add_nvlist_array(root_nvl, ZPOOL_CONFIG_CHILDREN,
		    (const nvlist_t * const *)children, (uint_t)total);
		PyMem_RawFree(children);
		children = NULL;
	}

	if (spare_n > 0) {
		fnvlist_add_nvlist_array(root_nvl, ZPOOL_CONFIG_SPARES,
		    (const nvlist_t * const *)spare_nvls, (uint_t)spare_n);
	}

	if (cache_n > 0) {
		fnvlist_add_nvlist_array(root_nvl, ZPOOL_CONFIG_L2CACHE,
		    (const nvlist_t * const *)cache_nvls, (uint_t)cache_n);
	}

	/*
	 * fnvlist_add_nvlist_array copies its inputs; all intermediate arrays
	 * can be freed now that the root nvlist is complete.
	 */
	Py_BEGIN_ALLOW_THREADS
	free_nvlist_array(storage_nvls, storage_n);
	free_nvlist_array(log_nvls, log_n);
	free_nvlist_array(special_nvls, special_n);
	free_nvlist_array(dedup_nvls, dedup_n);
	free_nvlist_array(spare_nvls, spare_n);
	free_nvlist_array(cache_nvls, cache_n);
	Py_END_ALLOW_THREADS

	return root_nvl;

fail:
	Py_BEGIN_ALLOW_THREADS
	free_nvlist_array(storage_nvls, storage_n);
	free_nvlist_array(log_nvls, log_n);
	free_nvlist_array(special_nvls, special_n);
	free_nvlist_array(dedup_nvls, dedup_n);
	free_nvlist_array(spare_nvls, spare_n);
	free_nvlist_array(cache_nvls, cache_n);
	fnvlist_free(root_nvl);
	PyMem_RawFree(children);
	Py_END_ALLOW_THREADS
	return NULL;
}

/*
 * Build a props nvlist suitable for zpool_create(), pre-populated with
 * feature@<fi_uname>=enabled for every feature the running kernel module
 * supports.
 *
 * Always returns a new nvlist_t; fnvlist_alloc() and fnvlist_add_string()
 * are fatal variants that abort() on allocation failure.
 */
static nvlist_t *
build_default_pool_props(void)
{
	nvlist_t *props;
	char propname[MAXPATHLEN];

	props = fnvlist_alloc();

	for (spa_feature_t i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t *feat = &spa_feature_table[i];

		if (!feat->fi_zfs_mod_supported)
			continue;

		(void) snprintf(propname, sizeof (propname),
		    "feature@%s", feat->fi_uname);
		fnvlist_add_string(props, propname, ZFS_FEATURE_ENABLED);
	}

	return props;
}

PyObject *
py_zfs_create_pool(PyObject *self, PyObject *args, PyObject *kwargs)
{
	py_zfs_t *plz = (py_zfs_t *)self;
	pylibzfs_state_t *state = py_get_module_state(plz);

	const char *pool_name = NULL;
	PyObject *py_storage = NULL;
	PyObject *py_cache = NULL;
	PyObject *py_log = NULL;
	PyObject *py_special = NULL;
	PyObject *py_dedup = NULL;
	PyObject *py_spare = NULL;
	PyObject *py_props = NULL;
	PyObject *py_fsprops = NULL;
	boolean_t force = B_FALSE;

	PyObject *storage_seq = NULL, *cache_seq = NULL, *log_seq = NULL;
	PyObject *special_seq = NULL, *dedup_seq = NULL, *spare_seq = NULL;

	nvlist_t *root_nvl = NULL;
	nvlist_t *props_nvl = NULL;
	nvlist_t *fsprops_nvl = NULL;

	py_zfs_error_t zfs_err;
	int err;

	char *kwnames[] = {
		"name", "storage_vdevs", "cache_vdevs", "log_vdevs",
		"special_vdevs", "dedup_vdevs", "spare_vdevs",
		"properties", "filesystem_properties", "force",
		NULL
	};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$sOOOOOOOOp",
	    kwnames,
	    &pool_name, &py_storage, &py_cache, &py_log,
	    &py_special, &py_dedup, &py_spare,
	    &py_props, &py_fsprops, &force))
		return NULL;

	if (pool_name == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "\"name\" keyword argument is required");
		return NULL;
	}

	if (py_storage == NULL || py_storage == Py_None) {
		PyErr_SetString(PyExc_ValueError,
		    "\"storage_vdevs\" is required and must be non-empty");
		return NULL;
	}

	/* Validate and normalise all vdev lists */
	if (!validate_vdev_list(state, py_storage, "storage_vdevs", &storage_seq))
		goto fail;

	if (!validate_vdev_list(state, py_cache, "cache_vdevs", &cache_seq))
		goto fail;

	if (!validate_vdev_list(state, py_log, "log_vdevs", &log_seq))
		goto fail;

	if (!validate_vdev_list(state, py_special, "special_vdevs", &special_seq))
		goto fail;

	if (!validate_vdev_list(state, py_dedup, "dedup_vdevs", &dedup_seq))
		goto fail;

	if (!validate_vdev_list(state, py_spare, "spare_vdevs", &spare_seq))
		goto fail;

	/* Topology validation (skip when force=True) */
	if (!force) {
		if (!validate_pool_topology(storage_seq, cache_seq, log_seq,
		    special_seq, dedup_seq, spare_seq))
			goto fail;
	}

	/* Audit before making any kernel calls */
	if (PySys_Audit(PYLIBZFS_MODULE_NAME ".create_pool", "s",
	    pool_name) < 0)
		goto fail;

	root_nvl = build_pool_root_nvlist(storage_seq, cache_seq, log_seq,
	    special_seq, dedup_seq, spare_seq);
	if (root_nvl == NULL)
		goto fail;

	/* Cannot fail: fnvlist_alloc() aborts on OOM */
	Py_BEGIN_ALLOW_THREADS
	props_nvl = build_default_pool_props();
	Py_END_ALLOW_THREADS

	if (py_props != NULL && py_props != Py_None) {
		nvlist_t *user_props = py_zpoolprops_to_nvlist(py_props);
		if (user_props == NULL)
			goto fail;
		/* Merge caller props on top of defaults; caller wins on collision */
		fnvlist_merge(props_nvl, user_props);
		fnvlist_free(user_props);
	}

	if (py_fsprops != NULL && py_fsprops != Py_None) {
		fsprops_nvl = py_zfsprops_to_nvlist(state, py_fsprops,
		    ZFS_TYPE_FILESYSTEM, B_FALSE);
		if (fsprops_nvl == NULL)
			goto fail;
	}

	Py_BEGIN_ALLOW_THREADS
	PY_ZFS_LOCK(plz);
	err = zpool_create(plz->lzh, pool_name, root_nvl,
	    props_nvl, fsprops_nvl);
	if (err)
		py_get_zfs_error(plz->lzh, &zfs_err);

	fnvlist_free(root_nvl);
	fnvlist_free(props_nvl);
	fnvlist_free(fsprops_nvl);

	PY_ZFS_UNLOCK(plz);
	Py_END_ALLOW_THREADS

	Py_XDECREF(storage_seq);
	Py_XDECREF(cache_seq);
	Py_XDECREF(log_seq);
	Py_XDECREF(special_seq);
	Py_XDECREF(dedup_seq);
	Py_XDECREF(spare_seq);

	if (err) {
		set_exc_from_libzfs(&zfs_err, "zpool_create() failed");
		return NULL;
	}

	err = py_log_history_fmt(plz, "zpool create %s", pool_name);
	if (err)
		return NULL;

	Py_RETURN_NONE;

fail:
	fnvlist_free(root_nvl);
	fnvlist_free(props_nvl);
	fnvlist_free(fsprops_nvl);
	Py_XDECREF(storage_seq);
	Py_XDECREF(cache_seq);
	Py_XDECREF(log_seq);
	Py_XDECREF(special_seq);
	Py_XDECREF(dedup_seq);
	Py_XDECREF(spare_seq);
	return NULL;
}

/* --------------------------------------------------------------------------
 * Module-state initialisation — called from init_py_zfs_state().
 * -------------------------------------------------------------------------- */

int
init_vdev_create_spec_state(pylibzfs_state_t *state, PyObject *module)
{
	PyTypeObject *obj;

	obj = PyStructSequence_NewType(&vdev_create_spec_desc);
	PYZFS_ASSERT(obj, "Failed to create struct_vdev_create_spec type");

	state->struct_vdev_create_spec_type = obj;

	return PyModule_AddObjectRef(module, "struct_vdev_create_spec",
	    (PyObject *)obj);
}
