#!/usr/bin/env python3
"""
실제 사람 영상 + Agent1 결과 overlay v2

변경점:
- 영상 위를 크게 가리지 않도록 오른쪽에 별도 정보 패널을 붙인다.
- 원본 영상 영역은 그대로 유지한다.
- 패널에는 Z / 관절각 / valid 상태를 표시한다.
"""

import argparse
import csv
import cv2
import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", default="real_person_cnn_overlay.mp4")
    ap.add_argument("--result", default="agent1_real_result.csv")
    ap.add_argument("--output", default="real_person_agent1_overlay_v2.mp4")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--panel-width", type=int, default=420)
    args = ap.parse_args()

    with open(args.result, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    if not rows:
        raise SystemExit("결과 CSV가 비었습니다.")

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        raise SystemExit(f"영상 열기 실패: {args.video}")

    fps = cap.get(cv2.CAP_PROP_FPS)
    if not fps or fps <= 0:
        fps = 15.0

    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    panel_w = max(320, args.panel_width)
    out_w = w + panel_w

    writer = cv2.VideoWriter(
        args.output,
        cv2.VideoWriter_fourcc(*"mp4v"),
        fps,
        (out_w, h),
    )

    def fval(row, key, default=0.0):
        try:
            return float(row[key])
        except Exception:
            return default

    def sval(row, key, default="0"):
        try:
            return str(row[key])
        except Exception:
            return default

    idx = 0

    while True:
        ok, frame = cap.read()
        if not ok or idx >= len(rows):
            break

        r = rows[idx]
        idx += 1

        # 원본 프레임 + 오른쪽 검정 패널
        canvas = np.zeros((h, out_w, 3), dtype=np.uint8)
        canvas[:, :w] = frame

        # 패널 구분선
        cv2.line(canvas, (w, 0), (w, h), (90, 90, 90), 1)

        x0 = w + 18
        y = 35
        line_gap = 28

        valid = sval(r, "target_valid", "0")

        # 제목
        cv2.putText(
            canvas,
            "Agent1 Result",
            (x0, y),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.72,
            (255, 255, 255),
            2,
            cv2.LINE_AA,
        )
        y += 42

        # frame 정보
        info_lines = [
            f"frame : {sval(r, 'frame_id', str(idx))}",
            f"valid : {valid}",
            "",
            "[Relative Z]",
            f"shoulder L : {fval(r, 'shoulder_l_z'): .3f}",
            f"shoulder R : {fval(r, 'shoulder_r_z'): .3f}",
            f"elbow      : {fval(r, 'elbow_z'): .3f}",
            f"wrist      : {fval(r, 'wrist_z'): .3f}",
            f"finger1    : {fval(r, 'finger1_z'): .3f}",
            f"finger2    : {fval(r, 'finger2_z'): .3f}",
            "",
            "[Joint Target]",
            f"base       : {fval(r, 'base_deg'): .1f} deg",
            f"shoulder   : {fval(r, 'shoulder_deg'): .1f} deg",
            f"elbow      : {fval(r, 'elbow_deg'): .1f} deg",
            f"wrist pitch: {fval(r, 'wrist_pitch_deg'): .1f} deg",
            f"wrist roll : {fval(r, 'wrist_roll_deg'): .1f} deg",
            f"gripper    : {fval(r, 'gripper_norm'): .0f}",
        ]

        for line in info_lines:
            if line == "":
                y += 10
                continue

            if line.startswith("["):
                color = (180, 220, 255)
                scale = 0.58
                thickness = 2
            elif line.startswith("valid"):
                color = (80, 220, 80) if valid == "1" else (80, 80, 255)
                scale = 0.55
                thickness = 1
            else:
                color = (235, 235, 235)
                scale = 0.55
                thickness = 1

            cv2.putText(
                canvas,
                line,
                (x0, y),
                cv2.FONT_HERSHEY_SIMPLEX,
                scale,
                color,
                thickness,
                cv2.LINE_AA,
            )
            y += line_gap

        writer.write(canvas)

        if args.show:
            # 큰 영상이면 화면 크기에 맞게 축소해서 표시
            preview = canvas
            max_w = 1600
            if preview.shape[1] > max_w:
                scale = max_w / preview.shape[1]
                preview = cv2.resize(
                    preview,
                    None,
                    fx=scale,
                    fy=scale,
                    interpolation=cv2.INTER_AREA,
                )

            cv2.imshow("Agent1 real person visualization", preview)
            if cv2.waitKey(1) & 0xFF == 27:
                break

    cap.release()
    writer.release()
    cv2.destroyAllWindows()

    print(f"[OK] output : {args.output}")
    print(f"[INFO] frames: {idx}")


if __name__ == "__main__":
    main()
