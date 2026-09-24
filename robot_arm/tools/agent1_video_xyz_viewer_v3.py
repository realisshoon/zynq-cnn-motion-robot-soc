#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Agent1 실제 영상 + 직관적 3D XYZ Viewer V3

핵심 변경
1) 3D 시각화 좌표계를 "사람 몸(양어깨) 기준"으로 재배치
   - 화면 가로축      : Body X  (사람 자신의 오른쪽 +)
   - 화면 깊이축      : Body Z  (사람 자신의 정면 +)
   - 화면 세로축      : Body Y  (사람 자신의 위쪽 +)
   원점은 그 frame의 양어깨 중점. 카메라가 움직이거나 사람이 카메라 앞에서
   돌아도, 사람이 같은 동작을 하면 이 좌표계에서는 같은 모양으로 보인다.
   CSV에 body_axes 컬럼이 없으면 기존 Camera XYZ로 자동 fallback한다.

2) GRIP 상태를 크게 표시
   - GRIP = 1 : OPEN
   - GRIP = 0 : CLOSE
   영상 좌측 상단과 오른쪽 3D 패널 모두 표시.

3) V2 기능 유지
   - X/Y/Z landmark 표시
   - 좌/우 패널 1:1
   - time_sec 기반 영상/CSV 동기화
   - 원본 영상 전체 길이 유지
   - 전체 valid 데이터 기준 고정축
