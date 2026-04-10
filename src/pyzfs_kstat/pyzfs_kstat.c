#include "pyzfs_kstat.h"

/*
 * truenas_pylibzfs.kstat -- ArcStats and ZilStats field docstrings and
 * struct sequence definitions.
 *
 * All PyDoc_STRVAR definitions for kstat fields live here, not in the
 * per-kstat implementation files (arcstats.c, zilstats.c), which contain
 * only file-parsing logic.
 *
 * Field names and documentation are derived from:
 *   module/zfs/arc.c       -- ARC kstat name list
 *   include/sys/arc_impl.h -- ARC per-field comments
 *   module/zfs/zil.c       -- ZIL kstat name list
 *   include/sys/zil.h      -- ZIL per-field comments
 * in the ZFS source tree (github.com/truenas/zfs,
 * branch truenas/zfs-2.4-release).
 */

/* -------------------------------------------------------------------------
 * ArcStats field docstrings
 * ------------------------------------------------------------------------- */

/* Aggregate hit/miss counters */

PyDoc_STRVAR(arcstat_hits__doc__,
"ARC requests satisfied from cache without I/O "
"(sum of all demand and prefetch, data and metadata hits).");

PyDoc_STRVAR(arcstat_iohits__doc__,
"ARC requests for which the required data was already being fetched "
"by in-progress I/O at the time of the request.");

PyDoc_STRVAR(arcstat_misses__doc__,
"ARC requests that required issuing new I/O (cache miss).");

/* Demand data */

PyDoc_STRVAR(arcstat_demand_data_hits__doc__,
"Demand data requests satisfied from the ARC without I/O.");

PyDoc_STRVAR(arcstat_demand_data_iohits__doc__,
"Demand data requests whose data was already being fetched by "
"in-progress I/O.");

PyDoc_STRVAR(arcstat_demand_data_misses__doc__,
"Demand data requests that required new I/O (cache miss).");

/* Demand metadata */

PyDoc_STRVAR(arcstat_demand_metadata_hits__doc__,
"Demand metadata requests satisfied from the ARC without I/O.");

PyDoc_STRVAR(arcstat_demand_metadata_iohits__doc__,
"Demand metadata requests whose data was already being fetched by "
"in-progress I/O.");

PyDoc_STRVAR(arcstat_demand_metadata_misses__doc__,
"Demand metadata requests that required new I/O (cache miss).");

/* Prefetch data */

PyDoc_STRVAR(arcstat_prefetch_data_hits__doc__,
"Prefetch data requests satisfied from the ARC without I/O.");

PyDoc_STRVAR(arcstat_prefetch_data_iohits__doc__,
"Prefetch data requests whose data was already being fetched by "
"in-progress I/O.");

PyDoc_STRVAR(arcstat_prefetch_data_misses__doc__,
"Prefetch data requests that required new I/O (cache miss).");

/* Prefetch metadata */

PyDoc_STRVAR(arcstat_prefetch_metadata_hits__doc__,
"Prefetch metadata requests satisfied from the ARC without I/O.");

PyDoc_STRVAR(arcstat_prefetch_metadata_iohits__doc__,
"Prefetch metadata requests whose data was already being fetched by "
"in-progress I/O.");

PyDoc_STRVAR(arcstat_prefetch_metadata_misses__doc__,
"Prefetch metadata requests that required new I/O (cache miss).");

/* ARC state hits */

PyDoc_STRVAR(arcstat_mru_hits__doc__,
"ARC hits on buffers in the MRU (Most Recently Used) state.");

PyDoc_STRVAR(arcstat_mru_ghost_hits__doc__,
"ARC hits on the MRU ghost list. Ghost entries track recently evicted "
"MRU buffers to bias re-admission toward MRU on the next access.");

PyDoc_STRVAR(arcstat_mfu_hits__doc__,
"ARC hits on buffers in the MFU (Most Frequently Used) state.");

PyDoc_STRVAR(arcstat_mfu_ghost_hits__doc__,
"ARC hits on the MFU ghost list. Ghost entries track recently evicted "
"MFU buffers to bias re-admission toward MFU on the next access.");

PyDoc_STRVAR(arcstat_uncached_hits__doc__,
"ARC hits on buffers marked ARC_FLAG_UNCACHED (scheduled for eviction "
"on the next opportunity).");

PyDoc_STRVAR(arcstat_deleted__doc__,
"ARC buffers deleted (freed) from the cache.");

/* Eviction */

PyDoc_STRVAR(arcstat_mutex_miss__doc__,
"Evictions skipped because the buffer's hash lock was held by another "
"thread. Hash locks are shared across multiple buffers so contention "
"may not involve the target buffer.");

PyDoc_STRVAR(arcstat_access_skip__doc__,
"Buffers skipped during access state update because the header was "
"released after the hash lock was acquired.");

PyDoc_STRVAR(arcstat_evict_skip__doc__,
"Buffers skipped during eviction because they had I/O in progress, "
"were indirect prefetch buffers that had not lived long enough, or "
"belonged to a different pool.");

PyDoc_STRVAR(arcstat_evict_not_enough__doc__,
"Times arc_evict_state() could not evict enough buffers to reach its "
"target amount.");

PyDoc_STRVAR(arcstat_evict_l2_cached__doc__,
"Bytes evicted from ARC that were already cached on an L2ARC device.");

PyDoc_STRVAR(arcstat_evict_l2_eligible__doc__,
"Bytes evicted from ARC that were eligible for caching on L2ARC.");

PyDoc_STRVAR(arcstat_evict_l2_eligible_mfu__doc__,
"MFU bytes evicted from ARC that were eligible for L2ARC caching.");

PyDoc_STRVAR(arcstat_evict_l2_eligible_mru__doc__,
"MRU bytes evicted from ARC that were eligible for L2ARC caching.");

PyDoc_STRVAR(arcstat_evict_l2_ineligible__doc__,
"Bytes evicted from ARC that were ineligible for L2ARC caching "
"(e.g. encrypted data or data that is already on L2ARC).");

PyDoc_STRVAR(arcstat_evict_l2_skip__doc__,
"ARC evictions for which the L2ARC write was skipped.");

/* Hash table */

PyDoc_STRVAR(arcstat_hash_elements__doc__,
"Current number of elements in the ARC hash table.");

