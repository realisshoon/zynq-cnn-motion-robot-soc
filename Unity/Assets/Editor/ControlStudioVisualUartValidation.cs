using System;
using System.IO;
using System.Reflection;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Batch-only validation on an isolated project copy. Does not save the scene.
public static class ControlStudioVisualUartValidation
{
    const string Scene = "Assets/Scenes/Demo_07_SingleArmControl.unity";
    static double enteredAt;
    static bool ran;
    static string output;

    public static void RunEdit()
    {
        Debug.Log("CONTROL STUDIO VISUAL UART EDIT VALIDATION START");
        output = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "captures"));
        Directory.CreateDirectory(output);
        EditorSceneManager.OpenScene(Scene);
        var adapter = UnityEngine.Object.FindFirstObjectByType<Demo06RealisticVisualAdapter>();
        Require(adapter != null && adapter.IsConfigured, "G51 visual adapter configured");
        var details = adapter.gameObject.AddComponent<G51IndustrialVisual>();
        Require(details.Initialize(adapter), "G51 presentation detail mesh");
        var camera = UnityEngine.Object.FindFirstObjectByType<ControlStudioOrbitCamera>();
        Require(camera != null && camera.router != null, "scene camera/router references");
        camera.SetPresentationCamera();
        Capture(camera.GetComponent<Camera>(), "g51_edit_view.png");
        var mockHost = new GameObject("Mock UART validation only");
        var uart = mockHost.AddComponent<ControlStudioUartOutput>();
        Require(uart.Connect("", 115200, true), "mock connect");
        Require(uart.Connected && !uart.TxEnabled && uart.HardwareTxCount == 0, "mock starts with real TX zero");
        var send = typeof(ControlStudioUartOutput).GetMethod("QueueApplied", BindingFlags.Instance | BindingFlags.NonPublic);
        Require(send != null, "mock send method");
        var command = new[] { 90f, 70f, 100f, 90f, .05f };
        Require(!(bool)send.Invoke(uart, new object[] { command, true }), "TX OFF blocks packet");
        uart.SetTxEnabled(true);
        Require((bool)send.Invoke(uart, new object[] { command, true }), "mock packet sent");
        Require(uart.MockTxCount == 1 && uart.HardwareTxCount == 0 &&
            uart.LastSent == "M0=90,M1=70,M2=100,M3=90,M4=0.05", "mock five-value contract");
        uart.Disconnect();
        Require(!uart.Connected && !uart.TxEnabled, "mock disconnect disarms");
        UnityEngine.Object.DestroyImmediate(mockHost);
        File.WriteAllText(Path.Combine(output, "edit_validation.txt"),
            "PASS: G51 adapter configured; original mechanical meshes relit; camera rendered; Mock UART TX OFF/send/disconnect; hardware TX=0. Scene not saved.\n");
        Debug.Log("CONTROL STUDIO VISUAL UART EDIT VALIDATION PASS");
    }

    public static void Run()
    {
        Debug.Log("CONTROL STUDIO VISUAL UART VALIDATION START");
        output = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "captures"));
        Directory.CreateDirectory(output);
        EditorSceneManager.OpenScene(Scene);
        EditorApplication.playModeStateChanged += OnMode;
        EditorApplication.isPlaying = true;
    }

    static void OnMode(PlayModeStateChange state)
    {
        if (state == PlayModeStateChange.EnteredPlayMode)
        {
            enteredAt = EditorApplication.timeSinceStartup;
            EditorApplication.update += Check;
        }
        else if (state == PlayModeStateChange.EnteredEditMode && ran)
        {
            EditorApplication.playModeStateChanged -= OnMode;
            EditorApplication.Exit(0);
        }
    }

    static void Check()
    {
        if (ran || EditorApplication.timeSinceStartup - enteredAt < 3) return;
        ran = true;
        EditorApplication.update -= Check;
        try
        {
            var router = UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();
            Require(router != null && router.Ready && router.HasApplied, "router ready/applied");
            var visuals = router.GetComponent<RobotVisualProfiles>();
            var presentation = router.GetComponent<ControlStudioPresentationUI>();
            var uart = router.GetComponent<ControlStudioUartOutput>();
            var orbit = UnityEngine.Object.FindFirstObjectByType<ControlStudioOrbitCamera>();
            Require(visuals != null && presentation != null && uart != null && orbit != null, "studio components");
            Require(visuals.Selected == RobotVisualProfileId.G51, "default G51 family");
            var details = UnityEngine.Object.FindFirstObjectByType<G51IndustrialVisual>();
            Require(details != null && details.transform.Find("G51 three-point product lighting / VISUAL ONLY") != null, "G51 material and lighting details");
            float[] original = (float[])router.Applied.Clone();

            visuals.SetWorkcellVisible(false);
            Require(visuals.SoloRobotView && !visuals.Environment.Root.gameObject.activeSelf, "workcell hidden");
            presentation.SetPresentationMode(true);
            Require(visuals.SoloRobotView, "presentation solo robot");
            Require(GameObject.Find("Presentation status canvas") != null, "compact presentation status");
            Capture(orbit.GetComponent<Camera>(), "g51_robot_only.png");
            presentation.SetPresentationMode(false);
            visuals.SetWorkcellVisible(true);
            Require(!visuals.SoloRobotView && visuals.Environment.Root.gameObject.activeSelf, "workcell restored");
            orbit.SetPresentationCamera();
            Capture(orbit.GetComponent<Camera>(), "g51_workcell.png");

            Require(uart.Connect("", 115200, true), "mock connection");
            Require(!uart.TxEnabled && !uart.SendCurrent(), "mock starts TX OFF");
            uart.SetTxEnabled(true);
            Require(uart.SendCurrent() && uart.MockTxCount == 1, "mock send current Applied");
            Require(uart.LastSent.StartsWith("M0=") && uart.LastSent.Contains(",M4="), "five-value packet");
            Require(uart.HardwareTxCount == 0, "hardware TX zero");
            uart.SetTxEnabled(false);
            Require(!uart.SendCurrent(), "TX OFF blocks send");
            uart.Disconnect();
            for (int i = 0; i < 5; i++) Require(Mathf.Abs(router.Applied[i] - original[i]) < .001f, "Applied unchanged M" + i);
            File.WriteAllText(Path.Combine(output, "validation.txt"),
                "PASS: G51 detail, workcell toggle, presentation status, Mock TX gate/packet, Applied unchanged, hardware TX=0\n");
            Debug.Log("CONTROL STUDIO VISUAL UART ACCEPTANCE PASS");
        }
        catch (Exception e)
        {
            File.WriteAllText(Path.Combine(output, "validation.txt"), "FAIL: " + e + "\n");
            Debug.LogError("CONTROL STUDIO VISUAL UART ACCEPTANCE FAIL: " + e);
            EditorApplication.Exit(1);
            return;
        }
        EditorApplication.isPlaying = false;
    }

    static void Require(bool condition, string name)
    {
        if (!condition) throw new InvalidOperationException(name);
    }

    static void Capture(Camera camera, string filename)
    {
        Require(camera != null, "capture camera");
        var previous = camera.targetTexture;
        var previousRect = camera.rect;
        var previousActive = RenderTexture.active;
        var render = RenderTexture.GetTemporary(1600, 900, 24);
        var texture = new Texture2D(1600, 900, TextureFormat.RGB24, false);
        try
        {
            camera.targetTexture = render;
            camera.rect = new Rect(0, 0, 1, 1);
            camera.Render();
            RenderTexture.active = render;
            texture.ReadPixels(new Rect(0, 0, 1600, 900), 0, 0);
            texture.Apply();
            File.WriteAllBytes(Path.Combine(output, filename), texture.EncodeToPNG());
        }
        finally
        {
            camera.targetTexture = previous;
            camera.rect = previousRect;
            RenderTexture.active = previousActive;
            RenderTexture.ReleaseTemporary(render);
            UnityEngine.Object.DestroyImmediate(texture);
        }
    }
}
