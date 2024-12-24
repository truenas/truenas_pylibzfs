from setuptools import setup, Extension

pylibzfs2 = Extension('libzfs2',
                      ['pylibzfs2.c'],
                      libraries = ['zfs', 'zfs_core', 'nvpair', 'uutil'],
                      include_dirs = ['/usr/include/libzfs', '/usr/include/libspl'],
                      library_dirs = ['/usr/lib/x86_64-linux-gnu/'])

setup(name='pylibzfs2',
      version='0.1',
      description='pylibzfs2 provides python bindings for libzfs',
      ext_modules=[pylibzfs2])