PyDoc_STRVAR(arcstat_hash_elements_max__doc__,
"Peak number of elements ever resident in the ARC hash table.");

PyDoc_STRVAR(arcstat_hash_collisions__doc__,
"Total number of hash collisions recorded in the ARC hash table.");

PyDoc_STRVAR(arcstat_hash_chains__doc__,
"Number of hash chains (buckets with more than one element) in the "
"ARC hash table.");

PyDoc_STRVAR(arcstat_hash_chain_max__doc__,
"Length of the longest hash chain ever observed in the ARC hash table.");

/* Cache size targets (bytes) */

PyDoc_STRVAR(arcstat_meta__doc__,
"ARC metadata limit in bytes (arc_meta_limit). Metadata consumption "
"above this target triggers eviction of metadata buffers.");

PyDoc_STRVAR(arcstat_pd__doc__,
"ARC prefetch data target size in bytes.");

PyDoc_STRVAR(arcstat_pm__doc__,
"ARC prefetch metadata target size in bytes.");

PyDoc_STRVAR(arcstat_c__doc__,
"Current ARC target size in bytes. The ARC grows toward arc_c_max and "
"shrinks toward arc_c_min in response to memory pressure.");

PyDoc_STRVAR(arcstat_c_min__doc__,
"Minimum ARC target size in bytes (arc_c_min). The ARC will not shrink "
"below this value.");

PyDoc_STRVAR(arcstat_c_max__doc__,
"Maximum ARC target size in bytes (arc_c_max). The ARC will not grow "
"beyond this value.");

/* Overall size (bytes) */

PyDoc_STRVAR(arcstat_size__doc__,
"Total size of all ARC-cached buffers in bytes.");

PyDoc_STRVAR(arcstat_compressed_size__doc__,
"Compressed size in bytes of data stored in arc_buf_hdr_t b_pabd "
"fields. Equals uncompressed_size when compressed ARC is disabled.");

PyDoc_STRVAR(arcstat_uncompressed_size__doc__,
"Uncompressed size in bytes of data stored in arc_buf_hdr_t b_pabd "
"fields. Equals compressed_size when compressed ARC is disabled.");

PyDoc_STRVAR(arcstat_overhead_size__doc__,
"Bytes held in short-lived uncompressed arc_buf_t copies attached to "
"headers. These copies are evicted when unreferenced unless "
"zfs_keep_uncompressed_metadata or zfs_keep_uncompressed_level is set.");

PyDoc_STRVAR(arcstat_hdr_size__doc__,
"Bytes consumed by internal ARC tracking structures (arc_buf_hdr_t and "
"arc_buf_t objects). These structures are not backed by ARC data "
"buffers.");

PyDoc_STRVAR(arcstat_data_size__doc__,
"Bytes consumed by ARC_BUFC_DATA buffers, which back on-disk user "
"data such as plain file contents.");

PyDoc_STRVAR(arcstat_metadata_size__doc__,
"Bytes consumed by ARC_BUFC_METADATA buffers, which back internal ZFS "
"structures such as ZAP objects, dnodes, and indirect blocks.");

PyDoc_STRVAR(arcstat_dbuf_size__doc__,
"Bytes consumed by dmu_buf_impl_t (dbuf) objects.");

PyDoc_STRVAR(arcstat_dnode_size__doc__,
"Bytes consumed by dnode_t objects.");

PyDoc_STRVAR(arcstat_bonus_size__doc__,
"Bytes consumed by dnode bonus buffers.");

/* Anon state (bytes) */

PyDoc_STRVAR(arcstat_anon_size__doc__,
"Total bytes in the arc_anon state. Anon buffers are newly allocated "
"and have not yet been promoted to MRU or MFU.");

PyDoc_STRVAR(arcstat_anon_data__doc__,
"ARC_BUFC_DATA bytes in the arc_anon state.");

PyDoc_STRVAR(arcstat_anon_metadata__doc__,
"ARC_BUFC_METADATA bytes in the arc_anon state.");

PyDoc_STRVAR(arcstat_anon_evictable_data__doc__,
"Evictable ARC_BUFC_DATA bytes in the arc_anon state "
"(no outstanding holds on the buffer).");

PyDoc_STRVAR(arcstat_anon_evictable_metadata__doc__,
"Evictable ARC_BUFC_METADATA bytes in the arc_anon state "
"(no outstanding holds on the buffer).");

/* MRU state (bytes) */

PyDoc_STRVAR(arcstat_mru_size__doc__,
"Total bytes in the MRU (Most Recently Used) state, including data, "
"metadata, evictable, and pinned buffers.");

PyDoc_STRVAR(arcstat_mru_data__doc__,
"ARC_BUFC_DATA bytes in the MRU state.");

PyDoc_STRVAR(arcstat_mru_metadata__doc__,
"ARC_BUFC_METADATA bytes in the MRU state.");

PyDoc_STRVAR(arcstat_mru_evictable_data__doc__,
"Evictable ARC_BUFC_DATA bytes in the MRU state "
"(no outstanding holds on the buffer).");

PyDoc_STRVAR(arcstat_mru_evictable_metadata__doc__,
"Evictable ARC_BUFC_METADATA bytes in the MRU state "
"(no outstanding holds on the buffer).");

/* MRU ghost state (bytes) */

PyDoc_STRVAR(arcstat_mru_ghost_size__doc__,
"Notional bytes tracked by MRU ghost headers. Ghost lists hold only "
"headers with no associated data buffers; this value represents what "
"those buffers would have consumed had they remained cached.");

PyDoc_STRVAR(arcstat_mru_ghost_data__doc__,
"Notional ARC_BUFC_DATA bytes tracked by MRU ghost headers.");

PyDoc_STRVAR(arcstat_mru_ghost_metadata__doc__,
"Notional ARC_BUFC_METADATA bytes tracked by MRU ghost headers.");

PyDoc_STRVAR(arcstat_mru_ghost_evictable_data__doc__,
"Notional evictable ARC_BUFC_DATA bytes on the MRU ghost list.");

PyDoc_STRVAR(arcstat_mru_ghost_evictable_metadata__doc__,
"Notional evictable ARC_BUFC_METADATA bytes on the MRU ghost list.");

/* MFU state (bytes) */

PyDoc_STRVAR(arcstat_mfu_size__doc__,
"Total bytes in the MFU (Most Frequently Used) state, including data, "
"metadata, evictable, and pinned buffers.");

