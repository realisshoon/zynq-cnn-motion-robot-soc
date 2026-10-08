using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;
using HumanMotion.MediaPipeDebug;

[InitializeOnLoad]
public static class Demo06LiveCheck
{
    const string Key = "Demo06.LiveCheck";
    static double start;
    static long frames, valid;
    static bool preview;
    static Demo06LiveCheck() { EditorApplication.update += Tick; }
    public static void Begin()
    {
        if (EditorApplication.isPlaying || SceneManager.GetActiveScene().path != Demo06Workbench.ScenePath) return;
        SessionState.SetBool(Key, true); EditorApplication.isPlaying = true;
    }
    static void Tick()
    {
        if (!SessionState.GetBool(Key, false) || !EditorApplication.isPlaying) return;
        var source = Object.FindFirstObjectByType<UdpMediaPipePoseSource>();
        var adapter = Object.FindFirstObjectByType<MediaPipeRobotRetargetController>();
        if (source == null || adapter == null) return;
        if (start == 0) start = EditorApplication.timeSinceStartup;
        frames = source.AcceptedPackets; preview |= source.HasPreview;
        if (adapter.Live) valid++;
        if (EditorApplication.timeSinceStartup - start < 45) return;
        File.WriteAllText(Demo06Workbench.Folder + "/webcam-check.txt",
            $"Accepted packets={frames}\nPreview observed={preview}\nValid robot observations={valid}\nSource error={source.Error}\nStatus={adapter.Status}\n");
        SessionState.SetBool(Key, false); EditorApplication.isPlaying = false;
    }
}
