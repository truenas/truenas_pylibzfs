import truenas_pylibzfs
import json

hdl = truenas_pylibzfs.open_handle()
pool = hdl.open_pool(name='dozer')

# dump stats twice in a row should
# generate identical output
config_dump = pool.dump_config()
config_dump2 = pool.dump_config()

assert config_dump == config_dump2

# refreshing vdev counters should cause
# dump to differ
pool.refresh_stats()

config_dump2 = pool.dump_config()
assert config_dump != config_dump2
