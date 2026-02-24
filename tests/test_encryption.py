import pytest
import truenas_pylibzfs

PASSPHRASE = 'Cats1234'
PASSPHRASE2 = 'Dogs1234'
HEX_KEY = '1234567890abcdef' * 4  # 64 hex chars = 32 bytes


@pytest.fixture
def enc_dataset(data_pool1):
    """Create an encrypted passphrase dataset and yield (lz, enc_rsrc)."""
    lz = truenas_pylibzfs.open_handle()
    rsrc_name = f'{data_pool1}/enc'

    crypto = lz.resource_cryptography_config(keyformat='passphrase', key=PASSPHRASE)
    lz.create_resource(
        name=rsrc_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        crypto=crypto
    )
    rsrc = lz.open_resource(name=rsrc_name)

    try:
        yield (lz, rsrc)
    finally:
        lz.destroy_resource(name=rsrc_name)


def test_crypto_config_passphrase(root_dataset):
    lz, root = root_dataset
    config = lz.resource_cryptography_config(keyformat='passphrase', key=PASSPHRASE)
    assert config.keyformat == 'passphrase'
    assert config.key == PASSPHRASE
    assert config.keylocation is None
    assert config.pbkdf2iters >= 1300000


def test_crypto_config_hex(root_dataset):
    lz, root = root_dataset
    config = lz.resource_cryptography_config(keyformat='hex', key=HEX_KEY)
    assert config.keyformat == 'hex'
    assert config.key == HEX_KEY
    assert config.keylocation is None


def test_create_encrypted_filesystem(root_dataset):
    lz, root = root_dataset
    rsrc_name = f'{root.name}/enc_test'
    crypto = lz.resource_cryptography_config(keyformat='passphrase', key=PASSPHRASE)
    lz.create_resource(
        name=rsrc_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        crypto=crypto
    )
    try:
        rsrc = lz.open_resource(name=rsrc_name)
        assert rsrc.encrypted
        assert rsrc.crypto() is not None
    finally:
        lz.destroy_resource(name=rsrc_name)


def test_crypto_info(enc_dataset):
    lz, rsrc = enc_dataset
    enc = rsrc.crypto()
    info = enc.info()
    assert info.is_root is True
    assert info.encryption_root == rsrc.name
    assert info.key_is_loaded is True


def test_asdict_crypto(enc_dataset):
    lz, rsrc = enc_dataset
    result = rsrc.asdict(
        properties={truenas_pylibzfs.ZFSProperty.ACLMODE},
        get_crypto=True
    )
    crypto_dict = result['crypto']
    assert crypto_dict is not None
    assert crypto_dict['is_root'] is True
    assert crypto_dict['encryption_root'] == rsrc.name
    assert crypto_dict['key_is_loaded'] is True

    # Unencrypted dataset reports crypto=None
    pool_root = lz.open_resource(name=rsrc.name.split('/')[0])
    plain_result = pool_root.asdict(
        properties={truenas_pylibzfs.ZFSProperty.ACLMODE},
        get_crypto=True
    )
    assert plain_result['crypto'] is None


def test_check_key(enc_dataset):
    lz, rsrc = enc_dataset
    enc = rsrc.crypto()
    rsrc.unmount()
    enc.unload_key()

    assert enc.check_key(key=PASSPHRASE) is True
    assert enc.check_key(key='wrongpassword') is False
    # Key should remain unloaded after a check
    assert enc.info().key_is_loaded is False


def test_load_unload_key(enc_dataset):
    lz, rsrc = enc_dataset
    enc = rsrc.crypto()

    rsrc.unmount()
    enc.unload_key()
    assert enc.info().key_is_loaded is False

    enc.load_key(key=PASSPHRASE)
    assert enc.info().key_is_loaded is True

    enc.unload_key()
    assert enc.info().key_is_loaded is False


def test_change_key(enc_dataset):
    lz, rsrc = enc_dataset
    enc = rsrc.crypto()

    # change_key requires the key to be loaded (it is after creation)
    new_crypto = lz.resource_cryptography_config(keyformat='passphrase', key=PASSPHRASE2)
    enc.change_key(info=new_crypto)

    # Unload and verify only the new key works
    rsrc.unmount()
    enc.unload_key()
    enc.load_key(key=PASSPHRASE2)
    assert enc.info().key_is_loaded is True


def test_inherit_key(data_pool1):
    lz = truenas_pylibzfs.open_handle()
    parent_name = f'{data_pool1}/enc_parent'
    child_name = f'{data_pool1}/enc_parent/enc_child'

    parent_crypto = lz.resource_cryptography_config(keyformat='passphrase', key=PASSPHRASE)
    lz.create_resource(
        name=parent_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        crypto=parent_crypto
    )
    try:
        child_crypto = lz.resource_cryptography_config(keyformat='passphrase', key=PASSPHRASE2)
        lz.create_resource(
            name=child_name,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
            crypto=child_crypto
        )
        try:
            child = lz.open_resource(name=child_name)
            child_enc = child.crypto()
            assert child_enc.info().is_root is True

            # Parent key is loaded (auto-loaded on creation); inherit from it
            child_enc.inherit_key()

            child_fresh = lz.open_resource(name=child_name)
            assert child_fresh.crypto().info().is_root is False
        finally:
            lz.destroy_resource(name=child_name)
    finally:
        lz.destroy_resource(name=parent_name)
