# This code snippet provides an example of how to use ZFS iterators
# to retrieve space information on ZFS resources
#

import truenas_pylibzfs

DATASETS = ('dozer/MANY', 'dozer/share')
SPACE_PROPS = truenas_pylibzfs.property_sets.ZFS_SPACE_PROPERTIES


def gather_stats(hdl, stats):
    properties = hdl.get_properties(properties=SPACE_PROPS)
    stats[hdl.name] = properties
    return True


def gather_stats_dict(hdl, stats):
    properties = hdl.asdict(properties=SPACE_PROPS)['properties']
    stats[hdl.name] = properties
    return True


def take_recursive_space_stats(lz_hdl, datasets, as_dict):
    stats = {}
    for dataset_name in datasets:
        rsrc = lz_hdl.open_resource(name=dataset_name)
        rsrc.iter_filesystems(
            callback=gather_stats if not as_dict else gather_stats_dict,
            state=stats,
            fast=True
        )

    return stats


lz = truenas_pylibzfs.open_handle()

stats = take_recursive_space_stats(lz, DATASETS, False)
print(f'stats: {stats}')

stats = take_recursive_space_stats(lz, DATASETS, True)
print(f'stats: {stats}')
