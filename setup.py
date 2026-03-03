from setuptools import setup, Extension

truenas_pylibzfs = Extension(
    name='truenas_pylibzfs',
    sources=[
        'src/truenas_pylibzfs.c',
        'src/truenas_pylibzfs_state.c',
        'src/common/error.c',
        'src/common/utils.c',
        'src/common/nvlist_utils.c',
        'src/common/py_zfs_prop_sets.c',
        'src/libzfs/py_zfs.c',
        'src/libzfs/py_zfs_dataset.c',
        'src/libzfs/py_zfs_common.c',
        'src/libzfs/py_zfs_crypto.c',
        'src/libzfs/py_zfs_enum.c',
        'src/libzfs/py_zfs_events.c',
        'src/libzfs/py_zfs_iter.c',
        'src/libzfs/py_zfs_mount.c',
        'src/libzfs/py_zfs_object.c',
        'src/libzfs/py_zfs_pool.c',
        'src/libzfs/py_zfs_pool_create.c',
        'src/libzfs/py_zfs_pool_status.c',
        'src/libzfs/py_zfs_prop.c',
        'src/libzfs/py_zfs_resource.c',
        'src/libzfs/py_zfs_snapshot.c',
        'src/libzfs/py_zfs_userquota.c',
        'src/libzfs/py_zfs_volume.c',
        'src/libzfs_core/py_zfs_core_module.c',
    ],
    libraries = [
        'zfs',
        'zfs_core',
        'nvpair',
        'uutil',
    ],
    include_dirs = ['/usr/include/libzfs', '/usr/include/libspl'],
    library_dirs = ['/usr/lib/x86_64-linux-gnu/'],
)

setup(name='truenas_pylibzfs',
      version='0.1',
      description='truenas_pylibzfs provides python bindings for libzfs for TrueNAS',
      ext_modules=[truenas_pylibzfs],
      packages=['truenas_pylibzfs'],
      package_dir={'truenas_pylibzfs': 'stubs'},
      package_data={'truenas_pylibzfs': ['*.pyi', 'py.typed']})

