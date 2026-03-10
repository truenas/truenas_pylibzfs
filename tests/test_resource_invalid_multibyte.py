# Check fix for NAS-138554

import pytest
import truenas_pylibzfs

ERRDESC = 'Invalid multibyte character'


def test_create_dataset_with_multibyte_name(root_dataset):
    lz, root = root_dataset
    rsrc_name = f'{root.name}/FOOBAR'

    with pytest.raises(truenas_pylibzfs.ZFSException, match=ERRDESC) as exc:
        lz.create_resource(
            name=rsrc_name,
            type=truenas_pylibzfs.ZFSType.ZFS_TYPE_FILESYSTEM,
        )

    assert exc.value.code == truenas_pylibzfs.ZFSError.EZFS_INVALIDNAME


def test_open_dataset_with_multibyte_name(root_dataset):
    lz, root = root_dataset
    rsrc_name = f'{root.name}/FOOBAR'

    with pytest.raises(truenas_pylibzfs.ZFSException, match=ERRDESC) as exc:
        lz.open_resource(name=rsrc_name)

    assert exc.value.code == truenas_pylibzfs.ZFSError.EZFS_INVALIDNAME