PyDoc_STRVAR(arcstat_mfu_data__doc__,
"ARC_BUFC_DATA bytes in the MFU state.");

PyDoc_STRVAR(arcstat_mfu_metadata__doc__,
"ARC_BUFC_METADATA bytes in the MFU state.");

PyDoc_STRVAR(arcstat_mfu_evictable_data__doc__,
"Evictable ARC_BUFC_DATA bytes in the MFU state "
"(no outstanding holds on the buffer).");

PyDoc_STRVAR(arcstat_mfu_evictable_metadata__doc__,
"Evictable ARC_BUFC_METADATA bytes in the MFU state "
"(no outstanding holds on the buffer).");

/* MFU ghost state (bytes) */

PyDoc_STRVAR(arcstat_mfu_ghost_size__doc__,
"Notional bytes tracked by MFU ghost headers. "
"See mru_ghost_size for a full description of ghost lists.");

PyDoc_STRVAR(arcstat_mfu_ghost_data__doc__,
"Notional ARC_BUFC_DATA bytes tracked by MFU ghost headers.");

PyDoc_STRVAR(arcstat_mfu_ghost_metadata__doc__,
"Notional ARC_BUFC_METADATA bytes tracked by MFU ghost headers.");

PyDoc_STRVAR(arcstat_mfu_ghost_evictable_data__doc__,
"Notional evictable ARC_BUFC_DATA bytes on the MFU ghost list.");

PyDoc_STRVAR(arcstat_mfu_ghost_evictable_metadata__doc__,
"Notional evictable ARC_BUFC_METADATA bytes on the MFU ghost list.");

/* Uncached state (bytes) */

PyDoc_STRVAR(arcstat_uncached_size__doc__,
"Total bytes pending eviction from ARC because ARC_FLAG_UNCACHED is "
"set. These buffers will be freed on the next eviction pass.");

PyDoc_STRVAR(arcstat_uncached_data__doc__,
"ARC_BUFC_DATA bytes pending eviction due to ARC_FLAG_UNCACHED.");

PyDoc_STRVAR(arcstat_uncached_metadata__doc__,
"ARC_BUFC_METADATA bytes pending eviction due to ARC_FLAG_UNCACHED.");

PyDoc_STRVAR(arcstat_uncached_evictable_data__doc__,
"Evictable ARC_BUFC_DATA bytes pending eviction due to ARC_FLAG_UNCACHED "
"(no outstanding holds on the buffer).");

PyDoc_STRVAR(arcstat_uncached_evictable_metadata__doc__,
"Evictable ARC_BUFC_METADATA bytes pending eviction due to "
"ARC_FLAG_UNCACHED (no outstanding holds on the buffer).");

/* L2ARC (Level 2 ARC cache device) */

PyDoc_STRVAR(arcstat_l2_hits__doc__,
"L2ARC (Level 2 ARC cache device) read hits.");

PyDoc_STRVAR(arcstat_l2_misses__doc__,
"L2ARC read misses (data not found in L2ARC; fell through to disk).");

PyDoc_STRVAR(arcstat_l2_prefetch_asize__doc__,
"Allocated bytes of L2ARC-cached buffers originating from the prefetch "
"ARC state.");

PyDoc_STRVAR(arcstat_l2_mru_asize__doc__,
"Allocated bytes of L2ARC-cached buffers originating from the MRU state.");

PyDoc_STRVAR(arcstat_l2_mfu_asize__doc__,
"Allocated bytes of L2ARC-cached buffers originating from the MFU state.");

PyDoc_STRVAR(arcstat_l2_bufc_data_asize__doc__,
"Allocated bytes of L2ARC-cached ARC_BUFC_DATA buffers.");

PyDoc_STRVAR(arcstat_l2_bufc_metadata_asize__doc__,
"Allocated bytes of L2ARC-cached ARC_BUFC_METADATA buffers.");

PyDoc_STRVAR(arcstat_l2_feeds__doc__,
"Number of ARC header batches (feeds) sent to L2ARC for writing.");

PyDoc_STRVAR(arcstat_l2_rw_clash__doc__,
"ARC evictions skipped because an L2ARC write was in progress on the "
"buffer at the time of eviction.");

PyDoc_STRVAR(arcstat_l2_read_bytes__doc__,
"Total bytes read from L2ARC devices.");

PyDoc_STRVAR(arcstat_l2_write_bytes__doc__,
"Total bytes written to L2ARC devices.");

PyDoc_STRVAR(arcstat_l2_writes_sent__doc__,
"L2ARC write I/Os issued.");

PyDoc_STRVAR(arcstat_l2_writes_done__doc__,
"L2ARC write I/Os completed successfully.");

PyDoc_STRVAR(arcstat_l2_writes_error__doc__,
"L2ARC write I/Os that completed with errors.");

PyDoc_STRVAR(arcstat_l2_writes_lock_retry__doc__,
"L2ARC write attempts that had to retry due to lock contention.");

PyDoc_STRVAR(arcstat_l2_evict_lock_retry__doc__,
"L2ARC eviction attempts that had to retry due to lock contention.");

PyDoc_STRVAR(arcstat_l2_evict_reading__doc__,
"L2ARC buffers skipped for eviction because a read was in progress on "
"the buffer.");

PyDoc_STRVAR(arcstat_l2_evict_l1cached__doc__,
"L2ARC buffers evicted from L2ARC that were still present in L1 ARC.");

PyDoc_STRVAR(arcstat_l2_free_on_write__doc__,
"L2ARC buffers queued to be freed on the next L2ARC write.");

PyDoc_STRVAR(arcstat_l2_abort_lowmem__doc__,
"L2ARC write passes aborted because system memory was too low to "
"proceed safely.");

PyDoc_STRVAR(arcstat_l2_cksum_bad__doc__,
"L2ARC reads that failed due to a checksum mismatch.");

PyDoc_STRVAR(arcstat_l2_io_error__doc__,
"L2ARC reads that failed due to an I/O error.");

PyDoc_STRVAR(arcstat_l2_size__doc__,
"Logical (uncompressed) size in bytes of all data currently cached on "
"L2ARC devices.");

PyDoc_STRVAR(arcstat_l2_asize__doc__,
"Actual (compressed, device-aligned) size in bytes of all data currently "
"stored on L2ARC devices.");

