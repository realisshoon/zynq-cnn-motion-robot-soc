import json
import time
import ctypes
import csv
import socket
from datetime import datetime
from pathlib import Path

import cv2
import numpy as np
from ai_edge_litert.interpreter import Interpreter
from xyz_telemetry_packet import make_xyz_packet


MODEL_PATH = r".\models\movenet_lightning_int8_v4.tflite"
DLL_PATH   = r".\native\pose3d_bridge_v2.dll"

CAMERA_INDEX = 0
CAMERA_WIDTH = 1280
CAMERA_HEIGHT = 720
MODEL_SIZE = 192
SCORE_THRESHOLD = 0.25

UDP_HOST = "127.0.0.1"
UDP_PORT = 5010

# Robot joint command UDP
JOINT_UDP_PORT = 5005



# MoveNet
# 5=L shoulder, 6=R shoulder
# 7=L elbow,    8=R elbow
# 9=L wrist,   10=R wrist
SKELETON = [
    (5, 6),
    (5, 7), (7, 9),
    (6, 8), (8, 10),
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
    pad_top  = (MODEL_SIZE - new_h) // 2

    padded = np.zeros(
        (MODEL_SIZE, MODEL_SIZE, 3),
        dtype=np.uint8
    )

    padded[
        pad_top:pad_top + new_h,
        pad_left:pad_left + new_w
    ] = resized

    return padded, scale, pad_left, pad_top


def restore_keypoints(output, scale, pad_left, pad_top, frame_w, frame_h):
    raw = output[0, 0]
    result = []

    for i in range(17):
        y_norm = float(raw[i, 0])
        x_norm = float(raw[i, 1])
        score  = float(raw[i, 2])

        model_x = x_norm * MODEL_SIZE
        model_y = y_norm * MODEL_SIZE

        x = (model_x - pad_left) / scale
        y = (model_y - pad_top) / scale

        valid = (
            score >= SCORE_THRESHOLD and
            0 <= x < frame_w and
            0 <= y < frame_h
        )

        result.append({
            "x": float(x) if valid else 0.0,
            "y": float(y) if valid else 0.0,
            "score": score,
            "valid": valid,
        })

    return result



def clamp_servo_angle(value):
    return max(20.0, min(160.0, float(value)))


def make_elbow_only_robot_command(frame_id, human_data):
    if human_data is None:
        return None

    human_roll = human_data.get("elbow_roll_deg")
    human_pitch = human_data.get("elbow_pitch_deg")

    if human_roll is None or human_pitch is None:
        return None

    return {
        "frame_id": int(frame_id),

        # Current dev/robot elbow calibration:
        # direction = -1, zero = 90 deg
        "elbowRoll": clamp_servo_angle(
            90.0 - human_roll
        ),

        "elbowPitch": clamp_servo_angle(
            90.0 - human_pitch
        ),

        # No finger landmarks in webcam mock:
        # retain Agent pipeline HOME command.
        "wristPitch": 100.0,
        "wristRoll": 90.0,
        "gripper": 0.05,

        "valid": True,
    }


def load_pose3d_dll():
    dll_path = Path(DLL_PATH).resolve()

    dll = ctypes.CDLL(str(dll_path))

    dll.pose3d_reset.argtypes = []
    dll.pose3d_reset.restype = None

    dll.pose3d_update.argtypes = [
        ctypes.c_int,
        ctypes.c_uint32,
        ctypes.c_float,

        ctypes.c_float,
        ctypes.c_float,
        ctypes.c_uint8,

        ctypes.c_float,
        ctypes.c_float,
        ctypes.c_uint8,

        ctypes.c_float,
        ctypes.c_float,
        ctypes.c_uint8,

        ctypes.c_float,
        ctypes.c_float,
        ctypes.c_uint8,

        ctypes.POINTER(ctypes.c_float),
    ]

    dll.pose3d_update.restype = ctypes.c_int

    dll.pose3d_get_elbow_angles.argtypes = [
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_uint8),
    ]

    dll.pose3d_get_elbow_angles.restype = ctypes.c_int

    dll.pose3d_reset()

    print("Robot C DLL loaded:", dll_path)

    return dll


