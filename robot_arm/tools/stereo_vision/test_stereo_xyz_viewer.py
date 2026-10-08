"""Regression tests for exact UART identities and read-only XYZ review."""

from datetime import datetime, timezone
import json
from pathlib import Path
import tempfile
import unittest

import stereo_xyz_viewer as viewer
import obs_session_recorder as recorder


def raw(sid, sequence, fid, wrist=(500, 300, 1)):
    return ",".join(map(str, ["RAW", 1000, sid, sequence, fid, 600, 300, 10, 1,
                            400, 300, 10, 1, 450, 450, 10, 1, wrist[0], wrist[1], 10, wrist[2],
                            520, 300, 1, 540, 300, 1]))


def pair(rsid, index, lsid=10, left_seq=20, left_fid=700, right_seq=30, right_fid=9):
    return ",".join(map(str, ["PAIR", 1000, rsid, index, lsid, left_seq, left_fid, right_seq, right_fid, 0, 10000, -2, 0, 0, 0, 0, 0]))


def pix(rsid, index, side, sid, sequence, fid):
    return ",".join(map(str, ["PIX", rsid, index, side, sid, sequence, fid,
                            600, 300, 1, 400, 300, 1, 450, 450, 1, 500, 300, 1, 520, 300, 1, 540, 300, 1]))


def pose(fid=700, flags=3, age=0):
    return ",".join(map(str, ["P3", fid, 63, flags, age, *([100, -50, 1200] * 6)]))


class ViewerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def load(self, records):
        (self.directory / "combined_uart.log").write_text("\n".join(
            f"2026-10-05T01:00:{index // 100:02d}.{index % 100:03d}+00:00 [{side}] {payload}"
            for index, (side, payload) in enumerate(records)), encoding="utf-8")
        return viewer.read_session(self.directory)

    def test_exact_session_sequence_frame_and_left_trace_identity(self):
        frames, counts = self.load([
            ("R", pair(40, 1)), ("L", raw(10, 20, 700)), ("R", raw(40, 30, 9)),
            ("R", pix(40, 1, "L", 10, 20, 700)), ("R", pix(40, 1, "R", 40, 30, 9)),
            ("R", "PG,40,1,1,OK"), ("R", pose(9)), ("R", pose(700)),
            ("R", "A1,700,1000,5,300,1,63,1,1,-20,30,40,50,0.5"),
            ("R", "A2,700,1000,5,R,0x2,-20,30,40,50,110,90,100,50,0.5")])
        self.assertEqual(len(frames), 1)
        self.assertEqual(counts["unassociated_traces"], 1)
        self.assertEqual(frames[0]["P3"]["fid"], 700)
        self.assertTrue(frames[0]["P3"]["points"]["wrist"]["fresh"])
        self.assertEqual(frames[0]["A2"]["state"], "R")
        self.assertEqual(frames[0]["A1"]["angles"], [-20, 30, 40, 50, 0.5])
        self.assertEqual(frames[0]["raw"]["R"]["wrist"]["xy"], [500, 300])

    def test_boot_sessions_do_not_merge_equal_frame_numbers(self):
        frames, _ = self.load([
            ("L", raw(10, 20, 700)), ("R", pair(40, 1)), ("R", pose()),
            ("L", raw(11, 20, 700, (900, 350, 1))), ("R", pair(41, 1, lsid=11)), ("R", pose())])
        self.assertEqual(len(frames), 2)
        self.assertEqual(frames[0]["raw"]["L"]["wrist"]["xy"], [500, 300])
        self.assertEqual(frames[1]["raw"]["L"]["wrist"]["xy"], [900, 350])

    def test_identity_mismatch_missing_and_conflicting_raw(self):
        frames, counts = self.load([
            ("L", raw(10, 20, 700)), ("L", raw(10, 20, 700, (900, 350, 1))),
            ("R", pair(40, 1)), ("R", pix(40, 1, "R", 41, 30, 9))])
        self.assertIsNone(frames[0]["raw"]["L"])
        self.assertIsNone(frames[0]["raw"]["R"])
        self.assertIsNone(frames[0]["pix"]["R"])
        self.assertEqual(counts["PIX_identity_mismatch"], 1)
        self.assertEqual(counts["conflicting_records"], 1)

    def test_invalid_and_nonfresh_points_are_not_fresh(self):
        frames, _ = self.load([("L", raw(10, 20, 700, (0, 0, 0))), ("R", pair(40, 1)), ("R", pose(flags=1))])
        self.assertIsNone(frames[0]["raw"]["L"]["wrist"]["xy"])
        self.assertTrue(frames[0]["P3"]["points"]["elbow"]["fresh"])
        self.assertFalse(frames[0]["P3"]["points"]["green"]["fresh"])
        held = viewer.trace_record("P3", pose(age=300).split(","))
        self.assertFalse(any(point["fresh"] for point in held["points"].values()))

    def test_malformed_pair_clears_trace_context(self):
        frames, counts = self.load([("R", pair(40, 1)), ("R", "PAIR,broken"), ("R", pose())])
        self.assertIsNone(frames[0]["P3"])
        self.assertEqual(counts["malformed_PAIR"], 1)

    def test_conflicting_pair_is_not_rendered(self):
        frames, counts = self.load([("R", pair(40, 1)), ("R", pair(40, 1, left_fid=701)), ("R", pair(40, 2))])
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["pair"]["pair"], 2)
        self.assertEqual(counts["skipped_ambiguous_pairs"], 1)

    def test_nonfinite_trace_is_rejected(self):
        frames, counts = self.load([("R", pair(40, 1)), ("R", pose().replace("1200", "nan"))])
        self.assertIsNone(frames[0]["P3"])
        self.assertEqual(counts["malformed_P3"], 1)
        json.dumps(frames, allow_nan=False)

    def test_invalid_target_keeps_trace_status_and_empty_angles(self):
        record = viewer.trace_record("A1", "A1,700,1000,5,100,0,0,0,0,,,,,".split(","))
        self.assertEqual(record["output_valid"], 0)
        self.assertEqual(record["angles"], [None] * 5)
        record = viewer.trace_record("A2", "A2,700,1000,5,V,0x4,,,,,,,,,".split(","))
        self.assertEqual(record["flags"], 4)
        self.assertEqual(record["command"], [None] * 5)

    def test_held_shoulders_are_not_marked_fresh_without_current_pix(self):
        frames, _ = self.load([("R", pair(40, 1)), ("R", pose())])
        self.assertFalse(frames[0]["P3"]["points"]["shoulder_l"]["fresh"])
        self.assertTrue(frames[0]["P3"]["points"]["wrist"]["fresh"])

    def test_geometry_native_projection_and_y_up(self):
        output = self.directory / "geometry"
        output.mkdir()
        geometry = viewer.Geometry(viewer.ROOT, output)
        self.addCleanup(geometry.close)
        calibration = geometry.calibration
        expected = [100, 50, 1200]
        def project(camera, coordinate):
            normalized_x, normalized_y = coordinate[0] / coordinate[2], coordinate[1] / coordinate[2]
            radius = normalized_x ** 2 + normalized_y ** 2
            first, second, tangent_x, tangent_y, third = camera.distortion
            radial = 1 + first * radius + second * radius ** 2 + third * radius ** 3
            distorted_x = normalized_x * radial + 2 * tangent_x * normalized_x * normalized_y + tangent_y * (radius + 2 * normalized_x ** 2)
            distorted_y = normalized_y * radial + tangent_x * (radius + 2 * normalized_y ** 2) + 2 * tangent_y * normalized_x * normalized_y
            return [camera.fx * distorted_x + camera.cx, camera.fy * distorted_y + camera.cy]
        right_xyz = [sum(calibration.rotation[axis * 3 + index] * expected[index] for index in range(3)) + calibration.translation_mm[axis] for axis in range(3)]
        frame = {"pix": {"L": {name: {"valid": True, "xy": project(calibration.left, expected)} for name in viewer.POINTS},
                         "R": {name: {"valid": True, "xy": project(calibration.right, right_xyz)} for name in viewer.POINTS}}}
        recovered = geometry.reconstruct(frame)["wrist"]
        self.assertEqual(recovered["status"], "OK")
        for actual, target in zip(recovered["xyz"], [100, -50, 1200]):
            self.assertAlmostEqual(actual, target, places=3)
        self.assertLess(max(recovered["errors"]), 0.001)
        frame["pix"]["R"]["wrist"]["valid"] = False
        self.assertIsNone(geometry.reconstruct(frame)["wrist"]["xyz"])

    def test_obs_rect_downscale_and_unsafe_transform(self):
        scene = {"video_settings": {"baseWidth": 1920, "baseHeight": 1080, "outputWidth": 1280, "outputHeight": 720},
                 "scene_items": [{"sourceName": "Left", "sceneItemEnabled": True, "sceneItemTransform": {
                     "boundsType": "OBS_BOUNDS_NONE", "rotation": 0, "scaleX": 0.5, "scaleY": 0.5,
                     "sourceWidth": 1920, "sourceHeight": 1080, "width": 960, "height": 540,
                     "positionX": 960, "positionY": 0, "alignment": 5}}]}
        self.assertEqual(viewer.source_rect(scene, "Left"), [640, 0, 640, 360])
        scene["scene_items"][0]["sceneItemTransform"]["rotation"] = 90
        self.assertIsNone(viewer.source_rect(scene, "Left"))


