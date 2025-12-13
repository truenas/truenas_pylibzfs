import pytest
import time
import truenas_pylibzfs

init_props = {
   truenas_pylibzfs.ZFSProperty.VOLSIZE: "1073741824",
   truenas_pylibzfs.ZFSProperty.READONLY: "on",
}

update_props = {
   truenas_pylibzfs.ZFSProperty.VOLSIZE: "1073741824",
   truenas_pylibzfs.ZFSProperty.READONLY: "off",
}

@pytest.fixture(scope='function')
def ro_zvol(root_dataset):
    # creating a dataset with specific property set
    lz, root = root_dataset
    rsrc_name = f'{root.name}/rozvol'
    lz.create_resource(
        name=rsrc_name,
        type=truenas_pylibzfs.ZFSType.ZFS_TYPE_VOLUME,
        properties=init_props
    )

    rsrc = lz.open_resource(name=rsrc_name)
    try:
        yield rsrc
    finally:
        lz.destroy_resource(name=rsrc_name)


def test_update_zvol_ro_with_volsize(ro_zvol):
    """ verify that keeping same volsize will not impact changing ro """
    ro_zvol.set_properties(properties=update_props)
    # insert short sleep to prevent EZFS_BUSY
    time.sleep(1)