def call_pose3d(dll, side, frame_id, dt, keypoints):
    shoulder_l = keypoints[5]
    shoulder_r = keypoints[6]

    if side == 0:
        # Human LEFT
        elbow = keypoints[7]
        wrist = keypoints[9]
    else:
        # Human RIGHT
        elbow = keypoints[8]
        wrist = keypoints[10]

    out = (ctypes.c_float * 12)()

    ret = dll.pose3d_update(
        side,
        frame_id,
        dt,

        shoulder_l["x"],
        shoulder_l["y"],
        int(shoulder_l["valid"]),

        shoulder_r["x"],
        shoulder_r["y"],
        int(shoulder_r["valid"]),

        elbow["x"],
        elbow["y"],
        int(elbow["valid"]),

        wrist["x"],
        wrist["y"],
        int(wrist["valid"]),

        out
    )

    if ret != 1:
        return None

    values = list(out)

    angles = (ctypes.c_float * 2)()
    roll_observable = ctypes.c_uint8(0)

    angle_ret = dll.pose3d_get_elbow_angles(
        side,
        angles,
        ctypes.byref(roll_observable)
    )

    if angle_ret >= 0:
        elbow_roll_deg = float(angles[0])
        elbow_pitch_deg = float(angles[1])
        elbow_roll_observable = bool(roll_observable.value)
    else:
        elbow_roll_deg = None
        elbow_pitch_deg = None
        elbow_roll_observable = False

    return {
        "shoulder_l": tuple(values[0:3]),
        "shoulder_r": tuple(values[3:6]),
        "elbow":      tuple(values[6:9]),
        "wrist":      tuple(values[9:12]),

        "elbow_roll_deg": elbow_roll_deg,
        "elbow_pitch_deg": elbow_pitch_deg,
        "elbow_roll_observable": elbow_roll_observable,
    }


def draw_pose(frame, keypoints):
    for a, b in SKELETON:
        ka = keypoints[a]
        kb = keypoints[b]

        if ka["valid"] and kb["valid"]:
            cv2.line(
                frame,
                (int(ka["x"]), int(ka["y"])),
                (int(kb["x"]), int(kb["y"])),
                (255, 255, 255),
                2
            )

    for idx in (5, 6, 7, 8, 9, 10):
        kp = keypoints[idx]

        if not kp["valid"]:
            continue

        x = int(kp["x"])
        y = int(kp["y"])

        cv2.circle(
            frame,
            (x, y),
            5,
            (255, 255, 255),
            -1
        )

        cv2.putText(
            frame,
            str(idx),
            (x + 7, y - 7),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (255, 255, 255),
            1,
            cv2.LINE_AA
        )


def xyz_text(p):
    if p is None:
        return "INVALID"

    return f"x={p[0]:+.3f} y={p[1]:+.3f} z={p[2]:+.3f}"


