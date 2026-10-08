#!/usr/bin/env python3
"""Build a read-only, interactive XYZ/angle review from actual UART PAIR logs."""

import argparse
import _ctypes
from collections import Counter
import ctypes as ctypes
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
from urllib.parse import quote

import analyze_stereo_pixel_jitter as pixel_log


ROOT = Path(__file__).resolve().parents[2]
POINTS = ("shoulder_l", "shoulder_r", "elbow", "wrist", "red", "green")
PREFIXES = ("sl", "sr", "e", "w", "red", "green")
STATUS = {0: "OK", -1: "INVALID_INPUT", -2: "INVALID_CALIBRATION", -3: "UNDISTORT_FAILED",
          -4: "DEGENERATE", -5: "BEHIND_CAMERA", -6: "LOW_QUALITY", -7: "NUMERIC_FAILURE"}
LINE = re.compile(r"^(\S+) \[([LR])\] (.*)$")


def number(value):
    result = float(value)
    if not math.isfinite(result):
        raise ValueError("Nonfinite trace value")
    return result


def integer(value):
    return int(value, 16) if value.lower().startswith("0x") else int(value)


def optional_number(value):
    return number(value) if value.strip() else None


def trace_record(tag, fields):
    expected = {"A1": 14, "P3": 23, "A2": 15, "RQ": 9}[tag]
    if len(fields) != expected:
        raise ValueError("Wrong trace field count")
    if tag == "P3":
        mask, flags, age = map(integer, fields[2:5])
        points = {}
        for index, name in enumerate(POINTS):
            valid = bool(mask & (1 << index))
            fresh = valid and age == 0 and bool(flags & (1 if index < 4 else 2))
            coordinates = list(map(number, fields[5 + index * 3:8 + index * 3]))
            points[name] = {"xyz": coordinates if valid else None, "fresh": fresh}
        return {"fid": integer(fields[1]), "mask": mask, "flags": flags, "age_ms": age, "points": points}
    if tag == "A1":
        return {"fid": integer(fields[1]), "t_ms": integer(fields[2]), "dt_ms": integer(fields[4]),
                "pose_valid": integer(fields[5]), "valid_mask": integer(fields[6]),
                "result": integer(fields[7]), "output_valid": integer(fields[8]),
                "angles": list(map(optional_number, fields[9:14]))}
    if tag == "A2":
        return {"fid": integer(fields[1]), "t_ms": integer(fields[2]), "state": fields[4],
                "flags": integer(fields[5]), "human": list(map(optional_number, fields[6:10])),
                "command": list(map(optional_number, fields[10:15]))}
    return {"rsid": integer(fields[1]), "pair": integer(fields[2]), "state": fields[3],
            "candidates": integer(fields[4]), "epoch": integer(fields[5]),
            "left_age_us": integer(fields[6]), "right_age_us": integer(fields[7]), "gap_us": integer(fields[8])}


def keep_unique(table, key, value, counts):
    if key not in table:
        table[key] = value
    elif table[key] != value:
        table[key] = None
        counts["conflicting_records"] += 1


