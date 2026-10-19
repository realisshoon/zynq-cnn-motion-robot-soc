"""RAW world landmark와 같은 입력 영상의 미리보기를 한 UDP datagram으로 전송한다."""
import base64
import json
import math
import socket
import uuid

ARM_INDICES = dict(zip(("left_shoulder", "left_elbow", "left_wrist",
                        "right_shoulder", "right_elbow", "right_wrist"),
                       (11, 13, 15, 12, 14, 16)))


def make_packet(result, frame_id, timestamp, fps, session_id):
    world = result.pose_world_landmarks[0] if result.pose_world_landmarks else []
    packet = dict(version=1, session_id=session_id, frame_id=frame_id,
                  timestamp=timestamp, source_fps=fps, valid=True, preview_jpeg="")
    for name, index in ARM_INDICES.items():
        point = world[index] if len(world) > index else None
        xyz = [point.x, point.y, point.z] if point else []
        if not all(v is not None and math.isfinite(v) for v in xyz):
            xyz = []
        scores = {}
        for key in ("visibility", "presence"):
            value = getattr(point, key, None)
            scores[key] = value if value is not None and math.isfinite(value) else -1.0
        packet[name] = dict(xyz=xyz, **scores)
        packet["valid"] &= len(xyz) == 3 and min(scores.values()) >= 0.5
    return packet


class PoseUdpSender:
    def __init__(self, host, port):
        self.address = (host, port)
        self.session_id = str(uuid.uuid4())
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setblocking(False)
        self.dropped = 0

    def send(self, result, frame_id, timestamp, fps, frame):
        import cv2
        packet = make_packet(result, frame_id, timestamp, fps, self.session_id)
        # 원본 추론 영상만 축소한다. landmark에는 축소/반전/필터를 적용하지 않는다.
        height, width = frame.shape[:2]
        preview = cv2.resize(frame, (320, max(1, round(height * 320 / width))))
        ok, jpeg = cv2.imencode(".jpg", preview, [cv2.IMWRITE_JPEG_QUALITY, 50])
        if ok:
            packet["preview_jpeg"] = base64.b64encode(jpeg).decode("ascii")
        payload = json.dumps(packet, separators=(",", ":"), allow_nan=False).encode("utf-8")
        # IPv4 UDP 최대 크기보다 작게 제한. 초과 시 좌표는 보내고 미리보기만 생략한다.
        if len(payload) > 60000:
            packet["preview_jpeg"] = ""
            payload = json.dumps(packet, separators=(",", ":"), allow_nan=False).encode("utf-8")
        try:
            self.socket.sendto(payload, self.address)
        except (BlockingIOError, OSError) as exc:
            self.dropped += 1
            if self.dropped == 1 or self.dropped % 100 == 0:
                print(f"UDP send dropped={self.dropped}: {exc}")

    def close(self):
        self.socket.close()
