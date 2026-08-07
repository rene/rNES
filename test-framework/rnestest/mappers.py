"""Discover which mappers rNES implements.

The authoritative list lives in ``src/mappers/mappers.c`` as the ``mappers[]``
initializer (a non-zero entry means "implemented"). We parse it so the test
framework automatically tracks new mappers as they are added, and fall back to
the known set if the file cannot be read.
"""

from __future__ import annotations

import os
import re
from typing import Optional

# Used if src/mappers/mappers.c cannot be located/parsed.
FALLBACK_MAPPERS = frozenset({0, 1, 2, 3, 4, 7})


def implemented_mappers(src_dir: Optional[str] = None) -> frozenset[int]:
    """Return the set of mapper numbers rNES implements.

    ``src_dir`` should point at the emulator ``src/`` directory; if omitted we
    look for it relative to this file (``../../src``).
    """
    path = _mappers_c_path(src_dir)
    if path and os.path.isfile(path):
        parsed = _parse_mappers_c(path)
        if parsed:
            return frozenset(parsed)
    return FALLBACK_MAPPERS


def _mappers_c_path(src_dir: Optional[str]) -> Optional[str]:
    if src_dir:
        return os.path.join(src_dir, "mappers", "mappers.c")
    here = os.path.dirname(os.path.abspath(__file__))
    guess = os.path.normpath(os.path.join(here, "..", "..", "src"))
    return os.path.join(guess, "mappers", "mappers.c")


def _parse_mappers_c(path: str) -> set[int]:
    """Extract implemented indices from the ``mappers[...] = { ... }`` array."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return set()

    # Grab the initializer body between the first '{' after "mappers[" and its
    # matching '}'.
    m = re.search(r"mappers\s*\[[^\]]*\]\s*=\s*\{", text)
    if not m:
        return set()
    body = text[m.end():]
    close = body.find("}")
    if close < 0:
        return set()
    body = body[:close]

    # Strip block comments (they hold the index numbers, e.g. /* 0 */).
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    body = re.sub(r"//.*", "", body)

    result: set[int] = set()
    for idx, entry in enumerate(body.split(",")):
        token = entry.strip()
        if not token:
            continue
        # "0" / "NULL" -> not implemented; "&m0_NROM" etc. -> implemented.
        if token in ("0", "NULL"):
            continue
        result.add(idx)
    return result
