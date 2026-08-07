"""Baseline load / compare / save for regression gating.

A baseline maps a ROM key (its basename) to its expected verdict:

    {
      "meta": {"generated": "...", "note": "..."},
      "roms": {
        "Some Game (USA).nes": {"verdict": "PASS", "reason": "running"},
        "Broken Game (USA).nes": {"verdict": "FAIL", "reason": "frozen screen"}
      }
    }

CI fails only on *regressions* (a ROM that was PASS becoming FAIL).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class Comparison:
    regressions: list = field(default_factory=list)   # PASS -> FAIL
    fixes: list = field(default_factory=list)          # FAIL -> PASS
    new: list = field(default_factory=list)            # not in baseline
    changed: list = field(default_factory=list)        # other verdict changes
    unchanged: list = field(default_factory=list)


def load_baseline(path: Optional[str]) -> dict:
    """Return the ``key -> {verdict, reason}`` mapping (empty if no baseline)."""
    if not path:
        return {}
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except FileNotFoundError:
        return {}
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"could not read baseline {path}: {exc}") from exc
    # Accept either the wrapped form or a bare mapping.
    if isinstance(data, dict) and "roms" in data:
        return data.get("roms") or {}
    return data if isinstance(data, dict) else {}


def compare(results: list, baseline: dict) -> Comparison:
    """Compare current results (list of (key, Verdict)) against a baseline."""
    cmp = Comparison()
    for key, verdict in results:
        base = baseline.get(key)
        if base is None:
            cmp.new.append((key, verdict))
            continue
        old = base.get("verdict")
        new = verdict.verdict
        if old == new:
            cmp.unchanged.append((key, verdict))
        elif old == "PASS" and new == "FAIL":
            cmp.regressions.append((key, verdict, base))
        elif old == "FAIL" and new == "PASS":
            cmp.fixes.append((key, verdict, base))
        else:
            cmp.changed.append((key, verdict, base))
    return cmp


def save_baseline(path: str, results: list, note: str = "",
                  generated: str = "") -> None:
    """Write current results as a new baseline (list of (key, Verdict))."""
    roms = {
        key: {"verdict": v.verdict, "reason": v.reason}
        for key, v in sorted(results, key=lambda kv: kv[0].lower())
    }
    payload = {
        "meta": {"generated": generated, "note": note},
        "roms": roms,
    }
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