def main():
    # ---------------------------------------------------------
    # MoveNet
    # ---------------------------------------------------------
    interpreter = Interpreter(model_path=MODEL_PATH)
    interpreter.allocate_tensors()

    input_info = interpreter.get_input_details()[0]
    output_info = interpreter.get_output_details()[0]

    # ---------------------------------------------------------
    # Actual dev/robot C reconstruction DLL
    # ---------------------------------------------------------
    dll = load_pose3d_dll()

    # ---------------------------------------------------------
    # XYZ telemetry transport
    # Current PC mock: UDP localhost
    # Final hardware: same packet bytes -> UART
    # ---------------------------------------------------------
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(f"XYZ UDP -> {UDP_HOST}:{UDP_PORT}")

    # ---------------------------------------------------------
    # Camera
    # ---------------------------------------------------------
    cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, CAMERA_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAMERA_HEIGHT)

    if not cap.isOpened():
        raise RuntimeError("Camera open failed")

    frame_id = 0
    last_time = time.perf_counter()

    fps_count = 0
    fps_start = last_time
    fps = 0.0

    last_left = None
    last_right = None

    log_dir = Path(r".\logs")
    log_dir.mkdir(parents=True, exist_ok=True)

    log_path = log_dir / (
        "live_pose3d_" +
        datetime.now().strftime("%Y%m%d_%H%M%S") +
        ".csv"
    )

    log_file = log_path.open(
        "w",
        newline="",
        encoding="utf-8"
    )

    writer = csv.writer(log_file)

    writer.writerow([
        "frame_id",
        "timestamp_sec",

        "left_fresh",
        "left_elbow_x",
        "left_elbow_y",
        "left_elbow_z",
        "left_wrist_x",
        "left_wrist_y",
        "left_wrist_z",

        "right_fresh",
        "right_elbow_x",
        "right_elbow_y",
        "right_elbow_z",
        "right_wrist_x",
        "right_wrist_y",
        "right_wrist_z",
    ])

    session_start = time.perf_counter()

    print("CSV log:", log_path.resolve())

    while True:
        ok, frame_bgr = cap.read()

        if not ok:
            break

        # Mirror correction:
        # apply before MoveNet / Robot C / visualization so all stages
        # use the same corrected camera coordinate system.
        frame_bgr = cv2.flip(frame_bgr, 1)

        now = time.perf_counter()
        dt = now - last_time
        last_time = now

        frame_id += 1

        h, w = frame_bgr.shape[:2]

        # -----------------------------------------------------
        # Webcam -> MoveNet
        # -----------------------------------------------------
        rgb = cv2.cvtColor(
            frame_bgr,
            cv2.COLOR_BGR2RGB
        )

        model_rgb, scale, pad_left, pad_top = resize_with_pad(rgb)

        inp = np.expand_dims(
            model_rgb.astype(np.uint8),
            axis=0
        )

        interpreter.set_tensor(
            input_info["index"],
            inp
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

        # -----------------------------------------------------
        # 2D -> actual Robot C 3D reconstruction
        # -----------------------------------------------------
        left_xyz = call_pose3d(
            dll,
            0,
            frame_id,
            dt,
            keypoints
        )

        right_xyz = call_pose3d(
            dll,
            1,
            frame_id,
            dt,
            keypoints
        )

        if left_xyz is not None:
            last_left = left_xyz

        if right_xyz is not None:
            last_right = right_xyz

        # -----------------------------------------------------
        # XYZ binary telemetry
        #
        # last_left/right:
        #   last successful Robot-C reconstruction = VALID
        #
        # left_xyz/right_xyz:
        #   successful on THIS frame = FRESH
        # -----------------------------------------------------
        packet = make_xyz_packet(
            frame_id=frame_id,

            left_fresh=(left_xyz is not None),
            right_fresh=(right_xyz is not None),

            left_data=last_left,
            right_data=last_right,
        )

        udp.sendto(
            packet,
            (UDP_HOST, UDP_PORT)
        )

        # -----------------------------------------------------
        # C elbow angles -> Robot joint UDP
        #
        # Mapping contract:
        # Human RIGHT -> Unity RobotArm_L
        # Human LEFT  -> Unity RobotArm_R
        #
        # IMPORTANT:
        # No XYZ -> angle calculation occurs in Unity anymore.
        # -----------------------------------------------------
        robot_left_cmd = make_elbow_only_robot_command(
            frame_id,
            last_right
        )

        robot_right_cmd = make_elbow_only_robot_command(
            frame_id,
            last_left
        )

        # Dual receiver uses atomic L/R commands.
        # Start transmission only after both C contexts have
        # produced at least one valid angle.
        if (
            robot_left_cmd is not None and
            robot_right_cmd is not None
        ):
            joint_packet = {
                "frame_id": int(frame_id),
                "source_frame_id": int(frame_id),

                # Human RIGHT -> RobotArm_L
                "left": robot_left_cmd,

                # Human LEFT -> RobotArm_R
                "right": robot_right_cmd,
            }

            joint_payload = json.dumps(
                joint_packet,
                separators=(",", ":")
            ).encode("utf-8")

            udp.sendto(
                joint_payload,
                (UDP_HOST, JOINT_UDP_PORT)
            )

            if frame_id % 15 == 0:
                print(
                    f"[JOINT {frame_id}] "
                    f"L: roll={robot_left_cmd['elbowRoll']:.1f}, "
                    f"pitch={robot_left_cmd['elbowPitch']:.1f} | "
                    f"R: roll={robot_right_cmd['elbowRoll']:.1f}, "
                    f"pitch={robot_right_cmd['elbowPitch']:.1f}"
                )

        # -----------------------------------------------------
        # CSV telemetry log
        # FRESH=1: this frame reconstructed successfully
        # FRESH=0: XYZ columns contain the last successful value (HOLD)
        # -----------------------------------------------------
        timestamp_sec = now - session_start

        le = None if last_left is None else last_left["elbow"]
        lw = None if last_left is None else last_left["wrist"]
        re = None if last_right is None else last_right["elbow"]
        rw = None if last_right is None else last_right["wrist"]

        def xyz_or_empty(p):
            if p is None:
                return ("", "", "")
            return (
                f"{p[0]:.6f}",
                f"{p[1]:.6f}",
                f"{p[2]:.6f}",
            )

        le_x, le_y, le_z = xyz_or_empty(le)
        lw_x, lw_y, lw_z = xyz_or_empty(lw)
        re_x, re_y, re_z = xyz_or_empty(re)
        rw_x, rw_y, rw_z = xyz_or_empty(rw)

        writer.writerow([
            frame_id,
            f"{timestamp_sec:.6f}",

            1 if left_xyz is not None else 0,
            le_x, le_y, le_z,
            lw_x, lw_y, lw_z,

            1 if right_xyz is not None else 0,
            re_x, re_y, re_z,
            rw_x, rw_y, rw_z,
        ])

        log_file.flush()

        # -----------------------------------------------------
        # Visual
        # -----------------------------------------------------
        draw_pose(frame_bgr, keypoints)

        fps_count += 1
        elapsed = now - fps_start

        if elapsed >= 1.0:
            fps = fps_count / elapsed
            fps_count = 0
            fps_start = now

        cv2.putText(
            frame_bgr,
            f"FPS {fps:.1f}",
            (20, 30),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (255, 255, 255),
            2
        )

        y0 = 60

        cv2.putText(
            frame_bgr,
            "Robot C reconstruction - relative XYZ",
            (20, y0),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (255, 255, 255),
            1
        )

        left_status = "FRESH" if left_xyz is not None else "HOLD"
        right_status = "FRESH" if right_xyz is not None else "HOLD"

        cv2.putText(
            frame_bgr,
            f"LEFT  [{left_status}]",
            (20, y0 + 30),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (255, 255, 255),
            1
        )

        cv2.putText(
            frame_bgr,
            " elbow " + xyz_text(
                None if last_left is None else last_left["elbow"]
            ),
            (20, y0 + 55),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
            1
        )

        cv2.putText(
            frame_bgr,
            " wrist " + xyz_text(
                None if last_left is None else last_left["wrist"]
            ),
            (20, y0 + 78),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
            1
        )

        cv2.putText(
            frame_bgr,
            f"RIGHT [{right_status}]",
            (20, y0 + 110),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (255, 255, 255),
            1
        )

        cv2.putText(
            frame_bgr,
            " elbow " + xyz_text(
                None if last_right is None else last_right["elbow"]
            ),
            (20, y0 + 135),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
            1
        )

        cv2.putText(
            frame_bgr,
            " wrist " + xyz_text(
                None if last_right is None else last_right["wrist"]
            ),
            (20, y0 + 158),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
            1
        )

        # -----------------------------------------------------
        # Actual Robot-C elbow angles
        # -----------------------------------------------------
        if last_left is not None and last_left.get("elbow_roll_deg") is not None:
            left_angle_text = (
                f"HUMAN LEFT  C: "
                f"Roll={last_left['elbow_roll_deg']:+7.2f}  "
                f"Pitch={last_left['elbow_pitch_deg']:+7.2f}  "
                f"RollObs={'YES' if last_left['elbow_roll_observable'] else 'NO'}"
            )
        else:
            left_angle_text = "HUMAN LEFT  C: ---"

        if last_right is not None and last_right.get("elbow_roll_deg") is not None:
            right_angle_text = (
                f"HUMAN RIGHT C: "
                f"Roll={last_right['elbow_roll_deg']:+7.2f}  "
                f"Pitch={last_right['elbow_pitch_deg']:+7.2f}  "
                f"RollObs={'YES' if last_right['elbow_roll_observable'] else 'NO'}"
            )
        else:
            right_angle_text = "HUMAN RIGHT C: ---"

        cv2.putText(
            frame_bgr,
            left_angle_text,
            (620, 35),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.52,
            (255, 255, 255),
            1
        )

        cv2.putText(
            frame_bgr,
            right_angle_text,
            (620, 62),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.52,
            (255, 255, 255),
            1
        )

        cv2.imshow(
            "LivePose - MoveNet -> Robot C XYZ + C Angles",
            frame_bgr
        )

        key = cv2.waitKey(1) & 0xFF

        if key == ord("q") or key == 27:
            break

    log_file.close()
    udp.close()
    cap.release()
    cv2.destroyAllWindows()

    print("CSV saved:", log_path.resolve())


if __name__ == "__main__":
    main()