PyDoc_STRVAR(arcstat_l2_hdr_size__doc__,
"Bytes consumed by L2ARC buffer header structures resident in L1 ARC "
"memory.");

/* L2ARC log blocks (persistent L2ARC rebuild) */

PyDoc_STRVAR(arcstat_l2_log_blk_writes__doc__,
"Number of L2ARC log blocks written. Log blocks record which ARC buffers "
"are stored on L2ARC and enable the cache to be rebuilt after a pool "
"import without re-reading all data from disk.");

PyDoc_STRVAR(arcstat_l2_log_blk_avg_asize__doc__,
"Moving average of the aligned size in bytes of L2ARC log blocks. "
"Updated during L2ARC rebuild and during log block writes.");

PyDoc_STRVAR(arcstat_l2_log_blk_asize__doc__,
"Total aligned size in bytes of all L2ARC log blocks currently on "
"L2ARC devices.");

PyDoc_STRVAR(arcstat_l2_log_blk_count__doc__,
"Number of L2ARC log blocks currently present on L2ARC devices.");

PyDoc_STRVAR(arcstat_l2_data_to_meta_ratio__doc__,
"Moving average ratio of restored L2ARC data size to the aligned size "
"of its log metadata. Updated during L2ARC rebuild and log block writes.");

/* L2ARC rebuild */

PyDoc_STRVAR(arcstat_l2_rebuild_success__doc__,
"Number of L2ARC devices for which log-based rebuild completed "
"successfully on pool import.");

PyDoc_STRVAR(arcstat_l2_rebuild_unsupported__doc__,
"L2ARC rebuild attempts aborted because the device header was in an "
"unsupported format or was corrupted.");

PyDoc_STRVAR(arcstat_l2_rebuild_io_errors__doc__,
"L2ARC rebuild attempts aborted due to I/O errors while reading a log "
"block.");

PyDoc_STRVAR(arcstat_l2_rebuild_dh_errors__doc__,
"L2ARC rebuild attempts aborted due to I/O errors while reading the "
"device header.");

PyDoc_STRVAR(arcstat_l2_rebuild_cksum_lb_errors__doc__,
"L2ARC log blocks skipped during rebuild due to checksum errors.");

PyDoc_STRVAR(arcstat_l2_rebuild_lowmem__doc__,
"L2ARC rebuild attempts aborted because system memory was too low.");

PyDoc_STRVAR(arcstat_l2_rebuild_size__doc__,
"Logical size in bytes of L2ARC data restored during the last rebuild.");

PyDoc_STRVAR(arcstat_l2_rebuild_asize__doc__,
"Aligned (on-device) size in bytes of L2ARC data restored during the "
"last rebuild.");

PyDoc_STRVAR(arcstat_l2_rebuild_bufs__doc__,
"Number of L2ARC log entries (buffers) successfully restored to ARC "
"during rebuild.");

PyDoc_STRVAR(arcstat_l2_rebuild_bufs_precached__doc__,
"Number of L2ARC log entries skipped during rebuild because those "
"buffers were already cached in L1 ARC.");

PyDoc_STRVAR(arcstat_l2_rebuild_log_blks__doc__,
"Number of L2ARC log blocks successfully read during rebuild. Each log "
"block may describe up to L2ARC_LOG_BLK_MAX_ENTRIES buffers.");

/* Memory */

PyDoc_STRVAR(arcstat_memory_throttle_count__doc__,
"Number of times ARC was asked to shrink in response to memory pressure "
"(throttle events triggered by the kernel reclaim path).");

PyDoc_STRVAR(arcstat_memory_direct_count__doc__,
"Number of direct memory reclaim events where the ARC shrank "
"synchronously to satisfy memory pressure.");

PyDoc_STRVAR(arcstat_memory_indirect_count__doc__,
"Number of indirect memory reclaim events where the ARC shrank "
"asynchronously in response to memory pressure.");

PyDoc_STRVAR(arcstat_memory_all_bytes__doc__,
"Total system memory in bytes as reported to the ARC.");

PyDoc_STRVAR(arcstat_memory_free_bytes__doc__,
"Free system memory in bytes as reported to the ARC.");

PyDoc_STRVAR(arcstat_memory_available_bytes__doc__,
"Estimated memory in bytes currently available for ARC growth. "
"Signed value (KSTAT_DATA_INT64); may be negative under sustained "
"memory pressure when the ARC owes memory back to the system.");

/* ARC control and state */

PyDoc_STRVAR(arcstat_no_grow__doc__,
"Non-zero when the ARC is suppressing growth due to memory pressure.");

PyDoc_STRVAR(arcstat_tempreserve__doc__,
"Bytes temporarily reserved in the ARC for in-progress dirty data "
"writes. Released when the write completes.");

PyDoc_STRVAR(arcstat_loaned_bytes__doc__,
"Bytes loaned from the ARC to other kernel subsystems (e.g. for "
"zero-copy I/O staging). Counted against the ARC size target.");

PyDoc_STRVAR(arcstat_prune__doc__,
"Number of times the ARC requested that registered subsystems release "
"memory via prune callbacks.");

PyDoc_STRVAR(arcstat_meta_used__doc__,
"Total ARC bytes consumed by metadata: sum of hdr_size, metadata_size, "
"dbuf_size, dnode_size, and bonus_size.");

PyDoc_STRVAR(arcstat_dnode_limit__doc__,
"Target upper bound in bytes for dnode_size within the ARC. Dnode "
"eviction is triggered when dnode_size approaches this limit.");

/* Prefetch */

PyDoc_STRVAR(arcstat_async_upgrade_sync__doc__,
"Async prefetch requests that were upgraded to synchronous because a "
"demand read arrived before the prefetch completed.");

PyDoc_STRVAR(arcstat_predictive_prefetch__doc__,
"Predictive prefetch requests issued. Predictive prefetch detects "
"sequential access patterns and fetches data ahead of demand.");

PyDoc_STRVAR(arcstat_demand_hit_predictive_prefetch__doc__,
"Demand reads satisfied by a predictive prefetch that had already "
"completed (prefetch was useful).");

PyDoc_STRVAR(arcstat_demand_iohit_predictive_prefetch__doc__,
"Demand reads that found a predictive prefetch already in progress "
"(overlapping prefetch and demand I/O).");

PyDoc_STRVAR(arcstat_prescient_prefetch__doc__,
"Prescient prefetch requests issued. Prescient prefetch uses explicit "
"read-ahead hints from upper layers such as dmu_prefetch.");

