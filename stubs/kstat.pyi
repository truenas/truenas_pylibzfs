from typing import Any, ClassVar, Self, final


@final
class ArcStats:
    """Snapshot of ZFS ARC (Adaptive Replacement Cache) statistics
    read from /proc/spl/kstat/zfs/arcstats.
    """

    # Aggregate hit/miss counters
    hits: int
    """ARC requests satisfied from cache without I/O (sum of all demand and
    prefetch, data and metadata hits)."""
    iohits: int
    """ARC requests for which the required data was already being fetched by
    in-progress I/O at the time of the request."""
    misses: int
    """ARC requests that required issuing new I/O (cache miss)."""

    # Demand data
    demand_data_hits: int
    """Demand data requests satisfied from the ARC without I/O."""
    demand_data_iohits: int
    """Demand data requests whose data was already being fetched by
    in-progress I/O."""
    demand_data_misses: int
    """Demand data requests that required new I/O (cache miss)."""

    # Demand metadata
    demand_metadata_hits: int
    """Demand metadata requests satisfied from the ARC without I/O."""
    demand_metadata_iohits: int
    """Demand metadata requests whose data was already being fetched by
    in-progress I/O."""
    demand_metadata_misses: int
    """Demand metadata requests that required new I/O (cache miss)."""

    # Prefetch data
    prefetch_data_hits: int
    """Prefetch data requests satisfied from the ARC without I/O."""
    prefetch_data_iohits: int
    """Prefetch data requests whose data was already being fetched by
    in-progress I/O."""
    prefetch_data_misses: int
    """Prefetch data requests that required new I/O (cache miss)."""

    # Prefetch metadata
    prefetch_metadata_hits: int
    """Prefetch metadata requests satisfied from the ARC without I/O."""
    prefetch_metadata_iohits: int
    """Prefetch metadata requests whose data was already being fetched by
    in-progress I/O."""
    prefetch_metadata_misses: int
    """Prefetch metadata requests that required new I/O (cache miss)."""

    # ARC state hits
    mru_hits: int
    """ARC hits on buffers in the MRU (Most Recently Used) state."""
    mru_ghost_hits: int
    """ARC hits on the MRU ghost list. Ghost entries track recently evicted
    MRU buffers to bias re-admission toward MRU on the next access."""
    mfu_hits: int
    """ARC hits on buffers in the MFU (Most Frequently Used) state."""
    mfu_ghost_hits: int
    """ARC hits on the MFU ghost list. Ghost entries track recently evicted
    MFU buffers to bias re-admission toward MFU on the next access."""
    uncached_hits: int
    """ARC hits on buffers marked ARC_FLAG_UNCACHED (scheduled for eviction
    on the next opportunity)."""
    deleted: int
    """ARC buffers deleted (freed) from the cache."""

    # Eviction
    mutex_miss: int
    """Evictions skipped because the buffer's hash lock was held by another
    thread. Hash locks are shared across multiple buffers so contention
    may not involve the target buffer."""
    access_skip: int
    """Buffers skipped during access state update because the header was
    released after the hash lock was acquired."""
    evict_skip: int
    """Buffers skipped during eviction because they had I/O in progress,
    were indirect prefetch buffers that had not lived long enough, or
    belonged to a different pool."""
    evict_not_enough: int
    """Times arc_evict_state() could not evict enough buffers to reach its
    target amount."""
    evict_l2_cached: int
    """Bytes evicted from ARC that were already cached on an L2ARC device."""
    evict_l2_eligible: int
    """Bytes evicted from ARC that were eligible for caching on L2ARC."""
    evict_l2_eligible_mfu: int
    """MFU bytes evicted from ARC that were eligible for L2ARC caching."""
    evict_l2_eligible_mru: int
    """MRU bytes evicted from ARC that were eligible for L2ARC caching."""
    evict_l2_ineligible: int
    """Bytes evicted from ARC that were ineligible for L2ARC caching
    (e.g. encrypted data or data that is already on L2ARC)."""
    evict_l2_skip: int
    """ARC evictions for which the L2ARC write was skipped."""

    # Hash table
    hash_elements: int
    """Current number of elements in the ARC hash table."""
    hash_elements_max: int
    """Peak number of elements ever resident in the ARC hash table."""
    hash_collisions: int
    """Total number of hash collisions recorded in the ARC hash table."""
    hash_chains: int
    """Number of hash chains (buckets with more than one element) in the
    ARC hash table."""
    hash_chain_max: int
    """Length of the longest hash chain ever observed in the ARC hash table."""

    # Cache size targets (bytes)
    meta: int
    """ARC metadata limit in bytes (arc_meta_limit). Metadata consumption
    above this target triggers eviction of metadata buffers."""
    pd: int
    """ARC prefetch data target size in bytes."""
    pm: int
    """ARC prefetch metadata target size in bytes."""
    c: int
    """Current ARC target size in bytes. The ARC grows toward arc_c_max and
    shrinks toward arc_c_min in response to memory pressure."""
    c_min: int
    """Minimum ARC target size in bytes (arc_c_min). The ARC will not shrink
    below this value."""
    c_max: int
    """Maximum ARC target size in bytes (arc_c_max). The ARC will not grow
    beyond this value."""

    # Overall size (bytes)
    size: int
    """Total size of all ARC-cached buffers in bytes."""
    compressed_size: int
    """Compressed size in bytes of data stored in arc_buf_hdr_t b_pabd
    fields. Equals uncompressed_size when compressed ARC is disabled."""
    uncompressed_size: int
    """Uncompressed size in bytes of data stored in arc_buf_hdr_t b_pabd
    fields. Equals compressed_size when compressed ARC is disabled."""
    overhead_size: int
    """Bytes held in short-lived uncompressed arc_buf_t copies attached to
    headers. These copies are evicted when unreferenced unless
    zfs_keep_uncompressed_metadata or zfs_keep_uncompressed_level is set."""
    hdr_size: int
    """Bytes consumed by internal ARC tracking structures (arc_buf_hdr_t and
    arc_buf_t objects). These structures are not backed by ARC data buffers."""
    data_size: int
    """Bytes consumed by ARC_BUFC_DATA buffers, which back on-disk user
    data such as plain file contents."""
    metadata_size: int
    """Bytes consumed by ARC_BUFC_METADATA buffers, which back internal ZFS
    structures such as ZAP objects, dnodes, and indirect blocks."""
    dbuf_size: int
    """Bytes consumed by dmu_buf_impl_t (dbuf) objects."""
    dnode_size: int
    """Bytes consumed by dnode_t objects."""
    bonus_size: int
    """Bytes consumed by dnode bonus buffers."""

    # Anon state (bytes)
    anon_size: int
    """Total bytes in the arc_anon state. Anon buffers are newly allocated
    and have not yet been promoted to MRU or MFU."""
    anon_data: int
    """ARC_BUFC_DATA bytes in the arc_anon state."""
    anon_metadata: int
    """ARC_BUFC_METADATA bytes in the arc_anon state."""
    anon_evictable_data: int
    """Evictable ARC_BUFC_DATA bytes in the arc_anon state
    (no outstanding holds on the buffer)."""
    anon_evictable_metadata: int
    """Evictable ARC_BUFC_METADATA bytes in the arc_anon state
    (no outstanding holds on the buffer)."""

    # MRU state (bytes)
    mru_size: int
    """Total bytes in the MRU (Most Recently Used) state, including data,
    metadata, evictable, and pinned buffers."""
    mru_data: int
    """ARC_BUFC_DATA bytes in the MRU state."""
    mru_metadata: int
    """ARC_BUFC_METADATA bytes in the MRU state."""
    mru_evictable_data: int
    """Evictable ARC_BUFC_DATA bytes in the MRU state
    (no outstanding holds on the buffer)."""
    mru_evictable_metadata: int
    """Evictable ARC_BUFC_METADATA bytes in the MRU state
    (no outstanding holds on the buffer)."""

    # MRU ghost state (bytes) -- notional, no actual buffers
    mru_ghost_size: int
    """Notional bytes tracked by MRU ghost headers. Ghost lists hold only
    headers with no associated data buffers; this value represents what
    those buffers would have consumed had they remained cached."""
    mru_ghost_data: int
    """Notional ARC_BUFC_DATA bytes tracked by MRU ghost headers."""
    mru_ghost_metadata: int
    """Notional ARC_BUFC_METADATA bytes tracked by MRU ghost headers."""
    mru_ghost_evictable_data: int
    """Notional evictable ARC_BUFC_DATA bytes on the MRU ghost list."""
    mru_ghost_evictable_metadata: int
    """Notional evictable ARC_BUFC_METADATA bytes on the MRU ghost list."""

    # MFU state (bytes)
    mfu_size: int
    """Total bytes in the MFU (Most Frequently Used) state, including data,
    metadata, evictable, and pinned buffers."""
    mfu_data: int
    """ARC_BUFC_DATA bytes in the MFU state."""
    mfu_metadata: int
    """ARC_BUFC_METADATA bytes in the MFU state."""
    mfu_evictable_data: int
    """Evictable ARC_BUFC_DATA bytes in the MFU state
    (no outstanding holds on the buffer)."""
    mfu_evictable_metadata: int
    """Evictable ARC_BUFC_METADATA bytes in the MFU state
    (no outstanding holds on the buffer)."""

    # MFU ghost state (bytes) -- notional, no actual buffers
    mfu_ghost_size: int
    """Notional bytes tracked by MFU ghost headers.
    See mru_ghost_size for a full description of ghost lists."""
    mfu_ghost_data: int
    """Notional ARC_BUFC_DATA bytes tracked by MFU ghost headers."""
    mfu_ghost_metadata: int
    """Notional ARC_BUFC_METADATA bytes tracked by MFU ghost headers."""
    mfu_ghost_evictable_data: int
    """Notional evictable ARC_BUFC_DATA bytes on the MFU ghost list."""
    mfu_ghost_evictable_metadata: int
    """Notional evictable ARC_BUFC_METADATA bytes on the MFU ghost list."""

    # Uncached state (bytes)
    uncached_size: int
    """Total bytes pending eviction from ARC because ARC_FLAG_UNCACHED is
    set. These buffers will be freed on the next eviction pass."""
    uncached_data: int
    """ARC_BUFC_DATA bytes pending eviction due to ARC_FLAG_UNCACHED."""
    uncached_metadata: int
    """ARC_BUFC_METADATA bytes pending eviction due to ARC_FLAG_UNCACHED."""
    uncached_evictable_data: int
    """Evictable ARC_BUFC_DATA bytes pending eviction due to ARC_FLAG_UNCACHED
    (no outstanding holds on the buffer)."""
    uncached_evictable_metadata: int
    """Evictable ARC_BUFC_METADATA bytes pending eviction due to
    ARC_FLAG_UNCACHED (no outstanding holds on the buffer)."""

    # L2ARC (Level 2 ARC cache device)
    l2_hits: int
    """L2ARC (Level 2 ARC cache device) read hits."""
    l2_misses: int
    """L2ARC read misses (data not found in L2ARC; fell through to disk)."""

    # L2ARC sizes by ARC state (bytes)
    l2_prefetch_asize: int
    """Allocated bytes of L2ARC-cached buffers originating from the prefetch
    ARC state."""
    l2_mru_asize: int
    """Allocated bytes of L2ARC-cached buffers originating from the MRU state."""
    l2_mfu_asize: int
    """Allocated bytes of L2ARC-cached buffers originating from the MFU state."""

    # L2ARC sizes by content type (bytes)
    l2_bufc_data_asize: int
    """Allocated bytes of L2ARC-cached ARC_BUFC_DATA buffers."""
    l2_bufc_metadata_asize: int
    """Allocated bytes of L2ARC-cached ARC_BUFC_METADATA buffers."""

    # L2ARC I/O
    l2_feeds: int
    """Number of ARC header batches (feeds) sent to L2ARC for writing."""
    l2_rw_clash: int
    """ARC evictions skipped because an L2ARC write was in progress on the
    buffer at the time of eviction."""
    l2_read_bytes: int
    """Total bytes read from L2ARC devices."""
    l2_write_bytes: int
    """Total bytes written to L2ARC devices."""
    l2_writes_sent: int
    """L2ARC write I/Os issued."""
    l2_writes_done: int
    """L2ARC write I/Os completed successfully."""
    l2_writes_error: int
    """L2ARC write I/Os that completed with errors."""
    l2_writes_lock_retry: int
    """L2ARC write attempts that had to retry due to lock contention."""
    l2_evict_lock_retry: int
    """L2ARC eviction attempts that had to retry due to lock contention."""
    l2_evict_reading: int
    """L2ARC buffers skipped for eviction because a read was in progress on
    the buffer."""
    l2_evict_l1cached: int
    """L2ARC buffers evicted from L2ARC that were still present in L1 ARC."""
    l2_free_on_write: int
    """L2ARC buffers queued to be freed on the next L2ARC write."""
    l2_abort_lowmem: int
    """L2ARC write passes aborted because system memory was too low to
    proceed safely."""
    l2_cksum_bad: int
    """L2ARC reads that failed due to a checksum mismatch."""
    l2_io_error: int
    """L2ARC reads that failed due to an I/O error."""
    l2_size: int
    """Logical (uncompressed) size in bytes of all data currently cached on
    L2ARC devices."""
    l2_asize: int
    """Actual (compressed, device-aligned) size in bytes of all data currently
    stored on L2ARC devices."""
    l2_hdr_size: int
    """Bytes consumed by L2ARC buffer header structures resident in L1 ARC
    memory."""

    # L2ARC log blocks (persistent L2ARC rebuild)
    l2_log_blk_writes: int
    """Number of L2ARC log blocks written. Log blocks record which ARC buffers
    are stored on L2ARC and enable the cache to be rebuilt after a pool
    import without re-reading all data from disk."""
    l2_log_blk_avg_asize: int
    """Moving average of the aligned size in bytes of L2ARC log blocks.
    Updated during L2ARC rebuild and during log block writes."""
    l2_log_blk_asize: int
    """Total aligned size in bytes of all L2ARC log blocks currently on
    L2ARC devices."""
    l2_log_blk_count: int
    """Number of L2ARC log blocks currently present on L2ARC devices."""
    l2_data_to_meta_ratio: int
    """Moving average ratio of restored L2ARC data size to the aligned size
    of its log metadata. Updated during L2ARC rebuild and log block writes."""

    # L2ARC rebuild
    l2_rebuild_success: int
    """Number of L2ARC devices for which log-based rebuild completed
    successfully on pool import."""
    l2_rebuild_unsupported: int
    """L2ARC rebuild attempts aborted because the device header was in an
    unsupported format or was corrupted."""
    l2_rebuild_io_errors: int
    """L2ARC rebuild attempts aborted due to I/O errors while reading a log
    block."""
    l2_rebuild_dh_errors: int
    """L2ARC rebuild attempts aborted due to I/O errors while reading the
    device header."""
    l2_rebuild_cksum_lb_errors: int
    """L2ARC log blocks skipped during rebuild due to checksum errors."""
    l2_rebuild_lowmem: int
    """L2ARC rebuild attempts aborted because system memory was too low."""
    l2_rebuild_size: int
    """Logical size in bytes of L2ARC data restored during the last rebuild."""
    l2_rebuild_asize: int
    """Aligned (on-device) size in bytes of L2ARC data restored during the
    last rebuild."""
    l2_rebuild_bufs: int
    """Number of L2ARC log entries (buffers) successfully restored to ARC
    during rebuild."""
    l2_rebuild_bufs_precached: int
    """Number of L2ARC log entries skipped during rebuild because those
    buffers were already cached in L1 ARC."""
    l2_rebuild_log_blks: int
    """Number of L2ARC log blocks successfully read during rebuild. Each log
    block may describe up to L2ARC_LOG_BLK_MAX_ENTRIES buffers."""

    # Memory
    memory_throttle_count: int
    """Number of times ARC was asked to shrink in response to memory pressure
    (throttle events triggered by the kernel reclaim path)."""
    memory_direct_count: int
    """Number of direct memory reclaim events where the ARC shrank
    synchronously to satisfy memory pressure."""
    memory_indirect_count: int
    """Number of indirect memory reclaim events where the ARC shrank
    asynchronously in response to memory pressure."""
    memory_all_bytes: int
    """Total system memory in bytes as reported to the ARC."""
    memory_free_bytes: int
    """Free system memory in bytes as reported to the ARC."""
    memory_available_bytes: int
    """Estimated memory in bytes currently available for ARC growth.
    Signed value (KSTAT_DATA_INT64); may be negative under sustained
    memory pressure when the ARC owes memory back to the system."""

    # ARC control and state
    arc_no_grow: int
    """Non-zero when the ARC is suppressing growth due to memory pressure."""
    arc_tempreserve: int
    """Bytes temporarily reserved in the ARC for in-progress dirty data
    writes. Released when the write completes."""
    arc_loaned_bytes: int
    """Bytes loaned from the ARC to other kernel subsystems (e.g. for
    zero-copy I/O staging). Counted against the ARC size target."""
    arc_prune: int
    """Number of times the ARC requested that registered subsystems release
    memory via prune callbacks."""
    arc_meta_used: int
    """Total ARC bytes consumed by metadata: sum of hdr_size, metadata_size,
    dbuf_size, dnode_size, and bonus_size."""
    arc_dnode_limit: int
    """Target upper bound in bytes for dnode_size within the ARC. Dnode
    eviction is triggered when dnode_size approaches this limit."""

    # Prefetch
    async_upgrade_sync: int
    """Async prefetch requests that were upgraded to synchronous because a
    demand read arrived before the prefetch completed."""
    predictive_prefetch: int
    """Predictive prefetch requests issued. Predictive prefetch detects
    sequential access patterns and fetches data ahead of demand."""
    demand_hit_predictive_prefetch: int
    """Demand reads satisfied by a predictive prefetch that had already
    completed (prefetch was useful)."""
    demand_iohit_predictive_prefetch: int
    """Demand reads that found a predictive prefetch already in progress
    (overlapping prefetch and demand I/O)."""
    prescient_prefetch: int
    """Prescient prefetch requests issued. Prescient prefetch uses explicit
    read-ahead hints from upper layers such as dmu_prefetch."""
    demand_hit_prescient_prefetch: int
    """Demand reads satisfied by a prescient prefetch that had already
    completed (prefetch was useful)."""
    demand_iohit_prescient_prefetch: int
    """Demand reads that found a prescient prefetch already in progress
    (overlapping prefetch and demand I/O)."""

    # Miscellaneous
    arc_need_free: int
    """Bytes the ARC is currently working to free in order to satisfy
    outstanding memory pressure requests."""
    arc_sys_free: int
    """Free memory target in bytes that the ARC aims to maintain for the
    rest of the system."""
    arc_raw_size: int
    """Bytes of encrypted (raw/ciphertext) data currently cached in the ARC."""
    cached_only_in_progress: int
    """Non-zero while a cache-only (dry-run) traversal of the pool is active."""
    abd_chunk_waste_size: int
    """Bytes wasted due to ABD (Aggregation Buffer Descriptor) chunk alignment
    padding. ABDs allocate memory in fixed-size chunks; unused space at the
    end of the last chunk of each allocation contributes to this counter."""

    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]           # = 147
    n_sequence_fields: ClassVar[int]  # = 147
    n_unnamed_fields: ClassVar[int]   # = 0
    def __replace__(self, **changes: Any) -> Self: ...


