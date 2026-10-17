"""Ensure integration-only targets can write idedata.json after a clean."""

from pathlib import Path

Import("env")

# PlatformIO 6.2's IDE metadata target writes here before SCons creates it.
Path(env.subst("$BUILD_DIR")).mkdir(parents=True, exist_ok=True)
