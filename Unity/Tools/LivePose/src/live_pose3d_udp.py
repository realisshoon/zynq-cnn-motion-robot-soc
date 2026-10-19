from json import encoder
import pathlib
import json
import threading
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

DLL_PATH   = r".\native\robot_git_bridge.dll"



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









ROBOT_DLL_LOCK = threading.Lock()





def load_robot_git_dll():

    dll_path = Path(DLL_PATH).resolve()



    dll = ctypes.CDLL(str(dll_path))



    # --------------------------------------------------------

    # reset

    # --------------------------------------------------------

    dll.robot_git_reset.argtypes = []

    dll.robot_git_reset.restype = ctypes.c_int



    # --------------------------------------------------------

    # update

    # --------------------------------------------------------

    dll.robot_git_update.argtypes = [

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

        ctypes.POINTER(ctypes.c_float),

        ctypes.POINTER(ctypes.c_float),

        ctypes.POINTER(ctypes.c_uint8),

        ctypes.POINTER(ctypes.c_uint32),

    ]



    dll.robot_git_update.restype = ctypes.c_int



    # --------------------------------------------------------

    # fixed 20 ms Agent2 tick

    # --------------------------------------------------------

    dll.robot_git_tick.argtypes = [

        ctypes.c_int,

        ctypes.POINTER(ctypes.c_float),

    ]



    dll.robot_git_tick.restype = ctypes.c_int



    rc = dll.robot_git_reset()



    if rc != 0:

        raise RuntimeError(

            f"robot_git_reset failed: {rc}"

        )



    print("Git Robot C DLL loaded:", dll_path)



    return dll





def call_robot_git_update(
    dll,
    side,
    frame_id,
    dt,
    keypoints
):
    # =========================================================
    # MoveNet / Git Robot C anatomical mapping
    #
    # IMPORTANT:
    # Input frame is NOT mirrored before MoveNet.
    #
    # COCO:
    # 5  = left shoulder
    # 6  = right shoulder
    # 7  = left elbow
    # 8  = right elbow
    # 9  = left wrist
    # 10 = right wrist
    # =========================================================

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

    out_xyz = (ctypes.c_float * 12)()
    out_human = (ctypes.c_float * 2)()
    out_target = (ctypes.c_float * 5)()

    roll_observable = ctypes.c_uint8(0)
    safety_flags = ctypes.c_uint32(0)

    with ROBOT_DLL_LOCK:
        ret = dll.robot_git_update(
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

            out_xyz,
            out_human,
            out_target,

            ctypes.byref(roll_observable),
            ctypes.byref(safety_flags),
        )

    if ret != 1:
        if ret < 0 and frame_id % 30 == 0:
            print(
                f"[GIT UPDATE] "
                f"side={side} rc={ret} "
                f"safety=0x{safety_flags.value:08X}"
            )

        return None

    xyz = list(out_xyz)
    human = list(out_human)
    target = list(out_target)

    return {
        "shoulder_l": tuple(xyz[0:3]),
        "shoulder_r": tuple(xyz[3:6]),
        "elbow": tuple(xyz[6:9]),
        "wrist": tuple(xyz[9:12]),

        "elbow_roll_deg": float(human[0]),
        "elbow_pitch_deg": float(human[1]),

        "elbow_roll_observable":
            bool(roll_observable.value),

        "git_target": tuple(target),

        "safety_flags":
            int(safety_flags.value),
    }

