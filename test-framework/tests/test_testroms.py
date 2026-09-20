"""Unit tests for the test-ROM path: protocol verdicts and per-ROM options."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from rnestest.__main__ import (parse_rom_options,  # noqa: E402
                               read_rom_list)
from rnestest.analyze import FAIL, PASS, REASON_MAX, analyze  # noqa: E402
from rnestest.runner import RunConfig, RunResult, run_rom  # noqa: E402


def testrom_result(**report):
    """A RunResult whose harness report carries a test-ROM verdict."""
    base = {"status": "ok", "mapper": 1, "cpu_jammed": False,
            "rendering_ever_enabled": True, "samples": []}
    base.update(report)
    return RunResult(rom="t.nes", key="t.nes", ran=True, returncode=0,
                     report=base, mapper=base["mapper"])


class TestProtocolVerdict(unittest.TestCase):
    def test_blargg_code_zero_passes(self):
        v = analyze(testrom_result(test_protocol="blargg", test_done=True,
                                   test_code=0,
                                   test_text="All 16 tests passed"))
        self.assertEqual(v.verdict, PASS)
        self.assertEqual(v.category, "test-pass")
        self.assertEqual(v.reason, "All 16 tests passed")

    def test_blargg_nonzero_code_fails_with_the_roms_own_words(self):
        v = analyze(testrom_result(test_protocol="blargg", test_done=True,
                                   test_code=3,
                                   test_text="01-vbl_basics Failed #2"))
        self.assertEqual(v.verdict, FAIL)
        self.assertEqual(v.category, "test-fail")
        self.assertIn("code 3", v.reason)
        self.assertIn("01-vbl_basics Failed #2", v.reason)

    def test_unfinished_test_fails(self):
        # The frame budget ran out before the ROM published anything: a
        # partial result is not a verdict.
        v = analyze(testrom_result(test_protocol="blargg", test_done=False))
        self.assertEqual(v.verdict, FAIL)
        self.assertEqual(v.category, "test-incomplete")

    def test_long_report_is_clipped(self):
        v = analyze(testrom_result(test_protocol="blargg", test_done=True,
                                   test_code=1, test_text="x " * 400))
        self.assertLessEqual(len(v.reason), REASON_MAX)

    def test_jam_outranks_the_roms_own_verdict(self):
        # nestest leaves $02/$03 at zero for every test it never reached, so
        # a jammed CPU must not be read as "everything passed".
        v = analyze(testrom_result(test_protocol="nestest", test_done=False,
                                   cpu_jammed=True,
                                   test_text="$02=00 $03=00"))
        self.assertEqual(v.verdict, FAIL)
        self.assertEqual(v.category, "cpu-jam")

    def test_finished_test_is_not_judged_by_the_heuristics(self):
        # A finished test ROM sits in a `jmp *` on an unchanging screen --
        # a tight loop and a frozen screen both, yet it passed.
        v = analyze(testrom_result(
            test_protocol="blargg", test_done=True, test_code=0,
            samples=[{"frame": f, "frame_hash": "same", "distinct_colors": 2,
                      "dominant_color": "000000", "dominant_frac": 0.97,
                      "distinct_pcs": 1, "instructions": 10, "pc": "e755",
                      "render_enabled": True, "nmi_enabled": True}
                     for f in (60, 90, 120)]))
        self.assertEqual(v.verdict, PASS)

    def test_no_protocol_falls_back_to_the_heuristics(self):
        # --test auto on a ROM with no blargg signature reports nothing, and
        # the screen/CPU analysis has to judge it as before.
        v = analyze(testrom_result(samples=[
            {"frame": f, "frame_hash": f"h{f}", "distinct_colors": 1,
             "dominant_color": "000000", "dominant_frac": 1.0,
             "distinct_pcs": 200, "instructions": 10, "pc": "c000",
             "render_enabled": True, "nmi_enabled": True}
            for f in (60, 90, 120)]))
        self.assertEqual(v.verdict, FAIL)
        self.assertEqual(v.category, "blank-screen")


class TestTestModeFlag(unittest.TestCase):
    def capture(self, **kw):
        completed = subprocess.CompletedProcess(args=[], returncode=0,
                                                stdout="{}", stderr="")
        with mock.patch("subprocess.run", return_value=completed) as m:
            run_rom("t.nes", "t.nes", RunConfig(harness="./h", **kw))
        return m.call_args[0][0]

    def test_off_is_not_passed(self):
        self.assertNotIn("--test", self.capture())

    def test_mode_is_passed(self):
        cmd = self.capture(test_mode="nestest")
        self.assertEqual(cmd[cmd.index("--test") + 1], "nestest")


class TestRomOptions(unittest.TestCase):
    def test_options_map_to_runconfig_fields(self):
        self.assertEqual(
            parse_rom_options("test=nestest frames=120 timeout=45"),
            {"test_mode": "nestest", "frames": 120, "timeout": 45.0})

    def test_input_none_disables_mashing(self):
        self.assertEqual(parse_rom_options("input=none"),
                         {"inject_input": False})

    def test_input_list_enables_mashing(self):
        self.assertEqual(parse_rom_options("input=start,select"),
                         {"inject_input": True, "buttons": "start,select"})

    def test_ram_fill_accepts_hex(self):
        self.assertEqual(parse_rom_options("ram-fill=0xff"),
                         {"ram_fill": 0xff})

    def test_unknown_option_is_an_error(self):
        # Silently ignoring it would run the ROM under flags nobody asked
        # for and record the resulting verdict as the baseline.
        with self.assertRaises(ValueError):
            parse_rom_options("nosuchoption=1")

    def test_bad_test_mode_is_an_error(self):
        with self.assertRaises(ValueError):
            parse_rom_options("test=sideways")

    def test_option_without_value_is_an_error(self):
        with self.assertRaises(ValueError):
            parse_rom_options("frames")


class TestReadRomList(unittest.TestCase):
    def read(self, text):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "list.txt")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
            return read_rom_list(path, "/roms")

    def test_plain_path_has_no_overrides(self):
        entries = self.read("sub/game.nes\n")
        self.assertEqual(entries, [("/roms/sub/game.nes", "game.nes", {})])

    def test_comments_and_blanks_ignored(self):
        self.assertEqual(self.read("# note\n\n  \nx.nes\n"),
                         [("/roms/x.nes", "x.nes", {})])

    def test_options_after_the_separator(self):
        path, key, opts = self.read("a/nestest.nes | test=nestest\n")[0]
        self.assertEqual(path, "/roms/a/nestest.nes")
        self.assertEqual(key, "nestest.nes")
        self.assertEqual(opts, {"test_mode": "nestest"})

    def test_spaces_in_rom_names_survive(self):
        # ROM filenames are full of spaces, which is why the separator is
        # " | " rather than whitespace.
        entries = self.read("Some Game (USA).nes\n")
        self.assertEqual(entries[0][1], "Some Game (USA).nes")

    def test_spaces_in_rom_names_survive_alongside_options(self):
        path, key, opts = self.read("Some Game (USA).nes | frames=90\n")[0]
        self.assertEqual(key, "Some Game (USA).nes")
        self.assertEqual(opts, {"frames": 90})

    def test_bad_option_names_the_line(self):
        with self.assertRaises(ValueError) as ctx:
            self.read("a.nes\nb.nes | bogus=1\n")
        self.assertIn(":2:", str(ctx.exception))


class TestManifest(unittest.TestCase):
    """The pinned test-ROM manifest has to stay consistent with the list."""

    @classmethod
    def setUpClass(cls):
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        cls.here = here
        with open(os.path.join(here, "testroms.json"), encoding="utf-8") as fh:
            cls.manifest = json.load(fh)

    def test_every_rom_names_a_known_source(self):
        sources = self.manifest["sources"]
        for rom in self.manifest["roms"]:
            self.assertIn(rom["source"], sources)

    def test_hashes_are_sha256(self):
        for rom in self.manifest["roms"]:
            self.assertRegex(rom["sha256"], r"^[0-9a-f]{64}$")

    def test_sources_pin_a_full_commit(self):
        for src in self.manifest["sources"].values():
            self.assertRegex(src["commit"], r"^[0-9a-f]{40}$")

    def test_destinations_are_unique_and_relative(self):
        dests = [rom["dest"] for rom in self.manifest["roms"]]
        self.assertEqual(len(dests), len(set(dests)))
        for dest in dests:
            self.assertFalse(os.path.isabs(dest))
            self.assertNotIn("..", dest.split("/"))

    def test_ci_list_and_manifest_agree(self):
        listed = {
            os.path.relpath(path, os.path.join(self.here, "roms", "external"))
            for path, _key, _opts in read_rom_list(
                os.path.join(self.here, "testroms-ci.txt"),
                self.here)
        }
        self.assertEqual(listed, {r["dest"] for r in self.manifest["roms"]})

    def test_baseline_covers_every_listed_rom(self):
        with open(os.path.join(self.here, "testroms-baseline.json"),
                  encoding="utf-8") as fh:
            baseline = json.load(fh)["roms"]
        keys = {os.path.basename(r["dest"]) for r in self.manifest["roms"]}
        self.assertEqual(set(baseline), keys)


if __name__ == "__main__":
    unittest.main()
