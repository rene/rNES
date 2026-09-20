#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# rNES test framework - test ROM fetcher
# Copyright 2021-2026 Renê de Souza Pinto
"""Download the NESdev test ROMs the CI suite runs against.

The ROMs are *not* part of this repository. blargg's NES test suites and
kevtris's nestest are freely available and universally used to validate
emulators, but neither ships a licence, so rNES -- which is BSD-3-Clause --
links to them rather than redistributing them. This script fetches them from
GitHub at a pinned commit, into a directory that is git-ignored.

Every ROM is verified against the SHA-256 recorded in testroms.json, so a run
either reproduces the exact bytes the baseline was built from or fails; an
upstream force-push or a truncated download cannot quietly change a verdict.

    ./fetch-testroms.py               # fetch into roms/external/
    ./fetch-testroms.py --check       # verify what is there, download nothing
    ./fetch-testroms.py --list        # print the ROM list and exit

Nothing outside the standard library is needed, so CI installs no extra
packages to run it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MANIFEST = os.path.join(HERE, "testroms.json")
DEFAULT_DEST = os.path.join(HERE, "roms", "external")
TIMEOUT = 60
RETRIES = 3


def load_manifest(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def rom_url(manifest: dict, rom: dict) -> str:
    src = manifest["sources"][rom["source"]]
    return src["raw"].format(
        repo=src["repo"],
        commit=src["commit"],
        path=urllib.parse.quote(rom["path"]),
    )


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url: str) -> bytes:
    """GET `url`, retrying a few times: CI networks drop connections."""
    last = None
    for attempt in range(1, RETRIES + 1):
        try:
            with urllib.request.urlopen(url, timeout=TIMEOUT) as resp:
                return resp.read()
        except (urllib.error.URLError, OSError) as exc:  # noqa: PERF203
            last = exc
            if attempt < RETRIES:
                print(f"    retry {attempt}/{RETRIES - 1}: {exc}",
                      file=sys.stderr)
    raise RuntimeError(f"download failed: {url} ({last})")


def fetch_one(manifest: dict, rom: dict, dest_dir: str, check_only: bool,
              force: bool) -> str:
    """Return one of 'ok' / 'fetched' / 'missing' / 'bad'."""
    target = os.path.join(dest_dir, rom["dest"])
    want = rom["sha256"]

    if os.path.exists(target) and not force:
        if sha256_file(target) == want:
            return "ok"
        if check_only:
            return "bad"
        # A wrong file is replaced rather than trusted: it is either a
        # partial download or a ROM from a different upstream revision.
        print(f"  ! {rom['dest']}: checksum mismatch, re-downloading")
    elif check_only:
        return "missing"

    data = download(rom_url(manifest, rom))
    got = hashlib.sha256(data).hexdigest()
    if got != want:
        raise RuntimeError(
            f"{rom['dest']}: SHA-256 mismatch\n"
            f"  expected {want}\n  got      {got}\n"
            f"  from     {rom_url(manifest, rom)}\n"
            "The upstream file changed. Verify the new bytes by hand before "
            "updating testroms.json, then regenerate the baseline."
        )
    os.makedirs(os.path.dirname(target), exist_ok=True)
    # Write-then-rename: an interrupted run leaves no half file behind for
    # the next one to mistake for a complete download.
    tmp = target + ".part"
    with open(tmp, "wb") as fh:
        fh.write(data)
    os.replace(tmp, target)
    return "fetched"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Fetch the NESdev test ROMs used by the rNES CI suite.")
    ap.add_argument("--manifest", default=DEFAULT_MANIFEST,
                    help="ROM manifest (default: testroms.json)")
    ap.add_argument("--dest", default=DEFAULT_DEST,
                    help="where to put them (default: roms/external/)")
    ap.add_argument("--check", action="store_true",
                    help="verify the ROMs already present and exit non-zero "
                         "if any is missing or altered; downloads nothing")
    ap.add_argument("--force", action="store_true",
                    help="re-download even ROMs that are already correct")
    ap.add_argument("--list", action="store_true",
                    help="print the manifest's ROMs and exit")
    args = ap.parse_args(argv)

    try:
        manifest = load_manifest(args.manifest)
    except OSError as exc:
        print(f"cannot read manifest: {exc}", file=sys.stderr)
        return 2

    roms = manifest.get("roms") or []
    if args.list:
        for rom in roms:
            print(f"{rom['dest']}\t{rom['author']}\t{rom['path']}")
        return 0

    for name, src in manifest.get("sources", {}).items():
        print(f"{name}: {src['url']} @ {src['commit'][:12]}")
    print(f"{len(roms)} ROMs -> {args.dest}")

    tally = {"ok": 0, "fetched": 0, "missing": 0, "bad": 0}
    try:
        for rom in roms:
            state = fetch_one(manifest, rom, args.dest, args.check, args.force)
            tally[state] += 1
            if state == "fetched":
                print(f"  + {rom['dest']}")
            elif state == "missing":
                print(f"  - {rom['dest']}: not downloaded")
            elif state == "bad":
                print(f"  ! {rom['dest']}: checksum mismatch")
    except RuntimeError as exc:
        print(f"\nerror: {exc}", file=sys.stderr)
        return 1

    print(f"verified {tally['ok']}, downloaded {tally['fetched']}"
          + (f", missing {tally['missing']}" if tally["missing"] else "")
          + (f", corrupt {tally['bad']}" if tally["bad"] else ""))
    return 1 if (tally["missing"] or tally["bad"]) else 0


if __name__ == "__main__":
    sys.exit(main())