PyDoc_STRVAR(arcstat_demand_hit_prescient_prefetch__doc__,
"Demand reads satisfied by a prescient prefetch that had already "
"completed (prefetch was useful).");

PyDoc_STRVAR(arcstat_demand_iohit_prescient_prefetch__doc__,
"Demand reads that found a prescient prefetch already in progress "
"(overlapping prefetch and demand I/O).");

/* Miscellaneous */

PyDoc_STRVAR(arcstat_need_free__doc__,
"Bytes the ARC is currently working to free in order to satisfy "
"outstanding memory pressure requests.");

PyDoc_STRVAR(arcstat_sys_free__doc__,
"Free memory target in bytes that the ARC aims to maintain for the "
"rest of the system.");

PyDoc_STRVAR(arcstat_raw_size__doc__,
"Bytes of encrypted (raw/ciphertext) data currently cached in the ARC.");

PyDoc_STRVAR(arcstat_cached_only_in_progress__doc__,
"Non-zero while a cache-only (dry-run) traversal of the pool is active.");

PyDoc_STRVAR(arcstat_abd_chunk_waste_size__doc__,
"Bytes wasted due to ABD (Aggregation Buffer Descriptor) chunk alignment "
"padding. ABDs allocate memory in fixed-size chunks; unused space at the "
"end of the last chunk of each allocation contributes to this counter.");

/* -------------------------------------------------------------------------
 * ArcStats struct sequence definition
 *
 * Field order must match the arc_stats initializer in module/zfs/arc.c
 * (lines 503-654, excluding the COMPAT_FREEBSD11-only "other_size" entry).
 *
 * The test tests/test_kstat_arcstats.py verifies this order against the
 * live " ARCSTATS_PATH " file in CI.
 * ------------------------------------------------------------------------- */

