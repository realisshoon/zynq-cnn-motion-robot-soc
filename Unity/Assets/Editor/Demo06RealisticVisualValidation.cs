using System;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Text;
using HumanMotion.MediaPipeDebug;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

// Synthetic UDP and direct-command acceptance check. Never saves Play Mode state.
[InitializeOnLoad]
public static class Demo06RealisticVisualValidation
{
    const string Key = "Demo06.RealisticVisualValidation";
    const string Folder = "Validation/YahboomIntegration";
    static int stage;
    static double deadline;
    static UdpClient sender;
    static UdpMediaPipePoseSource source;
    static MediaPipeRobotRetargetController retarget;
    static Demo06RealisticVisualAdapter adapter;
    static UdpMediaPipePoseSource.Packet packet;
    static Renderer[] visible;
    static string report;
    static int captureIndex, commandFrame;
    static readonly float[] TestPitch = { 70, 90, 110 };

    static Demo06RealisticVisualValidation() { EditorApplication.update += Tick; }

    public static void BeginBatch()
    {
        EditorSceneManager.OpenScene(BuildDemo06RealisticRobotVisuals.ScenePath, OpenSceneMode.Single);
        SessionState.SetBool(Key, true);
        EditorApplication.isPlaying = true;
    }

    static void Send(long id, bool valid)
    {
        packet.frame_id = id; packet.valid = valid;
        packet.timestamp = (DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
        byte[] bytes = Encoding.UTF8.GetBytes(JsonUtility.ToJson(packet));
        sender.Send(bytes, bytes.Length, "127.0.0.1", 15056);
        deadline = EditorApplication.timeSinceStartup + 8;
    }

    static void Tick()
    {
        if (!SessionState.GetBool(Key, false) || !EditorApplication.isPlaying || EditorApplication.isCompiling) return;
        try
        {
            if (stage == 0)
            {
                adapter = UnityEngine.Object.FindFirstObjectByType<Demo06RealisticVisualAdapter>();
                retarget = UnityEngine.Object.FindFirstObjectByType<MediaPipeRobotRetargetController>();
                source = UnityEngine.Object.FindFirstObjectByType<UdpMediaPipePoseSource>();
                Require(adapter != null && adapter.IsConfigured && retarget.robot == adapter.controller, "shared robot target");
                visible = adapter.GetComponentsInChildren<Renderer>().Where(r => r.gameObject.activeInHierarchy).ToArray();
                Require(visible.Length > 100, "realistic geometry exists");
                source.enabled = false; source.port = 15056; source.timeoutSeconds = 5; source.enabled = true;
                packet = UdpMediaPipePoseSource.Parse(File.ReadAllBytes("Validation/MediaPipeLive/sender-fixture.json"));
                packet.session_id = "demo06-realistic-visual-test";
                Vector3[] xyz = { new Vector3(.2f,-.3f,0), new Vector3(.2f,0,0), new Vector3(.2f,0,.3f),
                    new Vector3(-.2f,-.3f,0), new Vector3(-.2f,0,0), new Vector3(-.2f,0,.3f) };
                for (int i = 0; i < 6; i++)
                { packet.Joints[i].xyz = new[] { xyz[i].x, xyz[i].y, xyz[i].z }; packet.Joints[i].visibility = packet.Joints[i].presence = 1; }
                sender = new UdpClient();
                report = "실제 Unity Play Mode / 합성 localhost UDP 검증\n";
                Send(100, true); stage = 1;
            }
            else if (stage == 1 && retarget.AppliedFrameId == 100)
            {
                Require(retarget.Live && source.Connected && source.HasPreview, "UDP pose + preview + retarget");
                Require(visible.Any(r => r.enabled), "new visuals included in live visibility cache");
                Pass("UDP → PoseFrame → mapper → existing controller → realistic visual; preview decoded");
                Send(99, true); stage = 2;
            }
            else if (stage == 2 && source.RejectedPackets > 0)
            {
                Require(source.LastFrameId == 100 && retarget.AppliedFrameId == 100, "stale frame not applied");
                Pass("stale frame 99 rejected; frame 100 retained");
                Send(101, false); stage = 3;
            }
            else if (stage == 3 && source.LastFrameId == 101 && !retarget.Live)
            {
                Require(!visible.Any(r => r.enabled), "invalid hides realistic geometry");
                Pass("invalid input hides new visuals through unchanged retarget behavior");
                Send(102, true); stage = 4;
            }
            else if (stage == 4 && retarget.AppliedFrameId == 102)
            {
                Require(visible.Any(r => r.enabled), "valid restores visibility");
                Pass("valid recovery restores new visuals");
                // No more packets are sent. Keep the valid source alive during captures:
                // OnDisable intentionally invalidates the pose and hides all robot meshes.
                source.timeoutSeconds = 60;
                var tester = UnityEngine.Object.FindFirstObjectByType<Demo06WristPitchTester>();
                if (tester != null) tester.enabled = false;
                var replay = UnityEngine.Object.FindFirstObjectByType<Demo06M234Replay>();
                if (replay != null) replay.enabled = false;
                captureIndex = 0; ApplyCaptureCommand(); stage = 5;
            }
            else if (stage == 5 && Time.frameCount > commandFrame + 1)
            {
                float pitch = TestPitch[captureIndex];
                Require(visible.Any(r => r.enabled), "capture has visible robot meshes");
                Require(Quaternion.Angle(adapter.m2.visual.localRotation, adapter.m2.visualRest *
                    Quaternion.AngleAxis(pitch - 90, Vector3.forward)) < .05f, "M2 natural LateUpdate delta");
                Capture(pitch);
                if (++captureIndex < TestPitch.Length) { ApplyCaptureCommand(); return; }
                var grip = adapter.controller.gripperVisual;
                grip.Apply(0); var closed = grip.leftJawPivot.localRotation;
                grip.Apply(1); var open = grip.leftJawPivot.localRotation;
                Require(Quaternion.Angle(closed, open) > 1, "M4 jaw actually moves");
                Pass("Play Mode M2 70/90/110, M4 linkage motion and PNG render capture");
                Finish(0);
            }
            else if (stage > 0 && EditorApplication.timeSinceStartup > deadline)
                throw new Exception("Stage " + stage + " timeout: " + source.Error + "; " + retarget.Status);
        }
        catch (Exception e) { report += "FAIL " + e + "\n"; Finish(1); }
    }

    static void ApplyCaptureCommand()
    {
        Require(adapter.controller.ApplyCommand(new ForearmJointCommandData { valid = true,
            elbowRoll = 90, elbowPitch = 90, wristPitch = TestPitch[captureIndex], wristRoll = 87, gripper = 1 }),
            "direct final command");
        commandFrame = Time.frameCount;
        deadline = EditorApplication.timeSinceStartup + 15;
    }

    static void Capture(float pitch)
    {
        // Keep the actual Game View robot camera framing, including existing debug overlays.
        var camera = GameObject.Find("Demo06Robot").GetComponent<Camera>();
        var target = new RenderTexture(900, 900, 24);
        target.Create();
        var request = new UniversalRenderPipeline.SingleCameraRequest { destination = target };
        Require(RenderPipeline.SupportsRenderRequest(camera, request), "URP render request");
        RenderPipeline.SubmitRenderRequest(camera, request);
        var previous = RenderTexture.active;
        var image = new Texture2D(900, 900, TextureFormat.RGB24, false);
        RenderTexture.active = target;
        image.ReadPixels(new Rect(0, 0, 900, 900), 0, 0); image.Apply();
        File.WriteAllBytes(Folder + "/m2-" + pitch + ".png", image.EncodeToPNG());
        RenderTexture.active = previous;
        UnityEngine.Object.DestroyImmediate(image);
        target.Release(); UnityEngine.Object.DestroyImmediate(target);
    }

    static void Pass(string message) { report += "PASS " + message + "\n"; }
    static void Require(bool ok, string message) { if (!ok) throw new Exception(message); }
    static void Finish(int code)
    {
        SessionState.SetBool(Key, false);
        sender?.Dispose(); sender = null;
        File.WriteAllText(Folder + "/playmode-validation.txt", report);
        Debug.Log(report);
        EditorApplication.Exit(code);
    }
}
