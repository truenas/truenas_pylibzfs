# This example shows how to turn the raw counters from
# ZFSPool.iostat(extended=True) into the numbers that zpool iostat prints.
#
# Every counter from iostat() is a running total. Like zpool iostat -y, this
# keeps one pool handle open, takes a sample every interval, and reports the
# change since the previous sample scaled to one second. The output uses the
# same columns and order as these commands, so it can be compared directly.
#
#   zpool iostat -H -p -y -v -l -q 1   (the "iostat" section)
#   zpool iostat -H -p -y -v -w 1      (the "latency" section)
#   zpool iostat -H -p -y -v -r 1      (the "req_size" section)
#
# Usage: python3 iostat_histograms.py [pool] [interval]
import sys
import time

import truenas_pylibzfs

# zpool iostat -l columns. Each is an average taken from a latency histogram.
LATENCY = (
    "vdev_tot_r_lat_histo",
    "vdev_tot_w_lat_histo",
    "vdev_disk_r_lat_histo",
    "vdev_disk_w_lat_histo",
    "vdev_sync_r_lat_histo",
    "vdev_sync_w_lat_histo",
    "vdev_async_r_lat_histo",
    "vdev_async_w_lat_histo",
    "vdev_scrub_histo",
    "vdev_trim_histo",
    "vdev_rebuild_histo",
)

# zpool iostat -q columns. These are current readings, not running totals.
QUEUES = (
    "vdev_sync_r_pend_queue",
    "vdev_sync_r_active_queue",
    "vdev_sync_w_pend_queue",
    "vdev_sync_w_active_queue",
    "vdev_async_r_pend_queue",
    "vdev_async_r_active_queue",
    "vdev_async_w_pend_queue",
    "vdev_async_w_active_queue",
    "vdev_async_scrub_pend_queue",
    "vdev_async_scrub_active_queue",
    "vdev_async_trim_pend_queue",
    "vdev_async_trim_active_queue",
    "vdev_rebuild_pend_queue",
    "vdev_rebuild_active_queue",
)

# zpool iostat -r columns. Individual and aggregated I/O for each queue.
REQ_SIZE = (
    "vdev_sync_ind_r_histo",
    "vdev_sync_agg_r_histo",
    "vdev_sync_ind_w_histo",
    "vdev_sync_agg_w_histo",
    "vdev_async_ind_r_histo",
    "vdev_async_agg_r_histo",
    "vdev_async_ind_w_histo",
    "vdev_async_agg_w_histo",
    "vdev_ind_scrub_histo",
    "vdev_agg_scrub_histo",
    "vdev_ind_trim_histo",
    "vdev_agg_trim_histo",
    "vdev_ind_rebuild_histo",
    "vdev_agg_rebuild_histo",
)


def histo_average(buckets):
    """Average latency in nanoseconds, computed the way zpool iostat -l does.

    Bucket i counts I/Os that took at least 2^i and less than 2^(i+1)
    nanoseconds. Each I/O is counted at the middle of its bucket.
    """
    count = sum(buckets)
    if count == 0:
        return 0
    total = sum(c * ((1 << i) + (1 << i) // 2) for i, c in enumerate(buckets))
    return total // count


# Bucket 2 covers 4 to 7 ns, so its middle is 6.
assert histo_average([0, 0, 4]) == 6
assert histo_average([0, 2, 0, 2]) == (2 * 3 + 2 * 12) // 4
assert histo_average([0, 0, 0]) == 0


def walk(vdevs):
    for vdev in vdevs:
        yield vdev
        yield from walk(vdev.children or ())


def sample(pool):
    """Return a list of (guid, name, stats, stats_ex) in zpool iostat -v order."""
    io = pool.iostat(extended=True)
    sv = io.support_vdevs
    out = [(io.guid, io.name, io.stats, io.stats_ex)]
    for top in (io.storage_vdevs, sv.dedup, sv.special, sv.log, sv.cache):
        for vdev in walk(top):
            out.append((vdev.guid, vdev.name, vdev.stats, vdev.stats_ex))
    return out


def report(old_sample, new_sample):
    old_by_guid = {guid: (stats, ex) for guid, _, stats, ex in old_sample}
    iostat_lines = []
    latency_lines = []
    req_size_lines = []

    for guid, name, new, new_ex in new_sample:
        if guid not in old_by_guid:
            # The vdev was added since the last sample. Skip it for now.
            continue

        old, old_ex = old_by_guid[guid]

        # zpool iostat scales by each vdev's own elapsed time.
        elapsed = new.timestamp - old.timestamp
        scale = 1e9 / elapsed if elapsed else 1.0

        # The change in every histogram bucket since the last sample.
        delta = {
            key: [n - o for o, n in zip(old_ex[key], new_ex[key])]
            for key in LATENCY + REQ_SIZE
        }

        # Default columns. Only top-level vdevs have capacity numbers.
        if new.space:
            row = [new.allocated, new.space - new.allocated]
        else:
            row = [0, 0]
        for field in ("ops_read", "ops_write", "bytes_read", "bytes_write"):
            row.append(int((getattr(new, field) - getattr(old, field)) * scale))

        # -l averages the change in each latency histogram. They are not scaled.
        row += [histo_average(delta[key]) for key in LATENCY]

        # -q prints the current queue readings as they are.
        row += [new_ex[key] for key in QUEUES]
        iostat_lines.append("\t".join([name] + [str(v) for v in row]))

        # -w prints one row per latency bucket, labeled by the bucket's top
        # value in nanoseconds.
        latency_lines.append(name)
        histos = [delta[key] for key in LATENCY]
        for i in range(len(histos[0])):
            counts = [int(h[i] * scale) for h in histos]
            latency_lines.append(
                "\t".join(str(v) for v in [(1 << (i + 1)) - 1] + counts)
            )

        # -r prints one row per size bucket, labeled by the bucket's bottom
        # value in bytes. Buckets under 512 bytes are skipped.
        req_size_lines.append(name)
        histos = [delta[key] for key in REQ_SIZE]
        for i in range(9, len(histos[0])):
            counts = [int(h[i] * scale) for h in histos]
            req_size_lines.append("\t".join(str(v) for v in [1 << i] + counts))

    print(time.ctime())
    for title, lines in (
        ("iostat", iostat_lines),
        ("latency", latency_lines),
        ("req_size", req_size_lines),
    ):
        print(f"== {title}")
        print("\n".join(lines))


pool_name = sys.argv[1] if len(sys.argv) > 1 else "dozer"
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0

lz = truenas_pylibzfs.open_handle()
pool = lz.open_pool(name=pool_name)

prev = sample(pool)
while True:
    time.sleep(interval)
    cur = sample(pool)
    report(prev, cur)
    prev = cur
