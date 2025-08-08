import truenas_pylibzfs


LUA_SCRIPT = """
args = ...
pool = args["argv"][1]

function gather_destroy(root, to_destroy)
    for child in zfs.list.children(root) do
        to_destroy = gather_destroy(child, to_destroy)
    end
    for snap in zfs.list.snapshots(root) do
        for clone in zfs.list.clones(snap) do
            to_destroy = gather_destroy(clone, to_destroy)
        end
        table.insert(to_destroy, snap)
    end
    table.insert(to_destroy, root)
    return to_destroy
end

function cleanup_dataset(root)
    datasets = gather_destroy(root, {})
    for ds in datasets do
        err = zfs.check.destroy(ds)
        if err < 0 then
            error("failed to destroy " .. ds .. " errno: " .. err)
        end
    end
    for ds in datasets do
         assert(zfs.sync.destroy(ds) == 0)
    end
end

function recursive_cleanup(root)
    for child in zfs.list.children(root) do
        recursive_cleanup(child)
    end
    -- We may encounter these clones when recursing through children of some
    -- other filesystem, but we catch them here as well to make sure each is
    -- destroyed before its origin fs.
    for snap in zfs.list.snapshots(root) do
        for clone in zfs.list.clones(snap) do
            recursive_cleanup(clone)
        end
    end
    cleanup_dataset(root)
end

recursive_cleanup(pool)"""


try:
   truenas_pylibzfs.lzc.channel_program(pool_name='dozer', script=LUA_SCRIPT.strip(), script_arguments=['dozer/foo'], readonly=False)
except Exception as exc:
   print(exc.errors)
