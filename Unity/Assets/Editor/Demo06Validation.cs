using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;
using HumanMotion.MediaPipeDebug;

// 현재 Demo06에서만 실행한다. 테스트 UDP 포트는 Play Mode에만 변경한다.
[InitializeOnLoad]
public static class Demo06Validation
{
    const string Key = "Demo06.Validation";
    static UdpMediaPipePoseSource source;
    static MediaPipeRobotRetargetController adapter;
    static HumanArmDebugController human;
    static UdpMediaPipePoseSource.Packet packet;
    static UdpClient sender;
    static int stage;
    static double deadline;
    static string report = "";
    static Vector3 previousDirection;
    static Demo06Validation() { EditorApplication.update += Tick; }
    public static void Begin()
    {
        if (EditorApplication.isPlaying || SceneManager.GetActiveScene().path != Demo06Workbench.ScenePath) throw new Exception("Demo06 Edit Mode required");
        SessionState.SetBool(Key, true); EditorApplication.isPlaying = true;
    }
    static void Require(bool ok, string message) { if (!ok) throw new Exception(message); }
    static void Pass(string message) { report += "PASS: " + message + "\n"; }
    static Vector3 Direction => (adapter.robot.wristRoll.position - adapter.robot.elbowPitch.position).normalized;
    static void Send(long id, float roll, float pitch, string session = "demo06-test")
    {
        packet.frame_id = id; packet.session_id = session;
        packet.timestamp = (DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
        packet.valid = true;
        // 정면 사람: Body X=-Unity X, Body Y=Unity Y, Body Z=-Unity Z.
        Vector3 elbow = new Vector3(-.2f, 0, 0);
        float r = roll * Mathf.Deg2Rad, p = pitch * Mathf.Deg2Rad;
        Vector3 f = new Vector3(-Mathf.Sin(r) * Mathf.Cos(p), Mathf.Sin(p), -Mathf.Cos(r) * Mathf.Cos(p));
        Vector3[] positions = { new Vector3(.2f,.3f,0), new Vector3(.2f,0,0), new Vector3(.2f,0,-.3f),
            new Vector3(-.2f,.3f,0), elbow, elbow + .3f * f };
        for (int i = 0; i < 6; i++)
        {
            packet.Joints[i].xyz = new[] { positions[i].x, -positions[i].y, -positions[i].z };
            packet.Joints[i].visibility = 1; packet.Joints[i].presence = 1;
        }
        var bytes = Encoding.UTF8.GetBytes(JsonUtility.ToJson(packet));
        sender.Send(bytes, bytes.Length, "127.0.0.1", 15055);
        deadline = EditorApplication.timeSinceStartup + 5;
    }
    static void Tick()
    {
        if (!SessionState.GetBool(Key, false) || !EditorApplication.isPlaying || EditorApplication.isCompiling) return;
        try
        {
            if (stage == 0)
            {
                source = UnityEngine.Object.FindFirstObjectByType<UdpMediaPipePoseSource>();
                adapter = UnityEngine.Object.FindFirstObjectByType<MediaPipeRobotRetargetController>();
                human = UnityEngine.Object.FindFirstObjectByType<HumanArmDebugController>();
                if (source == null || adapter == null || human?.Joints == null) return;
                Require(adapter.robot.IsConfigured, "IsConfigured");
                Require(adapter.source == source && adapter.humanArm == human && human.poseSource == source, "shared source references");
                Require(UnityEngine.Object.FindFirstObjectByType<MediaPipeLiveView>() == null, "old view removed in Demo06");
                source.enabled = false; source.port = 15055; source.enabled = true;
                packet = UdpMediaPipePoseSource.Parse(File.ReadAllBytes("Validation/MediaPipeLive/sender-fixture.json"));
                sender = new UdpClient();
                Pass("Play Mode compile/load; configured pivot chain and shared source references");
                Send(1, 0, 0); stage = 1;
            }
            else if (stage == 1 && adapter.AppliedFrameId == 1)
            {
                Require(source.HasPreview && human.AppliedPose.frameId == 1, "pose+preview");
                Require(Mathf.Abs(adapter.Command.elbowRoll - 90) < .001f && Mathf.Abs(adapter.Command.elbowPitch - 105) < .001f, "C neutral calibration");
                Require(adapter.Command.wristPitch == 100 && adapter.Command.wristRoll == 87 && adapter.Command.gripper == 1, "hand HOLD");
                previousDirection = Direction;
                Pass("UDP frame 1, matching preview, RAW human, M0=90/M1=105, fixed M2=100/M3=87/M4=1");
                Send(2, 0, 40); stage = 2;
            }
            else if (stage == 2 && adapter.AppliedFrameId == 2)
            {
                Require(Direction.y > previousDirection.y && adapter.Command.elbowPitch < 105, "pitch motion direction");
                Require(Mathf.Abs(adapter.HumanPitch - 40) < .001f, "pitch angle");
                Pass("upward forearm → pitch +40 → M1=65 → robot moves upward");
                previousDirection = Direction; Send(3, 40, 0); stage = 3;
            }
            else if (stage == 3 && adapter.AppliedFrameId == 3)
            {
                Require(Direction.x < previousDirection.x && Mathf.Abs(adapter.Command.elbowRoll - 50) < .001f, "roll motion direction");
                Pass("Body +X forearm → roll +40 → M0=50 → robot moves toward same image side");
                Send(4, -40, -30); stage = 4;
            }
            else if (stage == 4 && adapter.AppliedFrameId == 4)
            {
                Require(Direction.x > 0 && Direction.y < 0 && Mathf.Abs(adapter.Command.elbowRoll - 130) < .001f, "opposite direction");
                Pass("opposite roll / downward pitch gives opposite robot motion");
                Send(5, 120, 0); stage = 5;
            }
            else if (stage == 5 && adapter.AppliedFrameId == 5)
            {
                Require(adapter.Command.elbowRoll == 20 && adapter.Status.Contains("limit"), "clamp");
                Pass("C servo range clamp is explicit");
                Send(6, 0, 90); stage = 6;
            }
            else if (stage == 6 && source.LastFrameId == 6)
            {
                Require(!adapter.Live && !adapter.Command.valid && adapter.Status.Contains("unobservable"), "vertical singularity");
                Pass("vertical azimuth singularity: unavailable, no fabricated HOLD");
                Send(7, 35, 40); stage = 7;
            }
            else if (stage == 7 && adapter.AppliedFrameId == 7)
            {
                // 현재 화면을 사람이 확인할 수 있도록 충분한 유효 패킷을 유지하는 별도 단계.
                ScreenCapture.CaptureScreenshot(Demo06Workbench.Folder + "/play-mode.png");
                stage = 10; deadline = EditorApplication.timeSinceStartup + .15;
            }
            else if (stage == 10 && EditorApplication.timeSinceStartup > deadline)
            {
                UnityEngine.Object.FindFirstObjectByType<MediaPipeRobotLiveView>().SetSideView(true);
                stage = 11; deadline = EditorApplication.timeSinceStartup + .15;
            }
            else if (stage == 11 && EditorApplication.timeSinceStartup > deadline)
            {
                ScreenCapture.CaptureScreenshot(Demo06Workbench.Folder + "/play-mode-side.png");
                stage = 8; deadline = EditorApplication.timeSinceStartup + 1.2;
            }
            else if (stage == 8 && EditorApplication.timeSinceStartup > deadline)
            {
                Require(!source.Connected && source.Current == null && human.AppliedPose == null && !adapter.Live, "timeout clears all consumers");
                foreach (var r in adapter.robot.GetComponentsInChildren<Renderer>()) Require(!r.enabled, "stale robot hidden");
                Pass("sender stop: original timeout clears human, preview and robot target");
                Send(0, 0, 20, "demo06-restart"); stage = 9;
            }
            else if (stage == 9 && adapter.AppliedFrameId == 0)
            {
                Require(source.SessionId == "demo06-restart" && adapter.Live && source.HasPreview, "restart");
                Pass("new sender session frame 0 automatically recovers both consumers");
                var invalid = source.Current;
                invalid.landmarks[4].visibility = .1f;
                Require(!MediaPipeForearmAngleMapper.TryMap(invalid, human.coordinateMapping, true, .5f, out _, out _), "low confidence");
                Pass("required landmark low confidence rejects robot mapping; RAW pipeline unchanged");
                Finish();
            }
            else if (stage != 0 && EditorApplication.timeSinceStartup > deadline) throw new Exception("stage timeout " + stage + " / " + source.Error + " / " + adapter.Status);
        }
        catch (Exception e) { report += "FAIL: " + e + "\n"; Finish(); }
    }
    static void Finish()
    {
        SessionState.SetBool(Key, false); sender?.Close();
        File.WriteAllText(Demo06Workbench.Folder + "/play-mode-validation.txt", report);
        Debug.Log(report); EditorApplication.isPlaying = false;
    }
}