static PyStructSequence_Field arcstats_fields[] = {
    {"hits", arcstat_hits__doc__},
    {"iohits", arcstat_iohits__doc__},
    {"misses", arcstat_misses__doc__},
    {"demand_data_hits", arcstat_demand_data_hits__doc__},
    {"demand_data_iohits", arcstat_demand_data_iohits__doc__},
    {"demand_data_misses", arcstat_demand_data_misses__doc__},
    {"demand_metadata_hits", arcstat_demand_metadata_hits__doc__},
    {"demand_metadata_iohits", arcstat_demand_metadata_iohits__doc__},
    {"demand_metadata_misses", arcstat_demand_metadata_misses__doc__},
    {"prefetch_data_hits", arcstat_prefetch_data_hits__doc__},
    {"prefetch_data_iohits", arcstat_prefetch_data_iohits__doc__},
    {"prefetch_data_misses", arcstat_prefetch_data_misses__doc__},
    {"prefetch_metadata_hits", arcstat_prefetch_metadata_hits__doc__},
    {"prefetch_metadata_iohits", arcstat_prefetch_metadata_iohits__doc__},
    {"prefetch_metadata_misses", arcstat_prefetch_metadata_misses__doc__},
    {"mru_hits", arcstat_mru_hits__doc__},
    {"mru_ghost_hits", arcstat_mru_ghost_hits__doc__},
    {"mfu_hits", arcstat_mfu_hits__doc__},
    {"mfu_ghost_hits", arcstat_mfu_ghost_hits__doc__},
    {"uncached_hits", arcstat_uncached_hits__doc__},
    {"deleted", arcstat_deleted__doc__},
    {"mutex_miss", arcstat_mutex_miss__doc__},
    {"access_skip", arcstat_access_skip__doc__},
    {"evict_skip", arcstat_evict_skip__doc__},
    {"evict_not_enough", arcstat_evict_not_enough__doc__},
    {"evict_l2_cached", arcstat_evict_l2_cached__doc__},
    {"evict_l2_eligible", arcstat_evict_l2_eligible__doc__},
    {"evict_l2_eligible_mfu", arcstat_evict_l2_eligible_mfu__doc__},
    {"evict_l2_eligible_mru", arcstat_evict_l2_eligible_mru__doc__},
    {"evict_l2_ineligible", arcstat_evict_l2_ineligible__doc__},
    {"evict_l2_skip", arcstat_evict_l2_skip__doc__},
    {"hash_elements", arcstat_hash_elements__doc__},
    {"hash_elements_max", arcstat_hash_elements_max__doc__},
    {"hash_collisions", arcstat_hash_collisions__doc__},
    {"hash_chains", arcstat_hash_chains__doc__},
    {"hash_chain_max", arcstat_hash_chain_max__doc__},
    {"meta", arcstat_meta__doc__},
    {"pd", arcstat_pd__doc__},
    {"pm", arcstat_pm__doc__},
    {"c", arcstat_c__doc__},
    {"c_min", arcstat_c_min__doc__},
    {"c_max", arcstat_c_max__doc__},
    {"size", arcstat_size__doc__},
    {"compressed_size", arcstat_compressed_size__doc__},
    {"uncompressed_size", arcstat_uncompressed_size__doc__},
    {"overhead_size", arcstat_overhead_size__doc__},
    {"hdr_size", arcstat_hdr_size__doc__},
    {"data_size", arcstat_data_size__doc__},
    {"metadata_size", arcstat_metadata_size__doc__},
    {"dbuf_size", arcstat_dbuf_size__doc__},
    {"dnode_size", arcstat_dnode_size__doc__},
    {"bonus_size", arcstat_bonus_size__doc__},
    {"anon_size", arcstat_anon_size__doc__},
    {"anon_data", arcstat_anon_data__doc__},
    {"anon_metadata", arcstat_anon_metadata__doc__},
    {"anon_evictable_data", arcstat_anon_evictable_data__doc__},
    {"anon_evictable_metadata", arcstat_anon_evictable_metadata__doc__},
    {"mru_size", arcstat_mru_size__doc__},
    {"mru_data", arcstat_mru_data__doc__},
    {"mru_metadata", arcstat_mru_metadata__doc__},
    {"mru_evictable_data", arcstat_mru_evictable_data__doc__},
    {"mru_evictable_metadata", arcstat_mru_evictable_metadata__doc__},
    {"mru_ghost_size", arcstat_mru_ghost_size__doc__},
    {"mru_ghost_data", arcstat_mru_ghost_data__doc__},
    {"mru_ghost_metadata", arcstat_mru_ghost_metadata__doc__},
    {"mru_ghost_evictable_data", arcstat_mru_ghost_evictable_data__doc__},
    {"mru_ghost_evictable_metadata", arcstat_mru_ghost_evictable_metadata__doc__},
    {"mfu_size", arcstat_mfu_size__doc__},
    {"mfu_data", arcstat_mfu_data__doc__},
    {"mfu_metadata", arcstat_mfu_metadata__doc__},
    {"mfu_evictable_data", arcstat_mfu_evictable_data__doc__},
    {"mfu_evictable_metadata", arcstat_mfu_evictable_metadata__doc__},
    {"mfu_ghost_size", arcstat_mfu_ghost_size__doc__},
    {"mfu_ghost_data", arcstat_mfu_ghost_data__doc__},
    {"mfu_ghost_metadata", arcstat_mfu_ghost_metadata__doc__},
    {"mfu_ghost_evictable_data", arcstat_mfu_ghost_evictable_data__doc__},
    {"mfu_ghost_evictable_metadata", arcstat_mfu_ghost_evictable_metadata__doc__},
    {"uncached_size", arcstat_uncached_size__doc__},
    {"uncached_data", arcstat_uncached_data__doc__},
    {"uncached_metadata", arcstat_uncached_metadata__doc__},
    {"uncached_evictable_data", arcstat_uncached_evictable_data__doc__},
    {"uncached_evictable_metadata", arcstat_uncached_evictable_metadata__doc__},
    {"l2_hits", arcstat_l2_hits__doc__},
    {"l2_misses", arcstat_l2_misses__doc__},
    {"l2_prefetch_asize", arcstat_l2_prefetch_asize__doc__},
    {"l2_mru_asize", arcstat_l2_mru_asize__doc__},
    {"l2_mfu_asize", arcstat_l2_mfu_asize__doc__},
    {"l2_bufc_data_asize", arcstat_l2_bufc_data_asize__doc__},
    {"l2_bufc_metadata_asize", arcstat_l2_bufc_metadata_asize__doc__},
    {"l2_feeds", arcstat_l2_feeds__doc__},
    {"l2_rw_clash", arcstat_l2_rw_clash__doc__},
    {"l2_read_bytes", arcstat_l2_read_bytes__doc__},
    {"l2_write_bytes", arcstat_l2_write_bytes__doc__},
    {"l2_writes_sent", arcstat_l2_writes_sent__doc__},
    {"l2_writes_done", arcstat_l2_writes_done__doc__},
    {"l2_writes_error", arcstat_l2_writes_error__doc__},
    {"l2_writes_lock_retry", arcstat_l2_writes_lock_retry__doc__},
    {"l2_evict_lock_retry", arcstat_l2_evict_lock_retry__doc__},
    {"l2_evict_reading", arcstat_l2_evict_reading__doc__},
    {"l2_evict_l1cached", arcstat_l2_evict_l1cached__doc__},
    {"l2_free_on_write", arcstat_l2_free_on_write__doc__},
    {"l2_abort_lowmem", arcstat_l2_abort_lowmem__doc__},
    {"l2_cksum_bad", arcstat_l2_cksum_bad__doc__},
    {"l2_io_error", arcstat_l2_io_error__doc__},
    {"l2_size", arcstat_l2_size__doc__},
    {"l2_asize", arcstat_l2_asize__doc__},
    {"l2_hdr_size", arcstat_l2_hdr_size__doc__},
    {"l2_log_blk_writes", arcstat_l2_log_blk_writes__doc__},
    {"l2_log_blk_avg_asize", arcstat_l2_log_blk_avg_asize__doc__},
    {"l2_log_blk_asize", arcstat_l2_log_blk_asize__doc__},
    {"l2_log_blk_count", arcstat_l2_log_blk_count__doc__},
    {"l2_data_to_meta_ratio", arcstat_l2_data_to_meta_ratio__doc__},
    {"l2_rebuild_success", arcstat_l2_rebuild_success__doc__},
    {"l2_rebuild_unsupported", arcstat_l2_rebuild_unsupported__doc__},
    {"l2_rebuild_io_errors", arcstat_l2_rebuild_io_errors__doc__},
    {"l2_rebuild_dh_errors", arcstat_l2_rebuild_dh_errors__doc__},
    {"l2_rebuild_cksum_lb_errors", arcstat_l2_rebuild_cksum_lb_errors__doc__},
    {"l2_rebuild_lowmem", arcstat_l2_rebuild_lowmem__doc__},
    {"l2_rebuild_size", arcstat_l2_rebuild_size__doc__},
    {"l2_rebuild_asize", arcstat_l2_rebuild_asize__doc__},
    {"l2_rebuild_bufs", arcstat_l2_rebuild_bufs__doc__},
    {"l2_rebuild_bufs_precached", arcstat_l2_rebuild_bufs_precached__doc__},
    {"l2_rebuild_log_blks", arcstat_l2_rebuild_log_blks__doc__},
    {"memory_throttle_count", arcstat_memory_throttle_count__doc__},
    {"memory_direct_count", arcstat_memory_direct_count__doc__},
    {"memory_indirect_count", arcstat_memory_indirect_count__doc__},
    {"memory_all_bytes", arcstat_memory_all_bytes__doc__},
    {"memory_free_bytes", arcstat_memory_free_bytes__doc__},
    {"memory_available_bytes", arcstat_memory_available_bytes__doc__},
    {"arc_no_grow", arcstat_no_grow__doc__},
    {"arc_tempreserve", arcstat_tempreserve__doc__},
    {"arc_loaned_bytes", arcstat_loaned_bytes__doc__},
    {"arc_prune", arcstat_prune__doc__},
    {"arc_meta_used", arcstat_meta_used__doc__},
    {"arc_dnode_limit", arcstat_dnode_limit__doc__},
    {"async_upgrade_sync", arcstat_async_upgrade_sync__doc__},
    {"predictive_prefetch", arcstat_predictive_prefetch__doc__},
    {"demand_hit_predictive_prefetch", arcstat_demand_hit_predictive_prefetch__doc__},
    {"demand_iohit_predictive_prefetch", arcstat_demand_iohit_predictive_prefetch__doc__},
    {"prescient_prefetch", arcstat_prescient_prefetch__doc__},
    {"demand_hit_prescient_prefetch", arcstat_demand_hit_prescient_prefetch__doc__},
    {"demand_iohit_prescient_prefetch", arcstat_demand_iohit_prescient_prefetch__doc__},
    {"arc_need_free", arcstat_need_free__doc__},
    {"arc_sys_free", arcstat_sys_free__doc__},
    {"arc_raw_size", arcstat_raw_size__doc__},
    {"cached_only_in_progress", arcstat_cached_only_in_progress__doc__},
    {"abd_chunk_waste_size", arcstat_abd_chunk_waste_size__doc__},
    {0},
};

