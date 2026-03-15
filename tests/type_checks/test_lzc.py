"""
Type tests for the lzc (libzfs_core) submodule.

These functions are never called at runtime; they exist solely to be checked by mypy.
A type error here means a stub signature is broken or a caller is using the API wrong.
"""
from collections.abc import Iterable

from truenas_pylibzfs import lzc


# ---------------------------------------------------------------------------
# create_holds — accepts Iterable[tuple[str, str]], not dict
# ---------------------------------------------------------------------------

def check_create_holds_list() -> None:
    holds: list[tuple[str, str]] = [("pool/fs@snap", "my-tag")]
    lzc.create_holds(holds=holds)


def check_create_holds_iterable() -> None:
    holds: Iterable[tuple[str, str]] = iter([("pool/fs@snap", "my-tag")])
    lzc.create_holds(holds=holds)


def check_create_holds_generator() -> None:
    lzc.create_holds(holds=(("pool/fs@snap", t) for t in ["tag1", "tag2"]))


def check_create_holds_with_cleanup_fd_int() -> None:
    lzc.create_holds(holds=[("pool/fs@snap", "tag")], cleanup_fd=5)


def check_create_holds_with_cleanup_fd_bool() -> None:
    lzc.create_holds(holds=[("pool/fs@snap", "tag")], cleanup_fd=False)


# ---------------------------------------------------------------------------
# release_holds — same shape as create_holds; return type is None
# ---------------------------------------------------------------------------

def check_release_holds_returns_none() -> None:
    result: None = lzc.release_holds(holds=[("pool/fs@snap", "my-tag")])
    _ = result


def check_release_holds_iterable() -> None:
    holds: Iterable[tuple[str, str]] = iter([("pool/fs@snap", "tag")])
    lzc.release_holds(holds=holds)


def check_release_holds_set() -> None:
    # set[tuple[str, str]] is a common middleware pattern; must remain compatible.
    holds: set[tuple[str, str]] = {("pool/fs@snap", "tag")}
    lzc.release_holds(holds=holds)


# ---------------------------------------------------------------------------
# snapshots
# ---------------------------------------------------------------------------

def check_create_snapshots() -> None:
    lzc.create_snapshots(snapshot_names=["pool/fs@snap1", "pool/fs@snap2"])


def check_create_snapshots_with_props() -> None:
    lzc.create_snapshots(
        snapshot_names=["pool/fs@snap"],
        user_properties={"custom:tag": "v1"},
    )


def check_destroy_snapshots() -> None:
    lzc.destroy_snapshots(snapshot_names=["pool/fs@snap1"])


def check_destroy_snapshots_deferred() -> None:
    lzc.destroy_snapshots(snapshot_names=["pool/fs@snap1"], defer_destroy=True)


# ---------------------------------------------------------------------------
# send
# ---------------------------------------------------------------------------

def check_send_basic() -> None:
    lzc.send(snapname="pool/fs@snap", fd=1)


def check_send_incremental() -> None:
    lzc.send(snapname="pool/fs@snap2", fd=1, fromsnap="pool/fs@snap1")


def check_send_with_flag() -> None:
    lzc.send(snapname="pool/fs@snap", fd=1, flags=lzc.SendFlags.COMPRESS)


def check_send_with_combined_flags() -> None:
    lzc.send(
        snapname="pool/fs@snap",
        fd=1,
        flags=lzc.SendFlags.COMPRESS | lzc.SendFlags.EMBED_DATA,
    )


def check_send_resume() -> None:
    lzc.send(snapname="pool/fs@snap", fd=1, resume_token="1-abc123")


def check_send_space_returns_int() -> None:
    size: int = lzc.send_space(snapname="pool/fs@snap")
    _ = size


def check_send_space_incremental() -> None:
    size: int = lzc.send_space(snapname="pool/fs@snap2", fromsnap="pool/fs@snap1")
    _ = size


def check_send_progress_returns_tuple() -> None:
    progress: tuple[int, int] = lzc.send_progress(snapshot_name="pool/fs@snap", fd=1)
    _ = progress


# ---------------------------------------------------------------------------
# receive
# ---------------------------------------------------------------------------

def check_receive_basic() -> None:
    lzc.receive(snapname="pool/fs@snap", fd=0)


def check_receive_resumable() -> None:
    lzc.receive(snapname="pool/fs@snap", fd=0, resumable=True)


def check_receive_full_args() -> None:
    lzc.receive(
        snapname="pool/fs@snap",
        fd=0,
        origin="pool/other@base",
        props={"compression": "lz4"},
        force=True,
        resumable=True,
        raw=False,
    )


# ---------------------------------------------------------------------------
# rollback
# ---------------------------------------------------------------------------

def check_rollback_returns_str() -> None:
    name: str = lzc.rollback(resource_name="pool/fs")
    _ = name


def check_rollback_to_snap() -> None:
    name: str = lzc.rollback(resource_name="pool/fs", snapshot_name="pool/fs@snap")
    _ = name


# ---------------------------------------------------------------------------
# ZFSCoreException fields
# ---------------------------------------------------------------------------

def check_exception_fields(exc: lzc.ZFSCoreException) -> None:
    code: int = exc.code
    msg: str = exc.msg
    name: str = exc.name
    errors: tuple[object, ...] | None = exc.errors
    _ = (code, msg, name, errors)
