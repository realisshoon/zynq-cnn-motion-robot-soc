#!/usr/bin/env python3
"""
실제 사람 영상 -> CNN 대체 2D landmark 추출 + overlay + CSV 저장 (v3)

중요 변경:
- 입력 영상을 Agent1 카메라 좌표계와 맞추기 위해 기본 640x480으로 resize한 뒤
  MediaPipe를 수행한다.
- 따라서 출력 pixel 좌표도 640x480 기준이다.
- PM_CAMERA_FX/FY/CX/CY 및 PM_MIN_SHOULDER_WIDTH_PX 같은 Agent1 설정과
  테스트 좌표계가 일치하도록 하기 위한 버전이다.
"""

import argparse
import csv
import cv2

try:
    import mediapipe as mp
except ImportError:
    raise SystemExit(
        "mediapipe가 없습니다.\n"
        "예: uv pip install --python .venv_pose/bin/python "
        "mediapipe==0.10.14 opencv-python matplotlib"
    )

mp_pose = mp.solutions.pose


def valid_lm(lm, min_visibility):
    return lm is not None and getattr(lm, "visibility", 1.0) >= min_visibility


def px(lm, w, h):
    return float(lm.x * w), float(lm.y * h)


def draw_point(frame, name, xy, valid, color):
    if not valid:
        return
    x, y = int(round(xy[0])), int(round(xy[1]))
    cv2.circle(frame, (x, y), 5, color, -1, lineType=cv2.LINE_AA)
    cv2.putText(
        frame,
        f"{name} ({x},{y})",
        (x + 7, y - 7),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.45,
        color,
        1,
        cv2.LINE_AA,
    )