class FakeOBS:
    def __init__(self, directory, fail_start=False, existing=False):
        self.directory = str(directory)
        self.original = self.directory
        self.active = existing
        self.fail_start = fail_start
        self.calls = []
        self.closed = False

    def close(self):
        self.closed = True

    def call(self, request, **values):
        self.calls.append(request)
        if request == "GetCurrentProgramScene":
            return {"sceneName": "test"}
        if request == "GetVersion":
            return {"availableRequests": ["SetRecordDirectory"]}
        if request == "GetVideoSettings":
            return {}
        if request == "GetSceneItemList":
            return {"sceneItems": []}
        if request == "GetRecordStatus":
            return {"outputActive": self.active, "outputPaused": False, "outputDuration": 100}
        if request == "GetRecordDirectory":
            return {"recordDirectory": self.directory}
        if request == "SetRecordDirectory":
            self.directory = values["recordDirectory"]
            return {}
        if request == "StartRecord":
            if self.fail_start:
                raise RuntimeError("start failed")
            self.active = True
            return {}
        if request == "StopRecord":
            self.active = False
            path = Path(self.directory) / "video.mp4"
            path.write_bytes(b"test")
            return {"outputPath": str(path)}
        raise AssertionError(request)


class RecorderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.session = self.directory / "monitor_test"
        self.session.mkdir()
        recorder.write_json(self.session / "session.json", {"status": "running", "started_utc": recorder.utc_now()})

    def test_json_update_retries_windows_reader_sharing_violation(self):
        from unittest.mock import patch

        original_replace = Path.replace
        attempts = []

        def replace(source, target):
            attempts.append(target)
            if len(attempts) == 1:
                raise PermissionError("reader still has the file open")
            return original_replace(source, target)

        target = self.session / "status.json"
        with patch.object(Path, "replace", replace), patch.object(recorder.time, "sleep"):
            recorder.write_json(target, {"status": "complete"})
        self.assertEqual(len(attempts), 2)
        self.assertEqual(recorder.read_json(target), {"status": "complete"})

    def test_integrated_recorder_waits_for_start_and_finalizes_on_close(self):
        from unittest.mock import patch

        client = FakeOBS(self.directory)
        video = recorder.SessionRecorder(self.session)
        with patch.object(recorder, "OBS", return_value=client):
            try:
                video.start()
                self.assertEqual(video.summary()["status"], "recording")
                self.assertTrue(client.active)
                self.assertEqual(recorder.read_json(self.session / "obs/recording.json")["status"], "recording")
            finally:
                video.close()
        self.assertEqual(video.summary()["status"], "complete")
        self.assertTrue(Path(video.summary()["video_path"]).is_file())
        self.assertFalse(client.active)
        self.assertTrue(client.closed)
        self.assertEqual(client.directory, client.original)
        self.assertFalse(video.thread.is_alive())

    def test_integrated_connection_failure_is_nonfatal_and_reported(self):
        from unittest.mock import patch

        video = recorder.SessionRecorder(self.session)
        with patch.object(recorder, "OBS", side_effect=ConnectionError("OBS is closed")):
            video.start()
            video.close()
        self.assertEqual(video.summary(), {"status": "error", "error": "OBS is closed"})
        self.assertFalse(video.thread.is_alive())

    def test_integrated_recorder_does_not_stop_existing_obs_recording(self):
        from unittest.mock import patch

        client = FakeOBS(self.directory, existing=True)
        video = recorder.SessionRecorder(self.session)
        with patch.object(recorder, "OBS", return_value=client):
            video.start()
            video.close()
        self.assertEqual(video.summary()["status"], "error")
        self.assertIn("already recording", video.summary()["error"])
        self.assertNotIn("StartRecord", client.calls)
        self.assertNotIn("StopRecord", client.calls)
        self.assertTrue(client.active)
        self.assertTrue(client.closed)

    def test_existing_recording_is_never_stopped(self):
        client = FakeOBS(self.directory, existing=True)
        with self.assertRaisesRegex(RuntimeError, "already recording"):
            recorder.record_session(client, self.session)
        self.assertNotIn("StopRecord", client.calls)
        self.assertNotIn("SetRecordDirectory", client.calls)

    def test_failed_start_restores_directory_without_stopping_other_recording(self):
        client = FakeOBS(self.directory, fail_start=True)
        with self.assertRaisesRegex(RuntimeError, "start failed"):
            recorder.record_session(client, self.session)
        self.assertEqual(client.directory, client.original)
        self.assertNotIn("StopRecord", client.calls)

    def test_stopped_monitor_finalizes_video_and_restores_directory(self):
        client = FakeOBS(self.directory)
        original_sample = recorder.status_sample
        def finishing_sample(connection):
            result = original_sample(connection)
            recorder.write_json(self.session / "session.json", {"status": "stopped"})
            return result
        from unittest.mock import patch
        with patch.object(recorder, "status_sample", finishing_sample):
            record = recorder.record_session(client, self.session, poll=0)
        self.assertEqual(record["status"], "complete")
        self.assertTrue(Path(record["video_path"]).is_file())
        self.assertEqual(client.directory, client.original)
        self.assertEqual(len(record["samples"]), 1)
        self.assertFalse((self.session / "obs/recorder.lock").exists())

    def test_discovery_ignores_old_and_already_recorded_sessions(self):
        earliest = datetime.now(timezone.utc).timestamp() - 5
        self.assertEqual(recorder.discover_session(self.directory, earliest), self.session)
        (self.session / "obs").mkdir()
        (self.session / "obs/recording.json").write_text("{}")
        self.assertIsNone(recorder.discover_session(self.directory, earliest))

    def test_user_directory_change_is_not_undone(self):
        client = FakeOBS(self.directory / "user_new")
        self.assertEqual(recorder.restore_directory(client, "old", self.directory / "temporary"), "not_restored_user_changed_directory")
        self.assertNotIn("SetRecordDirectory", client.calls)

    def test_async_record_state_waits_for_actual_transition(self):
        from unittest.mock import Mock, patch
        client = Mock()
        client.call.side_effect = [{"outputActive": False}, {"outputActive": True}]
        with patch.object(recorder.time, "sleep"):
            recorder.wait_record_state(client, True)
        self.assertEqual(client.call.call_count, 2)

    def test_file_finalization_never_accepts_empty_file(self):
        empty = self.directory / "empty.mp4"
        empty.write_bytes(b"")
        with self.assertRaises(TimeoutError):
            recorder.wait_file_finalized(empty, timeout=0.01)


if __name__ == "__main__":
    unittest.main()
