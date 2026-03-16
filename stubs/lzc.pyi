import enum
from collections.abc import Iterable, Sequence
from typing import Any

class SendFlags(enum.IntFlag):
    EMBED_DATA = 1
    """Include WRITE_EMBEDDED records for blocks that compress to a small
    fraction of the block size, reducing stream size for compressible data.
    Requires the embedded_data feature on the receiving pool."""
    LARGE_BLOCK = 2
    """Allow blocks larger than 128 KiB in the stream.
    Requires the large_blocks feature on the receiving pool."""
    COMPRESS = 4
    """Send compressed WRITE records, reducing wire size.
    Requires matching compression support on the receiving pool."""
    RAW = 8
    """Send raw encrypted/authenticated records without decrypting.
    Required for sending encrypted datasets without the wrapping key."""
    SAVED = 16
    """Send a partially-received (saved) stream (zfs send -S).
    Used to rescue interrupted receives when the source snapshot is gone."""

class ZpoolWaitActivity(enum.IntEnum):
    CKPT_DISCARD = 0
    FREE = 1
    INITIALIZE = 2
    REPLACE = 3
    REMOVE = 4
    RESILVER = 5
    SCRUB = 6
    TRIM = 7
    RAIDZ_EXPAND = 8

class ChannelProgramEnum(enum.StrEnum):
    DESTROY_RESOURCES = "DESTROY_RESOURCES"
    DESTROY_SNAPSHOTS = "DESTROY_SNAPSHOTS"
    TAKE_SNAPSHOTS = "TAKE_SNAPSHOTS"
    ROLLBACK_TO_TXG = "ROLLBACK_TO_TXG"

class ZFSCoreException(BaseException):
    code: int
    msg: str
    name: str
    errors: tuple[Any, ...] | None

def create_holds(*, holds: Iterable[tuple[str, str]], cleanup_fd: int | bool = False) -> tuple[Any, ...]: ...
def create_snapshots(*, snapshot_names: Sequence[str], user_properties: dict[str, Any] | None = None) -> None: ...
def destroy_snapshots(*, snapshot_names: Sequence[str], defer_destroy: bool = False) -> None: ...
def release_holds(*, holds: Iterable[tuple[str, str]]) -> None: ...
def rollback(*, resource_name: str, snapshot_name: str | None = None) -> str: ...
def run_channel_program(
    *,
    pool_name: str,
    script: str,
    script_arguments: Any = None,
    script_arguments_dict: dict[str, Any] | None = None,
    instruction_limit: int = 10000000,
    memory_limit: int = 10485760,
    readonly: bool = False,
) -> dict[str, Any]: ...
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
    props: dict[str, Any] | None = None,
    force: bool = False,
    resumable: bool = False,
    raw: bool = False,
) -> None: ...
