"""실제 송신기 datagram 검증 및 Unity 통합 테스트용 fixture 생성."""
import json
from pathlib import Path
import socket
import time
from types import SimpleNamespace
import unittest

from pose_udp_sender import ARM_INDICES, PoseUdpSender, make_packet


class UdpTests(unittest.TestCase):
    def test_raw_transport(self):
        import cv2
        import numpy as np
        points = [SimpleNamespace(x=i * .01, y=-i * .02, z=i * .03,
                                  visibility=.99, presence=.98) for i in range(33)]
        points[13].visibility = .1
        result = SimpleNamespace(pose_world_landmarks=[points])
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.bind(("127.0.0.1", 0))
        listener.settimeout(2)
        sender = PoseUdpSender("127.0.0.1", listener.getsockname()[1])
        try:
            sender.send(result, 42, time.time(), 20, np.full((180, 320, 3), 120, np.uint8))
            payload = listener.recv(65535)
            packet = json.loads(payload)
            self.assertLessEqual(len(payload), 60000)
            self.assertFalse(packet["valid"])
            self.assertTrue(packet["preview_jpeg"])
            for name, index in ARM_INDICES.items():
                p = points[index]
                self.assertEqual(packet[name]["xyz"], [p.x, p.y, p.z])
                self.assertEqual(packet[name]["visibility"], p.visibility)
            output = Path(__file__).resolve().parents[2] / "Validation" / "MediaPipeLive"
            output.mkdir(parents=True, exist_ok=True)
            (output / "sender-fixture.json").write_text(json.dumps(packet), encoding="utf-8")
        finally:
            sender.close()
            listener.close()

    def test_missing(self):
        p = make_packet(SimpleNamespace(pose_world_landmarks=[]), 3, time.time(), 0, "test")
        self.assertFalse(p["valid"])
        for name in ARM_INDICES:
            self.assertEqual(p[name]["xyz"], [])
            self.assertEqual(p[name]["visibility"], -1)
        json.dumps(p, allow_nan=False)


if __name__ == "__main__":
    unittest.main()
