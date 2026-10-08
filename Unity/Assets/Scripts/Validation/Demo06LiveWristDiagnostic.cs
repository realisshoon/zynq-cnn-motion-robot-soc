using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using HumanMotion.MediaPipeDebug;
using UnityEngine;
using UnityEngine.SceneManagement;

// Observation only: never maps a diagnostic candidate to any robot command.
public sealed class Demo06LiveWristDiagnostic : MonoBehaviour
{
    [Serializable]
    public sealed class Packet
    {
        public int version;
        public bool diagnostic_only, pose_valid, hand_detected, pair_success, finger_input_valid;
        public string session_id, source_head, pairing_status, failure_stage, marker;
        public long frame_id;
        public double timestamp;
        public float target_valid, hand_fresh, wrist_valid, wrist_pitch_deg, wrist_roll_deg;
        public float candidate_valid, m2_candidate, m3_candidate, m2_reachable, m3_reachable;
        public float m2_clamped_candidate, m3_clamped_candidate, major_fresh, finger_fresh;
        public float finger_pose3d_valid, branch_selected_valid, branch_selected, branch_elapsed_frames;
        public float reconstruction_rc, selection_rc, hand_calculation_rc;
        public float pose_wrist_x, pose_wrist_y, hand_wrist_x, hand_wrist_y;
        public float thumb_x, thumb_y, index_x, index_y, selected_distance_px;
        public float processing_ms, dropped_frames;
    }

