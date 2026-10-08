using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace HumanMotion.MediaPipeDebug
{
    public sealed class MediaPipeRobotLiveView : MonoBehaviour
    {
        public UdpMediaPipePoseSource source;
        public MediaPipeRobotRetargetController retarget;
        private Camera display, human, robot;
        private RenderTexture humanTexture, robotTexture;
        private bool side;
        private void Awake()
        {
            display = MakeCamera("Demo06Display", 0, out _);
            display.targetTexture = null;
            human = MakeCamera("Demo06Human", 1 << 30, out humanTexture);
            robot = MakeCamera("Demo06Robot", 1 << 29, out robotTexture);
            human.orthographicSize = .70f; robot.orthographicSize = .36f;
            SetView();
        }
        private Camera MakeCamera(string name, int mask, out RenderTexture texture)
        {
            var c = new GameObject(name).AddComponent<Camera>();
            c.clearFlags = CameraClearFlags.SolidColor; c.backgroundColor = new Color(.035f, .05f, .075f);
            c.cullingMask = mask; c.orthographic = true; c.nearClipPlane = .01f; c.farClipPlane = 20;
            texture = mask == 0 ? null : new RenderTexture(700, 700, 24);
            if (texture != null) { texture.Create(); c.targetTexture = texture; }
            return c;
        }
        private void SetView()
        {
            Vector3 direction = side ? Vector3.left : Vector3.back;
            var center = new Vector3(0, .2f, 0);
            human.transform.position = center + direction * 3; human.transform.LookAt(center);
            center = retarget.robot.elbowPitch.position + Vector3.up * .13f;
            robot.transform.position = center + direction * 3; robot.transform.LookAt(center);
        }
        public void SetSideView(bool value) { side = value; SetView(); }
        private void Update()
        {
#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null && (Keyboard.current.qKey.wasPressedThisFrame || Keyboard.current.escapeKey.wasPressedThisFrame)) Quit();
#elif ENABLE_LEGACY_INPUT_MANAGER
            if (Input.GetKeyDown(KeyCode.Q) || Input.GetKeyDown(KeyCode.Escape)) Quit();
#endif
        }
        private void OnGUI()
        {
            var previous = GUI.matrix;
            float scale = Mathf.Min(Screen.width / 1500f, Screen.height / 850f);
            GUI.matrix = Matrix4x4.TRS(new Vector3((Screen.width - 1500 * scale) * .5f,
                (Screen.height - 850 * scale) * .5f, 0), Quaternion.identity, new Vector3(scale, scale, 1));
            var label = new GUIStyle(GUI.skin.label) { fontSize = 19 };
            var title = new GUIStyle(label) { fontSize = 24, fontStyle = FontStyle.Bold };
            GUI.Label(new Rect(20, 14, 1460, 40), "MEDIAPIPE → HUMAN RAW → ROBOT  |  FAST VALIDATION", title);
            GUI.Label(new Rect(20, 60, 480, 30), "WEBCAM · unmirrored", label);
            GUI.Label(new Rect(510, 60, 480, 30), "HUMAN RAW · left cyan / right orange", label);
            GUI.Label(new Rect(1000, 60, 480, 30), "5-AXIS ROBOT · " + (retarget.rightArm ? "RIGHT arm" : "LEFT arm"), label);
            if (source.HasPreview) GUI.DrawTexture(new Rect(20, 100, 480, 440), source.Preview, ScaleMode.ScaleToFit);
            else GUI.Label(new Rect(20, 290, 480, 50), "Waiting for camera preview", label);
            GUI.DrawTexture(new Rect(510, 100, 480, 440), humanTexture, ScaleMode.ScaleToFit);
            GUI.DrawTexture(new Rect(1000, 100, 480, 440), robotTexture, ScaleMode.ScaleToFit);
            if (!retarget.Live) GUI.Label(new Rect(1020, 290, 440, 70), "No valid robot target\n" + retarget.Status, label);
            GUI.Label(new Rect(20, 550, 1460, 30), $"UDP Connected: {source.Connected}  |  Frame: {source.LastFrameId}  |  Receive FPS: {source.ReceiveFps:F1}  |  Source FPS: {source.Current?.sourceFps:F1}", label);
            GUI.Label(new Rect(20, 582, 1460, 30), $"Packet age: {source.PacketAgeMs:F0} ms  |  Capture-to-now: {source.CaptureAgeMs:F0} ms (same PC clock)  |  {retarget.Status}", label);
            string angles = retarget.Live ? $"roll {retarget.HumanRoll:F1}° / pitch {retarget.HumanPitch:F1}°  |  Elbow bend {retarget.ElbowBend:F1}° (diagnostic only)" : "unavailable";
            GUI.Label(new Rect(20, 622, 1460, 30), "Human: " + angles, label);
            var c = retarget.Command;
            GUI.Label(new Rect(20, 655, 1460, 30), retarget.Live ? $"M0: {c.elbowRoll:F1}° LIVE   |   M1: {c.elbowPitch:F1}° LIVE   |   M2: 100° HOLD   |   M3: 87° HOLD   |   M4: 1.0 HOLD" : "M0 / M1: UNAVAILABLE   |   M2: 100° HOLD   |   M3: 87° HOLD   |   M4: 1.0 HOLD", label);
            GUI.Label(new Rect(20, 690, 1460, 30), "M2/M3/M4 = HOLD (hand landmarks unavailable)  |  No smoothing  |  Servo limits applied", label);
            if (GUI.Button(new Rect(20, 735, 170, 40), side ? "Front view" : "Side view")) { side = !side; SetView(); }
            if (GUI.Button(new Rect(210, 735, 100, 40), "Quit")) Quit();
            GUI.Label(new Rect(335, 738, 1140, 40), $"Q / Esc: stop Unity  |  UDP {source.bindAddress}:{source.port}  |  Robot hardware disconnected", label);
            GUI.Label(new Rect(20, 790, 1460, 40), source.Error, label);
            GUI.matrix = previous;
        }
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
            foreach (var c in new[] { display, human, robot }) if (c != null) Destroy(c.gameObject);
            foreach (var t in new[] { humanTexture, robotTexture }) if (t != null) { t.Release(); Destroy(t); }
        }
    }
}