"""

import argparse
import bisect
import csv
import math
from dataclasses import dataclass
from typing import List, Tuple, Optional

import cv2
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
import numpy as np


@dataclass
class Pose2DFrame:
    frame_id: int
    time_sec: float
    frame_valid: int
    shoulder_l: Tuple[float, float]
    shoulder_l_valid: int
    shoulder_r: Tuple[float, float]
    shoulder_r_valid: int
    elbow: Tuple[float, float]
    elbow_valid: int
    wrist: Tuple[float, float]
    wrist_valid: int
    finger1: Tuple[float, float]
    finger1_valid: int
    finger2: Tuple[float, float]
    finger2_valid: int


@dataclass
class Result3DFrame:
    frame_id: int
    time_sec: float
    update_ret: int
    target_valid: int
    major_fresh: int
    finger_fresh: int
    shoulder_l: Tuple[float, float, float]
    shoulder_r: Tuple[float, float, float]
    elbow: Tuple[float, float, float]
    wrist: Tuple[float, float, float]
    finger1: Tuple[float, float, float]
    finger2: Tuple[float, float, float]
    base_deg: float
    shoulder_deg: float
    elbow_deg: float
    wrist_pitch_deg: float
    wrist_roll_deg: float
    gripper: float
    elbow_roll_deg: Optional[float] = None
    elbow_pitch_deg: Optional[float] = None
    body_axes: Optional[tuple] = None
    body_frame_valid: int = 0
    elbow_roll_observable: int = 0
    hand_fresh: int = 0
    active_arm: int = 1


def angle_summary(fr):
    if fr.elbow_roll_deg is not None:
        return (f"Human Elbow Roll={fr.elbow_roll_deg:.1f}  "
                f"Elbow Pitch={fr.elbow_pitch_deg:.1f}")
    return (f"LEGACY base={fr.base_deg:.1f} shoulder={fr.shoulder_deg:.1f} "
            f"elbow(inner)={fr.elbow_deg:.1f}")


def load_pose2d_csv(path: str) -> List[Pose2DFrame]:
    out = []
    with open(path, "r", newline="", encoding="utf-8") as f:
        for row in csv.reader(f):
            if not row:
                continue
            if row[0].strip().lower() in ("frame", "frame_id"):
                continue
            v = [x.strip() for x in row]
            if len(v) < 21:
                continue
            out.append(Pose2DFrame(
                int(float(v[0])), float(v[1]), int(float(v[2])),
                (float(v[3]), float(v[4])), int(float(v[5])),
                (float(v[6]), float(v[7])), int(float(v[8])),
                (float(v[9]), float(v[10])), int(float(v[11])),
                (float(v[12]), float(v[13])), int(float(v[14])),
                (float(v[15]), float(v[16])), int(float(v[17])),
                (float(v[18]), float(v[19])), int(float(v[20])),
            ))
    out.sort(key=lambda x: x.time_sec)
    return out


def load_result_csv(path: str) -> List[Result3DFrame]:
    out = []
    with open(path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        fields = set(reader.fieldnames or [])
        if "forearm_yaw_deg" in fields:
            raise ValueError("Obsolete fixed-frame CSV. Regenerate it with the current test_pose_csv.")
        forearm = "elbow_roll_deg" in fields
        required = {"frame_id", "time_sec", "update_ret", "target_valid",
                    "major_fresh", "finger_fresh", "wrist_pitch_deg",
                    "wrist_roll_deg", "gripper_norm"}
        required |= ({"elbow_roll_deg", "elbow_pitch_deg", "body_frame_valid",
                      "elbow_roll_observable", "hand_fresh"} if forearm else
                     {"base_deg", "shoulder_deg", "elbow_deg"})
        for name in ("shoulder_l", "shoulder_r", "elbow", "wrist", "finger1", "finger2"):
            required |= {name+"_x3d", name+"_y3d", name+"_z"}
        if forearm:
            required |= {f"body_{a}_{b}" for a in "xyz" for b in "xyz"}
        if not required <= fields:
            raise ValueError(f"Unsupported Agent1 CSV; missing columns: {sorted(required-fields)}")
        for row in reader:
            def point(name):
                return tuple(float(row[name+s]) for s in ("_x3d", "_y3d", "_z"))
            out.append(Result3DFrame(
                frame_id=int(row["frame_id"]), time_sec=float(row["time_sec"]),
                update_ret=int(row["update_ret"]), target_valid=int(row["target_valid"]),
                major_fresh=int(row["major_fresh"]), finger_fresh=int(row["finger_fresh"]),
                shoulder_l=point("shoulder_l"), shoulder_r=point("shoulder_r"),
                elbow=point("elbow"), wrist=point("wrist"),
                finger1=point("finger1"), finger2=point("finger2"),
                base_deg=float(row.get("base_deg", "nan")),
                shoulder_deg=float(row.get("shoulder_deg", "nan")),
                elbow_deg=float(row.get("elbow_deg", "nan")),
                wrist_pitch_deg=float(row["wrist_pitch_deg"]),
                wrist_roll_deg=float(row["wrist_roll_deg"]),
                gripper=float(row["gripper_norm"]),
                elbow_roll_deg=float(row["elbow_roll_deg"]) if forearm else None,
                elbow_pitch_deg=float(row["elbow_pitch_deg"]) if forearm else None,
                body_axes=tuple(tuple(float(row[f"body_{a}_{b}"]) for b in "xyz")
                                 for a in "xyz") if forearm else None,
                body_frame_valid=int(row.get("body_frame_valid", 0)),
                elbow_roll_observable=int(row.get("elbow_roll_observable", 0)),
                hand_fresh=int(row.get("hand_fresh", 0)),
                active_arm=int(row.get("active_arm", 1)),
            ))
    out.sort(key=lambda x: x.time_sec)
    return out


def nearest_by_time(items, times, t: float, max_gap: float):
    if not items:
        return None
    i = bisect.bisect_left(times, t)
    cand = []
    if i < len(items):
        cand.append(items[i])
    if i > 0:
        cand.append(items[i - 1])
    if not cand:
        return None
    best = min(cand, key=lambda x: abs(x.time_sec - t))
    return best if abs(best.time_sec - t) <= max_gap else None


def median_period(items, fallback=1.0 / 15.0):
    dts = []
    for a, b in zip(items[:-1], items[1:]):
        dt = b.time_sec - a.time_sec
        if 0.001 < dt < 1.0:
            dts.append(dt)
    return float(np.median(dts)) if dts else fallback


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def put_text_with_bg(img, text, org, color=(255, 255, 255),
                     scale=0.45, thickness=1,
                     bg=(0, 0, 0)):
    font = cv2.FONT_HERSHEY_SIMPLEX
    (tw, th), baseline = cv2.getTextSize(text, font, scale, thickness)
    h, w = img.shape[:2]
    x = int(clamp(org[0], 3, max(3, w - tw - 5)))
    y = int(clamp(org[1], th + 5, max(th + 5, h - baseline - 3)))
    cv2.rectangle(img,
                  (x - 3, y - th - 4),
                  (x + tw + 3, y + baseline + 3),
                  bg, -1)
    cv2.putText(img, text, (x, y), font, scale,
                color, thickness, cv2.LINE_AA)


def letterbox_square(img, size: int, bg=(0, 0, 0)):
    h, w = img.shape[:2]
    scale = min(size / float(w), size / float(h))
    nw = max(1, int(round(w * scale)))
    nh = max(1, int(round(h * scale)))
    resized = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_LINEAR)
    canvas = np.full((size, size, 3), bg, dtype=np.uint8)
    x0 = (size - nw) // 2
    y0 = (size - nh) // 2
    canvas[y0:y0 + nh, x0:x0 + nw] = resized
    return canvas


def body_origin(fr: "Result3DFrame"):
    """Shoulder midpoint in camera coordinates: the body frame's origin."""
    return tuple(0.5 * (a + b) for a, b in zip(fr.shoulder_l, fr.shoulder_r))


