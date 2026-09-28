#!/usr/bin/env python3
"""
실제 사람 영상 -> CNN 대체 2D landmark 추출 + overlay + CSV 저장 (v3)

손가락 필드: finger1=엄지 끝, finger2=검지 끝.
기본적으로 원본 영상의 손목 주변 crop에 Hand 모델을 적용해 실제 fingertip을
찾는다. 손을 찾지 못하면 손가락을 invalid로 남겨 Agent1이 이전 값을 HOLD한다.

Hands 모드에서는 원본 영상 비율로 추론하고 출력 좌표만 --width/--height로
변환한다. --finger-source pose를 지정하면 이전 Pose 손 점을 사용할 수 있다.
손끝 모델이 손을 찾지 못한 프레임은 손가락을 invalid로 기록한다.
"""

import argparse
import csv
import math
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
mp_hands = mp.solutions.hands


def valid_lm(lm, min_visibility):
    return (lm is not None and math.isfinite(lm.x) and math.isfinite(lm.y)
            and math.isfinite(getattr(lm, "visibility", 1.0))
            and getattr(lm, "visibility", 1.0) >= min_visibility)


def px(lm, w, h):
    return float(lm.x * w), float(lm.y * h)


def detect_hand_tips(source_frame, pose_lms, wrist_id, elbow_id,
                     shoulder_id, opposite_shoulder_id, hands,
                     min_visibility):
    """Find thumb/index tips near the active Pose wrist, in source pixels."""
    h, w = source_frame.shape[:2]
    wrist = pose_lms[wrist_id]
    if not valid_lm(wrist, min_visibility):
        return None
    wx, wy = px(wrist, w, h)
    elbow = pose_lms[elbow_id]
    ex, ey = px(elbow, w, h) if valid_lm(elbow, min_visibility) else (wx, wy)
    arm_len = math.hypot(wx - ex, wy - ey)
    left_shoulder = pose_lms[shoulder_id]
    right_shoulder = pose_lms[opposite_shoulder_id]
    shoulder_len = 0.0
    if valid_lm(left_shoulder, min_visibility) and valid_lm(right_shoulder, min_visibility):
        sx, sy = px(left_shoulder, w, h)
        ox, oy = px(right_shoulder, w, h)
        shoulder_len = math.hypot(sx - ox, sy - oy)

    # A square around the wrist enlarges a small hand for the hand model.
    # Offset slightly past the wrist, while leaving room for a folded hand.
    side = max(1.8 * arm_len, 0.7 * shoulder_len, 0.08 * min(w, h))
    cx, cy = wx + 0.2 * (wx - ex), wy + 0.2 * (wy - ey)
    x0, y0 = max(0, int(cx - side / 2)), max(0, int(cy - side / 2))
    x1, y1 = min(w, int(cx + side / 2)), min(h, int(cy + side / 2))
    if x1 - x0 < 20 or y1 - y0 < 20:
        return None
    crop = source_frame[y0:y1, x0:x1]
    result = hands.process(cv2.cvtColor(crop, cv2.COLOR_BGR2RGB))
    if not result.multi_hand_landmarks:
        return None

    # Hands handedness assumes a mirrored selfie image. Instead, associate
    # detections with the requested arm by proximity to its Pose wrist.
    candidates = []
    for hand in result.multi_hand_landmarks:
        lms = hand.landmark
        hand_wrist = lms[mp_hands.HandLandmark.WRIST.value]
        if not (math.isfinite(hand_wrist.x) and math.isfinite(hand_wrist.y)):
            continue
        hx = x0 + hand_wrist.x * (x1 - x0)
        hy = y0 + hand_wrist.y * (y1 - y0)
        distance = math.hypot(hx - wx, hy - wy)
        candidates.append((distance, lms))
    if not candidates:
        return None
    distance, hand_lms = min(candidates, key=lambda item: item[0])
    wrist_limit = min(0.35 * side,
                      max(0.5 * arm_len, 0.15 * shoulder_len, 0.025 * min(w, h)))
    if distance > wrist_limit:
        return None

    tips = []
    for landmark in (mp_hands.HandLandmark.THUMB_TIP,
                     mp_hands.HandLandmark.INDEX_FINGER_TIP):
        tip = hand_lms[landmark.value]
        if not (math.isfinite(tip.x) and math.isfinite(tip.y) and
                0.0 <= tip.x <= 1.0 and 0.0 <= tip.y <= 1.0):
            return None
        tips.append((x0 + tip.x * (x1 - x0), y0 + tip.y * (y1 - y0)))
    return tips


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
    ap.add_argument("--finger-source", choices=("hands", "pose"), default="hands",
                    help="Hands fingertip detector (default) or legacy Pose hand points")
    ap.add_argument("--model-complexity", type=int, choices=(0, 1, 2), default=1,
                    help="MediaPipe Pose model complexity (default: 1)")
    ap.add_argument("--infer-at-source-size", action="store_true",
                    help="Run Pose on the original video frame, but keep output coordinates at --width/--height")
    ap.add_argument("--visibility", type=float, default=0.25)
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--output-video", default="hands_pose_overlay.mp4")
    ap.add_argument("--output-csv", default="hands_pose2d.csv")
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
        model_complexity=args.model_complexity,
        smooth_landmarks=True,
        enable_segmentation=False,
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5,
    )
    hand_model = (mp_hands.Hands(static_image_mode=True, max_num_hands=2,
                                model_complexity=1, min_detection_confidence=0.45)
                  if args.finger_source == "hands" else None)

    src_idx = 0
    out_frame_id = 0
    last_sample_time = None
    hand_detected_frames = 0

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

        # Pose coordinates are normalized, so inference may use the source
        # frame while the CSV/overlay remain in the Agent1 output coordinate system.
        source_frame = frame
        frame = cv2.resize(frame, (out_w, out_h), interpolation=cv2.INTER_LINEAR)
        inference_frame = (source_frame if args.infer_at_source_size or hand_model
                           else frame)

        rgb = cv2.cvtColor(inference_frame, cv2.COLOR_BGR2RGB)
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
            }
            if hand_model is None:
                mapping["finger1"] = lms[thumb_id]
                mapping["finger2"] = lms[index_id]

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

            if hand_model is not None:
                pts["finger1"] = (0.0, 0.0)
                pts["finger2"] = (0.0, 0.0)
                vals["finger1"] = vals["finger2"] = False
                tips = detect_hand_tips(
                    source_frame, lms, wrist_id, elbow_id,
                    sr_id if args.side == "right" else sl_id,
                    sl_id if args.side == "right" else sr_id,
                    hand_model, args.visibility,
                )
                if tips is not None:
                    source_h, source_w = source_frame.shape[:2]
                    for name, xy in zip(("finger1", "finger2"), tips):
                        mapped = (xy[0] * out_w / source_w,
                                  xy[1] * out_h / source_h)
                        pts[name] = mapped
                        vals[name] = True
                        data[f"{name}_x"] = f"{mapped[0]:.3f}"
                        data[f"{name}_y"] = f"{mapped[1]:.3f}"
                        data[f"{name}_valid"] = 1
                    hand_detected_frames += 1

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
                    (12, 97),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.52,
                    (255, 255, 255),
                    1,
                    cv2.LINE_AA,
                )

        dt = 0.0 if last_sample_time is None else t - last_sample_time
        last_sample_time = t

        cv2.rectangle(overlay, (5, 5), (390, 80), (0, 0, 0), -1)
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
        cv2.putText(
            overlay,
            ("fingers=Hands tips " + ("OK" if data["finger1_valid"] else "missing"))
            if hand_model else "fingers=Pose points (legacy)",
            (12, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.50,
            (0, 220, 255), 1, cv2.LINE_AA,
        )

        cw.writerow(data)
        writer.write(overlay)

        if args.show:
            cv2.imshow("real person pose input", overlay)
            if cv2.waitKey(1) & 0xFF == 27:
                break

    pose_model.close()
    if hand_model is not None:
        hand_model.close()
    cap.release()
    writer.release()
    csvf.close()
    cv2.destroyAllWindows()

    print(f"[OK] overlay video : {args.output_video}")
    print(f"[OK] pose2d csv    : {args.output_csv}")
    print(f"[INFO] frames       : {out_frame_id}")
    print(f"[INFO] coord system : {out_w}x{out_h}")
    if hand_model is not None:
        print(f"[INFO] hand tips    : {hand_detected_frames}/{out_frame_id} frames; "
              "missing frames have finger1/2_valid=0")


if __name__ == "__main__":
    main()
