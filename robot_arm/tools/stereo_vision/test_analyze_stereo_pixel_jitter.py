"""Offline exact-identity pairing and pixel-statistics regression tests."""

from contextlib import redirect_stderr, redirect_stdout
import io
import json
import math
from pathlib import Path
import tempfile
import unittest

import analyze_stereo_pixel_jitter as jitter


def raw_row(sid, seq, fid, wrist=(10, 20, 1), t_ms=0):
    fields = ["RAW", t_ms, sid, seq, fid,
              10, 20, 90, 1, 30, 40, 80, 1, 50, 60, 70, 1,
              wrist[0], wrist[1], 60, wrist[2], 90, 100, 1, 110, 120, 1]
    return ",".join(map(str, fields))


def pair_row(rsid, pair, left, right, sync=0, control=0, t_ms=0):
    fields = ["PAIR", t_ms, rsid, pair, *left, *right, sync, 12345, control, -1, 2, 3, 4, 5]
    return ",".join(map(str, fields))


def pix_row(rsid, pair, side, identity, wrist=(10, 20, 1)):
    fields = ["PIX", rsid, pair, side, *identity,
              10, 20, 1, 30, 40, 1, 50, 60, 1, *wrist, 90, 100, 1, 110, 120, 1]
    return ",".join(map(str, fields))


class PixelJitterTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="paired-pixel-jitter-")
        self.addCleanup(self.directory.cleanup)
        self.session = Path(self.directory.name)

    def write_logs(self, left, right):
        for side, rows in (("left", left), ("right", right)):
            (self.session / (side + "_uart.log")).write_text("\n".join(rows) + "\n", encoding="utf-8")

    def complete_rows(self, rsid=200, count=3, lsid=100):
        left, right = [], []
        for pair in range(1, count + 1):
            left_id, right_id = (lsid, pair + 20, pair + 700), (pair + 90, pair + 10)
            left.append(raw_row(*left_id))
            right.extend([raw_row(rsid, *right_id), pair_row(rsid, pair, left_id, right_id),
                          pix_row(rsid, pair, "L", left_id), pix_row(rsid, pair, "R", (rsid, *right_id))])
        return left, right

    def test_disparate_frames_exact_pairs_out_of_order_raw_and_known_variance(self):
        left, right = [], []
        for pair, wrist in enumerate(((0, 0, 1), (3, 4, 1), (9, 12, 1)), 1):
            left_id, right_id = (100, pair + 20, pair + 700), (pair + 90, pair + 10)
            left.append(raw_row(*left_id, wrist=wrist, t_ms=9999 - pair))
            right.extend([pair_row(200, pair, left_id, right_id),
                          raw_row(200, *right_id, wrist=wrist, t_ms=pair),
                          pix_row(200, pair, "L", left_id, (pair - 1, 0, 1)),
                          pix_row(200, pair, "R", (200, *right_id), (pair - 1, 0, 1))])
        left.append(raw_row(100, 999, 999, (1000000, 1000000, 1)))
        self.write_logs(list(reversed(left)), list(reversed(right)))
        report = jitter.analyze(self.session)
        self.assertEqual(report["pairs"]["identified"], 3)
        self.assertEqual(report["links"]["complete_pairs"], 3)
        self.assertEqual(report["links"]["RAW_outside_selected_pairs"], 1)
        for side in jitter.SIDES:
            raw = report["stats"]["RAW"][side]["wrist"]
            self.assertEqual((raw["n"], raw["invalid"], raw["missing"]), (3, 0, 0))
            self.assertAlmostEqual(raw["std_x"], math.sqrt(14))
            self.assertAlmostEqual(raw["std_y"], math.sqrt(224 / 9))
            self.assertEqual((raw["span_x"], raw["span_y"], raw["step_n"], raw["step_max"]), (9, 12, 2, 10))
            self.assertAlmostEqual(raw["step_p95"], 9.75)
            processed = report["stats"]["PIX"][side]["wrist"]
            self.assertAlmostEqual(processed["std_x"], math.sqrt(2 / 3))
            self.assertEqual(processed["span_x"], 2)
            self.assertEqual(processed["step_p95"], 1)
            self.assertEqual(set(report["stats"]["RAW"][side]),
                             {"lshoulder", "rshoulder", "elbow", "wrist", "red", "green"})

    def test_matching_requires_session_sequence_and_frame_on_each_side(self):
        expected = (100, 21, 701)
        for wrong in ((101, 21, 701), (100, 22, 701), (100, 21, 702)):
            with self.subTest(wrong=wrong):
                self.write_logs([raw_row(*wrong)], [raw_row(200, 91, 11),
                                pair_row(200, 1, expected, (91, 11)),
                                pix_row(200, 1, "L", wrong), pix_row(200, 1, "R", (200, 91, 11))])
                report = jitter.analyze(self.session)
                self.assertEqual(report["stats"]["RAW"]["left"]["wrist"]["missing"], 1)
                self.assertEqual(report["stats"]["PIX"]["left"]["wrist"]["missing"], 1)
                self.assertEqual(report["links"]["PIX_identity_mismatch_left"], 1)
                self.assertEqual(report["stats"]["RAW"]["right"]["wrist"]["n"], 1)
        self.write_logs([raw_row(*expected)], [raw_row(201, 91, 11),
                        pair_row(200, 1, expected, (91, 11)), pix_row(200, 1, "R", (201, 91, 11))])
        report = jitter.analyze(self.session)
        self.assertEqual(report["stats"]["RAW"]["right"]["wrist"]["missing"], 1)
        self.assertEqual(report["links"]["PIX_identity_mismatch_right"], 1)

    def test_boot_sessions_reuse_ids_without_merging_or_cross_reset_steps(self):
        left, right = [], []
        for rsid, lsid, offset in ((900, 100, 0), (12, 101, 1000)):
            for pair in (1, 2):
                identity = (lsid, pair, pair + 700)
                wrist = (offset + pair, 0, 1)
                left.append(raw_row(*identity, wrist=wrist))
                right.extend([raw_row(rsid, pair, pair + 10, wrist),
                              pair_row(rsid, pair, identity, (pair, pair + 10)),
                              pix_row(rsid, pair, "L", identity, wrist),
                              pix_row(rsid, pair, "R", (rsid, pair, pair + 10), wrist)])
        self.write_logs(left, right)
        report = jitter.analyze(self.session)
        self.assertEqual(report["pairs"]["identified"], 4)
        self.assertEqual([entry["right_session"] for entry in report["window"]["observed"]], [900, 12])
        for side in jitter.SIDES:
            point = report["stats"]["RAW"][side]["wrist"]
            self.assertEqual((point["n"], point["step_n"], point["step_max"]), (4, 2, 1))
        selected = jitter.analyze(self.session, right_session=12, start_pair=2, end_pair=2)
        self.assertEqual(selected["pairs"]["selected"], 1)
        window = selected["window"]["observed"][0]
        self.assertEqual((window["first_pair"], window["last_pair"]), (2, 2))
        self.assertEqual(window["left"][0], {"session": 101, "seq_min": 2, "seq_max": 2,
                                           "fid_min": 702, "fid_max": 702})

    def test_left_reboot_and_pair_gaps_break_steps(self):
        self.write_logs([raw_row(100, 1, 701, (0, 0, 1)), raw_row(101, 2, 702, (99, 0, 1)),
                         raw_row(101, 3, 703, (100, 0, 1))],
                        [raw_row(200, 1, 11, (0, 0, 1)), raw_row(200, 2, 12, (99, 0, 1)),
                         raw_row(200, 3, 13, (100, 0, 1)),
                         pair_row(200, 1, (100, 1, 701), (1, 11)),
                         pair_row(200, 2, (101, 2, 702), (2, 12)),
                         pair_row(200, 4, (101, 3, 703), (3, 13))])
        report = jitter.analyze(self.session)
        for side in jitter.SIDES:
            point = report["stats"]["RAW"][side]["wrist"]
            self.assertEqual(point["n"], 3)
            self.assertEqual(point["step_n"], 0)
            self.assertIsNone(point["step_p95"])

    def test_invalid_and_missing_excluded_and_steps_do_not_bridge_them(self):
        left, right = [], []
        wrists = ((0, 0, 1), (1000000, 0, 0), (3, 0, 1), (6, 0, 1), (9, 0, 1))
        for pair, wrist in enumerate(wrists, 1):
            identity = (100, pair, pair + 700)
            if pair != 5:
                left.append(raw_row(*identity, wrist=wrist))
            right.extend([pair_row(200, pair, identity, (pair, pair + 10)),
                          pix_row(200, pair, "L", identity, wrist)])
        left.append(raw_row(100, 99, 799, (1e9, 0, 1)))
        self.write_logs(left, right)
        report = jitter.analyze(self.session)
        point = report["stats"]["RAW"]["left"]["wrist"]
        self.assertEqual((point["n"], point["invalid"], point["missing"]), (3, 1, 1))
        self.assertAlmostEqual(point["std_x"], math.sqrt(6))
        self.assertEqual((point["step_n"], point["step_max"]), (1, 3))
        self.assertEqual(report["stats"]["PIX"]["left"]["wrist"]["n"], 4)
        self.assertEqual(report["stats"]["RAW"]["right"]["wrist"]["missing"], 5)

    def test_pg_counts_admission_and_reasons_without_filtering_or_claiming_success(self):
        left, right = self.complete_rows()
        right.extend(["PG,200,1,1,OK", "PG,200,2,0,UNSYNC", "PG,200,99,1,OK",
                      pix_row(200, 99, "L", (100, 99, 799))])
        right[right.index(pair_row(200, 1, (100, 21, 701), (91, 11)))] = pair_row(
            200, 1, (100, 21, 701), (91, 11), sync=1, control=-2)
        self.write_logs(left, right)
        report = jitter.analyze(self.session)
        self.assertEqual(report["admission"], {"accepted": 1, "rejected": 1, "missing": 1,
                                              "accepted_reasons": {"OK": 1}, "rejected_reasons": {"UNSYNC": 1}})
        self.assertEqual(report["stats"]["PIX"]["left"]["wrist"]["n"], 3)
        self.assertEqual(report["pair_status"]["control"], {"-2": 1, "0": 2})
        self.assertEqual(report["sync"], {"verified_by_flag": 1, "not_verified": 2})
        self.assertEqual((report["links"]["orphan_PIX"], report["links"]["orphan_PG"]), (1, 1))
        self.assertTrue(any("not A2/PWM success" in note for note in report["notes"]))

    def test_duplicate_records_do_not_inflate_and_conflicting_keys_are_ambiguous(self):
        left, right = self.complete_rows(count=2)
        right.extend(["PG,200,1,1,OK", "PG,200,2,0,BAD"])
        self.write_logs(left * 2, right * 2)
        report = jitter.analyze(self.session)
        self.assertEqual(report["pairs"]["identified"], 2)
        self.assertEqual(report["stats"]["RAW"]["left"]["wrist"]["n"], 2)
        self.assertEqual(report["parse"]["right"]["duplicates"], {"RAW": 2, "PAIR": 2, "PIX": 4, "PG": 2})
        left.append(raw_row(100, 21, 701, (999, 999, 1)))
        right.extend([pix_row(200, 1, "L", (100, 21, 701), (999, 999, 1)),
                      "PG,200,1,0,CONFLICT", pair_row(200, 2, (100, 22, 702), (999, 12))])
        self.write_logs(left, right)
        report = jitter.analyze(self.session)
        self.assertEqual(report["pairs"]["ambiguous"], 1)
        self.assertEqual(report["pairs"]["identified"], 1)
        self.assertEqual(report["stats"]["RAW"]["left"]["wrist"]["n"], 0)
        self.assertEqual(report["stats"]["PIX"]["left"]["wrist"]["missing"], 2)
        self.assertEqual(report["admission"]["missing"], 1)
        self.assertEqual(report["parse"]["right"]["conflicts"], {"PIX": 1, "PG": 1, "PAIR": 1})

    def test_malformed_incomplete_headers_and_nonfinite_coordinates_never_crash(self):
        left, right = self.complete_rows(count=1)
        left.extend(["#RAW,t_ms,sid", "boot reset", "RAW,", raw_row(100, 90, 790) + ",extra",
                     raw_row(100, 91, 791).replace("RAW,0,", "RAW,nan,", 1),
                     raw_row(100, 92, 792, (0, 0, 2)), 'RAW,"unterminated'])
        right.extend(["PAIR,0,200", "PIX,200,1,Q", "PG,200,1,2,OK", "PG,200,1,0,",
                      "PG,200,2,1,OK,extra", "PAIR,0,-1,1", "\ufffdRAW,garbled",
                      pix_row(200, 2, "Q", (100, 22, 702)), "PG,nope,1,0,BAD",
                      pair_row(200, 2, (100, 22, 702), (92, 12)),
                      pix_row(200, 2, "L", (100, 22, 702), ("nan", "inf", 1))])
        self.write_logs(left, right)
        report = jitter.analyze(self.session)
        self.assertEqual(report["status"], "ok")
        self.assertEqual(report["parse"]["left"]["malformed"]["RAW"], 5)
        self.assertEqual(report["parse"]["right"]["malformed"], {"PAIR": 2, "PIX": 2, "PG": 4})
        point = report["stats"]["PIX"]["left"]["wrist"]
        self.assertEqual((point["n"], point["invalid"], point["missing"]), (1, 1, 0))
        json.dumps(report, allow_nan=False)

    def test_no_pair_rows_never_inferred_from_raw_pix_or_pg(self):
        self.write_logs([raw_row(100, 1, 11)], [raw_row(200, 1, 11),
                        pix_row(200, 1, "L", (100, 1, 11)), "PG,200,1,1,OK"])
        report = jitter.analyze(self.session)
        self.assertEqual(report["status"], "no_pairs")
        self.assertEqual(report["pairs"]["selected"], 0)
        self.assertIsNone(report["stats"]["RAW"]["left"]["wrist"]["std_x"])
        text = io.StringIO()
        with redirect_stdout(text):
            self.assertEqual(jitter.main(["--session", str(self.session)]), 1)
        self.assertIn("No identified pairs", text.getvalue())
        self.assertIn("new monitor session", text.getvalue())

    def test_no_observations_missing_records_and_empty_window_fail(self):
        self.write_logs([], [pair_row(200, 1, (100, 1, 701), (1, 11))])
        self.assertEqual(jitter.analyze(self.session)["status"], "no_observations")
        self.assertEqual(jitter.analyze(self.session, right_session=999)["status"], "no_pairs")
        self.assertEqual(jitter.analyze(self.session, start_pair=2)["status"], "no_pairs")
        with redirect_stdout(io.StringIO()):
            self.assertEqual(jitter.main(["--session", str(self.session)]), 1)

    def test_cli_json_readable_comparison_and_source_provenance(self):
        self.write_logs(*self.complete_rows())
        output = self.session / "jitter.json"
        before = {path.name: path.read_bytes() for path in self.session.iterdir()}
        text = io.StringIO()
        with redirect_stdout(text):
            result = jitter.main(["--session", str(self.session), "--right-session", "200",
                                  "--start-pair", "2", "--end-pair", "3", "--json", str(output)])
        self.assertEqual(result, 0)
        report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(report["pairs"]["selected"], 2)
        self.assertEqual(report["sources"]["left"], str(self.session / "left_uart.log"))
        for phrase in ("RAW", "PIX", "lshoulder", "wrist", "red", "green", "std_x", "step_p95",
                       "physical static state is not verified", "not A2/PWM success", "Observed source window"):
            self.assertIn(phrase, text.getvalue())
        for name, content in before.items():
            self.assertEqual((self.session / name).read_bytes(), content)
        self.assertEqual(set(path.name for path in self.session.iterdir()), set(before) | {"jitter.json"})

    def test_cli_missing_logs_invalid_bounds_and_source_overwrite_fail_cleanly(self):
        with redirect_stderr(io.StringIO()):
            self.assertEqual(jitter.main(["--session", str(self.session)]), 1)
        self.write_logs(*self.complete_rows())
        original = (self.session / "left_uart.log").read_bytes()
        for options in (["--start-pair", "3", "--end-pair", "2"], ["--right-session", "-1"],
                        ["--json", str(self.session / "left_uart.log")]):
            with self.subTest(options=options), redirect_stderr(io.StringIO()):
                self.assertEqual(jitter.main(["--session", str(self.session), *options]), 1)
        self.assertEqual((self.session / "left_uart.log").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
