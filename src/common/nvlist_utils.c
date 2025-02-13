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
			"Cannot resolve path %s", cpath);
		return (-1);
	}
	if (stat64(rpath, &statbuf) != 0) {
		PyErr_Format(PyExc_RuntimeError,
			"Cannot open %s", rpath);
		return (-1);
	}
	if (S_ISBLK(statbuf.st_mode)) {
		fnvlist_add_string(item, "type", "disk");
	} else if (S_ISREG(statbuf.st_mode)) {
		fnvlist_add_string(item, "type", "file");
	} else {
		PyErr_Format(PyExc_RuntimeError,
		    "%s is not a block device or regular file", cpath);
		return (-1);
	}
	fnvlist_add_string(item, "path", cpath);
	uint64_t wd = zfs_dev_is_whole_disk(rpath);
	fnvlist_add_uint64(item, "whole_disk", wd);
	return (0);
}

static
int add_draid_data(nvlist_t *nvl, PyObject *item, uint64_t size, const char *ctype)
{
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
		    "Expected a list for key \'draid_data_disks\'");
		Py_DECREF(nsd);
		Py_DECREF(nssp);
		return (-1);
	}
	nsp = PyDict_GetItem(item, nssp);
	if (!PyLong_Check(nsp)) {
		PyErr_SetString(PyExc_TypeError,
		    "Expected a list for key \'draid_spare_disks\'");
		Py_DECREF(nsd);
		Py_DECREF(nssp);
		return (-1);
	}
	uint64_t p = ctype[strlen(ctype) - 1] - '0';
	uint64_t d = PyLong_AsUnsignedLong(nd);
	if (PyErr_Occurred()) {
		PyErr_SetString(PyExc_TypeError,
			"Failed to convert to unsigned long");
		Py_DECREF(nsd);
		Py_DECREF(nssp);
		return (-1);
	}
	uint64_t s = PyLong_AsUnsignedLong(nsp);
	if (PyErr_Occurred()) {
		PyErr_SetString(PyExc_TypeError,
			"Failed to convert to unsigned long");
		Py_DECREF(nsd);
		Py_DECREF(nssp);
		return (-1);
	}
	uint64_t g = 1;
	while (g * (d + p) % (size - s) != 0)
		g++;
	fnvlist_add_string(nvl, "type", "draid");
	fnvlist_add_uint64(nvl, "nparity", p);
	fnvlist_add_uint64(nvl, "draid_ndata", d);
	fnvlist_add_uint64(nvl, "draid_nspares", s);
	fnvlist_add_uint64(nvl, "ngroups", g);
	Py_DECREF(nsd);
	Py_DECREF(nssp);
	return (0);
}

static
void free_nvlist_array(nvlist_t **nvlarr, uint64_t len)
{
	for (uint64_t i = 0; i < len; ++i)
		fnvlist_free(nvlarr[i]);
	free(nvlarr);
}

