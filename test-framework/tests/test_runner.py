"""Unit tests for the harness command line built by runner.run_rom."""
import os
import subprocess
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from rnestest.runner import RunConfig, _parse_report, run_rom  # noqa: E402


def capture_cmd(cfg):
    """Run run_rom with subprocess.run stubbed; return the argv it built."""
    completed = subprocess.CompletedProcess(args=[], returncode=0,
                                            stdout="{}", stderr="")
    with mock.patch("subprocess.run", return_value=completed) as m:
        run_rom("game.nes", "game.nes", cfg)
    return m.call_args[0][0]


class TestRunnerCmd(unittest.TestCase):
    def cfg(self, **kw):
        return RunConfig(harness="./rnes_headless", **kw)

    def test_blank_frames_passed_by_default(self):
        # A blank screen gets extra time to draw before being failed; the
        # harness needs to be told how much.
        cmd = capture_cmd(self.cfg())
        self.assertIn("--blank-frames", cmd)
        self.assertEqual(cmd[cmd.index("--blank-frames") + 1], "900")

    def test_blank_frames_zero_disables(self):
        # 0 is meaningful (turn the confirmation off) and must be forwarded,
        # not dropped as falsy.
        cmd = capture_cmd(self.cfg(blank_frames=0))
        self.assertIn("--blank-frames", cmd)
        self.assertEqual(cmd[cmd.index("--blank-frames") + 1], "0")

    def test_blank_frames_none_leaves_harness_default(self):
        cmd = capture_cmd(self.cfg(blank_frames=None))
        self.assertNotIn("--blank-frames", cmd)

    def test_freeze_frames_only_when_set(self):
        self.assertNotIn("--freeze-frames", capture_cmd(self.cfg()))
        cmd = capture_cmd(self.cfg(freeze_frames=1800))
        self.assertEqual(cmd[cmd.index("--freeze-frames") + 1], "1800")

    def test_ram_fill_only_when_set(self):
        # None leaves the harness on its own deterministic default; an
        # explicit 0 is a real value and must not be dropped as falsy.
        self.assertNotIn("--ram-fill", capture_cmd(self.cfg()))
        cmd = capture_cmd(self.cfg(ram_fill=0))
        self.assertEqual(cmd[cmd.index("--ram-fill") + 1], "0")
        cmd = capture_cmd(self.cfg(ram_fill=0xFF))
        self.assertEqual(cmd[cmd.index("--ram-fill") + 1], "255")

    def test_rom_path_is_last(self):
        self.assertEqual(capture_cmd(self.cfg())[-1], "game.nes")


class TestParseReport(unittest.TestCase):
    """The core shares the harness's stdout, so picking the report out of it
    has to be more selective than "the last line that looks like JSON"."""

    def test_plain_report(self):
        self.assertEqual(_parse_report('{"status": "ok", "frames_run": 12}'),
                         {"status": "ok", "frames_run": 12})

    def test_report_after_core_chatter(self):
        out = "loading rom...\nmapper 1 init\n{\"status\": \"ok\"}"
        self.assertEqual(_parse_report(out), {"status": "ok"})

    def test_trailing_debug_dict_is_not_the_report(self):
        # A mapper tracing a one-line dict after the report must not win just
        # by being last; only an object carrying "status" is a report.
        out = '{"status": "ok", "frames_run": 300}\n{"bank": 3, "prg": 1}'
        self.assertEqual(_parse_report(out),
                         {"status": "ok", "frames_run": 300})

    def test_trailing_unparsable_line_does_not_hide_the_report(self):
        # Scanning must continue past a line that looks like an object but
        # is not valid JSON, rather than giving up on it.
        out = '{"status": "hang"}\n{not json at all}'
        self.assertEqual(_parse_report(out), {"status": "hang"})

    def test_emit_status_early_exit_is_a_report(self):
        out = '{"status": "skipped", "reason": "unsupported_mapper"}'
        self.assertEqual(_parse_report(out)["status"], "skipped")

    def test_no_report(self):
        self.assertIsNone(_parse_report(""))
        self.assertIsNone(_parse_report("segmentation fault\n"))
        self.assertIsNone(_parse_report('{"bank": 3}'))

    def test_json_array_is_not_a_report(self):
        self.assertIsNone(_parse_report('[1, 2, 3]'))


if __name__ == "__main__":
    unittest.main()
