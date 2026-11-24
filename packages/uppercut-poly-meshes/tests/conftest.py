"""Puts this folder on the import path so the tests can share the cases in cases.py."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
