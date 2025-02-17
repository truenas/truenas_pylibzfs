#include "../truenas_pylibzfs.h"

/*
 * This function converts the nvlist containing user props
 * into a python dictionary of form:
 * {"<nvpair_name>": "<nvpair_value_string"}
 */
PyObject *user_props_nvlist_to_py_dict(nvlist_t *userprops)
{
	PyObject *d = NULL;
	nvpair_t *elem;

	d = PyDict_New();
	if (d == NULL)
		return NULL;

	for (elem = nvlist_next_nvpair(userprops, NULL);
	    elem != NULL;
	    elem = nvlist_next_nvpair(userprops, elem)) {
		const char *name = nvpair_name(elem);
		const char *cval;
		nvlist_t *nvl;

		PYZFS_ASSERT(
			(nvpair_type(elem) == DATA_TYPE_NVLIST),
			"Unexpected nvpair data type in user props"

		);

		nvl = fnvpair_value_nvlist(elem);
		cval = fnvlist_lookup_string(nvl, ZPROP_VALUE);

		PyObject *val = PyUnicode_FromString(cval);
		if (val == NULL) {
			Py_DECREF(d);
			return NULL;
		}

		if (PyDict_SetItemString(d, name, val) < 0)  {
			Py_DECREF(d);
			Py_DECREF(val);
			return NULL;
		}

		Py_DECREF(val);
	}

	return d;
}

/*
 * Convert a python dictionary of user props into an nvlist
 * for insertion as user properties.
 */
nvlist_t *py_userprops_dict_to_nvlist(PyObject *pyprops)
{
	nvlist_t *nvl = fnvlist_alloc();
	PyObject *key, *value;
	Py_ssize_t pos = 0;

	if (!PyDict_Check(pyprops)) {
		PyErr_SetString(PyExc_TypeError,
				"Not a dictionary.");
		fnvlist_free(nvl);
		return NULL;
	}

	while (PyDict_Next(pyprops, &pos, &key, &value)) {
		const char *name = NULL;
		const char *cval = NULL;

		name = PyUnicode_AsUTF8(key);
		if (name == NULL) {
			fnvlist_free(nvl);
			return NULL;
		}

		if (strstr(name, ":") == NULL) {
			PyErr_Format(PyExc_ValueError,
				     "%s: user properties must "
				     "contain a colon (:) in their "
				     "name.", name);

			fnvlist_free(nvl);
			return NULL;
		} else if (strlen(name) > ZAP_MAXNAMELEN) {
			PyErr_Format(PyExc_ValueError,
				     "%s: property name exceeds max "
				     "length of %d.", name,
				     ZAP_MAXNAMELEN);

			fnvlist_free(nvl);
			return NULL;
		}

		cval = PyUnicode_AsUTF8(value);
		if (cval == NULL) {
			fnvlist_free(nvl);
			return NULL;
		}

		fnvlist_add_string(nvl, name, cval);
	}

	return nvl;
}

/*
 * Below are functions that convert various forms of python ZFS properties
 * into an nvlist suitable for bulk changes.
 */
PyObject *py_nvlist_names_tuple(nvlist_t *nvl)
{
	PyObject *l = NULL;
	PyObject *out = NULL;
	nvpair_t *elem;

	l = PyList_New(0);
	if (l == NULL)
		return NULL;

	for (elem = nvlist_next_nvpair(nvl, NULL); elem != NULL;
	    elem = nvlist_next_nvpair(nvl, elem)) {
		PyObject *name = PyUnicode_FromString(nvpair_name(elem));
		if (name == NULL) {
			Py_DECREF(l);
			return NULL;
		}

		if (PyList_Append(l, name) < 0) {
			Py_DECREF(name);
			Py_DECREF(l);
			return NULL;
		}
		Py_DECREF(name);
	}

	out = PyList_AsTuple(l);
	Py_DECREF(l);
	return out;
}

