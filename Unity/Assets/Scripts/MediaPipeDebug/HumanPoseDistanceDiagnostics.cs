using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using UnityEngine;

namespace HumanMotion.MediaPipeDebug
{
    /// <summary>원본 MediaPipe world XYZ만 읽는 양팔 거리 진단. Robot command에는 관여하지 않는다.</summary>
    [DisallowMultipleComponent]
    public sealed class HumanPoseDistanceDiagnostics : MonoBehaviour
    {
        [Serializable]
        public sealed class ArmMeasurement
        {
            public bool available, lowConfidence;
            public Vector3 shoulderXYZ, elbowXYZ, wristXYZ;
            public float shoulderVisibility, shoulderPresence;
            public float elbowVisibility, elbowPresence;
            public float wristVisibility, wristPresence;
            public float upperArmDistance, forearmDistance, shoulderWristDistance;
            public float forearmDx, forearmDy, forearmDz;

            public void Capture(PoseLandmark shoulder, PoseLandmark elbow, PoseLandmark wrist, float threshold)
            {
                shoulderXYZ = shoulder.position; elbowXYZ = elbow.position; wristXYZ = wrist.position;
                shoulderVisibility = shoulder.visibility; shoulderPresence = shoulder.presence;
                elbowVisibility = elbow.visibility; elbowPresence = elbow.presence;
                wristVisibility = wrist.visibility; wristPresence = wrist.presence;
                lowConfidence = Low(shoulder, threshold) || Low(elbow, threshold) || Low(wrist, threshold);
                available = shoulder.HasPosition && elbow.HasPosition && wrist.HasPosition;
                if (!available) { ClearDistances(); return; }

                Vector3 forearm = wrist.position - elbow.position;
                upperArmDistance = (elbow.position - shoulder.position).magnitude;
                forearmDistance = forearm.magnitude;
                shoulderWristDistance = (wrist.position - shoulder.position).magnitude;
                forearmDx = forearm.x; forearmDy = forearm.y; forearmDz = forearm.z;
                available = PoseLandmark.Finite(upperArmDistance) && PoseLandmark.Finite(forearmDistance) &&
                    PoseLandmark.Finite(shoulderWristDistance) && PoseLandmark.Finite(forearmDx) &&
                    PoseLandmark.Finite(forearmDy) && PoseLandmark.Finite(forearmDz);
                if (!available) ClearDistances();
            }

            public void Clear()
            {
                available = false; lowConfidence = true;
                shoulderXYZ = elbowXYZ = wristXYZ = Vector3.one * float.NaN;
                shoulderVisibility = shoulderPresence = elbowVisibility = elbowPresence = -1f;
                wristVisibility = wristPresence = -1f;
                ClearDistances();
            }

            private void ClearDistances()
            {
                upperArmDistance = forearmDistance = shoulderWristDistance = float.NaN;
                forearmDx = forearmDy = forearmDz = float.NaN;
            }

            private static bool Low(PoseLandmark point, float threshold) =>
                !point.HasPosition || !PoseLandmark.Finite(point.visibility) ||
                !PoseLandmark.Finite(point.presence) || point.visibility < threshold ||
                point.presence < threshold;
        }

        [Header("Input")]
        public UdpMediaPipePoseSource source;
        public MediaPipeRobotRetargetController retarget;
        [Range(0f, 1f)] public float confidenceThreshold = .5f;
        public bool recordCsv;

