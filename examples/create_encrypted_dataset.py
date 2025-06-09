# Sample for creating encrypted dataset

import truenas_pylibzfs
PASSKEY = 'Cats1234'
PASSKEY2 = 'Dogs1234'

z = truenas_pylibzfs.open_handle()
c = z.resource_cryptography_config(keyformat='passphrase', key=PASSKEY)

z.create_resource(name='dozer/enc', type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM, crypto=c)
rsrc = z.open_resource(name='dozer/enc')
assert rsrc.encrypted, str(rsrc)
enc = rsrc.crypto()

# unmount also unloads key
rsrc.unmount()

# Verify that our passkey works
enc.load_key(key=PASSKEY)

# Change our crypto key
new_crypto = z.resource_cryptography_config(keyformat='passphrase', key=PASSKEY2)
enc.change_key(info=new_crypto)

# unload then load with new key
enc.unload_key()
enc.load_key(key=PASSKEY2)

z.destroy_resource(name='dozer/enc')
