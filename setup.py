from setuptools import setup, Extension

truenas_pylibzfs = Extension(
    name='truenas_pylibzfs',
    sources=[
        'src/error.c',
        'src/truenas_pylibzfs.c',
        'src/truenas_pylibzfs_state.c',
        'src/utils.c',
        'src/libzfs/py_zfs.c',
        'src/libzfs/py_zfs_dataset.c',
        'src/libzfs/py_zfs_enum.c',
        'src/libzfs/py_zfs_iter.c',
        'src/libzfs/py_zfs_object.c',
        'src/libzfs/py_zfs_pool.c',
        'src/libzfs/py_zfs_prop.c',
        'src/libzfs/py_zfs_resource.c',
        'src/libzfs/py_zfs_vdev.c',
        'src/libzfs/py_zfs_volume.c',
        'src/libzfs_core/py_zfs_core_module.c',
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

