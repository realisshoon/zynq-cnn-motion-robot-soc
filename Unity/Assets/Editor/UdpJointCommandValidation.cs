using System;
using System.IO;
using UnityEditor;
using UnityEngine;

/// <summary>명시적으로 시작한 검증 중에만 실제 Play Mode 상태를 파일로 기록한다.</summary>
[InitializeOnLoad]
public static class UdpJointCommandValidation
{
    private const string CaptureKey = "RobotArm.UdpValidationCapture";
    private const string JoinedKey = "RobotArm.UdpValidationJoined";
    private static UdpJointCommandReceiver observed;
    private static double nextCapture;

    [Serializable]
    private sealed class Capture
    {
        public long utc_ms;
        public bool playing, listening, lastStopJoined, cleanupObserved;
        public int mode, port;
        public long received, malformed, stale, invalid, applied;
        public uint lastReceivedFrameId, lastAppliedFrameId;
        public JointCommandData command;
        public float[] localAngles;
        public float elbowWorldX;
        public Vector3 left, right;
        public string error;
    }

    static UdpJointCommandValidation() { EditorApplication.update += CaptureUpdate; }

    [MenuItem("Tools/Human Motion/Start UDP Validation Capture")]
    public static void StartCapture()
    {
        SessionState.SetBool(CaptureKey, true);
        SessionState.SetBool(JoinedKey, false);
        SessionState.SetBool(JoinedKey + ".observed", false);
        Debug.Log("UDP 검증 캡처 시작: Tools/verify_udp_joint_command.py 실행 후 Play 종료까지 확인하세요.");
    }

    [MenuItem("Tools/Human Motion/Stop UDP Validation Capture")]
    public static void StopCapture()
    {
        SessionState.SetBool(CaptureKey, false);
        if (observed != null) observed.ListenerStopped -= OnStopped;
        observed = null;
    }

    private static void OnStopped(bool joined)
    {
        SessionState.SetBool(JoinedKey, joined);
        SessionState.SetBool(JoinedKey + ".observed", true);
    }

    private static void CaptureUpdate()
    {
        if (!SessionState.GetBool(CaptureKey, false) || EditorApplication.timeSinceStartup < nextCapture) return;
        nextCapture = EditorApplication.timeSinceStartup + 0.04;
        var receiver = UnityEngine.Object.FindFirstObjectByType<UdpJointCommandReceiver>();
        if (receiver == null || receiver.controller == null) return;
        if (observed != receiver)
        {
            if (observed != null) observed.ListenerStopped -= OnStopped;
            observed = receiver;
            receiver.ListenerStopped += OnStopped;
        }
        var c = receiver.controller;
        var stats = receiver.GetStatistics();
        var capture = new Capture
        {
            utc_ms = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            playing = EditorApplication.isPlaying,
            listening = receiver.IsListening,
            lastStopJoined = SessionState.GetBool(JoinedKey, false),
            cleanupObserved = SessionState.GetBool(JoinedKey + ".observed", false),
            mode = (int)c.inputMode, port = receiver.port,
            received = stats.received, malformed = stats.malformed, stale = stats.stale, invalid = stats.invalid,
            applied = receiver.AppliedCount, lastReceivedFrameId = stats.lastReceivedFrameId,
            lastAppliedFrameId = receiver.LastAppliedFrameId, command = receiver.LastAppliedCommand,
            localAngles = new[] { c.baseYaw.pivot.localEulerAngles.y, c.shoulderPitch.pivot.localEulerAngles.x,
                c.elbowPitch.pivot.localEulerAngles.x, c.wristPitch.pivot.localEulerAngles.x, c.wristRoll.pivot.localEulerAngles.y },
            elbowWorldX = c.elbowPitch.pivot.eulerAngles.x,
            left = c.fingerLeft.localPosition, right = c.fingerRight.localPosition, error = receiver.LastError
        };
        string folder = Path.GetFullPath(Path.Combine(Application.dataPath, "../Validation"));
        Directory.CreateDirectory(folder);
        string path = Path.Combine(folder, "udp_status.json");
        try
        {
            File.WriteAllText(path + ".tmp", JsonUtility.ToJson(capture, true));
            if (File.Exists(path)) File.Replace(path + ".tmp", path, null);
            else File.Move(path + ".tmp", path);
        }
        catch (IOException) { /* 읽기 경합 시 다음 Editor update에서 다시 기록한다. */ }
    }
}

[CustomEditor(typeof(UdpJointCommandReceiver))]
public sealed class UdpJointCommandReceiverEditor : Editor
{
    public override void OnInspectorGUI()
    {
        DrawDefaultInspector();
        var receiver = (UdpJointCommandReceiver)target;
        var stats = receiver.GetStatistics();
        EditorGUILayout.HelpBox($"Listening: {receiver.IsListening}\nReceived: {stats.received} / Applied: {receiver.AppliedCount}\n" +
            $"Frame: {receiver.LastAppliedFrameId}\nMalformed: {stats.malformed} / Stale: {stats.stale} / Invalid: {stats.invalid}\n" +
            $"{receiver.LastError}", string.IsNullOrEmpty(receiver.LastError) ? MessageType.Info : MessageType.Warning);
        using (new EditorGUI.DisabledScope(!Application.isPlaying))
            if (GUILayout.Button("Restart Listener / Reset Frame Sequence")) receiver.RestartListener();
        if (Application.isPlaying) Repaint();
    }
}