def to_body_frame(p, origin, body_axes):
    """Project a camera-space point into that frame's (BodyX, BodyY, BodyZ) basis."""
    d = tuple(pc - oc for pc, oc in zip(p, origin))
    bx, by, bz = body_axes
    return (
        d[0] * bx[0] + d[1] * bx[1] + d[2] * bx[2],
        d[0] * by[0] + d[1] * by[1] + d[2] * by[2],
        d[0] * bz[0] + d[1] * bz[1] + d[2] * bz[2],
    )


def compute_fixed_axis_limits(results: List[Result3DFrame], margin_ratio=0.12):
    have_body = any(r.body_axes is not None for r in results)

    valid = [r for r in results if r.target_valid == 1 and
             (not have_body or r.body_axes is not None)]
    if not valid:
        valid = [r for r in results if not have_body or r.body_axes is not None] or results

    xs, ys, zs = [], [], []
    for fr in valid:
        origin = body_origin(fr) if have_body and fr.body_axes is not None else None
        for p in (fr.shoulder_l, fr.shoulder_r, fr.elbow,
                  fr.wrist, fr.finger1, fr.finger2):
            if not all(math.isfinite(q) for q in p):
                continue
            bp = to_body_frame(p, origin, fr.body_axes) if origin is not None else p
            xs.append(bp[0])
            ys.append(bp[1])
            zs.append(bp[2])

    def lim(arr, min_span):
        lo, hi = min(arr), max(arr)
        span = max(hi - lo, min_span)
        c = 0.5 * (lo + hi)
        half = 0.5 * span * (1.0 + 2.0 * margin_ratio)
        return c - half, c + half

    return lim(xs, 2.0), lim(ys, 2.0), lim(zs, 2.0)


def draw_xyz_landmark(img, pt2d, short_name, xyz, color):
    x, y = int(round(pt2d[0])), int(round(pt2d[1]))
    if not (0 <= x < img.shape[1] and 0 <= y < img.shape[0]):
        return
    cv2.circle(img, (x, y), 5, color, -1, cv2.LINE_AA)
    text = f"{short_name} X={xyz[0]:.2f} Y={xyz[1]:.2f} Z={xyz[2]:.2f}"
    put_text_with_bg(img, text, (x + 7, y - 7),
                     color=color, scale=0.40, thickness=1)


