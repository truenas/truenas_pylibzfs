# This code snippet demonstrats how to iterate the
# root level datasets of all imported pools.
import truenas_pylibzfs

def print_pool_names(hdl, state):
    print(hdl.name)
    state["pools"].append(hdl)
    return True


lz = truenas_pylibzfs.open_handle()

state = {"pools": []}

lz.iter_root_filesystems(callback=print_pool_names, state=state)
pool_len = len(state["pools"])
assert pool_len != 0
