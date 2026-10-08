using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using HumanMotion.MediaPipeDebug;
using UnityEngine;

// Demo_06 only.
// Keeps live M0/M1 and replaces only M2/M3/M4 using the recorded Robot-C result.
[DisallowMultipleComponent]
public sealed class Demo06M234Replay : MonoBehaviour
{
    [Header("References")]
    public MediaPipeRobotRetargetController retarget;

    [Header("CSV")]
    public string csvPath =
        "Validation/HandPoseM234/WristGripper/Results/wrist_gripper_joint_replay.csv";

    [Header("Replay")]
    [Tooltip("Live UDP frame_id와 CSV frame_id를 맞춰 M2/M3/M4를 적용합니다.")]
    public bool applyReplay;

    [Header("Channels")]
    public bool overrideM2 = true;

    // M3는 ±180 wrap 검증 전까지 기본 OFF.
    public bool overrideM3 = false;

    public bool overrideM4 = false;

    [Header("Runtime read only")]
    [SerializeField] private bool csvLoaded;
    [SerializeField] private int loadedRows;
    [SerializeField] private bool rowFound;
    [SerializeField] private bool applied;

    [SerializeField] private uint liveFrameId;
    [SerializeField] private int csvValid;

    [SerializeField] private float appliedM2;
    [SerializeField] private float appliedM3;
    [SerializeField] private float appliedM4;

    private readonly Dictionary<uint, ReplayRow> rows =
        new Dictionary<uint, ReplayRow>();

    private struct ReplayRow
    {
        public uint frameId;
        public int valid;
        public float m2;
        public float m3;
        public float m4;
    }

    private void Awake()
    {
        LoadCsv();
    }

    private void LateUpdate()
    {
        applied = false;
        rowFound = false;

        if (!applyReplay)
            return;

        if (!csvLoaded)
            return;

        if (retarget == null ||
            retarget.robot == null ||
            !retarget.robot.IsConfigured)
            return;

        // M0/M1은 현재 live MediaPipe command를 그대로 사용한다.
        if (!retarget.Live || !retarget.Command.valid)
            return;

        ForearmJointCommandData command = retarget.Command;

        liveFrameId = command.frame_id;

        if (!rows.TryGetValue(liveFrameId, out ReplayRow row))
            return;

        rowFound = true;
        csvValid = row.valid;

        // CSV는 invalid frame에서도 이전 M2/M3/M4 HOLD 값을 이미 담고 있다.
        if (overrideM2)
            command.wristPitch = row.m2;

        if (overrideM3)
            command.wristRoll = row.m3;

        if (overrideM4)
            command.gripper = row.m4;

        applied = retarget.robot.ApplyCommand(command);

        if (!applied)
            return;

        appliedM2 = command.wristPitch;
        appliedM3 = command.wristRoll;
        appliedM4 = command.gripper;
    }

    private void LoadCsv()
    {
        rows.Clear();
        csvLoaded = false;
        loadedRows = 0;

        if (string.IsNullOrWhiteSpace(csvPath))
        {
            Debug.LogError("[Demo06M234Replay] csvPath is empty.");
            return;
        }

        if (!File.Exists(csvPath))
        {
            Debug.LogError(
                $"[Demo06M234Replay] CSV not found: {csvPath}");
            return;
        }

        string[] lines = File.ReadAllLines(csvPath);

        if (lines.Length < 2)
        {
            Debug.LogError("[Demo06M234Replay] CSV has no data.");
            return;
        }

        string[] header = SplitCsvLine(lines[0]);

        int frameIdx = FindColumn(header, "frame_id");
        int validIdx = FindColumn(header, "valid");
        int m2Idx = FindColumn(header, "m2");
        int m3Idx = FindColumn(header, "m3");
        int m4Idx = FindColumn(header, "m4");

        if (frameIdx < 0 ||
            validIdx < 0 ||
            m2Idx < 0 ||
            m3Idx < 0 ||
            m4Idx < 0)
        {
            Debug.LogError(
                "[Demo06M234Replay] Required CSV column missing.");
            return;
        }

        for (int i = 1; i < lines.Length; i++)
        {
            if (string.IsNullOrWhiteSpace(lines[i]))
                continue;

            string[] cols = SplitCsvLine(lines[i]);

            int maxRequired =
                Mathf.Max(
                    Mathf.Max(frameIdx, validIdx),
                    Mathf.Max(m2Idx, Mathf.Max(m3Idx, m4Idx)));

            if (cols.Length <= maxRequired)
                continue;

            if (!uint.TryParse(
                    cols[frameIdx],
                    NumberStyles.Integer,
                    CultureInfo.InvariantCulture,
                    out uint frameId))
                continue;

            if (!int.TryParse(
                    cols[validIdx],
                    NumberStyles.Integer,
                    CultureInfo.InvariantCulture,
                    out int valid))
                valid = 0;

            if (!float.TryParse(
                    cols[m2Idx],
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out float m2))
                continue;

            if (!float.TryParse(
                    cols[m3Idx],
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out float m3))
                continue;

            if (!float.TryParse(
                    cols[m4Idx],
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out float m4))
                continue;

            rows[frameId] = new ReplayRow
            {
                frameId = frameId,
                valid = valid,
                m2 = m2,
                m3 = m3,
                m4 = m4
            };
        }

        loadedRows = rows.Count;
        csvLoaded = loadedRows > 0;

        Debug.Log(
            $"[Demo06M234Replay] Loaded {loadedRows} rows.");
    }

    private static string[] SplitCsvLine(string line)
    {
        string[] values = line.Split(',');

        for (int i = 0; i < values.Length; i++)
        {
            values[i] = values[i]
                .Trim()
                .Trim('"')
                .Trim('\uFEFF');
        }

        return values;
    }

    private static int FindColumn(string[] header, string name)
    {
        for (int i = 0; i < header.Length; i++)
        {
            if (string.Equals(
                    header[i],
                    name,
                    StringComparison.OrdinalIgnoreCase))
                return i;
        }

        return -1;
    }
}
