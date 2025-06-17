from truenas_pylibzfs import open_handle
from truenas_pylibzfs import VDevTopKey
from truenas_pylibzfs import VDevTopRoot
from truenas_pylibzfs import VDevTopType
import sys

def acb(event, args):
    if event.startswith('truenas'):
        print(f'{event}: {args}')

sys.addaudithook(acb)
lz = open_handle()

# Example 1:
# Create a stripe pool with two disks
t = [{VDevTopKey.ROOT: VDevTopRoot.DATA, VDevTopKey.TYPE: VDevTopType.STRIPE, VDevTopKey.DEVICES: ['/dev/sdb', '/dev/sdc']}]
lz.pool_create(name='exp-1', topology=t)
lz.pool_destroy(name='exp-1')

# Example 2:
# Create a mirrored pool with 3 disks
t = [{VDevTopKey.ROOT: VDevTopRoot.DATA, VDevTopKey.TYPE: VDevTopType.MIRROR, VDevTopKey.DEVICES: ['/dev/sdd', '/dev/sde', '/dev/sdf']}]
lz.pool_create(name='exp-2', topology=t)
lz.pool_destroy(name='exp-2')

# Example 3:
# Create a stripe pool with 1 disk
t = [{VDevTopKey.ROOT: VDevTopRoot.DATA, VDevTopKey.TYPE: VDevTopType.STRIPE, VDevTopKey.DEVICES: ['/dev/sdg']}]
lz.pool_create(name='exp-3', topology=t)
lz.pool_destroy(name='exp-3')

# Example 4:
# Create a raidz1 pool with 3 disks
t = [{VDevTopKey.ROOT: VDevTopRoot.DATA, VDevTopKey.TYPE: VDevTopType.RAIDZ1, VDevTopKey.DEVICES: ['/dev/sdb', '/dev/sdc', '/dev/sdd']}]
lz.pool_create(name='exp-4', topology=t)
lz.pool_destroy(name='exp-4')

# Example 5:
# Create a raidz2 pool with 4 data disks, 2 spare disks, 1 disk as caches device
# and 1 disk as log device
t = [{VDevTopKey.ROOT: VDevTopRoot.DATA, VDevTopKey.TYPE: VDevTopType.RAIDZ2, VDevTopKey.DEVICES: ['/dev/sdb', '/dev/sdc', '/dev/sdd', '/dev/sde']},
     {VDevTopKey.ROOT: VDevTopRoot.SPARE, VDevTopKey.TYPE: VDevTopType.STRIPE, VDevTopKey.DEVICES: ['/dev/sdg', '/dev/sdh']},
     {VDevTopKey.ROOT: VDevTopRoot.LOG, VDevTopKey.TYPE: VDevTopType.STRIPE, VDevTopKey.DEVICES: ['/dev/sdf']},
     {VDevTopKey.ROOT: VDevTopRoot.CACHE, VDevTopKey.TYPE: VDevTopType.STRIPE, VDevTopKey.DEVICES: ['/dev/sdi']}]
lz.pool_create(name='exp-5', topology=t)
lz.pool_destroy(name='exp-5')
