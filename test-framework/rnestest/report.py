"""Reporting: console summary, JSON, JUnit XML, and GitHub annotations."""

from __future__ import annotations

import html
import json
import os
import sys
from dataclasses import dataclass
from typing import Optional

from .analyze import FAIL, PASS, SKIP, Verdict
from .baseline import Comparison


@dataclass
class TestRecord:
    key: str
    rom: str
    verdict: str
    reason: str
    category: str
    mapper: Optional[int] = None
    fmt: Optional[str] = None
    report: Optional[dict] = None


# --- console ------------------------------------------------------------

_COLORS = {
    PASS: "\033[32m",   # green
    FAIL: "\033[31m",   # red
    SKIP: "\033[33m",   # yellow
    "reset": "\033[0m",
    "bold": "\033[1m",
    "cyan": "\033[36m",
}


def _c(text: str, key: str, use_color: bool) -> str:
    if not use_color:
        return text
    return f"{_COLORS.get(key, '')}{text}{_COLORS['reset']}"


def print_summary(records: list, cmp: Optional[Comparison],
                  use_color: bool, stream=sys.stdout) -> None:
    def w(line=""):
        print(line, file=stream)

    npass = sum(1 for r in records if r.verdict == PASS)
    nfail = sum(1 for r in records if r.verdict == FAIL)
    nskip = sum(1 for r in records if r.verdict == SKIP)

    w()
    w(_c("rNES ROM test results", "bold", use_color))
    w("=" * 60)
    for r in sorted(records, key=lambda x: x.key.lower()):
        tag = _c(f"{r.verdict:4}", r.verdict, use_color)
        mapper = f"m{r.mapper}" if r.mapper is not None else "m?"
        w(f"  [{tag}] {mapper:>4}  {r.key}")
        if r.verdict != PASS:
            w(f"          -> {r.reason}")
    w("-" * 60)
    w(f"  {_c('PASS', PASS, use_color)}: {npass}   "
      f"{_c('FAIL', FAIL, use_color)}: {nfail}   "
      f"{_c('SKIP', SKIP, use_color)}: {nskip}   "
      f"total: {len(records)}")

    if cmp is not None:
        w()
        if cmp.regressions:
            w(_c(f"REGRESSIONS ({len(cmp.regressions)}): "
                 "ROMs that used to PASS and now FAIL", FAIL, use_color))
            for key, v, base in cmp.regressions:
                w(f"    - {key}: {v.reason}")
        if cmp.fixes:
            w(_c(f"FIXED ({len(cmp.fixes)}): ROMs that now PASS", PASS,
                 use_color))
            for key, v, base in cmp.fixes:
                w(f"    + {key}")
        if cmp.new:
            new_fail = [(k, v) for k, v in cmp.new if v.verdict == FAIL]
            w(_c(f"NEW ({len(cmp.new)} not in baseline; "
                 f"{len(new_fail)} failing)", "cyan", use_color))
        if not (cmp.regressions or cmp.fixes or cmp.new):
            w(_c("No changes vs baseline.", PASS, use_color))
    w()


# --- JSON ---------------------------------------------------------------

def write_json(path: str, records: list, cmp: Optional[Comparison],
               meta: dict) -> None:
    payload = {
        "meta": meta,
        "summary": {
            "pass": sum(1 for r in records if r.verdict == PASS),
            "fail": sum(1 for r in records if r.verdict == FAIL),
            "skip": sum(1 for r in records if r.verdict == SKIP),
            "total": len(records),
        },
        "results": [
            {
                "key": r.key,
                "rom": r.rom,
                "verdict": r.verdict,
                "reason": r.reason,
                "category": r.category,
                "mapper": r.mapper,
                "format": r.fmt,
            }
            for r in sorted(records, key=lambda x: x.key.lower())
        ],
    }
    if cmp is not None:
        payload["regressions"] = [k for k, v, b in cmp.regressions]
        payload["fixes"] = [k for k, v, b in cmp.fixes]
        payload["new"] = [k for k, v in cmp.new]
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2, ensure_ascii=False)
        fh.write("\n")


# --- JUnit XML ----------------------------------------------------------

def write_junit(path: str, records: list) -> None:
    npass = sum(1 for r in records if r.verdict == PASS)
    nfail = sum(1 for r in records if r.verdict == FAIL)
    nskip = sum(1 for r in records if r.verdict == SKIP)

    lines = ['<?xml version="1.0" encoding="UTF-8"?>']
    lines.append(
        f'<testsuite name="rNES-roms" tests="{len(records)}" '
        f'failures="{nfail}" skipped="{nskip}">'
    )
    for r in sorted(records, key=lambda x: x.key.lower()):
        name = html.escape(r.key, quote=True)
        reason = html.escape(r.reason, quote=True)
        if r.verdict == FAIL:
            lines.append(f'  <testcase name="{name}" classname="rNES.roms">')
            lines.append(f'    <failure message="{reason}">{reason}</failure>')
            lines.append("  </testcase>")
        elif r.verdict == SKIP:
            lines.append(f'  <testcase name="{name}" classname="rNES.roms">')
            lines.append(f'    <skipped message="{reason}"/>')
            lines.append("  </testcase>")
        else:
            lines.append(f'  <testcase name="{name}" classname="rNES.roms"/>')
    lines.append("</testsuite>")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


# --- GitHub Actions -----------------------------------------------------

def github_annotations(records: list, cmp: Optional[Comparison]) -> None:
    """Emit ::error::/::warning:: workflow commands and a step summary."""
    regressed_keys = {k for k, v, b in cmp.regressions} if cmp else set()
    for r in records:
        if r.verdict != FAIL:
            continue
        msg = f"{r.key}: {r.reason}"
        level = "error" if r.key in regressed_keys else "warning"
        print(f"::{level} title=ROM test::{msg}")

    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path:
        return
    npass = sum(1 for r in records if r.verdict == PASS)
    nfail = sum(1 for r in records if r.verdict == FAIL)
    nskip = sum(1 for r in records if r.verdict == SKIP)
    try:
        with open(summary_path, "a", encoding="utf-8") as fh:
            fh.write("## rNES ROM test results\n\n")
            fh.write(f"**PASS:** {npass}  **FAIL:** {nfail}  "
                     f"**SKIP:** {nskip}  (total {len(records)})\n\n")
            if cmp and cmp.regressions:
                fh.write(f"### ❌ Regressions ({len(cmp.regressions)})\n\n")
                for key, v, base in cmp.regressions:
                    fh.write(f"- `{key}` — {v.reason}\n")
                fh.write("\n")
            if cmp and cmp.fixes:
                fh.write(f"### ✅ Fixed ({len(cmp.fixes)})\n\n")
                for key, v, base in cmp.fixes:
                    fh.write(f"- `{key}`\n")
                fh.write("\n")
            if nfail:
                fh.write("<details><summary>All failing ROMs</summary>\n\n")
                for r in sorted(records, key=lambda x: x.key.lower()):
                    if r.verdict == FAIL:
                        fh.write(f"- `{r.key}` — {r.reason}\n")
                fh.write("\n</details>\n")
    except OSError:
        pass
