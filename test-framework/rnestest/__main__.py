"""Command-line entry point: ``python -m rnestest`` (see run_tests.py).

Reads a ROM list, skips ROMs whose mapper rNES does not implement, runs the
rest through the headless harness, judges each PASS/FAIL/SKIP, compares to a
baseline, and reports. Exit status is non-zero on regressions.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import os
import sys

from . import __version__
from .analyze import FAIL, Thresholds, Verdict, analyze
from .baseline import compare, load_baseline, save_baseline
from .mappers import implemented_mappers
from .report import (TestRecord, github_annotations, print_summary,
                     write_json, write_junit)
from .romheader import parse_rom_header
from .runner import RunConfig, default_key, make_skip_result, run_rom


def _default_harness() -> str:
    pkg_parent = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(pkg_parent, "rnes_headless")


def parse_args(argv=None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        prog="rnestest",
        description="Automated rNES ROM smoke-test framework.",
    )
    p.add_argument("--rom-list", required=True,
                   help="text file with one ROM path per line "
                        "(# comments and blank lines ignored)")
    p.add_argument("--roms", default=None,
                   help="base directory for relative ROM paths "
                        "(default: the rom-list's directory)")
    p.add_argument("--harness", default=_default_harness(),
                   help="path to the rnes_headless binary")
    p.add_argument("--src-dir", default=None,
                   help="path to the emulator src/ (to discover implemented "
                        "mappers); auto-detected by default")

    p.add_argument("--frames", type=int, default=300,
                   help="frames to emulate per ROM (~60/s NTSC; default 300)")
    p.add_argument("--warmup", type=int, default=60,
                   help="frames to ignore at the start (default 60)")
    p.add_argument("--interval", type=int, default=10,
                   help="sample the screen/CPU every N frames (default 10)")
    p.add_argument("--timeout", type=float, default=30.0,
                   help="per-ROM wall-clock timeout in seconds (default 30)")
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 1,
                   help="parallel ROMs to run (default: CPU count)")
    p.add_argument("--no-input", action="store_true",
                   help="do NOT inject controller input (by default the "
                        "harness mashes START/A to get past title/menu "
                        "screens that wait for a key press)")
    p.add_argument("--buttons", default=None,
                   help="comma list of buttons to mash "
                        "(start,a,b,select,up,down,left,right); "
                        "default: start,a")
    p.add_argument("--ram-fill", type=lambda v: int(v, 0), default=None,
                   help="byte the 2 KiB of CPU RAM powers on with (default "
                        "0x00). The emulator itself leaves it to malloc(); "
                        "the harness pins it so verdicts are reproducible. "
                        "Re-run a title with a different value to see whether "
                        "it depends on power-on RAM")

    # Detection thresholds
    p.add_argument("--blank-colors", type=int, default=1,
                   help="<= this many distinct colours counts as a flat screen")
    p.add_argument("--min-content-frac", type=float, default=0.001,
                   help="share of non-background pixels a screen needs to "
                        "count as drawn (default 0.001 = 0.1%%)")
    p.add_argument("--loop-max-pcs", type=int, default=8)
    p.add_argument("--detect-freeze", action="store_true",
                   help="also fail ROMs whose (colourful) screen never changes. "
                        "Off by default because many working games have long "
                        "static title screens; when on, the harness runs such "
                        "ROMs longer (--freeze-frames) to confirm before failing")
    p.add_argument("--freeze-frames", type=int, default=1800,
                   help="with --detect-freeze, keep running a static screen up "
                        "to this many frames to confirm it is frozen "
                        "(default 1800 ~= 30s)")

    # Baseline / gating
    p.add_argument("--blank-frames", type=int, default=900,
                   help="a ROM whose screen is still blank at the end of the "
                        "run keeps going up to this many frames to confirm "
                        "nothing is ever drawn -- some intros hold a black "
                        "screen for longer than the base window "
                        "(default 900 ~= 15s; 0 = off)")
    p.add_argument("--baseline", default=None,
                   help="baseline JSON for regression gating")
    p.add_argument("--update-baseline", action="store_true",
                   help="write current results as the new baseline and exit 0")
    p.add_argument("--fail-on-new", action="store_true",
                   help="also fail if a ROM not in the baseline FAILs")

    # Output
    p.add_argument("--json", default=None, help="write full results as JSON")
    p.add_argument("--junit", default=None, help="write JUnit XML")
    p.add_argument("--github", action="store_true",
                   help="emit GitHub Actions annotations + step summary")
    p.add_argument("--no-color", action="store_true")
    p.add_argument("--quiet", action="store_true",
                   help="suppress the per-ROM progress line")
    p.add_argument("--version", action="version",
                   version=f"rnestest {__version__}")
    return p.parse_args(argv)


def read_rom_list(path: str, roms_dir: str) -> list:
    """Return a list of (resolved_path, key) from the list file."""
    entries = []
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            resolved = line if os.path.isabs(line) else os.path.join(roms_dir, line)
            entries.append((os.path.normpath(resolved), default_key(line)))
    return entries


def main(argv=None) -> int:
    args = parse_args(argv)

    if not os.path.isfile(args.rom_list):
        print(f"rom list not found: {args.rom_list}", file=sys.stderr)
        return 2
    roms_dir = args.roms or os.path.dirname(os.path.abspath(args.rom_list))
    entries = read_rom_list(args.rom_list, roms_dir)

    if not entries:
        print(f"No ROMs listed in {args.rom_list}; nothing to test.")
        return 0

    impl = implemented_mappers(args.src_dir)
    th = Thresholds(
        blank_colors=args.blank_colors,
        min_content_frac=args.min_content_frac,
        loop_max_pcs=args.loop_max_pcs,
        detect_freeze=args.detect_freeze,
    )
    cfg = RunConfig(
        harness=args.harness,
        frames=args.frames,
        warmup=args.warmup,
        interval=args.interval,
        timeout=args.timeout,
        inject_input=not args.no_input,
        buttons=args.buttons,
        # Only ask the harness to run long when we'll actually judge freezes.
        freeze_frames=(args.freeze_frames if args.detect_freeze else None),
        blank_frames=args.blank_frames,
        ram_fill=args.ram_fill,
    )

    if not os.path.isfile(args.harness):
        print(f"harness binary not found: {args.harness}\n"
              f"Build it first: make -C {os.path.dirname(args.harness) or '.'}",
              file=sys.stderr)
        return 2

    # Decide per-ROM action: skip immediately, or run the harness.
    to_run = []       # (idx, path, key, info)
    prebuilt = {}     # idx -> RunResult (for pre-skips)
    infos = {}        # idx -> RomInfo
    for idx, (path, key) in enumerate(entries):
        if not os.path.exists(path):
            prebuilt[idx] = make_skip_result(path, key, "file not found")
            infos[idx] = None
            continue
        info = parse_rom_header(path)
        infos[idx] = info
        if not info.valid:
            prebuilt[idx] = make_skip_result(
                path, key, f"invalid ROM ({info.error})")
        elif info.mapper not in impl:
            prebuilt[idx] = make_skip_result(
                path, key, f"unsupported mapper {info.mapper}", info.mapper)
        else:
            to_run.append((idx, path, key, info))

    # Run the supported ROMs (parallel).
    results = dict(prebuilt)
    total = len(entries)
    done = 0
    progress = (not args.quiet) and sys.stderr.isatty()

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        futs = {
            ex.submit(run_rom, path, key, cfg): idx
            for (idx, path, key, info) in to_run
        }
        # account for pre-skipped ones in the progress counter
        done = len(prebuilt)
        for fut in concurrent.futures.as_completed(futs):
            idx = futs[fut]
            results[idx] = fut.result()
            done += 1
            if progress:
                v = analyze(results[idx], th)
                key = entries[idx][1]
                print(f"[{done}/{total}] {v.verdict:4} {key}", file=sys.stderr)

    # Build records.
    records = []
    for idx, (path, key) in enumerate(entries):
        res = results[idx]
        v = analyze(res, th)
        info = infos.get(idx)
        mapper = res.mapper if res.mapper is not None else (
            info.mapper if info else None)
        fmt = info.fmt if info and info.valid else None
        records.append(TestRecord(
            key=key, rom=path, verdict=v.verdict, reason=v.reason,
            category=v.category, mapper=mapper, fmt=fmt,
            report=res.report,
        ))

    # (key, Verdict) pairs reused for baseline compare / save.
    kv = [(r.key, Verdict(r.verdict, r.reason, r.category)) for r in records]

    # Baseline: update or compare.
    now = datetime.datetime.now().isoformat(timespec="seconds")
    if args.update_baseline:
        target = args.baseline or os.path.join(
            os.path.dirname(os.path.abspath(args.rom_list)), "baseline.json")
        save_baseline(
            target, kv,
            note=f"generated by rnestest from {os.path.basename(args.rom_list)}",
            generated=now,
        )
        print_summary(records, None, use_color=not args.no_color)
        print(f"Baseline written to {target} ({len(records)} ROMs).")
        return 0

    baseline = load_baseline(args.baseline)
    cmp = compare(kv, baseline) if args.baseline else None

    # Reports
    print_summary(records, cmp, use_color=not args.no_color)
    meta = {
        "generated": now,
        "rom_list": args.rom_list,
        "frames": args.frames,
        "harness": args.harness,
        "implemented_mappers": sorted(impl),
    }
    if args.json:
        write_json(args.json, records, cmp, meta)
    if args.junit:
        write_junit(args.junit, records)
    if args.github:
        github_annotations(records, cmp)

    # Exit status
    return _exit_status(records, cmp, args)


def _exit_status(records, cmp, args) -> int:
    if cmp is not None:
        if cmp.regressions:
            return 1
        if args.fail_on_new and any(v.verdict == FAIL for _, v in cmp.new):
            return 1
        return 0
    # No baseline: pass unless something failed (absolute mode) only if asked.
    if args.fail_on_new and any(r.verdict == FAIL for r in records):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
