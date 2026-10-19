"""기존 Heavy 모델로 webcam 20프레임을 처리하여 RAW UDP 통신을 확인한다. 영상은 저장하지 않는다."""
import json
from pathlib import Path
import socket
import time

import mediapipe_pose_test as existing
from pose_udp_sender import PoseUdpSender


def main():
    cv2, mp, np = existing.cv2, existing.mp, existing.np
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.bind(("127.0.0.1", 0))
    listener.settimeout(2)
    sender = PoseUdpSender("127.0.0.1", listener.getsockname()[1])
    capture = cv2.VideoCapture(0, cv2.CAP_DSHOW)
    if not capture.isOpened():
        capture.release()
        capture = cv2.VideoCapture(0)
    count, detected, ages = 0, 0, []
    try:
        if not capture.isOpened():
            raise RuntimeError("camera index 0 unavailable")
        capture.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        options = mp.tasks.vision.PoseLandmarkerOptions(
            base_options=mp.tasks.BaseOptions(model_asset_path=str(existing.DEFAULT_MODEL)),
            running_mode=mp.tasks.vision.RunningMode.VIDEO,
            num_poses=1, output_segmentation_masks=False)
        with mp.tasks.vision.PoseLandmarker.create_from_options(options) as landmarker:
            started = time.perf_counter()
            previous_ms = -1
            for i in range(20):
                ok, frame = capture.read()
                timestamp = time.time()
                if not ok:
                    raise RuntimeError("camera read failed")
                stamp = max(previous_ms + 1, int((time.perf_counter() - started) * 1000))
                previous_ms = stamp
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                result = landmarker.detect_for_video(mp.Image(image_format=mp.ImageFormat.SRGB,
                                                     data=np.ascontiguousarray(rgb)), stamp)
                sender.send(result, i, timestamp, 0, frame)
                packet = json.loads(listener.recv(65535))
                assert packet["frame_id"] == i and packet["preview_jpeg"]
                count += 1
                detected += bool(result.pose_world_landmarks)
                ages.append((time.time() - timestamp) * 1000)
            elapsed = time.perf_counter() - started
        report = dict(status="PASS", frames=count, pose_detected_frames=detected,
                      elapsed_sec=elapsed, processing_fps=count / elapsed,
                      capture_read_to_udp_mean_ms=sum(ages) / len(ages),
                      capture_read_to_udp_max_ms=max(ages),
                      note="장치 read 반환 이후 시간. 실제 동작/카메라 내부 버퍼/Unity 화면 지연은 사용자 확인 필요.")
    except Exception as exc:
        report = dict(status="FAIL", frames=count, error=str(exc))
    finally:
        capture.release(); sender.close(); listener.close()
    output = Path(__file__).resolve().parents[2] / "Validation" / "MediaPipeLive" / "webcam-smoke.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if report["status"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
