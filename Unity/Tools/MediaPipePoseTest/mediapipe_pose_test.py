"""독립적인 MediaPipe Pose Landmarker Heavy 오른팔 관측 POC."""

from __future__ import annotations

import argparse
import csv
import math
import time
from datetime import datetime
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np


ROOT = Path(__file__).resolve().parent
DEFAULT_MODEL = ROOT / "models" / "pose_landmarker_heavy.task"
LOG_DIR = ROOT / "logs"

# MediaPipe 공식 Pose Landmarker의 33개 landmark 인덱스.
LANDMARKS = {
    "left_shoulder": 11,
    "right_shoulder": 12,
    "left_elbow": 13,
    "right_elbow": 14,
    "left_wrist": 15,
    "right_wrist": 16,
    "left_pinky": 17,
    "right_pinky": 18,
    "left_index": 19,
    "right_index": 20,
    "left_thumb": 21,
    "right_thumb": 22,
}
RIGHT_ARM = ("right_shoulder", "right_elbow", "right_wrist")
SKELETON_EDGES = (
    (0, 1), (1, 2), (2, 3), (0, 4), (4, 5), (5, 6),
    (3, 7), (6, 8), (9, 10), (11, 12), (11, 13), (13, 15),
    (15, 17), (15, 19), (15, 21), (17, 19), (12, 14),
    (14, 16), (16, 18), (16, 20), (16, 22), (18, 20),
    (11, 23), (12, 24), (23, 24), (23, 25), (25, 27),
    (27, 29), (29, 31), (27, 31), (24, 26), (26, 28),
    (28, 30), (30, 32), (28, 32),
)
POSE_MARKERS = {
    ord("1"): "NEUTRAL",
    ord("2"): "FRONT",
    ord("3"): "OUT",
    ord("4"): "OUT_FOREARM_UP",
    ord("5"): "FOREARM_DOWN",
    ord("6"): "IN",
}
FIELDS = [
    "timestamp_sec", "frame_id", "fps", "source_type", "source_fps",
    "video_frame_id", "video_time_sec", "pose_marker", "session_event",
]
for name in LANDMARKS:
    for space in ("img", "world"):
        FIELDS += [f"{name}_{space}_{item}" for item in ("x", "y", "z", "visibility", "presence")]
FIELDS += [
    "upper_arm_dx", "upper_arm_dy", "upper_arm_dz",
    "forearm_dx", "forearm_dy", "forearm_dz",
    "upper_arm_nx", "upper_arm_ny", "upper_arm_nz",
    "forearm_nx", "forearm_ny", "forearm_nz",
    "elbow_bend_deg", "landmark_valid",
]


def finite_xyz(landmark):
    if landmark is None:
        return None
    values = (landmark.x, landmark.y, landmark.z)
    if any(value is None or not math.isfinite(value) for value in values):
        return None
    return values


def vector_between(start, end):
    a, b = finite_xyz(start), finite_xyz(end)
    return tuple(y - x for x, y in zip(a, b)) if a and b else None


def normalized(vector):
    if vector is None:
        return None
    length = math.sqrt(sum(value * value for value in vector))
    return tuple(value / length for value in vector) if length > 1e-9 else None


def elbow_bend(shoulder, elbow, wrist):
    a = vector_between(elbow, shoulder)
    b = vector_between(elbow, wrist)
    na, nb = normalized(a), normalized(b)
    if na is None or nb is None:
        return None
    dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(na, nb))))
    return math.degrees(math.acos(dot))


def confident(landmark, threshold):
    if finite_xyz(landmark) is None:
        return False
    # Tasks Landmark에서 미지원 score는 None이다. 지원되는 score만 검사한다.
    return all(
        score is None or (math.isfinite(score) and score >= threshold)
        for score in (landmark.visibility, landmark.presence)
    )


