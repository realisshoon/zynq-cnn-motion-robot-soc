using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace HumanMotion.MediaPipeDebug
{
    public sealed class MediaPipeReplayView : MonoBehaviour
    {
        public CsvVideoPoseSource source;
        public HumanArmDebugController humanArm;
        private Camera skeletonCamera;
        private RenderTexture skeletonTexture;
        private float viewYaw;

        private void Awake()
        {
            var screen = new GameObject("DisplayCamera").AddComponent<Camera>();
            screen.clearFlags = CameraClearFlags.SolidColor; screen.backgroundColor = new Color(.035f, .05f, .075f);
            screen.cullingMask = 0;
            screen.gameObject.AddComponent<AudioListener>();
            skeletonCamera = new GameObject("HumanArmDebugCamera").AddComponent<Camera>();
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
            viewYaw = angle;
            Vector3 center = new Vector3(0, .2f, 0);
            skeletonCamera.transform.position = center + Quaternion.AngleAxis(angle, Vector3.up) * new Vector3(0, 0, -3);
            skeletonCamera.transform.LookAt(center);
        }

        private void Update()
        {
#if ENABLE_INPUT_SYSTEM
            var k = Keyboard.current;
            if (k == null) return;
            if (k.spaceKey.wasPressedThisFrame) source.TogglePause();
            if (k.rKey.wasPressedThisFrame) source.Replay();
            if (k.leftArrowKey.wasPressedThisFrame) source.Step(-1);
            if (k.rightArrowKey.wasPressedThisFrame) source.Step(1);
            if (k.qKey.wasPressedThisFrame || k.escapeKey.wasPressedThisFrame) Quit();
#elif ENABLE_LEGACY_INPUT_MANAGER
            if (Input.GetKeyDown(KeyCode.Space)) source.TogglePause();
            if (Input.GetKeyDown(KeyCode.R)) source.Replay();
            if (Input.GetKeyDown(KeyCode.LeftArrow)) source.Step(-1);
            if (Input.GetKeyDown(KeyCode.RightArrow)) source.Step(1);
            if (Input.GetKeyDown(KeyCode.Q) || Input.GetKeyDown(KeyCode.Escape)) Quit();
#endif
        }

        private void OnGUI()
        {
            float sx = Screen.width / 1280f, sy = Screen.height / 800f;
            GUI.matrix = Matrix4x4.Scale(new Vector3(sx, sy, 1));
            GUI.skin.label.fontSize = 17; GUI.skin.button.fontSize = 16;
            GUI.Label(new Rect(24, 10, 1200, 30), "MEDIAPIPE HUMAN ARM | RAW XYZ | Video timeline / 1x playback");
            GUI.Label(new Rect(24, 48, 600, 25), "REFERENCE VIDEO");
            GUI.Label(new Rect(654, 48, 600, 25), "HUMAN ARM DEBUG   Left: cyan / Right: orange / Low: yellow");
            if (source.VideoTexture != null) GUI.DrawTexture(new Rect(24, 80, 600, 440), source.VideoTexture, ScaleMode.ScaleToFit);
            if (skeletonTexture != null) GUI.DrawTexture(new Rect(654, 80, 600, 440), skeletonTexture, ScaleMode.ScaleToFit);
            var p = source.Player;
            var f = source.Current;
            string state = source.Seeking ? "SEEKING" : p != null && p.isPlaying ? "PLAYING" : "PAUSED";
            GUI.Label(new Rect(24, 525, 1220, 26), $"{state} | Video frame: {(p == null ? -1 : p.frame)}  time: {(p == null ? 0 : p.time):F3}s | CSV frame: {(f == null ? -1 : f.frameId)}  time: {(f == null ? 0 : f.timeSec):F3}s | {source.SelectionMode}");
            GUI.Label(new Rect(24, 554, 1220, 26), $"landmark_valid: {(f != null && f.valid ? 1 : 0)} | RAW: always ON | {(humanArm.LowConfidenceCount > 0 ? "LOW CONFIDENCE" : "confidence OK")} | Missing XYZ: {humanArm.MissingPositionCount}");
            if (f != null)
            {
                GUI.Label(new Rect(24, 585, 1230, 26), Confidence("Left", f, 0));
                GUI.Label(new Rect(24, 614, 1230, 26), Confidence("Right", f, 3));
            }
            if (GUI.Button(new Rect(24, 653, 130, 32), "Play / Pause")) source.TogglePause();
            if (GUI.Button(new Rect(162, 653, 95, 32), "Replay")) source.Replay();
            if (GUI.Button(new Rect(265, 653, 95, 32), "< frame")) source.Step(-1);
            if (GUI.Button(new Rect(368, 653, 95, 32), "frame >")) source.Step(1);
            if (GUI.Button(new Rect(480, 653, 110, 32), "12.00s")) source.Seek(240);
            if (GUI.Button(new Rect(598, 653, 110, 32), "14.65s")) source.Seek(293);
            if (GUI.Button(new Rect(716, 653, 110, 32), "28.70s")) source.Seek(574);
            if (GUI.Button(new Rect(844, 653, 150, 32), "Front / side")) SetView(viewYaw == 0 ? 90 : 0);
            if (GUI.Button(new Rect(1004, 653, 95, 32), "Quit")) Quit();
            GUI.Label(new Rect(24, 697, 1215, 30), "Space: play/pause   R: replay   Left/Right: pause + step   Q/Esc: stop   |   No filter / no IK / no robot mapping");
            GUI.color = Color.yellow;
            GUI.Label(new Rect(24, 738, 1220, 45), source.Error);
            GUI.color = Color.white;
        }

        private static string Confidence(string name, PoseFrame p, int n) =>
            $"{name} S/E/W visibility: {p.landmarks[n].visibility:F3} / {p.landmarks[n+1].visibility:F3} / {p.landmarks[n+2].visibility:F3}    presence: {p.landmarks[n].presence:F3} / {p.landmarks[n+1].presence:F3} / {p.landmarks[n+2].presence:F3}";
        private static void Quit()
        {
#if UNITY_EDITOR
            UnityEditor.EditorApplication.isPlaying = false;
#else
            Application.Quit();
#endif
        }
        private void OnDestroy()
        { if (skeletonTexture != null) { skeletonTexture.Release(); Destroy(skeletonTexture); } }
    }
}