static
boolean_t get_prop_from_key(PyObject *zfs_property_enum,
			    PyObject *key,
			    zfs_prop_t *pprop)
{
	long lval;

	if (PyUnicode_Check(key)) {
		const char *val = PyUnicode_AsUTF8(key);
		zfs_prop_t prop;
		if (val == NULL)
			return B_FALSE;

		prop = zfs_name_to_prop(val);
		if (prop == ZPROP_INVAL) {
			PyErr_Format(PyExc_ValueError,
				     "%s: not a valid ZFS property.",
				     val);
			return B_FALSE;
		}
		*pprop = prop;
		return B_TRUE;
	}

	if (!PyObject_IsInstance(key, zfs_property_enum)) {
		PyObject *repr = PyObject_Repr(key);
		PyErr_Format(PyExc_TypeError,
			     "%V: unexpected key type. "
			     "Expected a truenas_pylibzfs.ZFSProperty "
			     "instance.", repr, "UNKNOWN");

		Py_XDECREF(repr);
		return B_FALSE;
	}

	lval = PyLong_AsLong(key);
	PYZFS_ASSERT(
		((lval >= 0) && (lval < ZFS_NUM_PROPS)),
		"Unexpected ZFSProperty enum value"
	);

	*pprop = lval;
	return B_TRUE;
}

static
nvlist_t *py_zfsprops_dict_to_nvlist(pylibzfs_state_t *state,
				     PyObject *pyprops,
				     zfs_type_t type,
				     boolean_t allow_ro)
{
	nvlist_t *nvl = fnvlist_alloc();
	PyObject *key, *value;
	Py_ssize_t pos = 0;

	while (PyDict_Next(pyprops, &pos, &key, &value)) {
		PyObject *pystrval = NULL;
		const char *cval = NULL;
		zfs_prop_t zprop;

		if (!get_prop_from_key(state->zfs_property_enum, key, &zprop)) {
			fnvlist_free(nvl);
			return NULL;
		}

		if (zfs_prop_readonly(zprop) && !allow_ro) {
			PyErr_Format(PyExc_ValueError,
				     "%s: ZFS property is readonly.",
				     zfs_prop_to_name(zprop));
			fnvlist_free(nvl);
			return NULL;
		}

		if (!py_zfs_prop_valid_for_type(zprop, type)) {
			fnvlist_free(nvl);
			return NULL;
		}

		/*
		 * we support either {"raw": "on"} or "raw" for
		 * the dictionary value. This is to make it so
		 * that all variants of getting properties can be
		 * converted directly into nvlist
		 */
		if (PyDict_Check(value)) {
			PyObject *pyval = NULL;
			PyObject *pykey = NULL;

			/* first try with raw key */
			pykey = PyUnicode_FromString("raw");
			if (pykey == NULL) {
				fnvlist_free(nvl);
				return NULL;
			}

			pyval = PyDict_GetItem(value, pykey);
			if (pyval == NULL) {
				/* raw key wasn't present, try with "value" */
				Py_DECREF(pykey);
				pykey = PyUnicode_FromString("value");
				if (pykey == NULL) {
					fnvlist_free(nvl);
					return NULL;
				}
				pyval = PyDict_GetItem(value, pykey);
			}

			Py_DECREF(pykey);

			if (pyval == NULL) {
				PyErr_SetString(PyExc_ValueError,
						"Property entry dict must "
						"contain either a raw or value "
						"key.");

				fnvlist_free(nvl);
				return NULL;
			}

			pystrval = PyObject_Str(pyval);
			if (pystrval == NULL) {
				fnvlist_free(nvl);
				return NULL;
			}
		} else {
			// Have python do heavy lifting of converting to string
			pystrval = PyObject_Str(value);
			if (pystrval == NULL) {
				fnvlist_free(nvl);
				return NULL;
			}
		}

		cval = PyUnicode_AsUTF8(pystrval);
		if (cval == NULL) {
			fnvlist_free(nvl);
			Py_DECREF(pystrval);
			return NULL;
		}

		fnvlist_add_string(nvl, zfs_prop_to_name(zprop), cval);
		Py_DECREF(pystrval);
	}

	return nvl;
}