@final
class ZilStats:
    """Snapshot of ZFS ZIL (ZFS Intent Log) statistics
    read from /proc/spl/kstat/zfs/zil.
    """

    # Commit counters
    zil_commit_count: int
    """Number of times a ZIL commit (e.g. fsync) has been requested."""
    zil_commit_writer_count: int
    """Number of times the ZIL has been flushed to stable storage. This is
    less than zil_commit_count when commits are merged."""
    zil_commit_error_count: int
    """ZIL commits that failed due to an I/O error during write or flush,
    forcing a fallback to txg_wait_synced()."""
    zil_commit_stall_count: int
    """ZIL commits that stalled because LWB (Log Write Block) allocation
    failed and the ZIL chain was abandoned, forcing a fallback to
    txg_wait_synced()."""
    zil_commit_suspend_count: int
    """ZIL commits that failed because the ZIL was suspended, forcing a
    fallback to txg_wait_synced()."""
    zil_commit_crash_count: int
    """ZIL commits that failed because the ZIL crashed, forcing a fallback
    to txg_wait_synced()."""

    # Intent transaction (itx) counters
    zil_itx_count: int
    """Total number of intent transactions (reads, writes, renames, etc.)
    committed to the ZIL."""
    zil_itx_indirect_count: int
    """Transactions written using indirect mode (WR_INDIRECT): data is written
    directly to the pool via dmu_sync() and a block pointer is stored in the
    log record instead of the data itself."""
    zil_itx_indirect_bytes: int
    """Bytes of transaction data written using indirect mode. Accumulates the
    logical data length, not the log record size."""
    zil_itx_copied_count: int
    """Transactions written using immediate-copy mode (WR_COPIED): data is
    copied directly into the log record at commit time because the transaction
    was synchronous (O_SYNC or O_DSYNC)."""
    zil_itx_copied_bytes: int
    """Bytes of transaction data written using immediate-copy mode."""
    zil_itx_needcopy_count: int
    """Transactions written using deferred-copy mode (WR_NEED_COPY): data is
    retrieved from the DMU and copied into the log record only if the write
    needs to be flushed."""
    zil_itx_needcopy_bytes: int
    """Bytes of transaction data written using deferred-copy mode."""

    # Normal pool metaslab
    zil_itx_metaslab_normal_count: int
    """Transactions allocated to the normal (non-slog) storage pool."""
    zil_itx_metaslab_normal_bytes: int
    """Log record bytes allocated to the normal pool. Accumulates actual
    log record sizes, which exclude data for indirect writes.
    Invariant: bytes <= write <= alloc."""
    zil_itx_metaslab_normal_write: int
    """Bytes written to the normal pool for ZIL log blocks."""
    zil_itx_metaslab_normal_alloc: int
    """Bytes allocated from the normal pool for ZIL log blocks."""

    # Slog (Separate Intent Log) metaslab
    zil_itx_metaslab_slog_count: int
    """Transactions allocated to the slog (Separate Intent Log) device.
    If no dedicated log device is configured, slog statistics remain zero
    and all activity is counted under the normal pool."""
    zil_itx_metaslab_slog_bytes: int
    """Log record bytes allocated to the slog device.
    Invariant: bytes <= write <= alloc."""
    zil_itx_metaslab_slog_write: int
    """Bytes written to the slog device for ZIL log blocks."""
    zil_itx_metaslab_slog_alloc: int
    """Bytes allocated from the slog device for ZIL log blocks."""

    __match_args__: ClassVar[tuple[str, ...]]
    n_fields: ClassVar[int]           # = 21
    n_sequence_fields: ClassVar[int]  # = 21
    n_unnamed_fields: ClassVar[int]   # = 0
    def __replace__(self, **changes: Any) -> Self: ...


def get_arcstats() -> ArcStats:
    """Read and return a snapshot of ZFS ARC statistics.

    Each call opens and reads /proc/spl/kstat/zfs/arcstats, so successive
    calls return independent snapshots reflecting the current kernel state.

    Raises
    ------
    OSError
        /proc/spl/kstat/zfs/arcstats could not be opened.
    ValueError
        /proc/spl/kstat/zfs/arcstats has an unexpected format or field
        count, indicating a ZFS version mismatch.
    """
    ...


def get_zilstats() -> ZilStats:
    """Read and return a snapshot of ZFS ZIL (ZFS Intent Log) statistics.

    Each call opens and reads /proc/spl/kstat/zfs/zil, so successive
    calls return independent snapshots reflecting the current kernel state.

    Raises
    ------
    OSError
        /proc/spl/kstat/zfs/zil could not be opened.
    ValueError
        /proc/spl/kstat/zfs/zil has an unexpected format or field
        count, indicating a ZFS version mismatch.
    """
    ...
