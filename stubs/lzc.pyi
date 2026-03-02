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