def make_row(result, timestamp_sec, frame_id, fps, pose_marker, session_event, threshold,
             source_type="webcam", source_fps=None, video_frame_id=None,
             video_time_sec=None):
    row = dict.fromkeys(FIELDS, "")
    row.update(timestamp_sec=f"{timestamp_sec:.6f}", frame_id=frame_id,
               fps=f"{fps:.2f}", source_type=source_type,
               source_fps=f"{source_fps:.6f}" if source_fps is not None else "",
               video_frame_id=video_frame_id if video_frame_id is not None else "",
               video_time_sec=(f"{video_time_sec:.6f}"
                               if video_time_sec is not None else ""),
               pose_marker=pose_marker, session_event=session_event,
               landmark_valid=0)
    image = result.pose_landmarks[0] if result.pose_landmarks else None
    world = result.pose_world_landmarks[0] if result.pose_world_landmarks else None
    for name, index in LANDMARKS.items():
        for space, source in (("img", image), ("world", world)):
            if source is None or len(source) <= index:
                continue
            landmark = source[index]
            for item in ("x", "y", "z", "visibility", "presence"):
                value = getattr(landmark, item, None)
                if value is not None and math.isfinite(value):
                    row[f"{name}_{space}_{item}"] = f"{value:.8f}"

    if image is None or world is None or len(image) < 33 or len(world) < 33:
        return row, image, world
    shoulder, elbow, wrist = (world[LANDMARKS[name]] for name in RIGHT_ARM)
    upper = vector_between(shoulder, elbow)
    forearm = vector_between(elbow, wrist)
    for prefix, values in (("upper_arm_d", upper), ("forearm_d", forearm),
                           ("upper_arm_n", normalized(upper)),
                           ("forearm_n", normalized(forearm))):
        if values is not None:
            for axis, value in zip("xyz", values):
                row[f"{prefix}{axis}"] = f"{value:.8f}"
    angle = elbow_bend(shoulder, elbow, wrist)
    if angle is not None:
        row["elbow_bend_deg"] = f"{angle:.4f}"
    row["landmark_valid"] = int(all(
        confident(source[LANDMARKS[name]], threshold)
        for source in (image, world) for name in RIGHT_ARM
    ))
    return row, image, world


def pixel(landmark, width, height):
    xyz = finite_xyz(landmark)
    return (round(xyz[0] * width), round(xyz[1] * height)) if xyz else None


def draw_camera(frame, image, world, row, threshold):
    height, width = frame.shape[:2]
    if image is not None and len(image) >= 33:
        for a, b in SKELETON_EDGES:
            if confident(image[a], threshold) and confident(image[b], threshold):
                cv2.line(frame, pixel(image[a], width, height),
                         pixel(image[b], width, height), (105, 105, 105), 1, cv2.LINE_AA)
        for a, b in ((12, 14), (14, 16)):
            if finite_xyz(image[a]) and finite_xyz(image[b]):
                cv2.line(frame, pixel(image[a], width, height),
                         pixel(image[b], width, height), (0, 210, 255), 4, cv2.LINE_AA)
        for name, color, radius in (
            ("right_shoulder", (0, 255, 255), 9),
            ("right_elbow", (0, 180, 255), 9),
            ("right_wrist", (0, 100, 255), 9),
            ("right_thumb", (255, 0, 255), 6),
            ("right_index", (255, 80, 255), 6),
            ("right_pinky", (255, 160, 255), 6),
        ):
            point = pixel(image[LANDMARKS[name]], width, height)
            if point:
                cv2.circle(frame, point, radius, color, -1, cv2.LINE_AA)
                cv2.putText(frame, name.replace("right_", "R ").title(),
                            (point[0] + 10, point[1] - 8), cv2.FONT_HERSHEY_SIMPLEX,
                            0.5, color, 1, cv2.LINE_AA)

    if row["source_type"] == "video":
        fps_line = (f"Proc FPS: {row['fps']}  Source FPS: "
                    f"{float(row['source_fps']):.2f}  valid: {row['landmark_valid']}")
        lines = [fps_line, f"Marker: {row['pose_marker'] or '-'}"]
        for name, label in (("right_shoulder", "R Shoulder"),
                            ("right_elbow", "R Elbow"), ("right_wrist", "R Wrist")):
            img = image[LANDMARKS[name]] if image and len(image) > LANDMARKS[name] else None
            wrd = world[LANDMARKS[name]] if world and len(world) > LANDMARKS[name] else None
            img_text = f"({img.x:.3f},{img.y:.3f})" if finite_xyz(img) else "---"
            world_text = (f"({wrd.x:+.3f},{wrd.y:+.3f},{wrd.z:+.3f})"
                          if finite_xyz(wrd) else "---")
            lines.append(f"{label} 2D {img_text}  W {world_text} m")
        lines.append(f"Elbow Bend: {row['elbow_bend_deg'] or '---'} deg")
        lines.append("1-6 pose  0 clear  R reset  V 3D  Space pause  Q/ESC quit")
        panel_height = min(height, 154)
        roi = frame[:panel_height, :]
        if roi.size:
            cv2.addWeighted(roi, 0.2, np.zeros_like(roi), 0.8, 0, dst=roi)
        for index, line in enumerate(lines):
            y = 19 + index * 21
            if y >= height:
                break
            cv2.putText(frame, line, (10, y), cv2.FONT_HERSHEY_SIMPLEX,
                        0.48, (240, 240, 240), 1, cv2.LINE_AA)
        return

    panel_width = min(width, 435)
    panel_height = min(height, 355)
    roi = frame[:panel_height, :panel_width]
    if roi.size:
        shade = np.zeros_like(roi)
        cv2.addWeighted(roi, 0.35, shade, 0.65, 0, dst=roi)
    fps_line = f"FPS: {row['fps']}   valid: {row['landmark_valid']}"
    lines = [fps_line, f"Marker: {row['pose_marker'] or '-'}"]
    for name, label in (("right_shoulder", "R Shoulder"),
                        ("right_elbow", "R Elbow"), ("right_wrist", "R Wrist")):
        img = image[LANDMARKS[name]] if image and len(image) > LANDMARKS[name] else None
        wrd = world[LANDMARKS[name]] if world and len(world) > LANDMARKS[name] else None
        if finite_xyz(img):
            lines.append(f"{label} 2D: ({img.x:.3f}, {img.y:.3f})")
        else:
            lines.append(f"{label} 2D: ---")
        if finite_xyz(wrd):
            lines.append(f"  W: ({wrd.x:+.3f}, {wrd.y:+.3f}, {wrd.z:+.3f}) m")
        else:
            lines.append("  W: ---")
    lines.append(f"Elbow Bend: {row['elbow_bend_deg'] or '---'} deg")
    lines.append("1-6 pose  0 clear  R reset  V 3D")
    lines.append("Space pause/resume  Q/ESC quit")
    for index, line in enumerate(lines):
        y = 28 + index * 27
        if y >= height:
            break
        cv2.putText(frame, line, (10, y), cv2.FONT_HERSHEY_SIMPLEX,
                    0.55, (240, 240, 240), 1, cv2.LINE_AA)


