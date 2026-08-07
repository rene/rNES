"""Unit tests for the PASS/FAIL/SKIP verdict engine (analyze.py)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from rnestest.analyze import FAIL, PASS, SKIP, Thresholds, analyze  # noqa: E402
from rnestest.runner import RunResult, make_skip_result  # noqa: E402


def sample(frame, hash_="a", colors=20, dom=0.4, pcs=200, pc="c000",
           render=True, nmi=True):
    return {
        "frame": frame, "frame_hash": hash_, "distinct_colors": colors,
        "dominant_color": "112233", "dominant_frac": dom, "distinct_pcs": pcs,
        "instructions": 10000, "pc": pc, "render_enabled": render,
        "nmi_enabled": nmi,
    }


def ok_result(samples, jammed=False, mapper=0):
    return RunResult(
        rom="x.nes", key="x.nes", ran=True, returncode=0,
        report={
            "status": "ok", "mapper": mapper, "cpu_jammed": jammed,
            "rendering_ever_enabled": True, "samples": samples,
        },
        mapper=mapper,
    )


class TestAnalyze(unittest.TestCase):
    def test_pass_varying_screen(self):
        # distinct hashes across the window, plenty of colors and PCs
        s = [sample(f, hash_=f"h{f}") for f in (70, 80, 90, 100, 110)]
        v = analyze(ok_result(s))
        self.assertEqual(v.verdict, PASS)

    def test_pass_static_colourful_screen_by_default(self):
        # A colourful static screen (e.g. a title screen held for the window)
        # must PASS by default -- freeze detection is opt-in.
        s = [sample(f, hash_="same", pcs=200) for f in (70, 80, 90, 100)]
        v = analyze(ok_result(s))
        self.assertEqual(v.verdict, PASS)

    def test_fail_frozen_screen_when_enabled(self):
        # busy CPU but identical framebuffer hash every sample
        s = [sample(f, hash_="same", pcs=200) for f in (70, 80, 90, 100)]
        v = analyze(ok_result(s), Thresholds(detect_freeze=True))
        self.assertEqual(v.verdict, FAIL)
        self.assertEqual(v.category, "frozen-screen")

    def test_fail_blank_by_colors(self):
        s = [sample(f, hash_=f"h{f}", colors=1, dom=1.0) for f in (70, 80, 90)]
        v = analyze(ok_result(s))
        self.assertEqual(v.category, "blank-screen")

    def test_fail_blank_by_dominant(self):
        s = [sample(f, hash_=f"h{f}", colors=5, dom=0.9995) for f in (70, 80, 90)]
        v = analyze(ok_result(s))
        self.assertEqual(v.category, "blank-screen")

    def test_pass_two_colour_text_screen(self):
        # Regression: Wolverine (USA) draws white text on a black background --
        # exactly two colours, but ~5.7% of the screen is drawn. Counting
        # colours alone used to call this blank.
        s = [sample(f, hash_="same", colors=2, dom=0.94339)
             for f in (70, 80, 90, 100)]
        v = analyze(ok_result(s))
        self.assertEqual(v.verdict, PASS)

    def test_fail_blank_two_colour_with_negligible_content(self):
        # Two colours but only 0.05% drawn -> a stray sliver, still blank.
        s = [sample(f, hash_=f"h{f}", colors=2, dom=0.9995)
             for f in (70, 80, 90)]
        v = analyze(ok_result(s))
        self.assertEqual(v.category, "blank-screen")

    def test_blank_reason_reports_drawn_percentage(self):
        s = [sample(f, hash_=f"h{f}", colors=1, dom=1.0) for f in (70, 80, 90)]
        v = analyze(ok_result(s))
        self.assertIn("0.00% drawn", v.reason)

    def test_pass_when_any_sample_has_content(self):
        # A screen that is blank for most of the window but draws once is not
        # a blank-screen failure (all-samples semantics preserved).
        s = [sample(70, hash_="a", colors=1, dom=1.0),
             sample(80, hash_="b", colors=2, dom=0.90),
             sample(90, hash_="c", colors=1, dom=1.0)]
        v = analyze(ok_result(s))
        self.assertEqual(v.verdict, PASS)

    def test_min_content_frac_is_configurable(self):
        # Raising the bar reclassifies a sparse screen as blank.
        s = [sample(f, hash_=f"h{f}", colors=2, dom=0.98) for f in (70, 80, 90)]
        self.assertEqual(analyze(ok_result(s)).verdict, PASS)
        strict = Thresholds(min_content_frac=0.05)
        self.assertEqual(analyze(ok_result(s), strict).category, "blank-screen")

    def test_fail_tight_loop(self):
        s = [sample(f, hash_=f"h{f}", pcs=1, pc="0000") for f in (70, 80, 90)]
        v = analyze(ok_result(s))
        self.assertEqual(v.category, "cpu-loop")

    def test_fail_cpu_jam(self):
        s = [sample(f, hash_=f"h{f}") for f in (70, 80, 90)]
        v = analyze(ok_result(s, jammed=True))
        self.assertEqual(v.category, "cpu-jam")

    def test_fail_crash_signal(self):
        r = RunResult(rom="x", key="x", ran=True, returncode=-11)
        v = analyze(r)
        self.assertEqual(v.category, "crash")

    def test_fail_timeout(self):
        r = RunResult(rom="x", key="x", ran=True, timed_out=True)
        v = analyze(r)
        self.assertEqual(v.category, "timeout")

    def test_fail_emulator_hang(self):
        r = RunResult(rom="x", key="x", ran=True, returncode=0,
                      report={"status": "hang", "samples": []})
        v = analyze(r)
        self.assertEqual(v.category, "emu-hang")

    def test_skip_unsupported_mapper(self):
        r = RunResult(rom="x", key="x", ran=True, returncode=3,
                      report={"status": "skipped", "mapper": 5})
        v = analyze(r)
        self.assertEqual(v.verdict, SKIP)
        self.assertEqual(v.category, "unsupported-mapper")

    def test_skip_preskip(self):
        r = make_skip_result("x", "x", "file not found")
        v = analyze(r)
        self.assertEqual(v.verdict, SKIP)

    def test_partial_animation_passes(self):
        # only one sample differs -> not frozen -> PASS
        s = [sample(70, hash_="a"), sample(80, hash_="a"),
             sample(90, hash_="b"), sample(100, hash_="a")]
        v = analyze(ok_result(s))
        self.assertEqual(v.verdict, PASS)


if __name__ == "__main__":
    unittest.main()
