using UnityEngine;

public sealed class DualArmStatusUI : MonoBehaviour
{
    public DualArmDemoController demo;
    private void OnGUI()
    {
        if (demo == null) return;
        GUI.matrix = Matrix4x4.Scale(Vector3.one * Mathf.Max(0.65f, Screen.height / 900f));
        GUILayout.BeginArea(new Rect(20, 20, 340, 340), GUI.skin.box);
        GUILayout.Label("G51  /  DUAL ROBOT DIGITAL TWIN");
        GUILayout.Space(8);
        GUILayout.Label("Mode: " + demo.mode);
        GUILayout.Label("Left: " + demo.LeftState);
        GUILayout.Label("Right: " + demo.RightState);
        GUILayout.Label("Ball: " + demo.transfer.Owner);
        GUILayout.Label("UDP frame: " + demo.rightReceiver.LastAppliedFrameId);
        GUILayout.Label("CSV: " + demo.CsvStatus);
        if (demo.Error.Length > 0) GUILayout.Label(demo.Error);
        if (demo.rightReceiver.LastError.Length > 0) GUILayout.Label(demo.rightReceiver.LastError);
        GUILayout.Space(8);
        if (demo.mode == DualArmDemoController.DemoMode.ScriptedHandoff && GUILayout.Button("START DEMO")) demo.StartDemo();
        if (GUILayout.Button("RESET  [Space]")) demo.ResetDemo();
        GUILayout.BeginHorizontal();
        if (GUILayout.Button("Scripted")) demo.SelectMode(DualArmDemoController.DemoMode.ScriptedHandoff);
        if (GUILayout.Button("CSV Right Arm")) demo.SelectMode(DualArmDemoController.DemoMode.CsvRightArm);
        GUILayout.EndHorizontal();
        if (demo.mode == DualArmDemoController.DemoMode.CsvRightArm)
            GUILayout.Label("Left: independent Idle / Manual\nRight: canonical Agent2 output / UDP 5005");
        GUILayout.EndArea();
        GUI.matrix = Matrix4x4.identity;
    }
}
