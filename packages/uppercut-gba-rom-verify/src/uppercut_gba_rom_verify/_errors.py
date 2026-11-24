"""Shared error type and file reading for the validators."""

from __future__ import annotations

from os import PathLike
from pathlib import Path
from typing import Iterable, Union

PathInput = Union[str, "PathLike[str]"]


class ValidationError(ValueError):
    """Raised when an artifact does not satisfy the release contract.

    ``errors`` lists every failed check, in the order they were found.
    """

    def __init__(self, errors: Union[str, Iterable[str]]) -> None:
        if isinstance(errors, str):
            details = (errors,)
        else:
            details = tuple(errors)
        if not details:
            details = ("artifact validation failed",)
        self.errors = details
        super().__init__("; ".join(details))


def read_artifact(path: PathInput, label: str) -> bytes:
    """Read a file, turning an OS error into a ``ValidationError`` that names the artifact."""
    artifact_path = Path(path)
    try:
        return artifact_path.read_bytes()
    except OSError as error:
        detail = error.strerror or str(error)
        raise ValidationError(f"cannot read {label} {artifact_path}: {detail}") from error
