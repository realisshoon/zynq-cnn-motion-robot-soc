using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Explicit Editor menu/CLI launch. Runtime transports retain their disconnected defaults.
public static class ControlStudioLauncher
{
    const string Pending = "ControlStudioInteractiveLaunchPending";

    [MenuItem("Control Studio/Run Demo 07")]
    public static void Run()
    {
        if (EditorApplication.isPlaying) { FocusGameView(); return; }
        if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
        EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        SessionState.SetBool(Pending, true);
        EditorApplication.isPlaying = true;
        Debug.Log("[CONTROL_STUDIO_LAUNCH] Entering Play Mode");
    }

    [InitializeOnLoadMethod]
    static void Register()
    {
        EditorApplication.update += WaitForRuntime;
    }

    static void WaitForRuntime()
    {
        if (!SessionState.GetBool(Pending, false) || !EditorApplication.isPlaying || EditorApplication.isCompiling) return;
        var router = Object.FindFirstObjectByType<SingleArmCommandRouter>();
        var menu = router == null ? null : router.GetComponent<ControlStudioStartupMenu>();
        if (menu == null || router.Status == "STARTING") return;
        SessionState.SetBool(Pending, false);
        FocusGameView();
        if (!router.Ready)
        {
            Debug.LogError("[CONTROL_STUDIO_LAUNCH] Native unavailable: " + router.NativeError);
            return;
        }
        Debug.Log("[CONTROL_STUDIO_LAUNCH] READY unity=" + Application.unityVersion +
            " source=" + router.Source + " startup_menu=" + menu.Visible +
            " native=" + router.Ready + " legacy_tx_connected=" + router.GetComponent<ControlStudioUartOutput>().Connected +
            " legacy_rx_connected=" + router.GetComponent<UartPose3DSource>().Connected);
    }

    static void FocusGameView()
    {
        var gameView = typeof(EditorWindow).Assembly.GetType("UnityEditor.GameView");
        if (gameView != null) EditorWindow.GetWindow(gameView).Focus();
    }
}