        [Header("Runtime diagnostics - MediaPipe raw world XYZ (m, model estimate)")]
        [SerializeField] private long frameId = -1;
        [SerializeField] private double timestamp;
        [SerializeField] private bool packetValid;
        [SerializeField] private string status = "Waiting for UDP pose";
        [SerializeField] private ArmMeasurement left = new ArmMeasurement();
        [SerializeField] private ArmMeasurement right = new ArmMeasurement();
        [SerializeField] private string csvPath = "";
        [SerializeField] private string csvError = "";
        [Header("Latest mapped command (active arm only)")]
        [SerializeField] private long mappedFrameId = -1;
        [SerializeField] private string mappedState = "Waiting for pose";
        [SerializeField] private float m0CommandDeg = float.NaN;
        [SerializeField] private float m1CommandDeg = float.NaN;
        [SerializeField] private float m0RawDeg = float.NaN;
        [SerializeField] private float m1RawDeg = float.NaN;
        [SerializeField] private float m0StabilizedDeg = float.NaN;
        [SerializeField] private float m1StabilizedDeg = float.NaN;
        [SerializeField] private float m0OutlierAcceptedDeg = float.NaN;
        [SerializeField] private float m1OutlierAcceptedDeg = float.NaN;
        [SerializeField] private string m0OutlierStatus = "WAITING";
        [SerializeField] private string m1OutlierStatus = "WAITING";
        [SerializeField] private float m0RawVelocityDegPerSec = float.NaN;
        [SerializeField] private float m1RawVelocityDegPerSec = float.NaN;
        [SerializeField] private long m0OutlierReferenceFrameId = -1;
        [SerializeField] private long m1OutlierReferenceFrameId = -1;
        [SerializeField] private float lastAppliedM0Deg = float.NaN;
        [SerializeField] private float lastAppliedM1Deg = float.NaN;

        public long FrameId => frameId;
        public double Timestamp => timestamp;
        public bool PacketValid => packetValid;
        public ArmMeasurement Left => left;
        public ArmMeasurement Right => right;
        private StreamWriter writer;
        private int rowsSinceFlush;
        private PoseFrame pendingFrame;
        private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;

        private void OnEnable()
        {
            if (source == null) source = GetComponent<UdpMediaPipePoseSource>();
            if (retarget == null) retarget = GetComponent<MediaPipeRobotRetargetController>();
            if (source == null) { status = "UdpMediaPipePoseSource reference missing"; return; }
            source.FrameChanged += OnFrame;
            OnFrame(source.Current);
        }

        private void Update()
        {
            if (!recordCsv && writer != null) CloseCsv();
            else if (recordCsv && writer == null && source != null) OpenCsv();
        }

        private void LateUpdate()
        {
            if (pendingFrame == null) return;
            PoseFrame frame = pendingFrame;
            pendingFrame = null;

            // FrameChanged에서 즉시 읽으면 retarget subscriber가 아직 실행되지 않았을 수 있다.
            // 모든 Update/event 처리가 끝난 뒤, 적용된 frame id를 확인한다.
            ObserveCommand(frame);
            if (!recordCsv) return;
            if (writer == null) OpenCsv();
            if (writer != null) WriteCsvRow();
        }

        private void OnDisable()
        {
            if (source != null) source.FrameChanged -= OnFrame;
            pendingFrame = null;
            CloseCsv();
        }

        private void OnFrame(PoseFrame frame)
        {
            if (frame == null)
            {
                frameId = -1; timestamp = 0; packetValid = false;
                status = "Waiting for UDP pose"; left.Clear(); right.Clear();
                pendingFrame = null; mappedFrameId = -1;
                mappedState = "Waiting for pose";
                m0CommandDeg = m1CommandDeg = float.NaN;
                m0RawDeg = m1RawDeg = m0StabilizedDeg = m1StabilizedDeg = float.NaN;
                m0OutlierAcceptedDeg = m1OutlierAcceptedDeg = float.NaN;
                m0OutlierStatus = m1OutlierStatus = "WAITING";
                m0RawVelocityDegPerSec = m1RawVelocityDegPerSec = float.NaN;
                m0OutlierReferenceFrameId = m1OutlierReferenceFrameId = -1;
                return;
            }

            frameId = frame.frameId; timestamp = frame.timeSec; packetValid = frame.valid;
            if (frame.landmarks == null || frame.landmarks.Length != 6)
            {
                status = "Landmark array unavailable"; left.Clear(); right.Clear();
            }
            else
            {
                left.Capture(frame.landmarks[0], frame.landmarks[1], frame.landmarks[2], confidenceThreshold);
                right.Capture(frame.landmarks[3], frame.landmarks[4], frame.landmarks[5], confidenceThreshold);
                status = !packetValid ? "Packet valid=false; finite RAW distances retained" :
                    left.lowConfidence || right.lowConfidence ? "LOW CONFIDENCE; finite RAW distances retained" :
                    "MediaPipe raw world XYZ (m, model estimate)";
            }

            pendingFrame = frame;
        }