static
boolean_t py_prop_struct_to_nvlist(PyObject *propstruct,
				   zfs_prop_t prop,
				   nvlist_t *nvl)
{
	PyObject *value;
	PyObject *pystrval;
	const char *cval;

	// Preference is for raw value since we know ZFS is okay with it.
	value = PyStructSequence_GET_ITEM(propstruct, 1);
	if (value == Py_None) {
		// We don't have a raw value. This means someone hand-rolled
		// the struct and so we need the parsed value
		value = PyStructSequence_GET_ITEM(propstruct, 0);
	}

	if (value == Py_None) {
		pystrval = PyUnicode_FromString(LIBZFS_NONE_VALUE);
	} else {
		pystrval = PyObject_Str(value);
	}

	if (pystrval == NULL)
		return B_FALSE;

	cval = PyUnicode_AsUTF8(pystrval);
	if (cval == NULL) {
		Py_DECREF(pystrval);
		return B_FALSE;
	}

	fnvlist_add_string(nvl, zfs_prop_to_name(prop), cval);
	Py_DECREF(pystrval);
	return B_TRUE;
}

static
nvlist_t *py_zfsprops_struct_to_nvlist(pylibzfs_state_t *state,
				       PyObject *pyprops,
				       zfs_type_t type,
				       boolean_t allow_ro)
{
	nvlist_t *nvl = fnvlist_alloc();
	int idx;

	for (idx = 0; idx < state->struct_zfs_prop_desc.n_in_sequence; idx++) {
		const char *name = state->struct_prop_fields[idx].name;
		zfs_prop_t zprop = zfs_prop_table[idx].prop;
		PyObject *value = NULL;

                // Check if this is a hidden property
		if (strcmp(name, PyStructSequence_UnnamedField) == 0)
			continue;

		value = PyStructSequence_GET_ITEM(pyprops, idx);
		if (value == Py_None)
			continue;

		if (zfs_prop_readonly(zprop) && !allow_ro) {
			PyErr_Format(PyExc_ValueError,
				     "%s: ZFS property is readonly.",
				     zfs_prop_to_name(zprop));
			fnvlist_free(nvl);
			return NULL;
		}

		if (!py_zfs_prop_valid_for_type(zprop, type)) {
			fnvlist_free(nvl);
			return NULL;
		}

		if (!py_prop_struct_to_nvlist(value, zprop, nvl)) {
			fnvlist_free(nvl);
			return NULL;
		}
        }

	return nvl;
}

nvlist_t *py_zfsprops_to_nvlist(pylibzfs_state_t *state,
				PyObject *pyprops,
				zfs_type_t type,
				boolean_t ro)
{
	PyObject *repr;

	if (PyDict_Check(pyprops)) {
		return py_zfsprops_dict_to_nvlist(state, pyprops, type, ro);
	} else if (PyObject_TypeCheck(pyprops, state->struct_zfs_props_type)) {
		return py_zfsprops_struct_to_nvlist(state,
						    pyprops,
						    type,
						    ro);
	}

	repr = PyObject_Repr(pyprops);

	PyErr_Format(PyExc_TypeError,
		     "%V: unexpected properties type. Expected a dictionary or "
		     "a " PYLIBZFS_MODULE_NAME ".struct_zfs_property instance.",
		     repr, "UNKNOWN TYPE");

	Py_XDECREF(repr);
	return NULL;
}

PyObject *py_dump_nvlist(nvlist_t *nvl, boolean_t json)
{
	PyObject *out = NULL;
	FILE *target = NULL;
	char *buf;
	size_t bufsz;
	boolean_t success = B_FALSE;

	Py_BEGIN_ALLOW_THREADS
	target = open_memstream(&buf, &bufsz);
	if (target != NULL) {
		if (json) {
			if (nvlist_print_json(target, nvl) == 0)
				success = B_TRUE;
		} else {
			nvlist_print(target, nvl);
			success = B_TRUE;
		}
		fflush(target);
	}
	Py_END_ALLOW_THREADS

	if (!success) {
		PyErr_Format(PyExc_RuntimeError,
			     "Failed to dump nvlist: %s",
			     strerror(errno));
		if (target) {
			fclose(target);
			free(buf);
		}

		return NULL;
	}

	out = PyUnicode_FromStringAndSize(buf, bufsz);
	Py_BEGIN_ALLOW_THREADS
	fclose(target);
	free(buf);
	Py_END_ALLOW_THREADS
	return out;
}