def draw_line(frame, a, b, va, vb, color):
    if va and vb:
        cv2.line(
            frame,
            (int(round(a[0])), int(round(a[1]))),
            (int(round(b[0])), int(round(b[1]))),
            color,
            2,
            cv2.LINE_AA,
        )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--side", choices=["left", "right"], default="right")
    ap.add_argument("--target-fps", type=float, default=15.0)
    ap.add_argument("--visibility", type=float, default=0.25)
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--output-video", default="real_person_cnn_overlay.mp4")
    ap.add_argument("--output-csv", default="real_person_pose2d.csv")
    ap.add_argument("--show", action="store_true")
    args = ap.parse_args()

    cap = cv2.VideoCapture(args.input)
    if not cap.isOpened():
        raise SystemExit(f"영상 열기 실패: {args.input}")

    src_fps = cap.get(cv2.CAP_PROP_FPS)
    if not src_fps or src_fps <= 0:
        src_fps = 30.0

    out_w = args.width
    out_h = args.height

    writer = cv2.VideoWriter(
        args.output_video,
        cv2.VideoWriter_fourcc(*"mp4v"),
        args.target_fps,
        (out_w, out_h),
    )

    sample_period = 1.0 / max(args.target_fps, 1.0)
    next_sample_time = 0.0

    if args.side == "right":
        elbow_id = mp_pose.PoseLandmark.RIGHT_ELBOW.value
        wrist_id = mp_pose.PoseLandmark.RIGHT_WRIST.value
        index_id = mp_pose.PoseLandmark.RIGHT_INDEX.value
        thumb_id = mp_pose.PoseLandmark.RIGHT_THUMB.value
    else:
        elbow_id = mp_pose.PoseLandmark.LEFT_ELBOW.value
        wrist_id = mp_pose.PoseLandmark.LEFT_WRIST.value
        index_id = mp_pose.PoseLandmark.LEFT_INDEX.value
        thumb_id = mp_pose.PoseLandmark.LEFT_THUMB.value

    sl_id = mp_pose.PoseLandmark.LEFT_SHOULDER.value
    sr_id = mp_pose.PoseLandmark.RIGHT_SHOULDER.value

    fields = [
        "frame_id", "time_sec", "frame_valid",
        "shoulder_l_x", "shoulder_l_y", "shoulder_l_valid",
        "shoulder_r_x", "shoulder_r_y", "shoulder_r_valid",
        "elbow_x", "elbow_y", "elbow_valid",
        "wrist_x", "wrist_y", "wrist_valid",
        "finger1_x", "finger1_y", "finger1_valid",
        "finger2_x", "finger2_y", "finger2_valid",
    ]

    csvf = open(args.output_csv, "w", newline="", encoding="utf-8")
    cw = csv.DictWriter(csvf, fieldnames=fields)
    cw.writeheader()

    pose_model = mp_pose.Pose(
        static_image_mode=False,
        model_complexity=1,
        smooth_landmarks=True,
        enable_segmentation=False,
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5,
    )

    src_idx = 0
    out_frame_id = 0
    last_sample_time = None

    while True:
        ok, frame = cap.read()
        if not ok:
            break

        t = src_idx / src_fps
        src_idx += 1

        if t + 1e-9 < next_sample_time:
            continue
        next_sample_time += sample_period
        out_frame_id += 1

        # 핵심: Agent1의 기본 카메라 좌표계와 동일한 640x480 기준으로 맞춘다.
        frame = cv2.resize(frame, (out_w, out_h), interpolation=cv2.INTER_LINEAR)

        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        result = pose_model.process(rgb)

        data = {
            "frame_id": out_frame_id,
            "time_sec": f"{t:.6f}",
            "frame_valid": 0,
        }

        names = ["shoulder_l", "shoulder_r", "elbow", "wrist", "finger1", "finger2"]
        for n in names:
            data[f"{n}_x"] = 0.0
            data[f"{n}_y"] = 0.0
            data[f"{n}_valid"] = 0

        overlay = frame.copy()

        if result.pose_landmarks:
            data["frame_valid"] = 1
            lms = result.pose_landmarks.landmark

            mapping = {
                "shoulder_l": lms[sl_id],
                "shoulder_r": lms[sr_id],
                "elbow": lms[elbow_id],
                "wrist": lms[wrist_id],
                "finger1": lms[index_id],
                "finger2": lms[thumb_id],
            }

            pts = {}
            vals = {}

            for name, lm in mapping.items():
                v = valid_lm(lm, args.visibility)
                xy = px(lm, out_w, out_h)
                pts[name] = xy
                vals[name] = v

                data[f"{name}_x"] = f"{xy[0]:.3f}"
                data[f"{name}_y"] = f"{xy[1]:.3f}"
                data[f"{name}_valid"] = 1 if v else 0

            draw_line(
                overlay, pts["shoulder_l"], pts["shoulder_r"],
                vals["shoulder_l"], vals["shoulder_r"], (255, 180, 0)
            )

            active_shoulder_name = "shoulder_r" if args.side == "right" else "shoulder_l"

            draw_line(
                overlay, pts[active_shoulder_name], pts["elbow"],
                vals[active_shoulder_name], vals["elbow"], (0, 255, 0)
            )
            draw_line(
                overlay, pts["elbow"], pts["wrist"],
                vals["elbow"], vals["wrist"], (0, 255, 0)
            )
            draw_line(
                overlay, pts["wrist"], pts["finger1"],
                vals["wrist"], vals["finger1"], (0, 220, 255)
            )
            draw_line(
                overlay, pts["wrist"], pts["finger2"],
                vals["wrist"], vals["finger2"], (0, 220, 255)
            )

            colors = {
                "shoulder_l": (255, 180, 0),
                "shoulder_r": (255, 180, 0),
                "elbow": (0, 255, 0),
                "wrist": (0, 255, 0),
                "finger1": (0, 220, 255),
                "finger2": (0, 220, 255),
            }

            for n in names:
                draw_point(overlay, n, pts[n], vals[n], colors[n])

            if vals["shoulder_l"] and vals["shoulder_r"]:
                dx = pts["shoulder_l"][0] - pts["shoulder_r"][0]
                dy = pts["shoulder_l"][1] - pts["shoulder_r"][1]
                shoulder_px = (dx * dx + dy * dy) ** 0.5
                cv2.putText(
                    overlay,
                    f"shoulder width = {shoulder_px:.1f}px",
                    (12, 75),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.52,
                    (255, 255, 255),
                    1,
                    cv2.LINE_AA,
                )

        dt = 0.0 if last_sample_time is None else t - last_sample_time
        last_sample_time = t

        cv2.rectangle(overlay, (5, 5), (330, 58), (0, 0, 0), -1)
        cv2.putText(
            overlay,
            f"Pose input: {args.target_fps:.1f} Hz  frame={out_frame_id}",
            (12, 25),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.50,
            (255, 255, 255),
            1,
            cv2.LINE_AA,
        )
        cv2.putText(
            overlay,
            f"{out_w}x{out_h}  side={args.side}  dt={dt*1000.0:.1f} ms",
            (12, 48),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.50,
            (255, 255, 255),
            1,
            cv2.LINE_AA,
        )

        cw.writerow(data)
        writer.write(overlay)

        if args.show:
            cv2.imshow("real person pose input", overlay)
            if cv2.waitKey(1) & 0xFF == 27:
                break

    pose_model.close()
    cap.release()
    writer.release()
    csvf.close()
    cv2.destroyAllWindows()

    print(f"[OK] overlay video : {args.output_video}")
    print(f"[OK] pose2d csv    : {args.output_csv}")
    print(f"[INFO] frames       : {out_frame_id}")
    print(f"[INFO] coord system : {out_w}x{out_h}")


if __name__ == "__main__":
    main()