nvlist_t *make_vdev_tree(PyObject *topology, PyObject *props)
{
	uint64_t tlc = 0, nsp = 0, nl2c = 0;
	char croot[128], ctype[128];
	const char *ctroot, *cttype;
	nvlist_t *nvl;
	nvlist_t **child = NULL, **ichild = NULL;
	uint64_t size, isize;
	PyObject *item, *pskroot, *psktype, *pskdev, *devs;
	PyObject *psroot, *pstype, *pspath;

	if (topology == NULL || !PyList_Check(topology)) {
		PyErr_SetString(PyExc_TypeError,
		    "Expected a list for \'topology\'");
		return (NULL);
	}

	nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, "type", "root");

	pskroot = PyUnicode_FromString("root");
	if (!pskroot) {
		PyErr_SetString(PyExc_RuntimeError,
		    "Failed to create Python String");
		return (NULL);
	}
	psktype = PyUnicode_FromString("type");
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
	size = (uint64_t)PyList_Size(topology);

	for (uint64_t i = 0; i < size; ++i) {
		item = PyList_GetItem(topology, i);
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
		if (strcmp(croot, "data") == 0 ||
		    strcmp(croot, "dedup") == 0 ||
		    strcmp(croot, "log") == 0 ||
		    strcmp(croot, "special") == 0) {
			if (strcmp(ctype, "stripe") == 0)
				tlc += (uint64_t)PyList_Size(devs);
			else
				tlc += 1;
		} else if (strcmp(croot, "spare") == 0) {
			if (strcmp(ctype, "stripe") != 0) {
				PyErr_SetString(PyExc_TypeError,
				    "Spare devices can only be of type stripe");
				goto fail;	
			}
			nsp = PyList_Size(devs);
		} else if (strcmp(croot, "cache") == 0) {
			if (strcmp(ctype, "stripe") != 0) {
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
	}

	if (tlc > 0)
		child = malloc(tlc * sizeof(nvlist_t *));

	uint64_t outer = 0, is_log = 0;
	for (uint64_t i = 0; i < size; ++i) {
		item = PyList_GetItem(topology, i);
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
		if (strcmp(croot, "log") == 0)
			is_log = 1;
		else
			is_log = 0;
		
		if (strcmp(ctype, "stripe") == 0) {
			if (strcmp(croot, "cache") == 0) {
				nvlist_t **cdevs;
				cdevs = malloc(nl2c * sizeof(nvlist_t *));
				for (uint64_t j = 0; j < nl2c; ++j) {
					cdevs[j] = fnvlist_alloc();
					pspath = PyList_GetItem(devs, j);
					if (add_leaf_vdev(cdevs[j],
					    pspath) != 0) {
						goto fail;
					}
					// TODO: set ashift prop as passed or
					// from ZPOOL props
				}
				fnvlist_add_nvlist_array(nvl, "l2cache",
				    (const nvlist_t **)cdevs, nl2c);
				free_nvlist_array(cdevs, nl2c);
			} else if (strcmp(croot, "spare") == 0) {
				nvlist_t **sdevs;
				sdevs = malloc(nsp * sizeof(nvlist_t *));
				for (uint64_t j = 0; j < nsp; ++j) {
					sdevs[j] = fnvlist_alloc();
					pspath = PyList_GetItem(devs, j);
					if (add_leaf_vdev(sdevs[j],
					    pspath) != 0) {
						goto fail;
					}
					fnvlist_add_uint64(sdevs[j], "is_spare", 1);
				}
				fnvlist_add_nvlist_array(nvl, "spares",
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
					    "is_log", is_log);
					if (strcmp(croot, "data") != 0) {
						fnvlist_add_string(child[outer],
						    "alloc_bias", croot);
					}
					outer++;
					// TODO: set ashift prop as passed or
					// from ZPOOL props
				}
			}
		} else {
			if (tlc > 0 && strcmp(croot, "cache") != 0 &&
			    strcmp(croot, "spare") != 0) {
				child[outer] = fnvlist_alloc();
				if (strncmp(ctype, "draid", strlen("draid")) == 0) {
					add_draid_data(child[outer], item,
					    isize, ctype);
				} else if (strncmp(ctype, "raidz", strlen("raidz")) == 0) {
					fnvlist_add_string(child[outer], "type",
					    "raidz");
					uint64_t p = ctype[strlen(ctype) - 1] - '0';
					fnvlist_add_uint64(child[outer], "nparity", p);
				} else {
					fnvlist_add_string(child[outer], "type",
					    ctype);	
				}
				fnvlist_add_uint64(child[outer], "is_log",
					is_log);
				if (strcmp(croot, "data") != 0) {
					fnvlist_add_string(child[outer],
					    "alloc_bias", croot);
				}
				// TODO: set ashift prop as passed or from
				// ZPOOL props
				ichild = malloc(isize * sizeof(nvlist_t *));
				for(uint64_t j = 0; j < isize; j++) {
					ichild[j] = fnvlist_alloc();
					pspath = PyList_GetItem(devs, j);
					if (add_leaf_vdev(ichild[j],
					    pspath) != 0) {
						goto fail;
					}
					// TODO: set ashift prop as passed or
					// from ZPOOL props
				}
				fnvlist_add_nvlist_array(child[outer],
				    "children",
				    (const nvlist_t **)ichild, isize);
				free_nvlist_array(ichild, isize);
				outer++;
			}
		}
	}

	if (tlc > 0) {
		fnvlist_add_nvlist_array(nvl, "children",
		    (const nvlist_t **)child, tlc);
		free_nvlist_array(child, tlc);
	}
	Py_DECREF(pskroot);
	Py_DECREF(psktype);
	Py_DECREF(pskdev);
	return (nvl);

fail:
	fnvlist_free(nvl);
	Py_DECREF(pskroot);
	Py_DECREF(psktype);
	Py_DECREF(pskdev);
	return (NULL);
}