static
void to_lower(char *s)
{
	int size = strlen(s);
	for (int i = 0; i < size; ++i) {
		s[i] = tolower(s[i]);
	}
}

static
int add_leaf_vdev(nvlist_t *item, PyObject *path)
{
	char rpath[PATH_MAX];
	struct stat64 statbuf;
	const char *cpath = PyUnicode_AsUTF8(path);
	if (!cpath) {
		PyErr_SetString(PyExc_RuntimeError,
			"Failed to create C string from Python string");
		return (-1);
	}
	if (realpath(cpath, rpath) == NULL) {
		PyErr_Format(PyExc_RuntimeError,
			"%s: realpath() failed: %s", cpath, strerror(errno));
		return (-1);
	}
	if (stat64(rpath, &statbuf) != 0) {
		PyErr_Format(PyExc_RuntimeError,
			"Cannot open %s", rpath);
		return (-1);
	}
	if (S_ISBLK(statbuf.st_mode)) {
		fnvlist_add_string(item, ZPOOL_CONFIG_TYPE, VDEV_TYPE_DISK);
	} else if (S_ISREG(statbuf.st_mode)) {
		fnvlist_add_string(item, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	} else {
		PyErr_Format(PyExc_RuntimeError,
		    "%s is not a block device or regular file", cpath);
		return (-1);
	}
	fnvlist_add_string(item, ZPOOL_CONFIG_PATH, cpath);
	uint64_t wd = zfs_dev_is_whole_disk(rpath);
	fnvlist_add_uint64(item, ZPOOL_CONFIG_WHOLE_DISK, wd);
	return (0);
}

static
uint64_t get_parity(const char *type)
{
	uint64_t parity = 0;
	const char *p;

	if (strncmp(type, VDEV_TYPE_RAIDZ, strlen(VDEV_TYPE_RAIDZ)) == 0) {
		p = type + strlen(VDEV_TYPE_RAIDZ);

		if (*p == '\0') {
			/* when unspecified default to single parity */
			return (1);
		} else if (*p == '0') {
			/* no zero prefixes allowed */
			return (0);
		} else {
			/* 0-3, no suffixes allowed */
			char *end;
			errno = 0;
			parity = strtol(p, &end, 10);
			if (errno != 0 || *end != '\0' ||
			    parity < 1 || parity > VDEV_RAIDZ_MAXPARITY) {
				return (0);
			}
		}
	} else if (strncmp(type, VDEV_TYPE_DRAID,
	    strlen(VDEV_TYPE_DRAID)) == 0) {
		p = type + strlen(VDEV_TYPE_DRAID);

		if (*p == '\0' || *p == ':') {
			/* when unspecified default to single parity */
			return (1);
		} else if (*p == '0') {
			/* no zero prefixes allowed */
			return (0);
		} else {
			/* 0-3, allowed suffixes: '\0' or ':' */
			char *end;
			errno = 0;
			parity = strtol(p, &end, 10);
			if (errno != 0 ||
			    parity < 1 || parity > VDEV_DRAID_MAXPARITY ||
			    (*end != '\0' && *end != ':')) {
				return (0);
			}
		}
	}

	return ((int)parity);
}

static
int add_draid_data(nvlist_t *nvl, PyObject *item, uint64_t size,
    const char *ctype)
{
	uint64_t p, d, s, g = 1;
	PyObject *nsd, *nssp;
	PyObject *nd, *nsp;
	nsd = PyUnicode_FromString("draid_data_disks");
	if (!nsd) {
		PyErr_SetString(PyExc_RuntimeError,
		    "Failed to create Python String");
		return (-1);
	}
	nssp = PyUnicode_FromString("draid_spare_disks");
	if (!nssp) {
		Py_DECREF(nsd);
		PyErr_SetString(PyExc_RuntimeError,
		    "Failed to create Python String");
		return (-1);
	}
	nd = PyDict_GetItem(item, nsd);
	if (!PyLong_Check(nd)) {
		PyErr_SetString(PyExc_TypeError,
		    "Expected an Int for key \'draid_data_disks\'");
		goto fail;
	}
	nsp = PyDict_GetItem(item, nssp);
	if (!PyLong_Check(nsp)) {
		PyErr_SetString(PyExc_TypeError,
		    "Expected an Int for key \'draid_spare_disks\'");
		goto fail;
	}
	p = get_parity(ctype);
	if (p == 0 || p > VDEV_DRAID_MAXPARITY) {
		PyErr_Format(PyExc_TypeError,
		    "invalid dRAID parity level %lu; must be between 1 and %d\n",
		    p, VDEV_DRAID_MAXPARITY);
		goto fail;
	}
	d = PyLong_AsUnsignedLong(nd);
	if ((d == (unsigned long)-1) && PyErr_Occurred()) {
		PyErr_SetString(PyExc_TypeError,
		    "Failed to convert to unsigned long");
		goto fail;

	}
	s = PyLong_AsUnsignedLong(nsp);
	if ((s == (unsigned long)-1) && PyErr_Occurred()) {
		PyErr_SetString(PyExc_TypeError,
		    "Failed to convert to unsigned long");
		goto fail;
	}

	/*
	 * When a specific number of data disks is not provided limit a
	 * redundancy group to 8 data disks.  This value was selected to
	 * provide a reasonable tradeoff between capacity and performance.
	 */
	if (d == UINT64_MAX) {
		if (size > s + p) {
			d = MIN(size - s - p, 8);
		} else {
			PyErr_Format(PyExc_TypeError, "request number of "
			    "distributed spares %lu and parity level %lu"
			    "leaves no disks available for data\n", s, p);
			goto fail;
		}
	}

	/* Verify the maximum allowed group size is never exceeded. */
	if (d == 0 || (d + p > size - s)) {
		PyErr_Format(PyExc_TypeError, "requested number of dRAID data "
		    "disks per group %lu is too high, at most %lu disks "
		    "are available for data", d, (size - s - p));
		goto fail;
	}

	/*
	 * Verify the requested number of spares can be satisfied.
	 * An arbitrary limit of 100 distributed spares is applied.
	 */
	if (s > 100 || s > (size - (d + p))) {
		PyErr_Format(PyExc_TypeError, "invalid number of dRAID spares "
		    "%lu; additional disks would be required", s);
		goto fail;
	}

	/* Verify the requested number children is sufficient. */
	if (size < (d + p + s)) {
		PyErr_Format(PyExc_TypeError, "%lu disks were provided, but "
		    "at least %lu disks are required for this config", size,
		    d + p + s);
		goto fail;
	}

	if (size > VDEV_DRAID_MAX_CHILDREN) {
		PyErr_Format(PyExc_TypeError, "%lu disks were provided, but "
		    "dRAID only supports up to %u disks", size,
		    VDEV_DRAID_MAX_CHILDREN);
		goto fail;
	}

	if ((d + p) % (size - s) != 0) {
		PyErr_SetString(PyExc_TypeError,
		    "Total number of disks does not go cleanly into number of "
		    "specified groups");
		goto fail;
	}
	/*
	 * Calculate the minimum number of groups required to fill a slice.
	 * This is the LCM of the stripe width (ndata + nparity) and the
	 * number of data drives (children - nspares).
	 */
	while (g * (d + p) % (size - s) != 0)
		g++;

	fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_DRAID);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_NPARITY, p);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAID_NDATA, d);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAID_NSPARES, s);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAID_NGROUPS, g);
	Py_DECREF(nsd);
	Py_DECREF(nssp);
	return (0);