        private void ObserveCommand(PoseFrame frame)
        {
            mappedFrameId = -1;
            m0CommandDeg = m1CommandDeg = float.NaN;
            m0RawDeg = m1RawDeg = m0StabilizedDeg = m1StabilizedDeg = float.NaN;
            m0OutlierAcceptedDeg = m1OutlierAcceptedDeg = float.NaN;
            m0OutlierStatus = m1OutlierStatus = "WAITING";
            m0RawVelocityDegPerSec = m1RawVelocityDegPerSec = float.NaN;
            m0OutlierReferenceFrameId = m1OutlierReferenceFrameId = -1;
            if (retarget == null || !retarget.isActiveAndEnabled || retarget.source != source)
            { mappedState = "RETARGET_UNAVAILABLE"; return; }
            if (!ReferenceEquals(source.Current, frame))
            { mappedState = "SOURCE_FRAME_CHANGED"; return; }

            mappedFrameId = retarget.AppliedFrameId;
            var command = retarget.Command;
            var gate = retarget.outlierGate;
            if (gate != null && gate.isActiveAndEnabled)
            {
                m0OutlierStatus = gate.M0Status; m1OutlierStatus = gate.M1Status;
                m0RawVelocityDegPerSec = gate.M0RawVelocityDegPerSec;
                m1RawVelocityDegPerSec = gate.M1RawVelocityDegPerSec;
                m0OutlierReferenceFrameId = gate.M0ReferenceFrameId;
                m1OutlierReferenceFrameId = gate.M1ReferenceFrameId;
            }
            else m0OutlierStatus = m1OutlierStatus = retarget.Live ? "BYPASS" : "INVALID_HOLD";
            if (retarget.Live && command.valid && mappedFrameId == frame.frameId)
            {
                var raw = retarget.RawCommand;
                if (!raw.valid) { mappedState = "RAW_COMMAND_UNAVAILABLE"; return; }
                var accepted = retarget.AcceptedCommand;
                m0RawDeg = raw.elbowRoll;
                m1RawDeg = raw.elbowPitch;
                if (accepted.valid)
                { m0OutlierAcceptedDeg = accepted.elbowRoll; m1OutlierAcceptedDeg = accepted.elbowPitch; }
                m0CommandDeg = command.elbowRoll;
                m1CommandDeg = command.elbowPitch;
                m0StabilizedDeg = m0CommandDeg;
                m1StabilizedDeg = m1CommandDeg;
                lastAppliedM0Deg = m0CommandDeg;
                lastAppliedM1Deg = m1CommandDeg;
                mappedState = retarget.Status.Contains("M0 HOLD-SINGULARITY") ? "M0_SINGULARITY_HOLD" : "LIVE";
            }
            else if (!retarget.Live && mappedFrameId == -1)
            {
                mappedState = "INVALID_NO_APPLY";
                m0StabilizedDeg = lastAppliedM0Deg;
                m1StabilizedDeg = lastAppliedM1Deg;
            }
            else mappedState = "FRAME_MISMATCH";
        }

        private void OpenCsv()
        {
            try
            {
                string project = Directory.GetParent(Application.dataPath).FullName;
                string folder = Path.Combine(project, "Validation", "MediaPipeDistance");
                Directory.CreateDirectory(folder);
                csvPath = Path.Combine(folder, "pose_distance_" + DateTime.Now.ToString("yyyyMMdd_HHmmss_fff") + ".csv");
                writer = new StreamWriter(new FileStream(csvPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read));
                writer.WriteLine(CsvHeader());
                rowsSinceFlush = 0; csvError = "";
            }
            catch (IOException e) { CsvFailed(e); }
            catch (UnauthorizedAccessException e) { CsvFailed(e); }
        }

