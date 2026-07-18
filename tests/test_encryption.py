import pytest
import truenas_pylibzfs

PASSPHRASE = 'Cats1234'
PASSPHRASE2 = 'Dogs1234'
HEX_KEY = '1234567890abcdef' * 4  # 64 hex chars = 32 bytes


def write_keyfile(path, passphrase=PASSPHRASE):
    """Write passphrase material and return its file:// URI."""
    path.write_bytes(passphrase.encode())
    return f'file://{path}'


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
    lz, _ = root_dataset
    config = lz.resource_cryptography_config(keyformat='passphrase', key=PASSPHRASE)
    assert config.keyformat == 'passphrase'
    assert config.key == PASSPHRASE
    assert config.keylocation is None
    # None means "use the minimum (1300000)"; an explicit value is also valid
    assert config.pbkdf2iters is None or config.pbkdf2iters >= 1300000


def test_crypto_config_hex(root_dataset):
    lz, _ = root_dataset
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
    _, rsrc = enc_dataset
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
    _, rsrc = enc_dataset
    enc = rsrc.crypto()
    rsrc.unmount()  # unloads the key by default

    assert enc.check_key(key=PASSPHRASE) is True
    assert enc.check_key(key='wrongpassword') is False
    # Key should remain unloaded after a check
    assert enc.info().key_is_loaded is False


def test_load_unload_key(enc_dataset):
    _, rsrc = enc_dataset
    enc = rsrc.crypto()

    rsrc.unmount()  # unloads the key by default
    assert enc.info().key_is_loaded is False

    enc.load_key(key=PASSPHRASE)
    assert enc.info().key_is_loaded is True

    enc.unload_key()
    assert enc.info().key_is_loaded is False


def test_unmount_key_unload_opt_out(enc_dataset):
    _, rsrc = enc_dataset
    enc = rsrc.crypto()

    rsrc.unmount(unload_encryption_key=False)
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
    rsrc.unmount()  # unloads the key by default
    enc.load_key(key=PASSPHRASE2)
    assert enc.info().key_is_loaded is True


def test_crypto_config_keylocation_file_uri(root_dataset, tmp_path):
    """A file:// URI with a path must be accepted, not just the bare prefix."""
    lz, _ = root_dataset
    uri = write_keyfile(tmp_path / 'wrappingkey')

    config = lz.resource_cryptography_config(
        keyformat='passphrase',
        keylocation=uri
    )
    assert config.keylocation == uri
    assert config.key is None


@pytest.mark.parametrize('keylocation', [
    'prompt',
    '/tmp/wrappingkey',
    'ftp://example.com/wrappingkey',
    'file:/tmp/wrappingkey',
])
def test_crypto_config_rejects_bad_keylocation(root_dataset, keylocation):
    """Only file:// and https:// prefixes are permitted."""
    lz, _ = root_dataset
    with pytest.raises(ValueError):
        lz.resource_cryptography_config(
            keyformat='passphrase',
            keylocation=keylocation
        )


def test_crypto_config_invalid_keyformat_names_the_format(root_dataset):
    """The error message must identify the rejected key format."""
    lz, _ = root_dataset
    with pytest.raises(ValueError, match='bogusformat'):
        lz.resource_cryptography_config(keyformat='bogusformat', key=PASSPHRASE)


def test_create_encrypted_filesystem_keylocation(data_pool1, tmp_path):
    """
    Create an encrypted dataset whose key material lives in a file, passing
    no properties.  Covers both halves of the keylocation path: the URI has
    to survive validation, and creating with a NULL property nvlist has to
    reach zfs_create() rather than aborting in fnvlist_merge().
    """
    lz = truenas_pylibzfs.open_handle()
    rsrc_name = f'{data_pool1}/enc_keyloc'
    uri = write_keyfile(tmp_path / 'wrappingkey')

    crypto = lz.resource_cryptography_config(
        keyformat='passphrase',
        keylocation=uri
    )
    lz.create_resource(
        name=rsrc_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        crypto=crypto
    )
    try:
        rsrc = lz.open_resource(name=rsrc_name)
        assert rsrc.encrypted
        assert rsrc.crypto().info().is_root is True
    finally:
        lz.destroy_resource(name=rsrc_name)


def test_load_key_honors_key_location_override(data_pool1, tmp_path):
    """
    load_key()/check_key() must read the caller's key_location rather than
    the dataset's own keylocation property.  The dataset's file is rewritten
    with the wrong passphrase so the two sources disagree: reading the
    property fails, reading the override succeeds.
    """
    lz = truenas_pylibzfs.open_handle()
    rsrc_name = f'{data_pool1}/enc_override'
    dataset_key = tmp_path / 'dataset_key'
    override_key = tmp_path / 'override_key'
    uri = write_keyfile(dataset_key)

    crypto = lz.resource_cryptography_config(
        keyformat='passphrase',
        keylocation=uri
    )
    lz.create_resource(
        name=rsrc_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        crypto=crypto
    )
    try:
        rsrc = lz.open_resource(name=rsrc_name)
        enc = rsrc.crypto()
        rsrc.unmount()  # unloads the key by default
        assert enc.info().key_is_loaded is False

        # The dataset's own keylocation now yields the wrong passphrase.
        write_keyfile(dataset_key, PASSPHRASE2)
        override_uri = write_keyfile(override_key, PASSPHRASE)

        assert enc.check_key() is False
        assert enc.check_key(key_location=override_uri) is True

        enc.load_key(key_location=override_uri)
        assert enc.info().key_is_loaded is True
    finally:
        lz.destroy_resource(name=rsrc_name)


def test_load_key_rejects_key_with_key_location(enc_dataset, tmp_path):
    """key and key_location remain mutually exclusive."""
    _, rsrc = enc_dataset
    enc = rsrc.crypto()
    uri = write_keyfile(tmp_path / 'wrappingkey')

    with pytest.raises(ValueError):
        enc.load_key(key=PASSPHRASE, key_location=uri)


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