fail:
	Py_DECREF(nsd);
	Py_DECREF(nssp);
	return (-1);
}

static
void free_nvlist_array(nvlist_t **nvlarr, uint64_t len)
{
	if (nvlarr != NULL) {
		for (uint64_t i = 0; i < len; ++i) {
			if (nvlarr[i] != NULL)
				fnvlist_free(nvlarr[i]);
		}
		PyMem_Free(nvlarr);
	}
}

#define VDT_DATA "data"
#define VDT_CACHE "cache"
#define VDT_STRIPE "stripe"

/*
 * Creates a valid nvlist VDEV tree from given topology. Topology can be an
 * iterable containing dictionaries specifying the formation of each VDEV.
 * Memory allocated for VDEV tree in nvlist that is returned must be freed by
 * the caller.
 */
nvlist_t *make_vdev_tree(PyObject *topology, PyObject *props)
{
	uint64_t tlc = 0, nsp = 0, nl2c = 0;
	char croot[ZAP_MAXNAMELEN], ctype[ZAP_MAXNAMELEN];
	const char *ctroot, *cttype;
	nvlist_t *nvl = NULL;
	nvlist_t **child = NULL, **ichild = NULL;
	uint64_t isize;
	PyObject *item, *pskroot, *psktype, *pskdev, *devs;
	PyObject *psroot, *pstype, *pspath;

	PyObject *iterator = PyObject_GetIter(topology);
	if (iterator == NULL)
		return (NULL);

	pskroot = PyUnicode_FromString(VDEV_TYPE_ROOT);
	if (!pskroot) {
		PyErr_SetString(PyExc_RuntimeError,
		    "Failed to create Python String");
		return (NULL);
	}
	psktype = PyUnicode_FromString(ZPOOL_CONFIG_TYPE);
	if (!psktype) {
		Py_DECREF(pskroot);
		PyErr_SetString(PyExc_RuntimeError,
		    "Failed to create Python String");
		return (NULL);
	}
	pskdev = PyUnicode_FromString("devices");
	if (!pskdev) {
		Py_DECREF(pskroot);
		Py_DECREF(psktype);
		PyErr_SetString(PyExc_RuntimeError,
		    "Failed to create Python String");
		return (NULL);
	}

	while((item = PyIter_Next(iterator))) {
		if (!PyDict_Check(item)) {
			PyErr_SetString(PyExc_TypeError,
			    "Expected Dictionaries in \'topology\'");
			goto fail;
		}
		psroot = PyDict_GetItem(item, pskroot);
		if (!PyUnicode_Check(psroot)) {
			PyErr_SetString(PyExc_TypeError,
			    "Expected a String for key \'root\'");
			goto fail;
		}
		pstype = PyDict_GetItem(item, psktype);
		if (!PyUnicode_Check(pstype)) {
			PyErr_SetString(PyExc_TypeError,
			    "Expected a String for key \'type\'");
			goto fail;
		}
		devs = PyDict_GetItem(item, pskdev);
		if (!PyList_Check(devs)) {
			PyErr_SetString(PyExc_TypeError,
			    "Expected a List for key \'devices\'");
			goto fail;
		}
		ctroot = PyUnicode_AsUTF8(psroot);
		if (!ctroot) {
			PyErr_SetString(PyExc_RuntimeError,
			    "Failed to create C string from Python string");
			goto fail;
		}
		cttype = PyUnicode_AsUTF8(pstype);
		if (!cttype) {
			PyErr_SetString(PyExc_RuntimeError,
			    "Failed to create C string from Python string");
			goto fail;
		}

		strlcpy(croot, ctroot, sizeof(croot));
		strlcpy(ctype, cttype, sizeof(ctype));
		to_lower(croot);
		to_lower(ctype);
		if (strcmp(croot, VDT_DATA) == 0 ||
		    strcmp(croot, VDEV_ALLOC_BIAS_DEDUP) == 0 ||
		    strcmp(croot, VDEV_ALLOC_BIAS_LOG) == 0 ||
		    strcmp(croot, VDEV_ALLOC_BIAS_SPECIAL) == 0) {
			if (strcmp(ctype, VDT_STRIPE) == 0)
				tlc += (uint64_t)PyList_Size(devs);
			else
				tlc += 1;
		} else if (strcmp(croot, VDEV_TYPE_SPARE) == 0) {
			if (strcmp(ctype, VDT_STRIPE) != 0) {
				PyErr_SetString(PyExc_TypeError,
				    "Spare devices can only be of type stripe");
				goto fail;	
			}
			nsp = PyList_Size(devs);
		} else if (strcmp(croot, VDT_CACHE) == 0) {
			if (strcmp(ctype, VDT_STRIPE) != 0) {
				PyErr_SetString(PyExc_TypeError,
				    "Cache devices can only be of type stripe");
				goto fail;	
			}
			nl2c = PyList_Size(devs);
		} else {
			PyErr_SetString(PyExc_TypeError,
			    "Unknown VDEV type for key \'root\'");
			goto fail;
		}
		Py_DECREF(item);
	}
	Py_DECREF(iterator);

	nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	if (tlc > 0) {
		child = PyMem_Calloc(tlc, sizeof(nvlist_t *));
		if (child == NULL)
			goto fail;
	}

	uint64_t outer = 0, is_log = 0;
	iterator = PyObject_GetIter(topology);
	if (iterator == NULL)
		goto fail;
	while((item = PyIter_Next(iterator))) {
		psroot = PyDict_GetItem(item, pskroot);
		pstype = PyDict_GetItem(item, psktype);
		ctroot = PyUnicode_AsUTF8(psroot);
		if (!ctroot) {
			PyErr_SetString(PyExc_RuntimeError,
			    "Failed to create C string from Python string");
			goto fail;
		}
		cttype = PyUnicode_AsUTF8(pstype);
		if (!cttype) {
			PyErr_SetString(PyExc_RuntimeError,
			    "Failed to create C string from Python string");
			goto fail;
		}
		strlcpy(croot, ctroot, sizeof(croot));
		strlcpy(ctype, cttype, sizeof(ctype));
		to_lower(croot);
		to_lower(ctype);
		devs = PyDict_GetItem(item, pskdev);
		isize = PyList_Size(devs);
		if (strcmp(croot, VDEV_TYPE_LOG) == 0)
			is_log = 1;
		else
			is_log = 0;
		
		if (strcmp(ctype, VDT_STRIPE) == 0) {
			if (strcmp(croot, VDT_CACHE) == 0) {
				nvlist_t **cdevs;
				cdevs = PyMem_Calloc(nl2c, sizeof(nvlist_t *));
				if (cdevs == NULL)
					goto fail;
				for (uint64_t j = 0; j < nl2c; ++j) {
					cdevs[j] = fnvlist_alloc();
					pspath = PyList_GetItem(devs, j);
					if (add_leaf_vdev(cdevs[j],
					    pspath) != 0) {
						free_nvlist_array(cdevs, nl2c);
						goto fail;
					}
					// TODO: set ashift prop as passed or
					// from ZPOOL props
				}
				fnvlist_add_nvlist_array(nvl, ZPOOL_CONFIG_L2CACHE,
				    (const nvlist_t **)cdevs, nl2c);
				free_nvlist_array(cdevs, nl2c);
			} else if (strcmp(croot, VDEV_TYPE_SPARE) == 0) {
				nvlist_t **sdevs;
				sdevs = PyMem_Calloc(nsp, sizeof(nvlist_t *));
				if (sdevs == NULL)
					goto fail;
				for (uint64_t j = 0; j < nsp; ++j) {
					sdevs[j] = fnvlist_alloc();
					pspath = PyList_GetItem(devs, j);
					if (add_leaf_vdev(sdevs[j],
					    pspath) != 0) {
						free_nvlist_array(sdevs, nsp);
						goto fail;
					}
					fnvlist_add_uint64(sdevs[j],
					    ZPOOL_CONFIG_IS_SPARE, 1);
				}
				fnvlist_add_nvlist_array(nvl, ZPOOL_CONFIG_SPARES,
				    (const nvlist_t **)sdevs, nsp);
				free_nvlist_array(sdevs, nsp);
			} else if (tlc > 0) {
				for(uint64_t j = 0; j < isize; ++j) {
					child[outer] = fnvlist_alloc();
					pspath = PyList_GetItem(devs, j);
					if (add_leaf_vdev(child[outer],
					    pspath) != 0) {
						goto fail;
					}
					fnvlist_add_uint64(child[outer],
					    ZPOOL_CONFIG_IS_LOG, is_log);
					if (strcmp(croot, VDT_DATA) != 0) {
						fnvlist_add_string(child[outer],
						    ZPOOL_CONFIG_ALLOCATION_BIAS,
						    croot);
					}
					outer++;
					// TODO: set ashift prop as passed or
					// from ZPOOL props
				}
			}
		} else {
			if (tlc > 0 && strcmp(croot, VDT_CACHE) != 0 &&
			    strcmp(croot, VDEV_TYPE_SPARE) != 0) {
				child[outer] = fnvlist_alloc();
				if (strncmp(ctype, VDEV_TYPE_DRAID,
				    strlen(VDEV_TYPE_DRAID)) == 0) {
					if (add_draid_data(child[outer], item,
					    isize, ctype) != 0) {
						goto fail;
					}
				} else if (strncmp(ctype, VDEV_TYPE_RAIDZ,
				    strlen(VDEV_TYPE_RAIDZ)) == 0) {
					fnvlist_add_string(child[outer],
					    ZPOOL_CONFIG_TYPE, VDEV_TYPE_RAIDZ);
					uint64_t p = get_parity(ctype);
					if (p == 0 || p > VDEV_RAIDZ_MAXPARITY) {
						PyErr_Format(PyExc_TypeError,
						    "invalid RAIDZ parity level "
						    "%lu; must be between 1 and %d",
						    p, VDEV_RAIDZ_MAXPARITY);
						goto fail;
					}
					fnvlist_add_uint64(child[outer],
					    ZPOOL_CONFIG_NPARITY, p);
				} else {
					fnvlist_add_string(child[outer],
					    ZPOOL_CONFIG_TYPE, ctype);	
				}
				fnvlist_add_uint64(child[outer],
				    ZPOOL_CONFIG_IS_LOG, is_log);
				if (strcmp(croot, VDT_DATA) != 0) {
					fnvlist_add_string(child[outer],
					    ZPOOL_CONFIG_ALLOCATION_BIAS, croot);
				}
				// TODO: set ashift prop as passed or from
				// ZPOOL props
				ichild = PyMem_Calloc(isize, sizeof(nvlist_t *));
				if (ichild == NULL)
					goto fail;
				for(uint64_t j = 0; j < isize; j++) {
					ichild[j] = fnvlist_alloc();
					pspath = PyList_GetItem(devs, j);
					if (add_leaf_vdev(ichild[j],
					    pspath) != 0) {
						free_nvlist_array(ichild, isize);
						goto fail;
					}
					// TODO: set ashift prop as passed or
					// from ZPOOL props
				}
				fnvlist_add_nvlist_array(child[outer],
				    ZPOOL_CONFIG_CHILDREN,
				    (const nvlist_t **)ichild, isize);
				free_nvlist_array(ichild, isize);
				outer++;
			}
		}
		Py_DECREF(item);
	}
	Py_DECREF(iterator);

	if (tlc > 0) {
		fnvlist_add_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
		    (const nvlist_t **)child, tlc);
		free_nvlist_array(child, tlc);
	}
	Py_DECREF(pskroot);
	Py_DECREF(psktype);
	Py_DECREF(pskdev);
	return (nvl);

fail:
	if (child && tlc > 0)
		free_nvlist_array(child, tlc);
	fnvlist_free(nvl);
	Py_DECREF(pskroot);
	Py_DECREF(psktype);
	Py_DECREF(pskdev);
	return (NULL);
}