static PyStructSequence_Desc arcstats_desc = {
    .name = "truenas_pylibzfs.kstat.ArcStats",
    .doc = "Snapshot of ZFS ARC (Adaptive Replacement Cache) statistics "
           "read from " ARCSTATS_PATH ".",
    .fields = arcstats_fields,
    .n_in_sequence = ARCSTATS_N_FIELDS,
};

static int
init_arcstats_type(PyObject *module)
{
    pyzfs_kstat_state_t *state = NULL;
    PyTypeObject *tp = NULL;

    state = (pyzfs_kstat_state_t *)PyModule_GetState(module);

    tp = PyStructSequence_NewType(&arcstats_desc);
    if (tp == NULL)
        return -1;

    state->arcstats_type = tp;

    return PyModule_AddObjectRef(module, "ArcStats", (PyObject *)tp);
}

/* -------------------------------------------------------------------------
 * ZilStats field docstrings
 *
 * Field order must match the zil_stats initializer in module/zfs/zil.c
 * (lines 101-123 in the ZFS source tree).
 * ------------------------------------------------------------------------- */

/* Commit counters */

PyDoc_STRVAR(zilstat_commit_count__doc__,
"Number of times a ZIL (ZFS Intent Log) commit (e.g. fsync) has been "
"requested.");

PyDoc_STRVAR(zilstat_commit_writer_count__doc__,
"Number of times the ZIL has been flushed to stable storage. This is "
"less than zil_commit_count when commits are merged.");

PyDoc_STRVAR(zilstat_commit_error_count__doc__,
"ZIL commits that failed due to an I/O error during write or flush, "
"forcing a fallback to txg_wait_synced().");

PyDoc_STRVAR(zilstat_commit_stall_count__doc__,
"ZIL commits that stalled because LWB (Log Write Block) allocation "
"failed and the ZIL chain was abandoned, forcing a fallback to "
"txg_wait_synced().");

PyDoc_STRVAR(zilstat_commit_suspend_count__doc__,
"ZIL commits that failed because the ZIL was suspended, forcing a "
"fallback to txg_wait_synced().");

PyDoc_STRVAR(zilstat_commit_crash_count__doc__,
"ZIL commits that failed because the ZIL crashed, forcing a fallback "
"to txg_wait_synced().");

/* Intent transaction (itx) counters */

PyDoc_STRVAR(zilstat_itx_count__doc__,
"Total number of intent transactions (reads, writes, renames, etc.) "
"committed to the ZIL.");

PyDoc_STRVAR(zilstat_itx_indirect_count__doc__,
"Transactions written using indirect mode (WR_INDIRECT): data is written "
"directly to the pool via dmu_sync() and a block pointer is stored in the "
"log record instead of the data itself.");

PyDoc_STRVAR(zilstat_itx_indirect_bytes__doc__,
"Bytes of transaction data written using indirect mode. Accumulates the "
"logical data length, not the log record size.");

PyDoc_STRVAR(zilstat_itx_copied_count__doc__,
"Transactions written using immediate-copy mode (WR_COPIED): data is "
"copied directly into the log record at commit time because the "
"transaction was synchronous (O_SYNC or O_DSYNC).");

PyDoc_STRVAR(zilstat_itx_copied_bytes__doc__,
"Bytes of transaction data written using immediate-copy mode.");

PyDoc_STRVAR(zilstat_itx_needcopy_count__doc__,
"Transactions written using deferred-copy mode (WR_NEED_COPY): data is "
"retrieved from the DMU (Data Management Unit) and copied into the log "
"record only if the write needs to be flushed.");

PyDoc_STRVAR(zilstat_itx_needcopy_bytes__doc__,
"Bytes of transaction data written using deferred-copy mode.");

/* Normal pool metaslab */

PyDoc_STRVAR(zilstat_itx_metaslab_normal_count__doc__,
"Transactions allocated to the normal (non-slog) storage pool.");

PyDoc_STRVAR(zilstat_itx_metaslab_normal_bytes__doc__,
"Log record bytes allocated to the normal pool. Accumulates actual "
"log record sizes, which exclude data for indirect writes. "
"Invariant: bytes <= write <= alloc.");

PyDoc_STRVAR(zilstat_itx_metaslab_normal_write__doc__,
"Bytes written to the normal pool for ZIL log blocks.");

PyDoc_STRVAR(zilstat_itx_metaslab_normal_alloc__doc__,
"Bytes allocated from the normal pool for ZIL log blocks.");

/* Slog (Separate Intent Log) metaslab */

PyDoc_STRVAR(zilstat_itx_metaslab_slog_count__doc__,
"Transactions allocated to the slog (Separate Intent Log) device. "
"If no dedicated log device is configured, slog statistics remain "
"zero and all activity is counted under the normal pool.");

PyDoc_STRVAR(zilstat_itx_metaslab_slog_bytes__doc__,
"Log record bytes allocated to the slog device. "
"Invariant: bytes <= write <= alloc.");

PyDoc_STRVAR(zilstat_itx_metaslab_slog_write__doc__,
"Bytes written to the slog device for ZIL log blocks.");

PyDoc_STRVAR(zilstat_itx_metaslab_slog_alloc__doc__,
"Bytes allocated from the slog device for ZIL log blocks.");

/* -------------------------------------------------------------------------
 * ZilStats struct sequence definition
 *
 * Field order must match the zil_stats initializer in module/zfs/zil.c.
 *
 * The test tests/test_kstat_zilstats.py verifies this order against the
 * live ZILSTATS_PATH file in CI.
 * ------------------------------------------------------------------------- */

