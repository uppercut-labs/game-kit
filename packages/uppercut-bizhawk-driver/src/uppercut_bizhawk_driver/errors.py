"""The errors this package raises."""

from __future__ import annotations


class BizhawkError(Exception):
    """Base class: everything this package raises on purpose."""


class InstallError(BizhawkError):
    """A download whose SHA-256 is not the pinned one, an archive that holds an unsafe path, or a folder that is in the way."""


class ConfigError(BizhawkError):
    """A config.ini that is not BizHawk's JSON, a path in it that is not there, or a config that cannot be edited now."""


class RunError(BizhawkError, RuntimeError):
    """A run that ended badly: the script wrote `.err`, the program exited before `.done`, or old output was in the way."""

    def __init__(self, message: str, *, returncode: int | None = None, error_text: str | None = None) -> None:
        super().__init__(message)
        self.returncode = returncode
        self.error_text = error_text


class RunTimeout(RunError, TimeoutError):
    """The script wrote neither `.done` nor `.err` within the time. The launched process has been stopped."""
