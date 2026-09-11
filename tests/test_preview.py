from pathlib import Path
import unittest
from tools.serve_preview import NativeRenderer, RendererSessions

EXE = Path(__file__).resolve().parents[1] / "build/live-preview"


class PreviewTests(unittest.TestCase):
    def test_native_frame_and_metadata(self):
        renderer = NativeRenderer(EXE)
        self.addCleanup(renderer.close)
        pixels, metadata = renderer.frame({"time": 0, "command": 3})
        self.assertEqual(len(pixels), 281600)
        self.assertEqual(float(metadata["turn"]), 0)
        self.assertEqual(float(metadata["openness"]), 1)
        self.assertEqual(metadata["automatic"], "1")

    def test_browser_sessions_do_not_reset_each_other(self):
        sessions = RendererSessions(EXE)
        self.addCleanup(sessions.close_all)
        first = sessions.get("first-tab")
        second = sessions.get("second-tab")
        first.frame({"time": 0, "command": 3, "seed": 123})
        first.frame({"time": 0, "command": 1, "direction": 1})
        before, state = first.frame({"time": 2})
        second.frame({"time": 0, "command": 3, "seed": 999})
        after, later = first.frame({"time": 2})
        self.assertEqual(before, after)
        self.assertEqual(state["automatic"], "0")
        self.assertEqual(later["automatic"], "0")

    def test_invalid_inputs_surface_errors(self):
        renderer = NativeRenderer(EXE)
        self.addCleanup(renderer.close)
        for payload in ([], {"time": float("nan")}, {"turn": 2},
                        {"direction": -1}, {"command": 9}, {"seed": -1}):
            with self.assertRaises(ValueError):
                renderer.frame(payload)


if __name__ == "__main__":
    unittest.main()
