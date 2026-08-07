#!/usr/bin/env python3
"""Convenience wrapper for the rNES ROM test framework.

Equivalent to ``python -m rnestest`` but runnable directly:

    ./run_tests.py --rom-list roms-ci.txt --baseline baseline.json
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rnestest.__main__ import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
