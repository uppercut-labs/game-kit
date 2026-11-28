"""A fixed-width C style check: integer spellings, pointer stars on the type and bounded byte-range loops."""

from ._check import CheckResult, Finding, check_paths, check_repo, is_excluded, list_sources, strip_literals
from ._config import CStyleError, StyleConfig, config_from_mapping, find_config, load_config
from .cli import main

__all__ = [
    "CStyleError",
    "CheckResult",
    "Finding",
    "StyleConfig",
    "check_paths",
    "check_repo",
    "config_from_mapping",
    "find_config",
    "is_excluded",
    "list_sources",
    "load_config",
    "main",
    "strip_literals",
]
__version__ = "1.0.0"