def read_session(session, allow_empty=False):
    raw, processed, pairs, admission, reacquire = {}, {}, {}, {}, {}
    order = []
    counts = Counter()
    current = None
    path = session / "combined_uart.log"
    with path.open(encoding="utf-8-sig", errors="replace") as stream:
        for line in stream:
            match = LINE.match(line.rstrip("\r\n"))
            if not match:
                continue
            timestamp, side, payload = match.groups()
            fields = payload.split(",")
            tag = fields[0]
            if tag not in ("RAW", "PIX", "PAIR", "PG", "RQ", "A1", "P3", "A2"):
                continue
            if side == "L" and tag != "RAW":
                continue
            try:
                receipt = datetime.fromisoformat(timestamp).timestamp()
                if tag in pixel_log.SCHEMAS:
                    record = pixel_log.parse_record(tag, fields)
                else:
                    record = trace_record(tag, fields)
                counts[tag] += 1
                if tag == "RAW":
                    keep_unique(raw, (side, record["sid"], record["seq"], record["fid"]), record, counts)
                elif tag == "PIX":
                    identity = (record["rsid"], record["pair"], "L" if record["side"] == "left" else "R")
                    keep_unique(processed, identity, record, counts)
                elif tag == "PAIR":
                    key = (record["rsid"], record["pair"])
                    if key not in pairs:
                        pairs[key] = {"pair": record, "receipt_utc": timestamp, "receipt_seconds": receipt,
                                      "A1": None, "P3": None, "A2": None}
                        order.append(key)
                    elif pairs[key]["pair"] != record:
                        pairs[key]["ambiguous"] = True
                        counts["conflicting_pairs"] += 1
                    current = key
                elif tag in ("PG", "RQ"):
                    keep_unique(admission if tag == "PG" else reacquire, (record["rsid"], record["pair"]), record, counts)
                elif current is not None and record["fid"] == pairs[current]["pair"]["lfid"]:
                    target = pairs[current]
                    if target[tag] is None:
                        target[tag] = record
                    elif target[tag] != record:
                        target["ambiguous"] = True
                        counts["conflicting_traces"] += 1
                else:
                    counts["unassociated_traces"] += 1
            except (ValueError, KeyError, OverflowError):
                counts["malformed_" + tag] += 1
                if tag == "PAIR":
                    current = None
    frames = []
    for key in order:
        target = pairs[key]
        if target.get("ambiguous"):
            counts["skipped_ambiguous_pairs"] += 1
            continue
        pair = target["pair"]
        target["PG"], target["RQ"] = admission.get(key), reacquire.get(key)
        target["raw"], target["pix"] = {}, {}
        for side in ("L", "R"):
            identity = ((pair["lsid"], pair["lseq"], pair["lfid"]) if side == "L" else
                        (pair["rsid"], pair["rseq"], pair["rfid"]))
            target["raw"][side] = coordinates(raw.get((side, *identity)))
            record = processed.get((*key, side))
            if record and (record["sid"], record["seq"], record["fid"]) != identity:
                record = None
                counts["PIX_identity_mismatch"] += 1
            target["pix"][side] = coordinates(record)
        if target["P3"]:
            for name in ("shoulder_l", "shoulder_r"):
                if not all(target["pix"][side] and target["pix"][side][name]["valid"] for side in ("L", "R")):
                    target["P3"]["points"][name]["fresh"] = False
        frames.append(target)
    if not frames:
        if allow_empty:
            return [], dict(counts)
        raise ValueError("No unambiguous PAIR rows in combined_uart.log; check the existing firmware trace settings")
    origin = frames[0]["receipt_seconds"]
    for target in frames:
        target["seconds"] = target["receipt_seconds"] - origin
    return frames, dict(counts)


def coordinates(record):
    if record is None:
        return None
    points = {}
    for name, prefix in zip(POINTS, PREFIXES):
        values = [record[prefix + "x"], record[prefix + "y"]]
        valid = record[prefix + "v"] == 1 and all(math.isfinite(value) for value in values)
        points[name] = {"xy": values if valid else None, "valid": valid}
    return points


class Camera(ctypes.Structure):
    _fields_ = [(name, ctypes.c_double) for name in ("fx", "fy", "cx", "cy")] + [("distortion", ctypes.c_double * 5)]


class Calibration(ctypes.Structure):
    _fields_ = [("left", Camera), ("right", Camera), ("rotation", ctypes.c_double * 9),
                ("translation_mm", ctypes.c_double * 3), ("image_width", ctypes.c_uint32), ("image_height", ctypes.c_uint32)]


class Options(ctypes.Structure):
    _fields_ = [("max_reprojection_error_px", ctypes.c_double), ("min_ray_sine", ctypes.c_double)]


class Context(ctypes.Structure):
    _fields_ = [("calibration", Calibration), ("options", Options), ("initialized", ctypes.c_uint32)]


class GeometryPoint(ctypes.Structure):
    _fields_ = [(name, ctypes.c_double) for name in ("x_mm", "y_mm", "z_mm", "left_error", "right_error")]


def find_compiler(requested=None):
    if requested:
        found = shutil.which(requested)
    else:
        found = next((shutil.which(name) for name in ("gcc", "cc") if shutil.which(name)), None)
        if found is None and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            found = "C:/msys64/ucrt64/bin/gcc.exe"
    if found is None:
        raise ValueError("Host GCC missing; supply --cc <gcc path> or use --no-geometry")
    return found