static PyStructSequence_Field zilstats_fields[] = {
    {"zil_commit_count", zilstat_commit_count__doc__},
    {"zil_commit_writer_count", zilstat_commit_writer_count__doc__},
    {"zil_commit_error_count", zilstat_commit_error_count__doc__},
    {"zil_commit_stall_count", zilstat_commit_stall_count__doc__},
    {"zil_commit_suspend_count", zilstat_commit_suspend_count__doc__},
    {"zil_commit_crash_count", zilstat_commit_crash_count__doc__},
    {"zil_itx_count", zilstat_itx_count__doc__},
    {"zil_itx_indirect_count", zilstat_itx_indirect_count__doc__},
    {"zil_itx_indirect_bytes", zilstat_itx_indirect_bytes__doc__},
    {"zil_itx_copied_count", zilstat_itx_copied_count__doc__},
    {"zil_itx_copied_bytes", zilstat_itx_copied_bytes__doc__},
    {"zil_itx_needcopy_count", zilstat_itx_needcopy_count__doc__},
    {"zil_itx_needcopy_bytes", zilstat_itx_needcopy_bytes__doc__},
    {"zil_itx_metaslab_normal_count", zilstat_itx_metaslab_normal_count__doc__},
    {"zil_itx_metaslab_normal_bytes", zilstat_itx_metaslab_normal_bytes__doc__},
    {"zil_itx_metaslab_normal_write", zilstat_itx_metaslab_normal_write__doc__},
    {"zil_itx_metaslab_normal_alloc", zilstat_itx_metaslab_normal_alloc__doc__},
    {"zil_itx_metaslab_slog_count", zilstat_itx_metaslab_slog_count__doc__},
    {"zil_itx_metaslab_slog_bytes", zilstat_itx_metaslab_slog_bytes__doc__},
    {"zil_itx_metaslab_slog_write", zilstat_itx_metaslab_slog_write__doc__},
    {"zil_itx_metaslab_slog_alloc", zilstat_itx_metaslab_slog_alloc__doc__},
    {0},
};

static PyStructSequence_Desc zilstats_desc = {
    .name = "truenas_pylibzfs.kstat.ZilStats",
    .doc = "Snapshot of ZFS ZIL (ZFS Intent Log) statistics "
           "read from " ZILSTATS_PATH ".",
    .fields = zilstats_fields,
    .n_in_sequence = ZILSTATS_N_FIELDS,
};

static int
init_zilstats_type(PyObject *module)
{
    pyzfs_kstat_state_t *state = NULL;
    PyTypeObject *tp = NULL;

    state = (pyzfs_kstat_state_t *)PyModule_GetState(module);

    tp = PyStructSequence_NewType(&zilstats_desc);
    if (tp == NULL)
        return -1;

    state->zilstats_type = tp;

    return PyModule_AddObjectRef(module, "ZilStats", (PyObject *)tp);
}

/* -------------------------------------------------------------------------
 * Module method docstrings and table
 * ------------------------------------------------------------------------- */

PyDoc_STRVAR(py_get_arcstats__doc__,
"get_arcstats() -> ArcStats\n"
"--------------------------\n\n"
"Read and return a snapshot of ZFS ARC (Adaptive Replacement Cache)\n"
"statistics from " ARCSTATS_PATH ".\n"
"\n"
"Each call opens and reads the file, so successive calls return\n"
"independent snapshots reflecting the current kernel state.\n"
"\n"
"Returns\n"
"-------\n"
"ArcStats\n"
"    Named struct sequence with one integer field per ARC statistic.\n"
"    Fields are accessible by name (stats.hits) or by index (stats[0]).\n"
"    The single signed field is memory_available_bytes; all others are\n"
"    unsigned.\n"
"\n"
"Raises\n"
"------\n"
"OSError\n"
"    " ARCSTATS_PATH " could not be opened.\n"
"ValueError\n"
"    " ARCSTATS_PATH " has an unexpected format or field count.\n"
"    This indicates a ZFS version mismatch; update arcstats.c and stubs.\n");

PyDoc_STRVAR(py_get_zilstats__doc__,
"get_zilstats() -> ZilStats\n"
"--------------------------\n\n"
"Read and return a snapshot of ZFS ZIL (ZFS Intent Log) statistics\n"
"from " ZILSTATS_PATH ".\n"
"\n"
"Each call opens and reads the file, so successive calls return\n"
"independent snapshots reflecting the current kernel state.\n"
"\n"
"Returns\n"
"-------\n"
"ZilStats\n"
"    Named struct sequence with one integer field per ZIL statistic.\n"
"    Fields are accessible by name (stats.zil_commit_count) or by\n"
"    index (stats[0]). All fields are unsigned.\n"
"\n"
"Raises\n"
"------\n"
"OSError\n"
"    " ZILSTATS_PATH " could not be opened.\n"
"ValueError\n"
"    " ZILSTATS_PATH " has an unexpected format or field count.\n"
"    This indicates a ZFS version mismatch; update zilstats.c and stubs.\n");

static PyMethodDef pyzfs_kstat_methods[] = {
    {
        "get_arcstats",
        py_get_arcstats,
        METH_NOARGS,
        py_get_arcstats__doc__,
    },
    {
        "get_zilstats",
        py_get_zilstats,
        METH_NOARGS,
        py_get_zilstats__doc__,
    },
    {NULL, NULL, 0, NULL},
};

/* -------------------------------------------------------------------------
 * Module definition and init
 * ------------------------------------------------------------------------- */

PyDoc_STRVAR(pyzfs_kstat_module__doc__,
"truenas_pylibzfs.kstat -- ZFS kernel statistics (kstat) reader.\n\n"
"Provides access to ZFS kernel statistics exposed via the SPL\n"
"(Solaris Porting Layer) kstat interface under /proc/spl/kstat/zfs/.\n"
"No libzfs dependency; reads directly from /proc.\n");

static struct PyModuleDef pyzfs_kstat_module = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "truenas_pylibzfs.kstat",
    .m_doc = pyzfs_kstat_module__doc__,
    .m_size = sizeof(pyzfs_kstat_state_t),
    .m_methods = pyzfs_kstat_methods,
};

PyObject *
py_setup_kstat_module(PyObject *parent)
{
    PyObject *m = NULL;

    m = PyModule_Create(&pyzfs_kstat_module);
    if (m == NULL)
        return NULL;

    if (init_arcstats_type(m) < 0) {
        Py_DECREF(m);
        return NULL;
    }

    if (init_zilstats_type(m) < 0) {
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