        private void WriteCsvRow()
        {
            try
            {
                var values = new List<string>(100) {
                    frameId.ToString(Invariant), timestamp.ToString("G17", Invariant), packetValid ? "1" : "0"
                };
                AppendArm(values, left, retarget != null && !retarget.rightArm);
                AppendArm(values, right, retarget != null && retarget.rightArm);
                values.Add(retarget == null ? "" : retarget.StabilizationEnabled ? "1" : "0");
                AppendStabilization(values, retarget != null && !retarget.rightArm);
                AppendStabilization(values, retarget != null && retarget.rightArm);
                values.Add(retarget == null ? "" : retarget.OutlierGateEnabled ? "1" : "0");
                AppendOutlier(values, retarget != null && !retarget.rightArm);
                AppendOutlier(values, retarget != null && retarget.rightArm);
                writer.WriteLine(string.Join(",", values));
                if (++rowsSinceFlush >= 30) { writer.Flush(); rowsSinceFlush = 0; }
            }
            catch (IOException e) { CsvFailed(e); }
            catch (UnauthorizedAccessException e) { CsvFailed(e); }
        }

        private void CsvFailed(Exception e)
        {
            csvError = e.Message;
            recordCsv = false;
            CloseCsv();
        }

        private void CloseCsv()
        {
            if (writer == null) return;
            writer.Dispose(); writer = null;
            rowsSinceFlush = 0;
        }

        private static string CsvHeader()
        {
            var fields = new List<string> { "frame_id", "timestamp", "packet_valid" };
            foreach (string side in new[] { "left", "right" })
            {
                foreach (string joint in new[] { "shoulder", "elbow", "wrist" })
                    foreach (string field in new[] { "x", "y", "z", "visibility", "presence" })
                        fields.Add(side + "_" + joint + "_" + field);
                foreach (string field in new[] { "upper_arm_distance", "forearm_distance",
                    "shoulder_wrist_distance", "forearm_dx", "forearm_dy", "forearm_dz",
                    "available", "low_confidence" })
                    fields.Add(side + "_" + field);
                foreach (string field in new[] { "m0_command_deg", "m1_command_deg",
                    "m0_live", "m1_live", "m0_state", "m1_state",
                    "applied_frame_id", "m0_last_applied_deg", "m1_last_applied_deg" })
                    fields.Add(side + "_" + field);
            }
            fields.Add("stabilization_enabled");
            foreach (string side in new[] { "left", "right" })
                foreach (string field in new[] { "m0_raw_deg", "m1_raw_deg",
                    "m0_stabilized_deg", "m1_stabilized_deg",
                    "m0_stabilizer_delta_deg", "m1_stabilizer_delta_deg" })
                    fields.Add(side + "_" + field);
            fields.Add("outlier_gate_enabled");
            foreach (string side in new[] { "left", "right" })
                foreach (string field in new[] { "m0_outlier_accepted_deg", "m1_outlier_accepted_deg",
                    "m0_outlier_status", "m1_outlier_status",
                    "m0_raw_velocity_deg_s", "m1_raw_velocity_deg_s",
                    "m0_outlier_reference_frame_id", "m1_outlier_reference_frame_id" })
                    fields.Add(side + "_" + field);
            return string.Join(",", fields);
        }

