"""
tests/test_pool_iostat.py -- tests for ZFSPool.iostat()

Requires root and a system with ZFS kernel support.
"""
import os
import pytest
import truenas_pylibzfs

WRITE_SZ = 4 * 1048576


def _write_and_sync(pool, root, name):
    root.mount()
    with open(os.path.join(f'/{name}', 'payload.dat'), 'wb') as f:
        f.write(os.urandom(WRITE_SZ))
    pool.sync_pool()


def test_iostat_shape(make_pool):
    lz, pool, root = make_pool('iostat_shape')
    io = pool.iostat()

    assert isinstance(io, truenas_pylibzfs.libzfs_types.struct_zpool_iostat)
    assert io.name == 'iostat_shape'
    assert io.guid == pool.status().guid
    assert isinstance(io.stats, truenas_pylibzfs.libzfs_types.struct_vdev_stats)
    assert len(io.storage_vdevs) == 1
    assert io.storage_vdevs[0].stats is not None
    assert io.support_vdevs.cache == ()
    assert io.support_vdevs.log == ()


def test_iostat_counters_advance(make_pool):
    lz, pool, root = make_pool('iostat_delta')
    before = pool.iostat()
    _write_and_sync(pool, root, 'iostat_delta')
    after = pool.iostat()

    # Each call refreshes, so a kept handle sees new data.
    assert after.stats.timestamp > before.stats.timestamp
    assert after.stats.bytes_write - before.stats.bytes_write >= WRITE_SZ
    top_before = before.storage_vdevs[0].stats
    top_after = after.storage_vdevs[0].stats
    assert top_after.bytes_write - top_before.bytes_write >= WRITE_SZ


def test_iostat_multiple_pools(make_pool):
    lz, pool_a, root_a = make_pool('iostat_a')
    lz, pool_b, root_b = make_pool('iostat_b')

    io_a = pool_a.iostat()
    io_b = pool_b.iostat()
    assert (io_a.name, io_b.name) == ('iostat_a', 'iostat_b')
    assert io_a.guid != io_b.guid

    # Losing one pool does not affect a handle on another.
    lz.export_pool(name='iostat_b')
    with pytest.raises(FileNotFoundError):
        pool_b.iostat()

    assert pool_a.iostat().guid == io_a.guid


def test_iostat_extended_off_by_default(make_pool):
    lz, pool, root = make_pool('iostat_ex_off')
    io = pool.iostat()
    assert io.stats_ex is None
    assert io.storage_vdevs[0].stats_ex is None
    assert pool.status().storage_vdevs[0].stats_ex is None


def test_iostat_extended(make_pool):
    lz, pool, root = make_pool('iostat_ex')
    before = pool.iostat(extended=True)
    _write_and_sync(pool, root, 'iostat_ex')
    after = pool.iostat(extended=True)

    for ex in (after.stats_ex, after.storage_vdevs[0].stats_ex):
        assert len(ex['vdev_tot_w_lat_histo']) == 37
        assert len(ex['vdev_async_ind_w_histo']) == 25
        assert isinstance(ex['vdev_async_w_active_queue'], int)

    # Write latency histograms are running totals, so the writes show up.
    key = 'vdev_tot_w_lat_histo'
    assert sum(after.stats_ex[key]) > sum(before.stats_ex[key])