    public int diagnosticPort = 5057;
    public bool showPanel = true;
    public Packet Current { get; private set; }
    public string Error { get; private set; } = "";
    private UdpClient socket;
    private MediaPipeRobotRetargetController retarget;
    private Rect panel = new Rect(510, 95, 970, 440);
    public double AgeMs => Current == null ? double.PositiveInfinity :
        (DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalMilliseconds - Current.timestamp * 1000;
    public bool MatchesPoseSession => Current != null && retarget != null && retarget.source != null &&
        Current.session_id == retarget.source.SessionId;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    private static void Install()
    {
        if (SceneManager.GetActiveScene().name != "Demo_06_MediaPipeRobotLive") return;
        var live = FindFirstObjectByType<MediaPipeRobotRetargetController>();
        if (live != null && live.GetComponent<Demo06LiveWristDiagnostic>() == null)
            live.gameObject.AddComponent<Demo06LiveWristDiagnostic>();
    }

    private void OnEnable()
    {
        Error = ""; Current = null;
        retarget = GetComponent<MediaPipeRobotRetargetController>();
        if (retarget == null) { enabled = false; return; }
        try
        {
            socket = new UdpClient(AddressFamily.InterNetwork);
            socket.Client.ExclusiveAddressUse = true;
            socket.Client.Bind(new IPEndPoint(IPAddress.Loopback, diagnosticPort));
            socket.Client.Blocking = false;
        }
        catch (Exception e) { Error = e.Message; socket?.Close(); socket = null; }
    }

    private void Update()
    {
        if (socket == null) return;
        for (int i = 0; i < 16 && socket.Available > 0; i++)
        {
            try
            {
                var remote = new IPEndPoint(IPAddress.Any, 0);
                byte[] data = socket.Receive(ref remote);
                if (data.Length > 16000) continue;
                var p = JsonUtility.FromJson<Packet>(Encoding.UTF8.GetString(data));
                if (p == null || p.version != 1 || !p.diagnostic_only || p.frame_id < 0 ||
                    string.IsNullOrEmpty(p.session_id) || double.IsNaN(p.timestamp) || double.IsInfinity(p.timestamp)) continue;
                if (retarget.source == null || p.session_id != retarget.source.SessionId) continue;
                if (Current != null && p.session_id == Current.session_id && p.frame_id <= Current.frame_id) continue;
                Current = p;
            }
            catch (Exception e) { Error = "Diagnostic packet: " + e.Message; }
        }
    }

    private void OnGUI()
    {
        var previous = GUI.matrix;
        float scale = Mathf.Min(Screen.width / 1500f, Screen.height / 850f);
        GUI.matrix = Matrix4x4.TRS(new Vector3((Screen.width - 1500 * scale) * .5f,
            (Screen.height - 850 * scale) * .5f, 0), Quaternion.identity, new Vector3(scale, scale, 1));
        int depth = GUI.depth; GUI.depth = -100;
        if (GUI.Button(new Rect(1200, 10, 280, 40), showPanel ? "Hide Wrist Diagnostic" : "Show Wrist Diagnostic"))
            showPanel = !showPanel;
        if (showPanel) panel = GUI.Window(60065057, panel, DrawPanel, "LIVE WRIST DIAGNOSTIC — OBSERVATION ONLY");
        GUI.depth = depth; GUI.matrix = previous;
    }

    private void DrawPanel(int id)
    {
        var style = new GUIStyle(GUI.skin.label) { fontSize = 18 };
        float y = 32;
        void Line(string s) { GUI.Label(new Rect(16, y, 940, 25), s, style); y += 27; }
        if (Current == null)
        {
            Line("Waiting for diagnostic UDP :" + diagnosticPort + " (Pose/control continues unchanged)");
            Line("Run Start-MediaPipeLive.ps1 -WristDiagnostic");
        }
        else
        {
            var p = Current;
            bool freshPacket = MatchesPoseSession && AgeMs >= -100 && AgeMs < 2000;
            Line($"Pose frame {retarget.source.LastFrameId} / Diagnostic frame {p.frame_id} | age {AgeMs:F0} ms | " +
                (freshPacket ? "same captured-frame input; async result" : "STALE / SESSION MISMATCH"));
            Line($"Hand detected: {p.hand_detected}  paired: {p.pair_success}  | {p.pairing_status}");
            Line($"target_valid={p.target_valid}  hand_fresh={p.hand_fresh}  wrist_valid={p.wrist_valid} | {p.failure_stage}");
            Line($"2D fresh={p.finger_fresh}  3D={p.finger_pose3d_valid}  branch={p.branch_selected} valid={p.branch_selected_valid} history={p.branch_elapsed_frames}");
            Line($"C return: reconstruction={p.reconstruction_rc} select={p.selection_rc} hand={p.hand_calculation_rc} (-999=not called)");
            Line(p.wrist_valid != 0 ? $"Human pitch={p.wrist_pitch_deg:F2}° roll={p.wrist_roll_deg:F2}° " +
                (p.hand_fresh != 0 ? "[fresh C observation]" : "[HELD / not a new observation]") : "Human pitch / roll: unavailable");
            Line(p.candidate_valid != 0 ? $"Candidate M2={p.m2_candidate:F2}  M3={p.m3_candidate:F2} | reachable={p.m2_reachable}/{p.m3_reachable}" : "M2/M3 candidates: unavailable");
            Line(p.candidate_valid != 0 ? $"Clamped candidates={p.m2_clamped_candidate:F2}/{p.m3_clamped_candidate:F2} — NOT approved/applied" : "Diagnostic candidates are never applied");
            Line($"1280x720 pixels: Pose wrist=({p.pose_wrist_x:F1},{p.pose_wrist_y:F1}) Hand wrist=({p.hand_wrist_x:F1},{p.hand_wrist_y:F1}) distance={p.selected_distance_px:F1}");
            Line($"Thumb=({p.thumb_x:F1},{p.thumb_y:F1}) Index=({p.index_x:F1},{p.index_y:F1}) | worker {p.processing_ms:F0} ms / dropped {p.dropped_frames}");
        }
        var c = retarget.Command;
        Line(retarget.Live ? $"Live applied command: M0={c.elbowRoll:F1} M1={c.elbowPitch:F1} M2={c.wristPitch:F1} HOLD M3={c.wristRoll:F1} HOLD M4={c.gripper:F1}" :
            "Live command unavailable. Configured HOLD remains M2=100 / M3=87.");
        var tester = retarget.GetComponent<Demo06WristPitchTester>();
        var replay = retarget.GetComponent<Demo06M234Replay>();
        if ((tester != null && tester.enabled && tester.applyM2Override) ||
            (replay != null && replay.enabled && replay.applyReplay))
            Line("WARNING: existing tester/replay override enabled; above values are the Live source command only.");
        if (!string.IsNullOrEmpty(Error)) Line(Error);
        GUI.DragWindow(new Rect(0, 0, 970, 28));
    }

    private void OnDisable() { socket?.Close(); socket = null; }
}
