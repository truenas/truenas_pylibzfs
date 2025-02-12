import truenas_pylibzfs


lz = truenas_pylibzfs.open_handle()
rsrc = lz.open_resource(name='dozer/foo')

mountpoint = rsrc.get_mountpoint()
assert mountpoint is not None
print(mountpoint)

rsrc.unmount()

mountpoint = rsrc.get_mountpoint()
assert mountpoint is None

rsrc.mount()

mountpoint = rsrc.get_mountpoint()
assert mountpoint is not None
print(mountpoint)
