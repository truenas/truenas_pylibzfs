#ifndef _TRUENAS_PYLIBZFS_CRYPTO_H
#define _TRUENAS_PYLIBZFS_CRYPTO_H

extern PyTypeObject ZFSCrypto;

/* initialize an object to control ZFS encryption settings */
extern PyObject *init_zfs_crypto(zfs_type_t type, PyObject *rsrc);

/* initialize cryptography info python struct for module */
extern void module_init_zfs_crypto(PyObject *module);

#endif /* _TRUENAS_PYLIBZFS_CRYPTO_H */
