import truenas_pylibzfs

ENC_DATASET = 'dozer/TEST_ENC'
PASSKEY = 'Cats1234'


def get_encrypted_datasets(hdl, state):
    state['cnt'] += 1

    if hdl.crypto() is not None:
        state['datasets'].append(hdl.name)

    hdl.iter_filesystems(callback=get_encrypted_datasets, state=state)
    return True


lz = truenas_pylibzfs.open_handle()
rsrc = lz.open_resource(name='dozer')
assert rsrc.asdict(get_crypto=True)['crypto'] is None

# Get count of child datasets and a list of encrypted ones
datasets = {
    'cnt': 0,
    'datasets': []
}
rsrc.iter_filesystems(callback=get_encrypted_datasets, state=datasets)
print(datasets)

assert ENC_DATASET in datasets['datasets']

# unload the key
rsrc = lz.open_resource(name=datasets['datasets'][0])
crypto_dict = rsrc.asdict(get_crypto=True)['crypto']
assert crypto_dict['is_root'] is True
assert crypto_dict['encryption_root'] == rsrc.name
assert crypto_dict['key_location'] == 'prompt'
assert crypto_dict['key_is_loaded'] is True

enc = rsrc.crypto()

# unmount also unloads key
rsrc.unmount()
assert rsrc.get_mountpoint() is None
crypto_dict = rsrc.asdict(get_crypto=True)['crypto']
assert crypto_dict['is_root'] is True
assert crypto_dict['encryption_root'] == rsrc.name
assert crypto_dict['key_location'] == 'prompt'
assert crypto_dict['key_is_loaded'] is False

# Test load shouldn't actually load the key
assert enc.check_key(key=PASSKEY) is True
crypto_dict = rsrc.asdict(get_crypto=True)['crypto']
assert crypto_dict['is_root'] is True
assert crypto_dict['encryption_root'] == rsrc.name
assert crypto_dict['key_location'] == 'prompt'
assert crypto_dict['key_is_loaded'] is False

# load key / unload key / load key / mount
# to verify that properties are being refreshed
enc.load_key(key=PASSKEY)
enc.unload_key()
enc.load_key(key=PASSKEY)
rsrc.mount()

assert enc.check_key(key="CANARY1234") is False

assert rsrc.get_mountpoint()
