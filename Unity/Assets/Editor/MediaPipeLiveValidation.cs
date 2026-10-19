using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using HumanMotion.MediaPipeDebug;

// 별도 batch 프로젝트에서만 명시적으로 실행하는 UDP 통합 테스트.
[InitializeOnLoad]
public static class MediaPipeLiveValidation
{
    private const string Key = "MediaPipe.LiveValidation";
    private static UdpMediaPipePoseSource source;
    private static HumanArmDebugController arm;
    private static UdpClient sender;
    private static UdpMediaPipePoseSource.Packet fixture;
    private static double deadline;
    private static int stage;
    private static string report = "";
    static MediaPipeLiveValidation() { EditorApplication.update += Tick; }
    public static void RunBatch()
    {
        EditorSceneManager.OpenScene("Assets/Scenes/Demo_05_MediaPipeLive.unity");
        UnityEngine.Object.FindFirstObjectByType<UdpMediaPipePoseSource>().port = 15055;
        SessionState.SetBool(Key, true);
        EditorApplication.isPlaying = true;
    }
    private static double UnixNow => (DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
    private static void Require(bool ok, string message) { if (!ok) throw new Exception(message); }
    private static void Send(long id, string session = "validation")
    {
        fixture.frame_id = id; fixture.session_id = session; fixture.timestamp = UnixNow;
        var bytes = Encoding.UTF8.GetBytes(JsonUtility.ToJson(fixture));
        sender.Send(bytes, bytes.Length, "127.0.0.1", 15055);
    }
    private static void Pass(string s) { report += "PASS: " + s + "\n"; }
    private static void Tick()
    {
        if (!SessionState.GetBool(Key, false) || !EditorApplication.isPlaying) return;
        try
        {
            if (stage == 0)
            {
                source = UnityEngine.Object.FindFirstObjectByType<UdpMediaPipePoseSource>();
                arm = UnityEngine.Object.FindFirstObjectByType<HumanArmDebugController>();
                if (source == null || arm == null || arm.Joints == null) return;
                Require(string.IsNullOrEmpty(source.Error), source.Error);
                fixture = UdpMediaPipePoseSource.Parse(File.ReadAllBytes("Validation/MediaPipeLive/sender-fixture.json"));
                sender = new UdpClient(); Send(42); stage = 1; deadline = EditorApplication.timeSinceStartup + 5;
            }
            else if (stage == 1 && source.Current != null)
            {
                Require(source.Current.frameId == 42 && source.HasPreview, "frame/preview");
                for (int i = 0; i < 6; i++)
                {
                    var p = fixture.Joints[i];
                    Require(Vector3.Distance(arm.Joints[i].position, new Vector3(p.xyz[0], -p.xyz[1], -p.xyz[2])) < 1e-6f, "RAW geometry");
                    Require(source.Current.landmarks[i].visibility == p.visibility, "visibility");
                }
                for (int i = 0; i < 4; i++)
                {
                    int a = i / 2 * 3 + i % 2;
                    Require(arm.Links[i].GetPosition(0) == arm.Joints[a].position && arm.Links[i].GetPosition(1) == arm.Joints[a+1].position, "links");
                }
                Require(!source.Current.valid && arm.LowConfidenceCount == 1, "low confidence raw retained");
                Pass("Python sender fixture → UDP → 6 RAW joints / 4 links / low confidence / matching preview");
                Send(41); Send(42);
                byte[] bad = Encoding.UTF8.GetBytes("{broken"); sender.Send(bad, bad.Length, "127.0.0.1", 15055);
                stage = 2; deadline = EditorApplication.timeSinceStartup + .25;
            }
            else if (stage == 2 && EditorApplication.timeSinceStartup > deadline)
            {
                Require(source.LastFrameId == 42 && source.RejectedPackets >= 3, "old/duplicate/malformed");
                Pass("old/duplicate/malformed rejected");
                foreach (var j in fixture.Joints) j.xyz = new float[0];
                fixture.preview_jpeg = ""; Send(43);
                stage = 3; deadline = EditorApplication.timeSinceStartup + 5;
            }
            else if (stage == 3 && source.LastFrameId == 43)
            {
                Require(arm.MissingPositionCount == 6 && !source.HasPreview, "missing hidden");
                Pass("no detection clears geometry; no HOLD");
                stage = 4; deadline = EditorApplication.timeSinceStartup + 1;
            }
            else if (stage == 4 && EditorApplication.timeSinceStartup > deadline)
            {
                Require(!source.Connected && source.Current == null && arm.AppliedPose == null, "timeout");
                Pass("timeout disconnect + clear");
                fixture = UdpMediaPipePoseSource.Parse(File.ReadAllBytes("Validation/MediaPipeLive/sender-fixture.json"));
                Send(0, "restart"); stage = 5; deadline = EditorApplication.timeSinceStartup + 5;
            }
            else if (stage == 5 && source.Current != null)
            {
                Require(source.LastFrameId == 0 && source.SessionId == "restart", "restart");
                Pass("sender restart accepts frame 0");
                Send(44, "validation");
                stage = 6; deadline = EditorApplication.timeSinceStartup + .2;
            }
            else if (stage == 6 && EditorApplication.timeSinceStartup > deadline)
            {
                Require(source.SessionId == "restart" && source.LastFrameId == 0, "retired session");
                source.enabled = false; source.enabled = true;
                Send(1, "reenable"); stage = 7; deadline = EditorApplication.timeSinceStartup + 5;
            }
            else if (stage == 7 && source.Current != null)
            {
                Require(source.LastFrameId == 1 && string.IsNullOrEmpty(source.Error), "port rebind");
                Pass("retired session rejected / receiver disable-enable port cleanup");
                Finish(true);
            }
            else if (stage != 2 && stage != 4 && stage != 6 && EditorApplication.timeSinceStartup > deadline)
                throw new Exception("timeout stage " + stage + " " + source.Error);
        }
        catch (Exception e) { report += "FAIL: " + e + "\n"; Finish(false); }
    }
    private static void Finish(bool pass)
    {
        SessionState.SetBool(Key, false); sender?.Close();
        Directory.CreateDirectory("Validation/MediaPipeLive");
        File.WriteAllText("Validation/MediaPipeLive/unity-udp-validation.txt", report);
        Debug.Log(report); EditorApplication.Exit(pass ? 0 : 1);
    }
}
