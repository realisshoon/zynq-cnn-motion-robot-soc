#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Agent1 실제 영상 + 3D XYZ 고정축 Viewer V2

개선점
1. 영상 위에 각 landmark의 3D X/Y/Z를 함께 표시
2. 왼쪽 영상 패널 / 오른쪽 3D 패널을 같은 1:1 정사각형 크기로 고정
3. 원본 영상 전체 길이를 유지
   - 원본 FPS(약 30fps)와 Agent1 CSV(약 15Hz)의 frame_id를 직접 대응하지 않음
   - 시간(time_sec) 기준으로 가장 가까운 Agent1 결과를 매칭
4. 3D 축은 전체 VALID 데이터 기준 min/max + margin으로 한 번만 계산해서 고정
5. target_valid=0이면 과거 3D를 현재값처럼 보이지 않도록 INVALID 표시
"""

import argparse
import bisect
import csv
import math
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

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


def load_pose2d_csv(path: str) -> List[Pose2DFrame]:
    out: List[Pose2DFrame] = []
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
                frame_id=int(float(v[0])),
                time_sec=float(v[1]),
                frame_valid=int(float(v[2])),
                shoulder_l=(float(v[3]), float(v[4])),
                shoulder_l_valid=int(float(v[5])),
                shoulder_r=(float(v[6]), float(v[7])),
                shoulder_r_valid=int(float(v[8])),
                elbow=(float(v[9]), float(v[10])),
                elbow_valid=int(float(v[11])),
                wrist=(float(v[12]), float(v[13])),
                wrist_valid=int(float(v[14])),
                finger1=(float(v[15]), float(v[16])),
                finger1_valid=int(float(v[17])),
                finger2=(float(v[18]), float(v[19])),
                finger2_valid=int(float(v[20])),
            ))
    out.sort(key=lambda x: x.time_sec)
    return out


def load_result_csv(path: str) -> List[Result3DFrame]:
    out: List[Result3DFrame] = []
    with open(path, "r", newline="", encoding="utf-8") as f:
        for row in csv.reader(f):
            if not row:
                continue
            if row[0].strip().lower() in ("frame", "frame_id"):
                continue
            v = [x.strip() for x in row]
            if len(v) < 30:
                continue
            out.append(Result3DFrame(
                frame_id=int(float(v[0])),
                time_sec=float(v[1]),
                update_ret=int(float(v[2])),
                target_valid=int(float(v[3])),
                major_fresh=int(float(v[4])),
                finger_fresh=int(float(v[5])),
                shoulder_l=(float(v[6]), float(v[7]), float(v[8])),
                shoulder_r=(float(v[9]), float(v[10]), float(v[11])),
                elbow=(float(v[12]), float(v[13]), float(v[14])),
                wrist=(float(v[15]), float(v[16]), float(v[17])),
                finger1=(float(v[18]), float(v[19]), float(v[20])),
                finger2=(float(v[21]), float(v[22]), float(v[23])),
                base_deg=float(v[24]),
                shoulder_deg=float(v[25]),
                elbow_deg=float(v[26]),
                wrist_pitch_deg=float(v[27]),
                wrist_roll_deg=float(v[28]),
                gripper=float(v[29]),
            ))
    out.sort(key=lambda x: x.time_sec)
    return out


def nearest_by_time(items, times, t: float, max_gap: float):
    if not items:
        return None
    i = bisect.bisect_left(times, t)
    candidates = []
    if i < len(items):
        candidates.append(items[i])
    if i > 0:
        candidates.append(items[i - 1])
    if not candidates:
        return None
    best = min(candidates, key=lambda x: abs(x.time_sec - t))
    if abs(best.time_sec - t) > max_gap:
        return None
    return best


def median_period(items, fallback=1.0 / 15.0):
    if len(items) < 2:
        return fallback
    dts = []
    for a, b in zip(items[:-1], items[1:]):
        dt = b.time_sec - a.time_sec
        if 0.001 < dt < 1.0:
            dts.append(dt)
    if not dts:
        return fallback
    return float(np.median(np.asarray(dts)))


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def put_text_with_bg(img, text, org, color=(255, 255, 255),
                     scale=0.45, thickness=1):
    font = cv2.FONT_HERSHEY_SIMPLEX
    (tw, th), baseline = cv2.getTextSize(text, font, scale, thickness)
    h, w = img.shape[:2]
    x = int(clamp(org[0], 3, max(3, w - tw - 5)))
    y = int(clamp(org[1], th + 5, max(th + 5, h - baseline - 3)))
    cv2.rectangle(img,
                  (x - 2, y - th - 3),
                  (x + tw + 2, y + baseline + 2),
                  (0, 0, 0), -1)
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


def compute_fixed_axis_limits(results: List[Result3DFrame], margin_ratio=0.12):
    # INVALID 초기 0값 때문에 축이 0까지 늘어나지 않도록 valid frame만 사용
    valid = [r for r in results if r.target_valid == 1]
    if not valid:
        valid = results

    xs, ys, zs = [], [], []
    for fr in valid:
        for p in (fr.shoulder_l, fr.shoulder_r, fr.elbow,
                  fr.wrist, fr.finger1, fr.finger2):
            if all(math.isfinite(x) for x in p):
                xs.append(p[0])
                ys.append(p[1])
                zs.append(p[2])

    if not xs:
        return (-2, 2), (-2, 2), (0, 10)

    def lim(a, min_span):
        lo = min(a)
        hi = max(a)
        span = max(hi - lo, min_span)
        center = 0.5 * (lo + hi)
        half = 0.5 * span * (1.0 + 2.0 * margin_ratio)
        return center - half, center + half

    return lim(xs, 2.0), lim(ys, 2.0), lim(zs, 2.0)


def draw_xyz_landmark(img, pt2d, short_name, xyz, color):
    x, y = int(round(pt2d[0])), int(round(pt2d[1]))
    if x < 0 or y < 0 or x >= img.shape[1] or y >= img.shape[0]:
        return
    cv2.circle(img, (x, y), 5, color, -1, cv2.LINE_AA)
    text = f"{short_name} X={xyz[0]:.2f} Y={xyz[1]:.2f} Z={xyz[2]:.2f}"
    put_text_with_bg(img, text, (x + 7, y - 7),
                     color=color, scale=0.40, thickness=1)


def render_no_data_panel(size: int, text: str):
    img = np.full((size, size, 3), 245, dtype=np.uint8)
    cv2.putText(img, text, (40, size // 2),
                cv2.FONT_HERSHEY_SIMPLEX, 0.9,
                (30, 30, 30), 2, cv2.LINE_AA)
    return img


def render_3d_panel(fr: Result3DFrame,
                    xlim, ylim, zlim,
                    size=720, elev=28, azim=-60):
    fig = plt.figure(figsize=(size / 100.0, size / 100.0), dpi=100)
    canvas = FigureCanvas(fig)

    # 아래 25%는 XYZ 표 영역으로 남김
    ax = fig.add_axes([0.08, 0.28, 0.84, 0.62], projection="3d")

    pts = {
        "SL": fr.shoulder_l,
        "SR": fr.shoulder_r,
        "E": fr.elbow,
        "W": fr.wrist,
        "F1": fr.finger1,
        "F2": fr.finger2,
    }

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
        ("SR", "E", "tab:orange"),
        ("E", "W", "tab:orange"),
        ("W", "F1", "tab:green"),
        ("W", "F2", "tab:green"),
    ]

    for a, b, c in segments:
        pa, pb = pts[a], pts[b]
        ax.plot([pa[0], pb[0]],
                [pa[1], pb[1]],
                [pa[2], pb[2]],
                linewidth=2.4, color=c, alpha=alpha)

    point_colors = {
        "SL": "tab:blue", "SR": "tab:blue",
        "E": "tab:orange", "W": "tab:orange",
        "F1": "tab:green", "F2": "tab:green",
    }

    for name, p in pts.items():
        ax.scatter([p[0]], [p[1]], [p[2]],
                   s=38, color=point_colors[name], alpha=alpha)
        ax.text(p[0], p[1], p[2], name, fontsize=10, alpha=alpha)

    ax.set_xlim(xlim)
    ax.set_ylim(ylim)
    ax.set_zlim(zlim)
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_zlabel("Relative Z")
    ax.view_init(elev=elev, azim=azim)
    ax.grid(True)

    try:
        ax.set_box_aspect((
            xlim[1] - xlim[0],
            ylim[1] - ylim[0],
            zlim[1] - zlim[0]
        ))
    except Exception:
        pass

    fig.suptitle(
        f"frame={fr.frame_id}  t={fr.time_sec:.3f}s  [{status}]\n"
        f"base={fr.base_deg:.1f}  shoulder={fr.shoulder_deg:.1f}  "
        f"elbow={fr.elbow_deg:.1f}  pitch={fr.wrist_pitch_deg:.1f}  "
        f"roll={fr.wrist_roll_deg:.1f}",
        fontsize=12, y=0.965
    )

    table_lines = [
        "        X        Y        Z",
        f"SL  {fr.shoulder_l[0]:7.3f}  {fr.shoulder_l[1]:7.3f}  {fr.shoulder_l[2]:7.3f}",
        f"SR  {fr.shoulder_r[0]:7.3f}  {fr.shoulder_r[1]:7.3f}  {fr.shoulder_r[2]:7.3f}",
        f"E   {fr.elbow[0]:7.3f}  {fr.elbow[1]:7.3f}  {fr.elbow[2]:7.3f}",
        f"W   {fr.wrist[0]:7.3f}  {fr.wrist[1]:7.3f}  {fr.wrist[2]:7.3f}",
        f"F1  {fr.finger1[0]:7.3f}  {fr.finger1[1]:7.3f}  {fr.finger1[2]:7.3f}",
        f"F2  {fr.finger2[0]:7.3f}  {fr.finger2[1]:7.3f}  {fr.finger2[2]:7.3f}",
    ]
    fig.text(0.12, 0.025, "\n".join(table_lines),
             family="monospace", fontsize=9, va="bottom")

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

    ap.add_argument("--panel-size", type=int, default=720,
                    help="왼쪽/오른쪽 패널의 동일한 정사각형 크기")
    ap.add_argument("--pose-width", type=int, default=640,
                    help="MediaPipe/CNN 좌표계 width")
    ap.add_argument("--pose-height", type=int, default=480,
                    help="MediaPipe/CNN 좌표계 height")
    ap.add_argument("--output-fps", type=float, default=0.0,
                    help="0이면 result CSV 주기에서 자동 결정(보통 약 15fps)")
    ap.add_argument("--sync-gap", type=float, default=0.15,
                    help="영상 시간과 CSV 시간이 이 값보다 멀면 NO DATA 처리")
    ap.add_argument("--elev", type=float, default=28.0)
    ap.add_argument("--azim", type=float, default=-60.0)
    ap.add_argument("--max-seconds", type=float, default=-1.0,
                    help="빠른 테스트용. -1이면 원본 영상 전체")

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
    if args.max_seconds > 0:
        duration = min(video_duration, args.max_seconds)
    else:
        duration = video_duration

    panel = args.panel_size
    out_w = panel * 2
    out_h = panel

    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(args.output, fourcc, output_fps, (out_w, out_h))
    if not writer.isOpened():
        raise RuntimeError(f"VideoWriter open failed: {args.output}")

    print(f"[INFO] source video: {src_w}x{src_h}, fps={src_fps:.3f}, "
          f"duration={video_duration:.3f}s")
    print(f"[INFO] result csv: frames={len(results)}, "
          f"t={results[0].time_sec:.3f}~{results[-1].time_sec:.3f}s, "
          f"estimated fps={result_fps:.3f}")
    print(f"[INFO] output: fps={output_fps:.3f}, duration={duration:.3f}s, "
          f"size={out_w}x{out_h}")
    print(f"[INFO] fixed axis: X={xlim}, Y={ylim}, Z={zlim}")

    n_out = int(math.floor(duration * output_fps)) + 1
    current_src_idx = -1
    current_frame = None

    for out_idx in range(n_out):
        t = out_idx / output_fps
        target_src_idx = int(round(t * src_fps))
        target_src_idx = min(max(target_src_idx, 0), max(0, src_count - 1))

        # 순차적으로 필요한 source frame까지 진행
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

        # 실제 Pose 추출 좌표계(기본 640x480)로 영상 크기를 맞춘 뒤 overlay
        video_view = resize_to_pose_space(
            current_frame, args.pose_width, args.pose_height
        )

        if fr is not None:
            if p2 is not None:
                entries = [
                    ("SL", p2.shoulder_l, p2.shoulder_l_valid,
                     fr.shoulder_l, (255, 120, 120)),
                    ("SR", p2.shoulder_r, p2.shoulder_r_valid,
                     fr.shoulder_r, (255, 120, 120)),
                    ("E", p2.elbow, p2.elbow_valid,
                     fr.elbow, (0, 180, 255)),
                    ("W", p2.wrist, p2.wrist_valid,
                     fr.wrist, (0, 180, 255)),
                    ("F1", p2.finger1, p2.finger1_valid,
                     fr.finger1, (0, 255, 120)),
                    ("F2", p2.finger2, p2.finger2_valid,
                     fr.finger2, (0, 255, 120)),
                ]
                for name, pt, valid, xyz, color in entries:
                    if valid:
                        draw_xyz_landmark(video_view, pt, name, xyz, color)

            info = [
                f"video t={t:.3f}s   csv frame={fr.frame_id} t={fr.time_sec:.3f}s",
                f"ret={fr.update_ret} valid={fr.target_valid} "
                f"major={fr.major_fresh} finger={fr.finger_fresh}",
                f"base={fr.base_deg:.2f} shoulder={fr.shoulder_deg:.2f} "
                f"elbow={fr.elbow_deg:.2f}",
                f"pitch={fr.wrist_pitch_deg:.2f} roll={fr.wrist_roll_deg:.2f} "
                f"grip={fr.gripper:.1f}",
            ]
            y = 20
            for line in info:
                put_text_with_bg(video_view, line, (8, y),
                                 color=(255, 255, 255),
                                 scale=0.45, thickness=1)
                y += 21

            right = render_3d_panel(
                fr, xlim, ylim, zlim,
                size=panel, elev=args.elev, azim=args.azim
            )
        else:
            put_text_with_bg(video_view,
                             f"video t={t:.3f}s / NO AGENT1 RESULT",
                             (8, 20), color=(0, 0, 255),
                             scale=0.55, thickness=1)
            right = render_no_data_panel(panel, "NO AGENT1 RESULT")

        left = letterbox_square(video_view, panel, bg=(0, 0, 0))
        combo = np.hstack([left, right])
        writer.write(combo)

    cap.release()
    writer.release()

    print(f"[OK] saved: {args.output}")


if __name__ == "__main__":
    main()