def draw_world_view(world, threshold):
    canvas = np.full((480, 480, 3), (25, 25, 25), dtype=np.uint8)
    origin = np.array([235.0, 310.0])
    # X/Y/Z를 하나의 고정 축척 사영도로 표시한다. 점 사이 3D 거리는 CSV 원본으로 판단한다.
    def project(xyz):
        x, y, z = xyz
        return tuple(np.rint(origin + 340 * np.array([x + 0.45 * z, y - 0.35 * z])).astype(int))

    for axis, color, label in (((0.25, 0, 0), (255, 100, 100), "X"),
                               ((0, -0.25, 0), (100, 255, 100), "Y"),
                               ((0, 0, 0.25), (100, 100, 255), "Z")):
        end = project(axis)
        cv2.arrowedLine(canvas, tuple(origin.astype(int)), end, color, 2, cv2.LINE_AA)
        cv2.putText(canvas, label, end, cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)
    cv2.putText(canvas, "World landmarks (m) - fixed projection", (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (230, 230, 230), 1, cv2.LINE_AA)
    if world is None or len(world) < 17:
        return canvas
    for a, b in ((11, 12), (12, 14), (14, 16)):
        pa, pb = finite_xyz(world[a]), finite_xyz(world[b])
        if pa and pb:
            cv2.line(canvas, project(pa), project(pb), (0, 210, 255), 3, cv2.LINE_AA)
    for index, label in ((11, "L Shoulder"), (12, "R Shoulder"),
                         (14, "R Elbow"), (16, "R Wrist")):
        xyz = finite_xyz(world[index])
        if xyz:
            point = project(xyz)
            color = (0, 255, 255) if confident(world[index], threshold) else (0, 100, 255)
            cv2.circle(canvas, point, 6, color, -1, cv2.LINE_AA)
            cv2.putText(canvas, label, (point[0] + 7, point[1] - 7),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1, cv2.LINE_AA)
    return canvas


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera", type=int, default=0, help="OpenCV 카메라 인덱스")
    parser.add_argument("--video", type=Path, help="입력 MP4 영상 경로; 생략 시 webcam 사용")
    parser.add_argument("--crop-left", type=int, metavar="PIXELS",
                        help="영상의 왼쪽 PIXELS 너비만 사람 영상으로 처리")
    parser.add_argument("--save-overlay", action="store_true",
                        help="표시 중인 skeleton 영상을 logs/의 MP4로 저장")
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--min-landmark-confidence", type=float, default=0.5,
                        help="오른팔 3점의 visibility/presence 유효성 기준")
    parser.add_argument("--no-3d", action="store_true", help="3D 사영 창 비활성화")
    parser.add_argument("--udp", action="store_true", help="RAW 양팔 world pose + 입력 영상 미리보기를 Unity로 전송")
    parser.add_argument("--udp-host", default="127.0.0.1")
    parser.add_argument("--udp-port", type=int, default=5055)
    args = parser.parse_args()
    if not 1 <= args.udp_port <= 65535:
        parser.error("--udp-port는 1~65535이어야 합니다")
    if not 0 <= args.min_landmark_confidence <= 1:
        parser.error("--min-landmark-confidence 값은 0~1이어야 합니다")
    if args.crop_left is not None and args.crop_left <= 0:
        parser.error("--crop-left는 1 이상의 pixel 너비여야 합니다")
    if args.crop_left is not None and args.video is None:
        parser.error("--crop-left는 --video와 함께 사용해야 합니다")
    return args


