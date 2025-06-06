#ifndef	_ZFS_H
#define _ZFS_H

#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#define __STRING(x) #x
#define __STRINGSTRING(x) __STRING(x)
#define __LINESTR__ __STRINGSTRING(__LINE__)
#define __location__ __FILE__ ":" __LINESTR__
#define _GNU_SOURCE

#include <libzfs.h>
#include <pthread.h>
#include <Python.h>
#include <sys/mman.h>

#endif /* _ZFS_H */
