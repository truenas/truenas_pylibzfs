# This code snippet demonstrats how to iterate the
# root level datasets of all imported pools.
import truenas_pylibzfs

def print_dataset_names(hdl, state):
    print(hdl.name)
    state["datasets"].append(hdl)
    if state["recursive"]:
        hdl.iter_filesystems(
            callback=print_dataset_names,
            state=state
        )

    return True


lz = truenas_pylibzfs.open_handle()

# First non-recursive
state = {
    "recursive": False,
    "datasets": []
}

lz.iter_root_filesystems(callback=print_dataset_names, state=state)
first_len = len(state["datasets"])
assert first_len != 0

first_len = len(state["datasets"])

# Now recursively
state = {
    "recursive": True,
    "datasets": []
}

lz.iter_root_filesystems(callback=print_dataset_names, state=state)
assert len(state["datasets"]) != first_len
