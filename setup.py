from setuptools import setup, Extension

truenas_pylibzfs = Extension(
    name='truenas_pylibzfs',
    sources=[
        'src/error.c',
        'src/py_zfs.c',
        'src/py_zfs_dataset.c',
        'src/py_zfs_enum.c',
        'src/py_zfs_iter.c',
        'src/py_zfs_object.c',
        'src/py_zfs_pool.c',
        'src/py_zfs_prop.c',
        'src/py_zfs_resource.c',
        'src/py_zfs_state.c',
        'src/py_zfs_vdev.c',
        'src/pylibzfs2.c',
        'src/utils.c',
    ],
    libraries = [
        'zfs',
        'zfs_core',
        'nvpair',
        'uutil'
    ],
    include_dirs = ['/usr/include/libzfs', '/usr/include/libspl'],
    library_dirs = ['/usr/lib/x86_64-linux-gnu/'],
)

setup(name='truenas_pylibzfs',
      version='0.1',
      description='truenas_pylibzfs provides python bindings for libzfs for TrueNAS',
      ext_modules=[truenas_pylibzfs])

