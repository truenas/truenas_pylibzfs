import truenas_pylibzfs
import sys

def acb(event, args):
    if event.startswith('truenas'):
        print(f'{event}: {args}')

sys.addaudithook(acb)
lz = truenas_pylibzfs.open_handle()

# Example 1:
# Create a stripe pool with two disks
t = [{'root': 'DATA', 'type': 'STRIPE', 'devices': ['/dev/sdb', '/dev/sdc']}]
lz.create_pool(name='exp-1', topology=t)
lz.destroy_pool(name='exp-1')

# Example 2:
# Create a mirrored pool with 3 disks
t = [{'root': 'DATA', 'type': 'MIRROR', 'devices': ['/dev/sdd', '/dev/sde', '/dev/sdf']}]
lz.create_pool(name='exp-2', topology=t)
lz.destroy_pool(name='exp-2')

# Example 3:
# Create a stripe pool with 1 disk
t = [{'root': 'DATA', 'type': 'STRIPE', 'devices': ['/dev/sdg']}]
lz.create_pool(name='exp-3', topology=t)
lz.destroy_pool(name='exp-3')

# Example 4:
# Create a raidz1 pool with 3 disks
t = [{'root': 'DATA', 'type': 'RAIDZ1', 'devices': ['/dev/sdb', '/dev/sdc', '/dev/sdd']}]
lz.create_pool(name='exp-4', topology=t)
lz.destroy_pool(name='exp-4')

# Example 5:
# Create a raidz2 pool with 4 data disks, 2 spare disks, 1 disk as caches device
# and 1 disk as log device
t = [{'root': 'DATA', 'type': 'RAIDZ2', 'devices': ['/dev/sdb', '/dev/sdc', '/dev/sdd', '/dev/sde']},
     {'root': 'SPARE', 'type': 'STRIPE', 'devices': ['/dev/sdg', '/dev/sdh']},
     {'root': 'LOG', 'type': 'STRIPE', 'devices': ['/dev/sdf']},
     {'root': 'CACHE', 'type': 'STRIPE', 'devices': ['/dev/sdi']}]
lz.create_pool(name='exp-5', topology=t)
lz.destroy_pool(name='exp-5')