def grip_state(grip: float):
    is_open = grip >= 0.5
    # Open=green, Close=orange/red-ish in BGR
    if is_open:
        return "OPEN", (40, 220, 40), (20, 80, 20)
    return "CLOSE", (80, 180, 255), (40, 60, 100)


def draw_grip_badge(img, grip: float, x=8, y=115):
    state, fg, bg = grip_state(grip)
    put_text_with_bg(
        img,
        f"GRIP = {int(grip >= 0.5)}  {state}",
        (x, y),
        color=fg,
        scale=0.72,
        thickness=2,
        bg=bg,
    )


def render_no_data_panel(size: int, text: str):
    img = np.full((size, size, 3), 245, dtype=np.uint8)
    cv2.putText(img, text, (40, size // 2),
                cv2.FONT_HERSHEY_SIMPLEX, 0.9,
                (30, 30, 30), 2, cv2.LINE_AA)
    return img


def render_3d_panel(fr: Result3DFrame,
                    xlim, ylim, zlim,
                    size=720, elev=20, azim=-62):
    """
    사람 몸(양어깨) 기준 좌표계로 표현:

        표시 X축  = Body X   (사람 자신의 오른쪽 +)
        표시 Y축  = Body Z   (사람 자신의 정면 +)
        표시 Z축  = Body Y   (사람 자신의 위쪽 +)

    원점은 그 frame의 양어깨 중점이다. 카메라가 움직이거나 사람이 카메라
    앞에서 돌아도, 사람이 같은 동작을 하면 이 좌표계에서는 같은 모양으로
    보인다 (Camera XYZ 고정 좌표계와 달리 Body XYZ는 매 frame 어깨로부터
    다시 계산됨. docs/coordinate_system.md 참고).

    body_axes가 없는 frame(구버전 CSV, body frame 미확정 등)은
    Camera XYZ로 그대로 fallback한다.
    """
    fig = plt.figure(figsize=(size / 100.0, size / 100.0), dpi=100)
    canvas = FigureCanvas(fig)

    ax = fig.add_axes([0.06, 0.29, 0.88, 0.60], projection="3d")

    pts_raw = {
        "SL": fr.shoulder_l,
        "SR": fr.shoulder_r,
        "E": fr.elbow,
        "W": fr.wrist,
        "F1": fr.finger1,
        "F2": fr.finger2,
    }

    use_body_frame = fr.body_axes is not None
    if use_body_frame:
        origin = body_origin(fr)
        pts_body = {k: to_body_frame(p, origin, fr.body_axes) for k, p in pts_raw.items()}
        # display coordinate = (BodyX, BodyZ_front, BodyY_up)
        pts = {k: (p[0], p[2], p[1]) for k, p in pts_body.items()}
    else:
        pts_body = None
        # fallback: raw camera coordinate = (X, Z_depth, Y)
        pts = {k: (p[0], p[2], p[1]) for k, p in pts_raw.items()}

    status = "FRESH"
    alpha = 1.0
    if fr.target_valid == 0:
        status = "INVALID / last 3D held"
        alpha = 0.28
    elif fr.update_ret == 0:
        status = "HOLD"
        alpha = 0.55

    segments = [
        ("SL", "SR", "tab:blue"),
        ("SR" if fr.active_arm else "SL", "E", "tab:orange"),
        ("E", "W", "tab:orange"),
        ("W", "F1", "tab:green"),
        ("W", "F2", "tab:green"),
    ]

    for a, b, c in segments:
        pa, pb = pts[a], pts[b]
        ax.plot([pa[0], pb[0]],
                [pa[1], pb[1]],
                [pa[2], pb[2]],
                linewidth=2.6, color=c, alpha=alpha)

    if use_body_frame and fr.body_frame_valid:
        origin = pts["E"]
        length = max(0.1, math.dist(fr.elbow, fr.wrist) * 0.65)
        # In body-frame display coordinates, Body X/Y/Z are just the
        # standard basis (display axes = BodyX, BodyZ, BodyY).
        axis_vectors = {"X": (1, 0, 0), "Y": (0, 0, 1), "Z": (0, 1, 0)}
        for name, color in zip("XYZ", ("red", "green", "blue")):
            vector = tuple(v * length for v in axis_vectors[name])
            ax.quiver(*origin, *vector, color=color, alpha=alpha, arrow_length_ratio=0.18)
            end = tuple(a+b for a, b in zip(origin, vector))
            ax.text(*end, f"Body {name}", color=color, fontsize=8)
        vector = tuple(b-a for a, b in zip(origin, pts["W"]))
        ax.quiver(*origin, *vector, color="purple", alpha=alpha, arrow_length_ratio=0.18)
        ax.text2D(0.02, 0.83,
                  "HUMAN BODY FRAME (this panel's axes)"
                  + f"\nelbow roll observable={fr.elbow_roll_observable} hand fresh={fr.hand_fresh}",
                  transform=ax.transAxes, fontsize=8, color="purple")
    elif not use_body_frame:
        ax.text2D(0.02, 0.83,
                  "no body_axes in CSV -> showing Camera XYZ instead",
                  transform=ax.transAxes, fontsize=8, color="red")

    colors = {
        "SL": "tab:blue", "SR": "tab:blue",
        "E": "tab:orange", "W": "tab:orange",
        "F1": "tab:green", "F2": "tab:green",
    }

    for name, p in pts.items():
        ax.scatter([p[0]], [p[1]], [p[2]],
                   s=42, color=colors[name], alpha=alpha)
        ax.text(p[0], p[1], p[2], name, fontsize=10, alpha=alpha)

    # 변환된 표시축의 고정 범위
    ax.set_xlim(xlim)
    ax.set_ylim(zlim)
    ax.set_zlim(ylim)

    if use_body_frame:
        ax.set_xlabel("Body X  -> person's right")
        ax.set_ylabel("Body Z  -> person's front")
        ax.set_zlabel("Body Y  -> person's up (+)")
    else:
        ax.set_xlabel("Camera X  -> right")
        ax.set_ylabel("Camera Z  -> depth / away")
        ax.set_zlabel("Camera Y  -> up (+)")
    ax.view_init(elev=elev, azim=azim)
    ax.grid(True)

    try:
        ax.set_box_aspect((
            xlim[1] - xlim[0],
            zlim[1] - zlim[0],
            ylim[1] - ylim[0],
        ))
    except Exception:
        pass

    # 축 방향 안내
    if use_body_frame:
        hint = "Body Y+ = up along person\nBody Z+ = toward person's front"
    else:
        hint = "IMAGE TOP = +Y\nZ+ = deeper into scene"
    ax.text2D(
        0.02, 0.94,
        hint,
        transform=ax.transAxes,
        fontsize=9,
        bbox=dict(facecolor="white", alpha=0.75, edgecolor="gray")
    )

    gstate, _, _ = grip_state(fr.gripper)

    fig.suptitle(
        f"frame={fr.frame_id}  t={fr.time_sec:.3f}s  [{status}]"
        f"     GRIP={int(fr.gripper >= 0.5)} ({gstate})\n"
        + angle_summary(fr) + "\n"
        + f"Wrist Pitch={fr.wrist_pitch_deg:.1f}  Wrist Roll={fr.wrist_roll_deg:.1f}",
        fontsize=10, y=0.982
    )

    if use_body_frame:
        table_lines = [
            "BODY FRAME COORDINATES (origin = shoulder midpoint)",
            "      BodyX    BodyY    BodyZ",
        ]
        for name in ("SL", "SR", "E", "W", "F1", "F2"):
            p = pts_body[name]
            table_lines.append(f"{name:<3} {p[0]:7.3f}  {p[1]:7.3f}  {p[2]:7.3f}")
    else:
        table_lines = [
            "RAW CAMERA COORDINATES",
            "        X        Y        Z(depth)",
            f"SL  {fr.shoulder_l[0]:7.3f}  {fr.shoulder_l[1]:7.3f}  {fr.shoulder_l[2]:7.3f}",
            f"SR  {fr.shoulder_r[0]:7.3f}  {fr.shoulder_r[1]:7.3f}  {fr.shoulder_r[2]:7.3f}",
            f"E   {fr.elbow[0]:7.3f}  {fr.elbow[1]:7.3f}  {fr.elbow[2]:7.3f}",
            f"W   {fr.wrist[0]:7.3f}  {fr.wrist[1]:7.3f}  {fr.wrist[2]:7.3f}",
            f"F1  {fr.finger1[0]:7.3f}  {fr.finger1[1]:7.3f}  {fr.finger1[2]:7.3f}",
            f"F2  {fr.finger2[0]:7.3f}  {fr.finger2[1]:7.3f}  {fr.finger2[2]:7.3f}",
        ]
    fig.text(0.09, 0.018, "\n".join(table_lines),
             family="monospace", fontsize=8.5, va="bottom")

    canvas.draw()
    buf = np.asarray(canvas.buffer_rgba())
    img = cv2.cvtColor(buf, cv2.COLOR_RGBA2BGR)
    plt.close(fig)
    return img


def resize_to_pose_space(frame, pose_w, pose_h):
    return cv2.resize(frame, (pose_w, pose_h), interpolation=cv2.INTER_LINEAR)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", required=True)
    ap.add_argument("--result-csv", required=True)
    ap.add_argument("--pose2d-csv", default=None)
    ap.add_argument("--output", required=True)

    ap.add_argument("--panel-size", type=int, default=720)
    ap.add_argument("--pose-width", type=int, default=1280)
    ap.add_argument("--pose-height", type=int, default=720)
    ap.add_argument("--output-fps", type=float, default=0.0)
    ap.add_argument("--sync-gap", type=float, default=0.15)
    ap.add_argument("--elev", type=float, default=20.0)
    ap.add_argument("--azim", type=float, default=-62.0)
    ap.add_argument("--max-seconds", type=float, default=-1.0)

    args = ap.parse_args()

    results = load_result_csv(args.result_csv)
    poses = load_pose2d_csv(args.pose2d_csv) if args.pose2d_csv else []

    if not results:
        raise RuntimeError("result CSV에 읽을 수 있는 frame이 없습니다.")

    result_times = [x.time_sec for x in results]
    pose_times = [x.time_sec for x in poses]

    result_dt = median_period(results)
    result_fps = 1.0 / result_dt if result_dt > 0 else 15.0
    output_fps = args.output_fps if args.output_fps > 0 else result_fps

    xlim, ylim, zlim = compute_fixed_axis_limits(results)

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        raise RuntimeError(f"video open failed: {args.video}")

    src_fps = cap.get(cv2.CAP_PROP_FPS)
    src_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    src_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    src_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    if src_fps <= 1.0:
        src_fps = 30.0

    video_duration = src_count / src_fps if src_count > 0 else results[-1].time_sec
    duration = min(video_duration, args.max_seconds) if args.max_seconds > 0 else video_duration

    panel = args.panel_size
    out_w = panel * 2
    out_h = panel

    writer = cv2.VideoWriter(
        args.output,
        cv2.VideoWriter_fourcc(*"mp4v"),
        output_fps,
        (out_w, out_h),
    )
    if not writer.isOpened():
        raise RuntimeError(f"VideoWriter open failed: {args.output}")

    print(f"[INFO] source video: {src_w}x{src_h}, fps={src_fps:.3f}, duration={video_duration:.3f}s")
    print(f"[INFO] result csv: frames={len(results)}, t={results[0].time_sec:.3f}~{results[-1].time_sec:.3f}s, estimated fps={result_fps:.3f}")
    print(f"[INFO] output: fps={output_fps:.3f}, duration={duration:.3f}s, size={out_w}x{out_h}")
    have_body = any(r.body_axes is not None for r in results)
    if have_body:
        print(f"[INFO] fixed BODY frame axis: BodyX={xlim}, BodyY={ylim}, BodyZ={zlim}")
        print("[INFO] 3D display axis: horizontal=Body X, depth=Body Z, vertical=Body Y (+ up)")
    else:
        print(f"[INFO] fixed RAW camera axis: X={xlim}, Y={ylim}, Z(depth)={zlim}")
        print("[INFO] 3D display axis: horizontal=X, depth=Z, vertical=Camera Y (+ up) [no body_axes in CSV]")

    n_out = int(math.floor(duration * output_fps)) + 1
    current_src_idx = -1
    current_frame = None

    for out_idx in range(n_out):
        t = out_idx / output_fps
        target_src_idx = int(round(t * src_fps))
        target_src_idx = min(max(target_src_idx, 0), max(0, src_count - 1))

        while current_src_idx < target_src_idx:
            ok, f = cap.read()
            if not ok:
                break
            current_src_idx += 1
            current_frame = f

        if current_frame is None:
            break

        fr = nearest_by_time(results, result_times, t, args.sync_gap)
        p2 = nearest_by_time(poses, pose_times, t, args.sync_gap) if poses else None

        video_view = resize_to_pose_space(
            current_frame, args.pose_width, args.pose_height
        )

        if fr is not None:
            if p2 is not None:
                entries = [
                    ("SL", p2.shoulder_l, p2.shoulder_l_valid, fr.shoulder_l, (255, 120, 120)),
                    ("SR", p2.shoulder_r, p2.shoulder_r_valid, fr.shoulder_r, (255, 120, 120)),
                    ("E", p2.elbow, p2.elbow_valid, fr.elbow, (0, 180, 255)),
                    ("W", p2.wrist, p2.wrist_valid, fr.wrist, (0, 180, 255)),
                    ("F1", p2.finger1, p2.finger1_valid, fr.finger1, (0, 255, 120)),
                    ("F2", p2.finger2, p2.finger2_valid, fr.finger2, (0, 255, 120)),
                ]
                for name, pt, valid, xyz, color in entries:
                    if valid:
                        draw_xyz_landmark(video_view, pt, name, xyz, color)

            info = [
                f"video t={t:.3f}s   csv frame={fr.frame_id} t={fr.time_sec:.3f}s",
                f"ret={fr.update_ret} valid={fr.target_valid} major={fr.major_fresh} finger={fr.finger_fresh}",
                angle_summary(fr),
                f"pitch={fr.wrist_pitch_deg:.2f} roll={fr.wrist_roll_deg:.2f}",
            ]
            y = 20
            for line in info:
                put_text_with_bg(video_view, line, (8, y),
                                 color=(255, 255, 255),
                                 scale=0.45, thickness=1)
                y += 21

            # 로그 한 줄이 아니라 눈에 띄는 별도 배지
            draw_grip_badge(video_view, fr.gripper, x=8, y=112)

            right = render_3d_panel(
                fr, xlim, ylim, zlim,
                size=panel, elev=args.elev, azim=args.azim
            )
        else:
            put_text_with_bg(
                video_view,
                f"video t={t:.3f}s / NO AGENT1 RESULT",
                (8, 20),
                color=(0, 0, 255),
                scale=0.55,
                thickness=1,
            )
            right = render_no_data_panel(panel, "NO AGENT1 RESULT")

        left = letterbox_square(video_view, panel)
        writer.write(np.hstack([left, right]))

    cap.release()
    writer.release()
    print(f"[OK] saved: {args.output}")


if __name__ == "__main__":
    main()
