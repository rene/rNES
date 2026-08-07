"""iNES / NES 2.0 ROM header parsing.

The mapper-number and format decoding here mirrors ``src/romdec.c`` exactly
(including rNES's non-standard NES 2.0 detection, ``(flag7 & 0x0C) == 0x0C``)
so that the mapper number computed in Python always agrees with what the
emulator's ``load_rom()`` computes. This lets us skip unsupported mappers
without even launching the harness.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

HEADER_SIZE = 16
MAGIC = b"NES\x1a"

FMT_INES = "iNES"
FMT_NES2 = "NES2.0"
FMT_UNKNOWN = "unknown"


@dataclass
class RomInfo:
    """Decoded ROM header information."""

    valid: bool
    fmt: str = FMT_UNKNOWN
    mapper: Optional[int] = None
    submapper: int = 0
    prg_size: int = 0  # bytes
    chr_size: int = 0  # bytes
    error: Optional[str] = None


def _fmt_and_mapper(hdr: bytes) -> tuple[str, int]:
    flag6, flag7, flag8 = hdr[6], hdr[7], hdr[8]
    # sflags: bytes 12..15 must all be zero for plain iNES (see romdec.c)
    sflags = hdr[12] + hdr[13] + hdr[14] + hdr[15]

    if (flag7 & 0x0C) == 0 and sflags == 0:
        fmt = FMT_INES
    elif (flag7 & 0x0C) == 0x0C:
        fmt = FMT_NES2
    else:
        fmt = FMT_UNKNOWN

    if fmt == FMT_INES:
        mapper = (flag7 & 0xF0) | ((flag6 >> 4) & 0x0F)
    else:
        # NES 2.0 (and, as in romdec.c, the "unknown" fallback) 12-bit mapper
        mapper = ((flag8 & 0x0F) << 8) | (flag7 & 0xF0) | ((flag6 >> 4) & 0x0F)
    return fmt, mapper


def parse_rom_header(path: str) -> RomInfo:
    """Parse the 16-byte header of a ``.nes`` file.

    Returns a :class:`RomInfo`; ``valid`` is ``False`` when the file is
    missing, too short, or lacks the ``NES\\x1a`` signature.
    """
    try:
        with open(path, "rb") as fh:
            hdr = fh.read(HEADER_SIZE)
    except FileNotFoundError:
        return RomInfo(valid=False, error="file not found")
    except OSError as exc:
        return RomInfo(valid=False, error=f"read error: {exc}")

    if len(hdr) < HEADER_SIZE:
        return RomInfo(valid=False, error="file too short")
    if hdr[0:4] != MAGIC:
        return RomInfo(valid=False, error="missing NES header signature")

    fmt, mapper = _fmt_and_mapper(hdr)
    submapper = (hdr[8] >> 4) & 0x0F if fmt == FMT_NES2 else 0

    # PRG/CHR sizes (bytes). Best-effort; only used for reporting.
    if fmt == FMT_NES2:
        prg_msb = hdr[9] & 0x0F
        chr_msb = (hdr[9] >> 4) & 0x0F
        prg = _nes2_size(hdr[4], prg_msb, unit=16 * 1024)
        chr = _nes2_size(hdr[5], chr_msb, unit=8 * 1024)
    else:
        prg = hdr[4] * 16 * 1024
        chr = hdr[5] * 8 * 1024

    return RomInfo(
        valid=True,
        fmt=fmt,
        mapper=mapper,
        submapper=submapper,
        prg_size=prg,
        chr_size=chr,
    )


def _nes2_size(lsb: int, msb: int, unit: int) -> int:
    """NES 2.0 PRG/CHR size (linear or exponent form), matching romdec.c."""
    if msb < 0x0F:
        return (((msb << 8) & 0xF00) | lsb) * unit
    exponent = (lsb & 0xFC) >> 2
    multiplier = (lsb & 0x03) * 2 + 1
    return (1 << exponent) * multiplier
