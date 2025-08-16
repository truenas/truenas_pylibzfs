import truenas_pylibzfs

program = truenas_pylibzfs.lzc.ChannelProgramEnum.RECURSIVE_DESTROY

z = truenas_pylibzfs.open_handle()

def create_datasets():
    for ds in ('dozer/foo', 'dozer/foo/bar', 'dozer/foo/bar/tar'):
        z.create_resource(name=ds, type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM)

create_datasets()
rsrc = z.open_resource(name='dozer/foo')

# We need to recursively unmount the dataset first
rsrc.unmount(recursive=True)

# execute channel program explicitly to destroy datasets
truenas_pylibzfs.lzc.run_channel_program(
    pool_name='dozer',
    script=program,
    script_arguments_dict={"recursive": True, "defer": True, "target": "dozer/foo"},
    readonly=False
)
