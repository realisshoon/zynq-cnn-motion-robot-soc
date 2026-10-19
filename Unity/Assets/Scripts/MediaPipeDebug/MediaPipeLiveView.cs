using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace HumanMotion.MediaPipeDebug
{
    public sealed class MediaPipeLiveView : MonoBehaviour
    {
        public UdpMediaPipePoseSource source;
        public HumanArmDebugController humanArm;
        private RenderTexture skeletonTexture;
        private Camera skeletonCamera, displayCamera;
        private float yaw;
        private void Awake()
        {
            displayCamera = new GameObject("LiveDisplayCamera").AddComponent<Camera>();
            displayCamera.clearFlags = CameraClearFlags.SolidColor;
            displayCamera.backgroundColor = new Color(.035f, .05f, .075f);
            displayCamera.cullingMask = 0;
            displayCamera.gameObject.AddComponent<AudioListener>();
            skeletonCamera = new GameObject("LiveHumanArmCamera").AddComponent<Camera>();
            skeletonCamera.clearFlags = CameraClearFlags.SolidColor;
            skeletonCamera.backgroundColor = new Color(.07f, .09f, .13f);
            skeletonCamera.cullingMask = 1 << 30;
            skeletonCamera.orthographic = true; skeletonCamera.orthographicSize = .85f;
            skeletonCamera.nearClipPlane = .01f; skeletonCamera.farClipPlane = 20;
            skeletonTexture = new RenderTexture(1000, 800, 24);
            skeletonTexture.Create(); skeletonCamera.targetTexture = skeletonTexture;
            SetView(0);
        }
        private void SetView(float angle)
        {
            yaw = angle;
            var center = new Vector3(0, .2f, 0);
            skeletonCamera.transform.position = center + Quaternion.AngleAxis(angle, Vector3.up) * new Vector3(0, 0, -3);
            skeletonCamera.transform.LookAt(center);
        }
        private void Update()
        {
#if ENABLE_INPUT_SYSTEM
            var k = Keyboard.current;
            if (k != null && (k.qKey.wasPressedThisFrame || k.escapeKey.wasPressedThisFrame)) Quit();
#elif ENABLE_LEGACY_INPUT_MANAGER
            if (Input.GetKeyDown(KeyCode.Q) || Input.GetKeyDown(KeyCode.Escape)) Quit();
#endif
        }
        private void OnGUI()
        {
            GUI.matrix = Matrix4x4.Scale(new Vector3(Screen.width / 1280f, Screen.height / 800f, 1));
            GUI.skin.label.fontSize = 17; GUI.skin.button.fontSize = 16;
            GUI.Label(new Rect(24, 10, 1230, 30), "MEDIAPIPE HEAVY LIVE | RAW world XYZ | Same inference frame preview + pose");
            GUI.Label(new Rect(24, 48, 620, 25), "WEBCAM (Python capture, unmirrored)");
            GUI.Label(new Rect(654, 48, 610, 25), "HUMAN ARM   Left: cyan / Right: orange / Low: yellow");
            if (source.HasPreview) GUI.DrawTexture(new Rect(24, 80, 600, 440), source.Preview, ScaleMode.ScaleToFit);
            else GUI.Label(new Rect(24, 260, 600, 60), "Waiting for webcam preview / no preview in current packet");
            GUI.DrawTexture(new Rect(654, 80, 600, 440), skeletonTexture, ScaleMode.ScaleToFit);
            GUI.Label(new Rect(24, 525, 1230, 28), $"UDP Connected: {source.Connected} | Frame: {source.LastFrameId} | Receive fps: {source.ReceiveFps:F1} | Packet age: {source.PacketAgeMs:F0} ms");
            GUI.Label(new Rect(24, 555, 1230, 28), $"Capture-to-now: {source.CaptureAgeMs:F0} ms (same PC clock) | Accepted: {source.AcceptedPackets} | Rejected: {source.RejectedPackets}");
            var f = source.Current;
            if (f != null)
            {
                GUI.Label(new Rect(24, 587, 1230, 28), Confidence("Left", f, 0));
                GUI.Label(new Rect(24, 617, 1230, 28), Confidence("Right", f, 3));
            }
            GUI.Label(new Rect(24, 647, 1230, 28), $"RAW always ON | valid: {(f != null && f.valid ? 1 : 0)} | {(humanArm.LowConfidenceCount > 0 ? "LOW CONFIDENCE" : "")} | Missing XYZ: {humanArm.MissingPositionCount} | {(source.Connected ? "" : "DISCONNECTED / STALE - geometry cleared")}");
            if (GUI.Button(new Rect(24, 691, 160, 32), "Front / side")) SetView(yaw == 0 ? 90 : 0);
            if (GUI.Button(new Rect(200, 691, 100, 32), "Quit")) Quit();
            GUI.Label(new Rect(320, 693, 920, 30), $"Q/Esc: stop Unity | UDP {source.bindAddress}:{source.port} | Python Q/Esc: stop sender");
            GUI.color = Color.yellow;
            GUI.Label(new Rect(24, 741, 1230, 40), source.Error);
            GUI.color = Color.white;
        }
        private static string Confidence(string name, PoseFrame f, int i) =>
            $"{name} S/E/W visibility: {f.landmarks[i].visibility:F3} / {f.landmarks[i+1].visibility:F3} / {f.landmarks[i+2].visibility:F3}    presence: {f.landmarks[i].presence:F3} / {f.landmarks[i+1].presence:F3} / {f.landmarks[i+2].presence:F3}";
        private static void Quit()
        {
#if UNITY_EDITOR
            UnityEditor.EditorApplication.isPlaying = false;
#else
            Application.Quit();
#endif
        }
        private void OnDestroy()
        {
            if (skeletonTexture != null) { skeletonTexture.Release(); Destroy(skeletonTexture); }
            if (skeletonCamera != null) Destroy(skeletonCamera.gameObject);
            if (displayCamera != null) Destroy(displayCamera.gameObject);
        }
    }
}