        private void AppendOutlier(List<string> values, bool mappedArm)
        {
            values.Add(mappedArm ? Number(m0OutlierAcceptedDeg) : "");
            values.Add(mappedArm ? Number(m1OutlierAcceptedDeg) : "");
            values.Add(mappedArm ? m0OutlierStatus : "");
            values.Add(mappedArm ? m1OutlierStatus : "");
            values.Add(mappedArm ? Number(m0RawVelocityDegPerSec) : "");
            values.Add(mappedArm ? Number(m1RawVelocityDegPerSec) : "");
            values.Add(mappedArm && m0OutlierReferenceFrameId >= 0
                ? m0OutlierReferenceFrameId.ToString(Invariant) : "");
            values.Add(mappedArm && m1OutlierReferenceFrameId >= 0
                ? m1OutlierReferenceFrameId.ToString(Invariant) : "");
        }

        private void AppendStabilization(List<string> values, bool mappedArm)
        {
            values.Add(mappedArm ? Number(m0RawDeg) : "");
            values.Add(mappedArm ? Number(m1RawDeg) : "");
            values.Add(mappedArm ? Number(m0StabilizedDeg) : "");
            values.Add(mappedArm ? Number(m1StabilizedDeg) : "");
            values.Add(mappedArm && PoseLandmark.Finite(m0RawDeg) && PoseLandmark.Finite(m0StabilizedDeg)
                ? Number(m0StabilizedDeg - m0RawDeg) : "");
            values.Add(mappedArm && PoseLandmark.Finite(m1RawDeg) && PoseLandmark.Finite(m1StabilizedDeg)
                ? Number(m1StabilizedDeg - m1RawDeg) : "");
        }

        private void AppendArm(List<string> values, ArmMeasurement arm, bool mappedArm)
        {
            AppendJoint(values, arm.shoulderXYZ, arm.shoulderVisibility, arm.shoulderPresence);
            AppendJoint(values, arm.elbowXYZ, arm.elbowVisibility, arm.elbowPresence);
            AppendJoint(values, arm.wristXYZ, arm.wristVisibility, arm.wristPresence);
            values.Add(Number(arm.upperArmDistance)); values.Add(Number(arm.forearmDistance));
            values.Add(Number(arm.shoulderWristDistance));
            values.Add(Number(arm.forearmDx)); values.Add(Number(arm.forearmDy)); values.Add(Number(arm.forearmDz));
            values.Add(arm.available ? "1" : "0"); values.Add(arm.lowConfidence ? "1" : "0");
            if (!mappedArm)
            {
                values.Add(""); values.Add(""); values.Add(""); values.Add("");
                values.Add("NOT_MAPPED"); values.Add("NOT_MAPPED");
                values.Add(""); values.Add(""); values.Add("");
                return;
            }
            bool alignedLive = mappedFrameId == frameId && PoseLandmark.Finite(m0CommandDeg) &&
                PoseLandmark.Finite(m1CommandDeg);
            values.Add(alignedLive ? Number(m0CommandDeg) : "");
            values.Add(alignedLive ? Number(m1CommandDeg) : "");
            values.Add(mappedState == "FRAME_MISMATCH" || mappedState == "SOURCE_FRAME_CHANGED" ||
                mappedState == "RETARGET_UNAVAILABLE" ? "" : alignedLive ? "1" : "0");
            values.Add(mappedState == "FRAME_MISMATCH" || mappedState == "SOURCE_FRAME_CHANGED" ||
                mappedState == "RETARGET_UNAVAILABLE" ? "" : alignedLive ? "1" : "0");
            values.Add(mappedState);
            values.Add(alignedLive ? "LIVE" : mappedState);
            values.Add(mappedFrameId >= 0 ? mappedFrameId.ToString(Invariant) : "");
            values.Add(Number(lastAppliedM0Deg)); values.Add(Number(lastAppliedM1Deg));
        }

        private static void AppendJoint(List<string> values, Vector3 xyz, float visibility, float presence)
        {
            values.Add(Number(xyz.x)); values.Add(Number(xyz.y)); values.Add(Number(xyz.z));
            values.Add(Number(visibility)); values.Add(Number(presence));
        }

        private static string Number(float value) =>
            PoseLandmark.Finite(value) ? value.ToString("G9", Invariant) : "";
    }
}