def main():
    args = parse_args()
    if not args.model.is_file():
        raise SystemExit(f"Heavy 모델 파일이 없습니다: {args.model}\nREADME의 모델 다운로드 절차를 확인하세요.")
    source_type = "video" if args.video is not None else "webcam"
    if source_type == "video":
        if not args.video.is_file():
            raise SystemExit(f"영상 파일이 없습니다: {args.video}")
        capture = cv2.VideoCapture(str(args.video))
        if not capture.isOpened():
            raise SystemExit(f"영상 파일을 열 수 없습니다: {args.video}")
        source_fps = capture.get(cv2.CAP_PROP_FPS)
        if not math.isfinite(source_fps) or source_fps <= 0:
            capture.release()
            raise SystemExit("영상의 FPS metadata를 읽을 수 없어 timestamp를 만들 수 없습니다.")
        print(f"영상 FPS: {source_fps:.6f} | 파일: {args.video}")
    else:
        capture = cv2.VideoCapture(args.camera, cv2.CAP_DSHOW)
        if not capture.isOpened():
            capture.release()
            capture = cv2.VideoCapture(args.camera)
        if not capture.isOpened():
            raise SystemExit(f"카메라 index {args.camera}를 열 수 없습니다.")
        capture.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        source_fps = None
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    session_id = f"{datetime.now():%Y%m%d_%H%M%S_%f}"
    csv_path = LOG_DIR / f"mediapipe_pose_{session_id}.csv"
    overlay_path = LOG_DIR / f"mediapipe_overlay_{session_id}.mp4"
    options = mp.tasks.vision.PoseLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(args.model)),
        running_mode=mp.tasks.vision.RunningMode.VIDEO,
        num_poses=1,
        output_segmentation_masks=False,
    )
    print(f"Running mode: VIDEO | Model: {args.model} | CSV: {csv_path}")
    frame_id = 0
    pose_marker = ""
    session_event = ""
    show_3d = not args.no_3d
    start = time.perf_counter()
    previous_frame_time = None
    previous_timestamp_ms = -1
    overlay_writer = None
    paused = False
    udp_sender = None
    try:
        if args.udp:
            from pose_udp_sender import PoseUdpSender
            udp_sender = PoseUdpSender(args.udp_host, args.udp_port)
            print(f"RAW UDP: {args.udp_host}:{args.udp_port} | session={udp_sender.session_id}")
        with csv_path.open("w", newline="", encoding="utf-8-sig") as stream, \
                mp.tasks.vision.PoseLandmarker.create_from_options(options) as landmarker:
            writer = csv.DictWriter(stream, fieldnames=FIELDS)
            writer.writeheader()
            while True:
                if paused:
                    key = cv2.waitKey(30) & 0xFF
                    if key in (27, ord("q"), ord("Q")):
                        break
                    if key == ord(" "):
                        paused = False
                        previous_frame_time = None
                        print("재개")
                    continue
                ok, source_frame = capture.read()
                capture_unix_sec = time.time()
                if not ok:
                    if source_type == "video":
                        print(f"영상 종료: {frame_id} frames 처리")
                    else:
                        print("카메라 프레임 읽기에 실패하여 종료합니다.")
                    break
                if frame_id == 0:
                    print(f"실제 입력 해상도: {source_frame.shape[1]}x{source_frame.shape[0]}")
                if args.crop_left is not None:
                    if source_frame.shape[1] < args.crop_left:
                        raise RuntimeError(
                            f"--crop-left {args.crop_left}가 입력 너비 "
                            f"{source_frame.shape[1]}보다 큽니다")
                    frame = source_frame[:, :args.crop_left].copy()
                else:
                    frame = source_frame
                if frame_id == 0 and args.crop_left is not None:
                    print(f"MediaPipe 처리 영역: 왼쪽 {frame.shape[1]}x{frame.shape[0]}")
                capture_time = time.perf_counter()
                timestamp_sec = capture_time - start
                if source_type == "video":
                    video_time_sec = frame_id / source_fps
                    timestamp_ms = max(previous_timestamp_ms + 1,
                                       round(video_time_sec * 1000))
                else:
                    video_time_sec = None
                    timestamp_ms = max(previous_timestamp_ms + 1,
                                       int(timestamp_sec * 1000))
                previous_timestamp_ms = timestamp_ms
                fps = (1 / (capture_time - previous_frame_time)
                       if previous_frame_time and capture_time > previous_frame_time else 0.0)
                previous_frame_time = capture_time
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                mp_image = mp.Image(image_format=mp.ImageFormat.SRGB,
                                    data=np.ascontiguousarray(rgb))
                result = landmarker.detect_for_video(mp_image, timestamp_ms)
                if udp_sender is not None:
                    udp_sender.send(result, frame_id, capture_unix_sec, fps, frame)
                row, image, world = make_row(result, timestamp_sec, frame_id, fps,
                                             pose_marker, session_event,
                                             args.min_landmark_confidence,
                                             source_type=source_type,
                                             source_fps=source_fps,
                                             video_frame_id=(frame_id if source_type == "video"
                                                             else None),
                                             video_time_sec=video_time_sec)
                session_event = ""
                writer.writerow(row)
                stream.flush()
                draw_camera(frame, image, world, row, args.min_landmark_confidence)
                if args.save_overlay:
                    if overlay_writer is None:
                        output_fps = source_fps
                        if output_fps is None:
                            camera_fps = capture.get(cv2.CAP_PROP_FPS)
                            output_fps = (camera_fps if math.isfinite(camera_fps)
                                          and camera_fps > 0 else 30.0)
                        overlay_writer = cv2.VideoWriter(
                            str(overlay_path), cv2.VideoWriter_fourcc(*"mp4v"),
                            output_fps, (frame.shape[1], frame.shape[0]))
                        if not overlay_writer.isOpened():
                            raise RuntimeError(f"overlay MP4를 열 수 없습니다: {overlay_path}")
                        print(f"Overlay MP4: {overlay_path} ({output_fps:.3f} FPS)")
                    overlay_writer.write(frame)
                cv2.imshow("MediaPipe Pose Heavy - Camera", frame)
                if show_3d:
                    cv2.imshow("MediaPipe Pose Heavy - World 3D", draw_world_view(
                        world, args.min_landmark_confidence))
                key = cv2.waitKey(1) & 0xFF
                if key in (27, ord("q"), ord("Q")):
                    break
                if key == ord(" "):
                    paused = True
                    print("일시정지: Space 재개, Q/ESC 종료")
                elif key in POSE_MARKERS:
                    pose_marker = POSE_MARKERS[key]
                    print(f"frame {frame_id + 1}부터 pose_marker={pose_marker}")
                elif key == ord("0"):
                    pose_marker = ""
                elif key in (ord("r"), ord("R")):
                    pose_marker = ""
                    session_event = "RESET"
                    print(f"frame {frame_id + 1}에 RESET 표시")
                elif key in (ord("v"), ord("V")):
                    show_3d = not show_3d
                    if not show_3d:
                        cv2.destroyWindow("MediaPipe Pose Heavy - World 3D")
                frame_id += 1
    finally:
        if udp_sender is not None:
            udp_sender.close()
        capture.release()
        if overlay_writer is not None:
            overlay_writer.release()
        cv2.destroyAllWindows()
        print(f"CSV 저장: {csv_path}")
        if overlay_writer is not None:
            print(f"Overlay 저장: {overlay_path}")


if __name__ == "__main__":
    main()