class Geometry:
    def __init__(self, root, output, compiler=None, max_error=15):
        compiler = find_compiler(compiler)
        self.directory_handle = None
        if os.name == "nt":
            self.directory_handle = os.add_dll_directory(str(Path(compiler).resolve().parent))
        library = output / ("geometry.dll" if os.name == "nt" else "geometry.so")
        sources = [root / "src/stereo_vision" / name for name in ("stereo_geometry.c", "stereo_calibration.c")]
        command = [compiler, "-shared", "-O2", "-std=c99", "-I", str(root / "include")]
        command += ["-Wl,--export-all-symbols"] if os.name == "nt" else ["-fPIC"]
        command += [*map(str, sources), "-lm", "-o", str(library)]
        environment = dict(os.environ)
        environment["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + environment.get("PATH", "")
        result = subprocess.run(command, capture_output=True, text=True, env=environment)
        (output / "geometry_build.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"Host geometry build failed ({result.returncode}); see geometry_build.log")
        self.library = ctypes.CDLL(str(library.resolve()))
        self.calibration = Calibration.in_dll(self.library, "stereo_calibration_current")
        self.context = Context()
        self.library.stereo_geometry_init.argtypes = [ctypes.POINTER(Context), ctypes.POINTER(Calibration), ctypes.POINTER(Options)]
        self.library.stereo_geometry_init.restype = ctypes.c_int
        self.library.stereo_reconstruct_point.argtypes = [ctypes.POINTER(Context), *([ctypes.c_double] * 4), ctypes.POINTER(GeometryPoint)]
        self.library.stereo_reconstruct_point.restype = ctypes.c_int
        options = Options(max_error, 1e-6)
        if self.library.stereo_geometry_init(ctypes.byref(self.context), ctypes.byref(self.calibration), ctypes.byref(options)):
            raise ValueError("Firmware calibration rejected")
        headers = [root / "include" / name for name in ("stereo_vision/stereo_geometry.h", "stereo_vision/stereo_pose.h", "common/robot_types.h")]
        self.hashes = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in [*sources, *headers]}

    def close(self):
        library = self.library
        self.library = None
        self.calibration = None
        if library is not None:
            if os.name == "nt":
                _ctypes.FreeLibrary(library._handle)
            else:
                _ctypes.dlclose(library._handle)
        if self.directory_handle is not None:
            self.directory_handle.close()
            self.directory_handle = None

    def reconstruct(self, frame):
        points = {}
        for name in POINTS:
            left, right = frame["pix"]["L"], frame["pix"]["R"]
            point = GeometryPoint()
            status = -1
            if left and right and left[name]["valid"] and right[name]["valid"]:
                status = self.library.stereo_reconstruct_point(ctypes.byref(self.context),
                         *left[name]["xy"], *right[name]["xy"], ctypes.byref(point))
            points[name] = {"status": STATUS.get(status, str(status)),
                            "xyz": [point.x_mm, -point.y_mm, point.z_mm] if status in (0, -6) else None,
                            "errors": [point.left_error, point.right_error] if status in (0, -6) else None}
        return points

    def cameras(self):
        rotation = list(self.calibration.rotation)
        translation = list(self.calibration.translation_mm)
        center = [-sum(rotation[row * 3 + axis] * translation[row] for row in range(3)) for axis in range(3)]
        return [[0, 0, 0], [center[0], -center[1], center[2]]]


def source_rect(scene, source_name):
    items = [item for item in scene.get("scene_items", []) if item.get("sourceName") == source_name and item.get("sceneItemEnabled")]
    if len(items) != 1:
        return None
    transform = items[0]["sceneItemTransform"]
    if (transform["boundsType"] != "OBS_BOUNDS_NONE" or abs(transform["rotation"]) > 1e-5
            or any(transform.get("crop" + edge, 0) for edge in ("Left", "Right", "Top", "Bottom"))
            or transform["scaleX"] <= 0 or transform["scaleY"] <= 0
            or abs(transform["sourceWidth"] / transform["sourceHeight"] - 1280 / 720) > 1e-5):
        return None
    width, height = transform["width"], transform["height"]
    alignment = transform["alignment"]
    origin_x = transform["positionX"] - (0 if alignment & 1 else width if alignment & 2 else width / 2)
    origin_y = transform["positionY"] - (0 if alignment & 4 else height if alignment & 8 else height / 2)
    settings = scene["video_settings"]
    factor_x, factor_y = settings["outputWidth"] / settings["baseWidth"], settings["outputHeight"] / settings["baseHeight"]
    if origin_x < -0.01 or origin_y < -0.01 or origin_x + width > settings["baseWidth"] + 0.01 or origin_y + height > settings["baseHeight"] + 0.01:
        return None
    return [origin_x * factor_x, origin_y * factor_y, width * factor_x, height * factor_y]


def recording_info(session, output):
    path = session / "obs/recording.json"
    if not path.is_file():
        return None
    record = json.loads(path.read_text(encoding="utf-8-sig"))
    source = Path(record.get("browser_video_path", record.get("video_path", "")))
    video_url = quote(Path(os.path.relpath(source, output)).as_posix()) if source.is_file() else None
    scene = record.get("scene", {})
    sources = [item.get("sourceName", "") for item in scene.get("scene_items", [])]
    rects = {}
    for side, names in (("L", ("왼쪽", "Left", "LEFT", "left")), ("R", ("오른쪽", "Right", "RIGHT", "right"))):
        choices = [name for name in sources if name in names]
        rects[side] = source_rect(scene, choices[0]) if len(choices) == 1 and not record.get("layout_changed") else None
    samples = []
    for sample in record.get("samples", []):
        if not sample["active"]:
            continue
        samples.append({"utc": datetime.fromisoformat(sample["utc"]).timestamp(),
                        "seconds": sample["video_seconds"], "paused": sample["paused"], "active": sample["active"]})
    return {"url": video_url, "samples": samples, "rects": rects,
            "video_size": [scene.get("video_settings", {}).get("outputWidth"), scene.get("video_settings", {}).get("outputHeight")],
            "status": record.get("status"), "timing": "approximate_host_receipt_not_exposure"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True, help="Folder printed as Session by the existing monitor")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--firmware-root", type=Path, default=ROOT, help="Source/snapshot whose calibration was used by the board")
    parser.add_argument("--cc")
    parser.add_argument("--no-geometry", action="store_true", help="Show recorded P3 and angles without recompiling geometry")
    parser.add_argument("--allow-empty", action="store_true", help="Skip sessions without PAIR rows without creating a review")
    parser.add_argument("--max-frames", type=int)
    parser.add_argument("--last-run", action="store_true", help="Only the last host r A through r S interval")
    args = parser.parse_args()
    if args.max_frames is not None and args.max_frames <= 0:
        parser.error("--max-frames must be positive")
    session = args.session.resolve()
    output = (args.output_dir or session / "xyz_review").resolve()
    try:
        frames, counts = read_session(session, allow_empty=args.allow_empty)
        if args.last_run:
            commands = [json.loads(line) for line in (session / "host_commands.jsonl").read_text(encoding="utf-8-sig").splitlines() if line.strip()]
            starts = [index for index, command in enumerate(commands) if command.get("input", "").strip() == "r A" and command.get("status") == "sent"]
            if not starts:
                raise ValueError("No sent r A command in this session")
            start = starts[-1]
            started = datetime.fromisoformat(commands[start]["timestamp_utc"]).timestamp()
            stopped = next((datetime.fromisoformat(command["timestamp_utc"]).timestamp() for command in commands[start + 1:]
                            if command.get("input", "").strip() == "r S" and command.get("status") == "sent"), math.inf)
            frames = [frame for frame in frames if started <= frame["receipt_seconds"] <= stopped]
        if args.max_frames:
            frames = frames[:args.max_frames]
        if not frames:
            if args.allow_empty:
                print("[SKIP] No PAIR rows in the selected interval")
                return 0
            raise ValueError("No PAIR rows in the selected interval")
        output.mkdir(parents=True, exist_ok=False)
        geometry = None if args.no_geometry else Geometry(args.firmware_root.resolve(), output, args.cc)
        for frame in frames:
            frame["geometry"] = geometry.reconstruct(frame) if geometry else None
            if frame["P3"] and frame["geometry"]:
                for name in ("shoulder_l", "shoulder_r"):
                    if frame["geometry"][name]["status"] != "OK":
                        frame["P3"]["points"][name]["fresh"] = False
        filtered_fresh = sum(bool(frame["P3"] and frame["P3"]["age_ms"] == 0 and frame["PG"] and frame["PG"]["accepted"]) for frame in frames)
        summary = {"session": str(session), "pairs": len(frames), "accepted": sum(bool(frame["PG"] and frame["PG"]["accepted"]) for frame in frames),
                   "fresh_P3_pairs": filtered_fresh, "angle_pairs": sum(frame["A1"] is not None for frame in frames),
                   "counts": counts, "geometry_source_hashes": geometry.hashes if geometry else {},
                   "coordinate_system": "Left camera origin; X camera-right, Y up, Z away, mm. A1 angles are logged human-axis angles, not derived here.",
                   "limitations": ["Video/UART host timing is approximate, not exposure synchronization.",
                                   "Geometry candidates are not control-approved points.",
                                   "P3 may contain held finger history; nonfresh points are not drawn as fresh.",
                                   "A2 commands are targets, not measured robot positions."]}
        data = {"summary": summary, "frames": frames, "points": POINTS,
                "cameras": geometry.cameras() if geometry else [], "recording": recording_info(session, output)}
        if geometry is not None:
            geometry.close()
        content = json.dumps(data, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
        (output / "review.json").write_text(content, encoding="utf-8")
        (output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        template = Path(__file__).with_suffix(".html").read_text(encoding="utf-8")
        (output / "review.html").write_text(template.replace("__REVIEW_DATA__", content.replace("<", "\\u003c")), encoding="utf-8")
        print(f"[OK] {output / 'review.html'}")
        print(f"[PAIR] {summary['pairs']} | PG accepted {summary['accepted']} | fresh P3 {filtered_fresh} | A1 {summary['angle_pairs']}")
        return 0
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        parser.exit(1, f"[ERROR] {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
