import time
import cv2
import numpy as np
from ai_edge_litert.interpreter import Interpreter


MODEL_PATH = r".\models\movenet_lightning_int8_v4.tflite"

CAMERA_INDEX = 0
CAMERA_WIDTH = 1280
CAMERA_HEIGHT = 720
MODEL_SIZE = 192

SCORE_THRESHOLD = 0.25


KEYPOINT_NAMES = [
    "nose",
    "left_eye",
    "right_eye",
    "left_ear",
    "right_ear",
    "left_shoulder",
    "right_shoulder",
    "left_elbow",
    "right_elbow",
    "left_wrist",
    "right_wrist",
    "left_hip",
    "right_hip",
    "left_knee",
    "right_knee",
    "left_ankle",
    "right_ankle",
]

SKELETON = [
    (5, 6),

    (5, 7),
    (7, 9),

    (6, 8),
    (8, 10),

    (5, 11),
    (6, 12),
    (11, 12),

    (11, 13),
    (13, 15),

    (12, 14),
    (14, 16),
]


def resize_with_pad(frame_rgb):
    h, w = frame_rgb.shape[:2]

    scale = min(MODEL_SIZE / w, MODEL_SIZE / h)

    new_w = int(round(w * scale))
    new_h = int(round(h * scale))

    resized = cv2.resize(
        frame_rgb,
        (new_w, new_h),
        interpolation=cv2.INTER_LINEAR
    )

    pad_left = (MODEL_SIZE - new_w) // 2
    pad_right = MODEL_SIZE - new_w - pad_left

    pad_top = (MODEL_SIZE - new_h) // 2
    pad_bottom = MODEL_SIZE - new_h - pad_top

    padded = cv2.copyMakeBorder(
        resized,
        pad_top,
        pad_bottom,
        pad_left,
        pad_right,
        cv2.BORDER_CONSTANT,
        value=(0, 0, 0)
    )

    return padded, scale, pad_left, pad_top


def restore_keypoints(output, scale, pad_left, pad_top, frame_w, frame_h):
    keypoints = []

    raw = output[0, 0]

    for i in range(17):
        y_norm = float(raw[i, 0])
        x_norm = float(raw[i, 1])
        score = float(raw[i, 2])

        model_x = x_norm * MODEL_SIZE
        model_y = y_norm * MODEL_SIZE

        x = (model_x - pad_left) / scale
        y = (model_y - pad_top) / scale

        valid = (
            score >= SCORE_THRESHOLD
            and 0 <= x < frame_w
            and 0 <= y < frame_h
        )

        if valid:
            x = int(round(x))
            y = int(round(y))
        else:
            x = 0
            y = 0

        keypoints.append({
            "index": i,
            "name": KEYPOINT_NAMES[i],
            "x": x,
            "y": y,
            "score": score,
            "valid": valid,
        })

    return keypoints


def draw_pose(frame, keypoints):
    for a, b in SKELETON:
        ka = keypoints[a]
        kb = keypoints[b]

        if ka["valid"] and kb["valid"]:
            cv2.line(
                frame,
                (ka["x"], ka["y"]),
                (kb["x"], kb["y"]),
                (255, 255, 255),
                2
            )

    for kp in keypoints:
        if not kp["valid"]:
            continue

        cv2.circle(
            frame,
            (kp["x"], kp["y"]),
            5,
            (255, 255, 255),
            -1
        )

        if kp["index"] in (5, 6, 7, 8, 9, 10):
            cv2.putText(
                frame,
                str(kp["index"]),
                (kp["x"] + 7, kp["y"] - 7),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                (255, 255, 255),
                1,
                cv2.LINE_AA
            )


def main():
    interpreter = Interpreter(model_path=MODEL_PATH)
    interpreter.allocate_tensors()

    input_info = interpreter.get_input_details()[0]
    output_info = interpreter.get_output_details()[0]

    print("MoveNet loaded")
    print("input :", input_info["shape"], input_info["dtype"])
    print("output:", output_info["shape"], output_info["dtype"])

    cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, CAMERA_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAMERA_HEIGHT)

    if not cap.isOpened():
        raise RuntimeError("Camera open failed")

    frame_count = 0
    fps = 0.0
    fps_start = time.perf_counter()

    while True:
        ok, frame_bgr = cap.read()

        if not ok:
            print("Camera read failed")
            break

        h, w = frame_bgr.shape[:2]

        frame_rgb = cv2.cvtColor(
            frame_bgr,
            cv2.COLOR_BGR2RGB
        )

        input_rgb, scale, pad_left, pad_top = resize_with_pad(frame_rgb)

        input_tensor = np.expand_dims(
            input_rgb.astype(np.uint8),
            axis=0
        )

        interpreter.set_tensor(
            input_info["index"],
            input_tensor
        )

        interpreter.invoke()

        output = interpreter.get_tensor(
            output_info["index"]
        )

        keypoints = restore_keypoints(
            output,
            scale,
            pad_left,
            pad_top,
            w,
            h
        )

        draw_pose(frame_bgr, keypoints)

        frame_count += 1

        now = time.perf_counter()
        elapsed = now - fps_start

        if elapsed >= 1.0:
            fps = frame_count / elapsed
            frame_count = 0
            fps_start = now

        cv2.putText(
            frame_bgr,
            f"FPS: {fps:.1f}",
            (20, 35),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.9,
            (255, 255, 255),
            2,
            cv2.LINE_AA
        )

        cv2.putText(
            frame_bgr,
            "5:L Shoulder  6:R Shoulder  7:L Elbow  8:R Elbow  9:L Wrist  10:R Wrist",
            (20, h - 20),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
            1,
            cv2.LINE_AA
        )

        cv2.imshow(
            "LivePose - Webcam MoveNet Mock",
            frame_bgr
        )

        key = cv2.waitKey(1) & 0xFF

        if key == 27 or key == ord("q"):
            break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
