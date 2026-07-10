"""Exception hierarchy for boot environment operations.

Callers (middleware, the CLI) catch BEError and map the subclasses onto
their own error model.
"""

from __future__ import annotations


class BEError(Exception):
    """Base class for all boot environment operation failures."""

    def __init__(self, msg: str, errno: int | None = None) -> None:
        super().__init__(msg)
        self.errno = errno


class BENotFound(BEError):
    """The referenced boot environment dataset does not exist."""


class BEExists(BEError):
    """The clone target boot environment already exists."""


class BEBusy(BEError):
    """The target boot environment is in use.

    Raised when the target is the running or activated BE, or a snapshot
    in its subtree is held. May succeed on retry once the condition
    clears.
    """


class BEDestroyUnsafe(BEError):
    """Dependent clones make the destroy unsafe.

    Raised by the pre-flight checks before anything is destroyed, or
    from the destroy walk when ZFS reports a surviving dependency. In
    the second case the walk stopped at the offending snapshot and the
    destroy can be retried once the dependency is resolved.
    """


class BEGrubError(BEError):
    """update-grub failed after the ZFS state was already changed.

    Raised only on the CLI's bare update-grub path. The middleware path
    regenerates grub via etc.generate('grub') and propagates its own
    errors instead. Retryable once the operator fixes the grub
    failure.
    """
