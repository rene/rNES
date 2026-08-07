"""Invoke the ``rnes_headless`` harness for a single ROM and capture results."""

from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class RunConfig:
    harness: str
    frames: int = 300
    warmup: int = 60
    interval: int = 10
    timeout: float = 30.0
    inject_input: bool = True   # mash buttons to get past title/menu screens
    buttons: Optional[str] = None  # e.g. "start,a,select"; None = harness default
    freeze_frames: Optional[int] = None  # >frames enables adaptive freeze confirm
    blank_frames: Optional[int] = 900  # >frames confirms a blank screen is dead


@dataclass
class RunResult:
    rom: str                        # resolved path used to launch the harness
    key: str                        # stable key (basename) for the baseline
    ran: bool = False               # was the harness actually launched?
    returncode: Optional[int] = None
    timed_out: bool = False
    report: Optional[dict] = None   # parsed JSON from the harness
    stdout: str = ""
    stderr: str = ""
    # Set when we skip before launching (bad file / unsupported mapper).
    pre_skip_reason: Optional[str] = None
    mapper: Optional[int] = None
    error: Optional[str] = None     # framework-level error (harness missing, ...)
    meta: dict = field(default_factory=dict)


def run_rom(rom_path: str, key: str, cfg: RunConfig) -> RunResult:
    """Run one ROM through the harness with a wall-clock timeout."""
    res = RunResult(rom=rom_path, key=key, ran=True)
    cmd = [
        cfg.harness,
        "--frames", str(cfg.frames),
        "--warmup", str(cfg.warmup),
        "--interval", str(cfg.interval),
        "--json",
    ]
    if not cfg.inject_input:
        cmd.append("--no-input")
    elif cfg.buttons:
        cmd += ["--buttons", cfg.buttons]
    if cfg.freeze_frames:
        cmd += ["--freeze-frames", str(cfg.freeze_frames)]
    if cfg.blank_frames is not None:
        cmd += ["--blank-frames", str(cfg.blank_frames)]
    cmd.append(rom_path)
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=cfg.timeout,
        )
    except subprocess.TimeoutExpired as exc:
        res.timed_out = True
        res.stdout = _as_text(exc.stdout)
        res.stderr = _as_text(exc.stderr)
        return res
    except FileNotFoundError:
        res.ran = False
        res.error = f"harness not found: {cfg.harness}"
        return res

    res.returncode = proc.returncode
    res.stdout = proc.stdout or ""
    res.stderr = proc.stderr or ""
    res.report = _parse_report(res.stdout)
    if res.report is not None and "mapper" in res.report:
        res.mapper = res.report.get("mapper")
    return res


def make_skip_result(rom_path: str, key: str, reason: str,
                     mapper: Optional[int] = None) -> RunResult:
    """Build a RunResult for a ROM skipped before launching the harness."""
    return RunResult(
        rom=rom_path,
        key=key,
        ran=False,
        pre_skip_reason=reason,
        mapper=mapper,
    )


def _parse_report(stdout: str) -> Optional[dict]:
    """The harness prints exactly one JSON object; parse the last JSON line."""
    for line in reversed(stdout.strip().splitlines()):
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                return None
    return None


def _as_text(data) -> str:
    if data is None:
        return ""
    if isinstance(data, bytes):
        return data.decode("utf-8", errors="replace")
    return str(data)


def default_key(rom_path: str) -> str:
    """Baseline key: the ROM's basename (stable across machines/paths)."""
    return os.path.basename(rom_path)
