import truenas_pylibzfs
import time

zh = truenas_pylibzfs.open_handle()

events_init = list(zh.zpool_events()) 
assert len(events_init) != 0
print(len(events_init))

# ZEVENT_SEEK_END to end of events list. Should be None there 
events_seek_end = list(zh.zpool_events(skip_existing_events=True)) 
assert len(events_seek_end) == 0

events_final = list(zh.zpool_events())

assert events_init == events_final

print(events_final)