class GitJointTickSender:

    """

    Actual Git Agent2 motion controller tick.



    Webcam / MoveNet:

        ~10 FPS variable frame path



    Robot motion:

        fixed 20 ms = 50 Hz control path



    Human LEFT  -> RobotArm_L

    Human RIGHT -> RobotArm_R

    """



    def __init__(

        self,

        dll,

        host,

        port

    ):

        self.dll = dll

        self.host = host

        self.port = port



        self.stop_event = threading.Event()



        self.source_lock = threading.Lock()
        self.latest_source_frame_id = 0
        self.latest_right_lateral = 0.0
        self.last_good_m0 = 90.0

        # Avoid stale frame IDs if Unity was already running.

        self.command_frame_id = (

            int(time.time() * 1000)

            & 0x7FFFFFFF

        )



        self.thread = None




    def set_source_frame_id(

        self,

        frame_id

    ):

        with self.source_lock:

            self.latest_source_frame_id = int(frame_id)





    def get_source_frame_id(self):

        with self.source_lock:

            return self.latest_source_frame_id


    def set_right_lateral(self, lateral):
        with self.source_lock:
            self.latest_right_lateral = float(lateral)


    def get_right_lateral(self):
        with self.source_lock:
            return self.latest_right_lateral



    @staticmethod

    def command_dict(

        frame_id,

        values

    ):

        return {

            "frame_id": int(frame_id),



            "elbowRoll":

                float(values[0]),



            "elbowPitch":

                float(values[1]),



            "wristPitch":

                float(values[2]),



            "wristRoll":

                float(values[3]),



            "gripper":

                float(values[4]),



            "valid": True,

        }





    def start(self):

        if self.thread is not None:

            return



        self.thread = threading.Thread(

            target=self._run,

            name="GitAgent2Tick",

            daemon=True

        )



        self.thread.start()





    def stop(self):

        self.stop_event.set()



        if self.thread is not None:

            self.thread.join(

                timeout=1.0

            )



            self.thread = None





    def _run(self):
        sock = socket.socket(
            socket.AF_INET,
            socket.SOCK_DGRAM
        )

        next_tick = time.perf_counter()
        tick_count = 0

        try:
            while not self.stop_event.is_set():

                # =================================================
                # RIGHT ARM ONLY LIVE TEST
                #
                # side = 1 : Human RIGHT
                # M0/M1    : actual Git Agent2 output
                #
                # M2/M3/M4 :
                # 현재 Webcam mock에는 hand landmark가 없으므로
                # Unity 관찰용 neutral 값으로 고정
                # =================================================

                right_out = (
                    ctypes.c_float * 5
                )()

                with ROBOT_DLL_LOCK:
                    right_rc = (
                        self.dll.robot_git_tick(
                            1,
                            right_out
                        )
                    )

                if right_rc == 1:

                    self.command_frame_id = (
                        self.command_frame_id + 1
                    ) & 0x7FFFFFFF

                    source_frame_id = (
                        self.get_source_frame_id()
                    )

                    # ---------------------------------------------
                    # LEFT ARM
                    # 완전히 고정
                    # ---------------------------------------------
                    left_fixed = [
                        90.0,
                        70.0,
                        100.0,
                        90.0,
                        0.05
                    ]

                    # -------------------------------------------------
                    # PC MOCK M0 direction guard
                    #
                    # lateral > 0 : Human RIGHT arm OUT
                    # lateral < 0 : Human RIGHT arm IN
                    #
                    # Expected:
                    #   OUT -> M0 < 90
                    #   IN  -> M0 > 90
                    # -------------------------------------------------
                    lateral = self.get_right_lateral()

                    m0_raw = float(right_out[0])
                    m1_raw = float(right_out[1])

                    # Current Git calibration:
                    # M1 = 105 - Human Pitch
                    pitch_est = 105.0 - m1_raw


                    # ============================================================
                    # M0 HOLD
                    #
                    # 팔을 벌린 상태에서 전완을 위로 세우면
                    # 약 30도 이후부터 3D Roll 추정이 불안정해짐.
                    #
                    # 따라서:
                    #   Pitch < 30 deg  -> M0 정상 추적
                    #   Pitch >= 30 deg -> 마지막 정상 M0 HOLD
                    # ============================================================

                    if abs(pitch_est) >= 30.0:

                        # 전완이 충분히 올라간 상태
                        # 마지막으로 신뢰했던 벌림 각도 유지
                        m0 = self.last_good_m0

                    else:

                        m0 = m0_raw

                        # --------------------------------------------------------
                        # OUT / IN 방향 guard
                        # --------------------------------------------------------
                        if abs(lateral) > 5000.0:

                            # Human RIGHT OUT
                            if lateral > 0.0 and m0 > 90.0:
                                m0 = 180.0 - m0

                            # Human RIGHT IN
                            elif lateral < 0.0 and m0 < 90.0:
                                m0 = 180.0 - m0

                        # Pitch가 낮고 Roll을 믿을 수 있을 때만 저장
                        self.last_good_m0 = m0


                    right_live = [
                        m0,
                        float(right_out[1]),
                        float(right_out[2]),
                        float(right_out[3]),
                        float(right_out[4]),
                    ]

                    packet = {
                        "frame_id":
                            self.command_frame_id,

                        "source_frame_id":
                            source_frame_id,

                        "left":
                            self.command_dict(
                                self.command_frame_id,
                                left_fixed
                            ),

                        "right":
                            self.command_dict(
                                self.command_frame_id,
                                right_live
                            ),
                    }

                    payload = json.dumps(
                        packet,
                        separators=(",", ":")
                    ).encode("utf-8")

                    sock.sendto(
                        payload,
                        (
                            self.host,
                            self.port
                        )
                    )

                    tick_count += 1

                    # ---------------------------------------------
                    # 1초마다 오른팔 상태 출력
                    # ---------------------------------------------
                    if tick_count % 50 == 0:
                        print(
                            "[RIGHT LIVE] "
                            f"M0={right_live[0]:6.1f} "
                            f"M1={right_live[1]:6.1f} "
                            f"M2={right_live[2]:6.1f} "
                            f"M3={right_live[3]:6.1f}"
                        )

                # =================================================
                # 50 Hz / 20 ms Agent2 tick
                # =================================================

                next_tick += 0.020

                delay = (
                    next_tick -
                    time.perf_counter()
                )

                if delay > 0.0:
                    self.stop_event.wait(
                        delay
                    )
                else:
                    next_tick = time.perf_counter()

        finally:
            sock.close()








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

    dll = load_robot_git_dll()



    joint_sender = GitJointTickSender(

        dll,

        UDP_HOST,

        JOINT_UDP_PORT

    )



    joint_sender.start()





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

        # frame_bgr = cv2.flip(frame_bgr, 1)



        now = time.perf_counter()

        dt = now - last_time

        last_time = now



        frame_id += 1



        joint_sender.set_source_frame_id(

            frame_id

        )



        h, w = frame_bgr.shape[:2]

        if frame_id == 1:
            print(
                f"[CAMERA] actual frame = {w}x{h}"
            )

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

        left_xyz = call_robot_git_update(

            dll,

            0,

            frame_id,

            dt,

            keypoints

        )



        right_xyz = call_robot_git_update(

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


        if right_xyz is not None:
            pitch = right_xyz["elbow_pitch_deg"]

            if abs(pitch) > 20.0:
                target = right_xyz["git_target"]

                e2 = keypoints[8]
                w2 = keypoints[10]

                e3 = right_xyz["elbow"]
                w3 = right_xyz["wrist"]

                print(
                    "[PITCH SPIKE] "
                    f"frame={frame_id} "
                    f"Pitch={pitch:+7.2f} "
                    f"M1={target[1]:6.1f} | "
                    f"E2D=({e2['x']:.1f},{e2['y']:.1f},s={e2['score']:.2f}) "
                    f"W2D=({w2['x']:.1f},{w2['y']:.1f},s={w2['score']:.2f}) | "
                    f"E3D=({e3[0]:+.3f},{e3[1]:+.3f},{e3[2]:+.3f}) "
                    f"W3D=({w3[0]:+.3f},{w3[1]:+.3f},{w3[2]:+.3f})"
                )

        if right_xyz is not None and frame_id % 5 == 0:
            sl = keypoints[5]
            sr = keypoints[6]
            e  = keypoints[8]
            w  = keypoints[10]

            # Human LEFT shoulder -> RIGHT shoulder
            # = anatomical body-right direction
            body_rx = sr["x"] - sl["x"]
            body_ry = sr["y"] - sl["y"]

            # Right elbow -> wrist
            arm_x = w["x"] - e["x"]
            arm_y = w["y"] - e["y"]

            lateral = body_rx * arm_x + body_ry * arm_y

            joint_sender.set_right_lateral(lateral)

            roll = right_xyz["elbow_roll_deg"]
            m0 = right_xyz["git_target"][0]

            print(
                "[ROLL CHECK] "
                f"{'OUT' if lateral > 0 else 'IN '} "
                f"lat={lateral:+9.1f} | "
                f"Roll={roll:+7.2f} "
                f"M0={m0:6.1f}"
            )


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

        # Human LEFT  -> Unity RobotArm_L

        # Human RIGHT -> Unity RobotArm_R

        #

        # IMPORTANT:

        # No XYZ -> angle calculation occurs in Unity anymore.

        # -----------------------------------------------------

        # Robot joint UDP is emitted by GitJointTickSender

        # at the actual fixed 20 ms Agent2 control tick.

        # Python performs NO joint-angle mapping here.

        # -----------------------------------------------------



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

    joint_sender.stop()

    udp.close()

    cap.release()

    cv2.destroyAllWindows()



    print("CSV saved:", log_path.resolve())





if __name__ == "__main__":

    main()


