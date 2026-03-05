import enum

class SendFlags(enum.IntFlag):
    EMBED_DATA: int
    """Include WRITE_EMBEDDED records for blocks that compress to a small
    fraction of the block size, reducing stream size for compressible data.
    Requires the embedded_data feature on the receiving pool."""
    LARGE_BLOCK: int
    """Allow blocks larger than 128 KiB in the stream.
    Requires the large_blocks feature on the receiving pool."""
    COMPRESS: int
    """Send compressed WRITE records, reducing wire size.
    Requires matching compression support on the receiving pool."""
    RAW: int
    """Send raw encrypted/authenticated records without decrypting.
    Required for sending encrypted datasets without the wrapping key."""
    SAVED: int
    """Send a partially-received (saved) stream (zfs send -S).
    Used to rescue interrupted receives when the source snapshot is gone."""

class ZpoolWaitActivity:
    CKPT_DISCARD: int
    FREE: int
    INITIALIZE: int
    REPLACE: int
    REMOVE: int
    RESILVER: int
    SCRUB: int
    TRIM: int
    RAIDZ_EXPAND: int

class ChannelProgramEnum:
    DESTROY_RESOURCES = ""
    DESTROY_SNAPSHOTS = ""
    TAKE_SNAPSHOTS = ""
    ROLLBACK_TO_TXG = ""

class ZFSCoreException(BaseException):
    code: int
    name: str
    errors: tuple

def create_holds(*, holds, cleanup_fd=False) -> tuple: ...
def create_snapshots(*, snapshot_names, user_properties=None) -> None: ...
def destroy_snapshots(*, snapshot_names, defer_destroy=False) -> None: ...
def release_holds(*, holds) -> tuple: ...
def rollback(*, resource_name, snapshot_name=None) -> str: ...
def run_channel_program(*, pool_name, script, script_arguments=None, script_arguments_dict=None,
                        instruction_limit=10000000, memory_limit=10485760, readonly=False) -> dict: ...
def wait(*, pool_name: str, activity: ZpoolWaitActivity | int, tag: int | None = None) -> bool: ...
def send(
    *,
    snapname: str,
    fd: int,
    fromsnap: str | None = None,
    flags: SendFlags | int = 0,
    resume_token: str | None = None,
) -> None: ...
def send_space(
    *,
    snapname: str,
    fromsnap: str | None = None,
    flags: SendFlags | int = 0,
) -> int: ...
def send_progress(*, snapshot_name: str, fd: int) -> tuple[int, int]: ...
def receive(
    *,
    snapname: str,
    fd: int,
    origin: str | None = None,
    props: dict | None = None,
    force: bool = False,
    resumable: bool = False,
    raw: bool = False,
) -> None: ...
