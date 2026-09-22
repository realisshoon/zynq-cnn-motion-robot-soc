"""Run with the viewer's Python environment; CSV arguments are existing files."""
import sys
import unittest
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import agent1_video_xyz_viewer_v3 as viewer

new_path, legacy_path = sys.argv[1:3]


class ViewerTest(unittest.TestCase):
    def test_obsolete_schema_is_not_mislabelled_as_human(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".csv") as f:
            f.write("frame_id,forearm_yaw_deg,forearm_pitch_deg\n")
            f.flush()
            with self.assertRaisesRegex(ValueError, "Obsolete fixed-frame"):
                viewer.load_result_csv(f.name)

    def test_new_schema_and_render(self):
        rows = viewer.load_result_csv(new_path)
        self.assertEqual(len(rows), 522)
        first = rows[0]
        self.assertIsNotNone(first.body_axes)
        self.assertIn("Human Elbow Roll", viewer.angle_summary(first))
        self.assertNotIn("base", viewer.angle_summary(first))
        panel = viewer.render_3d_panel(first, (-2,2), (-2,2), (4,9), size=480, elev=10, azim=-75)
        self.assertEqual(panel.shape, (480,480,3))

    def test_legacy_schema_and_render(self):
        rows = viewer.load_result_csv(legacy_path)
        self.assertGreater(len(rows), 0)
        self.assertIsNone(rows[0].elbow_roll_deg)
        self.assertIn("LEGACY", viewer.angle_summary(rows[0]))
        panel = viewer.render_3d_panel(rows[0], (-2,2), (-2,2), (4,9), size=480, elev=10, azim=-75)
        self.assertEqual(panel.shape, (480,480,3))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
