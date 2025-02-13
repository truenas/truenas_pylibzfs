#ifndef _TRUENAS_PYLIBZFS_ENC_H
#define _TRUENAS_PYLIBZFS_ENC_H

extern PyTypeObject ZFSEncrypt;

/* initialize an object to control ZFS encryption settings */
extern PyObject *init_zfs_enc(zfs_type_t type, PyObject *rsrc);

#endif /* _TRUENAS_PYLIBZFS_ENC_H */
