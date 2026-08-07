"""rNES automated ROM test framework.

Drives the SDL-free ``rnes_headless`` harness over a list of ROMs, decides
PASS / FAIL / SKIP for each, and gates CI on regressions against a baseline.

See the package README for usage.
"""

__version__ = "1.0"
