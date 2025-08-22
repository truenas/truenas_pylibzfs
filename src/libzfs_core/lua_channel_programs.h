#ifndef _PY_ZFS_CORE_LUA_SCRIPT_H
#define _PY_ZFS_CORE_LUA_SCRIPT_H

/*
 * Lua channel program to recursively destroy dataset (all snapshots and clones
 * of snapshots). Dataset name is passed via argv "script_arguments" kwarg
 */
static const char RECURSIVE_DESTROY_LUA[] =
"failed = {}\n"
"\n"
"function destroy_datasets(root)\n"
"    -- recurse into child datasets\n"
"    for child in zfs.list.children(root) do\n"
"        destroy_datasets(child)\n"
"    end\n"
"\n"
"    -- iterate and destroy snapshots\n"
"    for snap in zfs.list.snapshots(root) do\n"
"        -- iterate and destroy clones first\n"
"        for clone in zfs.list.clones(snap) do\n"
"            err = zfs.sync.destroy(clone)\n"
"            if (err ~= 0) then\n"
"                failed[clone] = err\n"
"            end\n"
"        end\n"
"        -- now do the snapshot destroy\n"
"        err = zfs.sync.destroy(snap)\n"
"        if (err ~= 0) then\n"
"            failed[snap] = err\n"
"        end\n"
"    end\n"
"    -- dependents are destroyed, we may now destroy this dataset\n"
"    err = zfs.sync.destroy(root)\n"
"    if (err ~= 0) then\n"
"        failed[root] = err\n"
"    end\n"
"end\n"
"\n"
"args = ...\n"
"target = args[\"target\"]\n"
"recurse = args[\"recursive\"]\n"
"defer = args[\"defer\"]\n"
"\n"
"if recurse then\n"
"    destroy_datasets(target)\n"
"else\n"
"    err = zfs.sync.destroy(target)\n"
"    if (err ~= 0) then\n"
"        failed[target] = err\n"
"    end\n"
"end\n"
"\n"
"return failed\n";

/*
 * Lua channel program to take recursive snapshot
 */
static const char SNAPSHOT_TAKE_LUA[] =
"failed = {}\n"
"\n"
"function snapshot_recursive(root, name)\n"
"    for child in zfs.list.children(root) do\n"
"        snapshot_recursive(child, name)\n"
"    end\n"
"    local snapname = root..\"@\"..name\n"
"    err = zfs.sync.snapshot(snapname)\n"
"    if (err ~= 0) then\n"
"        failed[snapname] = err\n"
"    end\n"
"end\n"
"\n"
"args = ...\n"
"argv = args[\"argv\"]\n"
"snapshot_recursive(argv[1], argv[2])\n"
"\n"
"return failed\n";

static const char SNAPSHOT_DESTROY_LUA[] =
"failed = {}\n"
"\n"
"function snapshot_recursive(root, recurse)\n"
"    if recurse then\n"
"        for child in zfs.list.children(root) do\n"
"            snapshot_recursive(child, recurse)\n"
"        end\n"
"    end\n"
"    for snap in zfs.list.snapshots(root) do\n"
"        -- iterate and destroy clones first\n"
"        for clone in zfs.list.clones(snap) do\n"
"            err = zfs.sync.destroy(clone)\n"
"            if (err ~= 0) then\n"
"                failed[clone] = err\n"
"            end\n"
"        end\n"
"        -- now do the snapshot destroy\n"
"        err = zfs.sync.destroy(snap)\n"
"        if (err ~= 0) then\n"
"            failed[snap] = err\n"
"        end\n"
"    end\n"
"end\n"
"\n"
"args = ...\n"
"target = args[\"target\"]\n"
"recurse = args[\"recursive\"]\n"
"defer = args[\"defer\"]\n"
"snapshot_recursive(target, recurse)\n"
"\n"
"\n"
"return failed\n";

static const struct {
	const char *name;
	const char *script;
} zcp_table[] = {
	{ "DESTROY_RESOURCES", RECURSIVE_DESTROY_LUA },
	{ "DESTROY_SNAPSHOTS", SNAPSHOT_DESTROY_LUA },
	{ "TAKE_SNAPSHOTS", SNAPSHOT_TAKE_LUA },
};

#endif /* PY_ZFS_CORE_LUA_SCRIPT_H */
