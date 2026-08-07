"""Turn a harness :class:`RunResult` into a PASS / FAIL / SKIP verdict.

Heuristics, in priority order (first match wins). All post-warmup *samples*
are considered; a signal only fails the ROM if it holds for the whole window,
which keeps false positives low (a screen that animates even once, or a CPU
that ever escapes a tight loop, passes).

Definitive failures (very low false-positive rate):
  - crashed (killed by a signal)
  - hang / timeout (emulator never produced frames, or wall-clock timeout)
  - CPU jammed (executed a JAM/KIL opcode)
  - blank / solid screen for the entire window (black / gray / white)
  - CPU stuck in a tight loop for the entire window (infinite / interrupt loop)

"Blank" is measured by *content coverage* -- the share of pixels that differ
from the dominant (background) colour -- rather than by the number of distinct
colours on screen. A two-colour screen is not evidence of failure: monochrome
text on a black background (title cards, story text, "PRESS START", copyright
screens) is exactly two colours yet perfectly healthy. What actually separates
a dead screen from a sparse one is whether *anything was drawn at all*.

Softer failure (can be disabled with detect_freeze=False):
  - frozen screen: the framebuffer never changed across the window, even though
    the CPU keeps running. This catches "busy-wait" hangs, but a game sitting
    on a genuinely static title screen with no animation also trips it -- the
    baseline absorbs those, so CI still only reacts to *changes*.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from .runner import RunResult

PASS = "PASS"
FAIL = "FAIL"
SKIP = "SKIP"


@dataclass
class Thresholds:
    blank_colors: int = 1          # <= this many distinct colors == flat screen
    min_content_frac: float = 0.001  # need this share of non-background pixels
    loop_max_pcs: int = 8          # <= this many distinct PCs/frame == tight loop
    min_samples: int = 3           # need this many samples to judge freeze
    detect_freeze: bool = False    # opt-in: a colourful static screen is an
                                   # unreliable signal (many working games have
                                   # long static title screens)


@dataclass
class Verdict:
    verdict: str          # PASS | FAIL | SKIP
    reason: str
    category: str         # short machine-friendly tag


def analyze(res: RunResult, th: Optional[Thresholds] = None) -> Verdict:
    th = th or Thresholds()

    # --- Framework / pre-launch outcomes ---------------------------------
    if res.error:
        return Verdict(SKIP, res.error, "framework-error")
    if res.pre_skip_reason:
        return Verdict(SKIP, res.pre_skip_reason, "pre-skip")

    # --- Process-level failures -----------------------------------------
    if res.timed_out:
        return Verdict(FAIL, "hang: wall-clock timeout", "timeout")
    if res.returncode is not None and res.returncode < 0:
        sig = -res.returncode
        return Verdict(FAIL, f"crashed (signal {sig})", "crash")

    report = res.report
    if report is None:
        rc = res.returncode
        return Verdict(FAIL, f"no report from harness (rc={rc})", "no-report")

    status = report.get("status")
    if status == "skipped":
        m = report.get("mapper")
        return Verdict(SKIP, f"unsupported mapper {m}", "unsupported-mapper")
    if status == "invalid":
        return Verdict(SKIP, "invalid / not a NES ROM", "invalid-rom")
    if status == "error":
        return Verdict(SKIP, f"load error: {report.get('reason')}", "load-error")
    if status == "hang":
        return Verdict(FAIL, "emulator hang (no video output)", "emu-hang")
    if status != "ok":
        return Verdict(FAIL, f"unexpected status '{status}'", "bad-status")

    # --- Emulation-level failures ---------------------------------------
    if report.get("cpu_jammed"):
        return Verdict(FAIL, "CPU jammed (JAM/KIL opcode)", "cpu-jam")

    samples = report.get("samples") or []
    if not samples:
        # Ran but produced no samples (frames < warmup). Not enough to judge.
        return Verdict(PASS, "ran; no samples collected", "insufficient-data")

    # Blank / solid screen for the whole window.
    def content_frac(s):
        """Share of pixels that differ from the dominant (background) colour."""
        return 1.0 - s.get("dominant_frac", 0.0)

    def is_blank(s):
        # A single flat colour is unambiguously blank.
        if s.get("distinct_colors", 99) <= th.blank_colors:
            return True
        # Otherwise judge by how much is actually *drawn*, not by how many
        # colours are used. Plenty of healthy screens are two-colour (white
        # text on black); counting colours alone marks all of those blank.
        return content_frac(s) < th.min_content_frac

    if all(is_blank(s) for s in samples):
        color = samples[-1].get("dominant_color", "??????")
        pct = max(content_frac(s) for s in samples) * 100.0
        return Verdict(
            FAIL,
            f"blank/solid screen (#{color}, {pct:.2f}% drawn)",
            "blank-screen",
        )

    # CPU stuck in a tight loop for the whole window.
    if all(s.get("distinct_pcs", 9999) <= th.loop_max_pcs for s in samples):
        pc = samples[-1].get("pc", "????")
        return Verdict(FAIL, f"CPU stuck in tight loop (pc=${pc})", "cpu-loop")

    # Frozen screen: no framebuffer change across the whole window.
    if th.detect_freeze and len(samples) >= th.min_samples:
        hashes = {s.get("frame_hash") for s in samples}
        if len(hashes) == 1:
            span = samples[-1]["frame"] - samples[0]["frame"]
            return Verdict(
                FAIL,
                f"frozen screen (no change for {span} frames)",
                "frozen-screen",
            )

    return Verdict(PASS, "running", "ok")
