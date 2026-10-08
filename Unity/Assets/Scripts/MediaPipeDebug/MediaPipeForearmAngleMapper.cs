using UnityEngine;

namespace HumanMotion.MediaPipeDebug
{
    // Demo_06 FAST VALIDATION용.
    // Shoulder / Elbow / Wrist만으로 M0/M1을 만든다.
    // M2/M3/M4는 hand landmark가 없으므로 HOLD.
    public static class MediaPipeForearmAngleMapper
    {
        // Robot C forearm_calibration_config.c의 M0/M1 servo limits.
        private const float ServoMin = 20f;
        private const float ServoMax = 160f;
        private const float PitchServoMin = 20f;
        private const float PitchServoMax = 180f;

        // Robot C forearm_mapping_internal.h의 singularity hysteresis.
        private const float RollSingularityEnter = 0.02f;
        private const float RollSingularityLeave = 0.04f;

        private static readonly bool[] hasStableRoll = new bool[2];
        private static readonly bool[] rollSingular = new bool[2];
        private static readonly float[] lastStableM0 = { 90f, 90f };

        public struct Result
        {
            public float roll, pitch, elbowBend, rawM0, rawM1;
            public Vector3 upperArm, forearm;
            public ForearmJointCommandData command;

            public bool saturated;
            public bool rollHeld;
        }

        public static bool TryMap(
            PoseFrame frame,
            MediaPipeCoordinateMapping coordinates,
            bool rightArm,
            float confidence,
            out Result result,
            out string reason)
        {
            result = default;
            reason = "No pose";

            if (frame?.landmarks == null ||
                frame.landmarks.Length != 6 ||
                coordinates == null)
                return false;

            int s = rightArm ? 3 : 0;

            int[] required = { 0, 3, s + 1, s + 2 };

            foreach (int i in required)
            {
                var p = frame.landmarks[i];

                if (!p.HasPosition ||
                    !PoseLandmark.Finite(p.visibility) ||
                    !PoseLandmark.Finite(p.presence) ||
                    p.visibility < confidence ||
                    p.presence < confidence)
                {
                    reason = "Missing / low-confidence required landmark " + i;
                    return false;
                }
            }

            Vector3 left =
                coordinates.MediaPipeToUnity(frame.landmarks[0].position);

            Vector3 right =
                coordinates.MediaPipeToUnity(frame.landmarks[3].position);

            Vector3 shoulder = rightArm ? right : left;

            Vector3 elbow =
                coordinates.MediaPipeToUnity(frame.landmarks[s + 1].position);

            Vector3 wrist =
                coordinates.MediaPipeToUnity(frame.landmarks[s + 2].position);

            foreach (var point in new[] { left, right, elbow, wrist })
            {
                if (!PoseLandmark.Finite(point.x) ||
                    !PoseLandmark.Finite(point.y) ||
                    !PoseLandmark.Finite(point.z))
                {
                    reason = "Invalid coordinate mapping";
                    return false;
                }
            }

            // -------------------------------------------------
            // Body coordinate frame
            // x : shoulder left -> right
            // y : body up
            // z : body forward/back direction
            // -------------------------------------------------

            Vector3 x = right - left;

            if (x.sqrMagnitude < 1e-10f)
            {
                reason = "Degenerate shoulders";
                return false;
            }

            x.Normalize();

            Vector3 y =
                Vector3.up -
                Vector3.Dot(Vector3.up, x) * x;

            if (y.sqrMagnitude < 1e-10f)
            {
                reason = "Body up unobservable";
                return false;
            }

            y.Normalize();

            Vector3 z = Vector3.Cross(x, y).normalized;
            y = Vector3.Cross(z, x).normalized;

            result.upperArm = elbow - shoulder;
            result.forearm = wrist - elbow;

            if (result.upperArm.sqrMagnitude < 1e-10f ||
                result.forearm.sqrMagnitude < 1e-10f)
            {
                reason = "Degenerate arm segment";
                return false;
            }

            Vector3 f = result.forearm.normalized;

            float fx = Vector3.Dot(f, x);
            float fy = Vector3.Dot(f, y);
            float fz = Vector3.Dot(f, z);

            float horizontal =
                Mathf.Sqrt(fx * fx + fz * fz);

            int armIndex = rightArm ? 1 : 0;

            rollSingular[armIndex] = horizontal <
                (rollSingular[armIndex]
                    ? RollSingularityLeave
                    : RollSingularityEnter);

            // =================================================
            // M0 : ELBOW ROLL
            //
            // forearm이 거의 수직이면 horizontal projection이
            // 사라져 atan2 방위각이 노이즈에 매우 민감해진다.
            //
            // 이 구간에서는 새로운 roll을 계산하지 않고
            // 마지막 안정값을 유지한다.
            //
            // 최초부터 수직이면 90도를 사용한다.
            // → Robot 정면/중립 기준
            // =================================================

            if (!rollSingular[armIndex])
            {
                float rawRoll =
                    Mathf.Atan2(fx, fz) * Mathf.Rad2Deg;

                // Unity Z 반전 좌표계의 방위각을 Robot C BodyFrame
                // (+Body Z=0, +Body X 쪽 양수) 규약으로 바꾼다.
                result.roll =
                    -Mathf.DeltaAngle(180f, rawRoll);

                // Robot M0의 가운데 90°를 정면으로 사용
                float candidateM0 =
                    90f - result.roll;

                result.rawM0 = candidateM0;

                float m0 =
                    Mathf.Clamp(
                        candidateM0,
                        ServoMin,
                        ServoMax
                    );

                lastStableM0[armIndex] = m0;
                hasStableRoll[armIndex] = true;

                result.rollHeld = false;
            }
            else
            {
                // 수직 singularity.
                // 정면 기준을 잃었으므로 마지막 정상 방향 유지.
                result.roll = float.NaN;

                result.rawM0 =
                    hasStableRoll[armIndex]
                        ? lastStableM0[armIndex]
                        : 90f;

                result.rollHeld = true;
            }

            // =================================================
            // Human forearm elevation
            //
            // -90° : 팔 아래
            //   0° : 수평
            // +90° : 팔 위
            // =================================================

            result.pitch =
                Mathf.Atan2(
                    fy,
                    horizontal
                ) * Mathf.Rad2Deg;

            result.elbowBend =
                Vector3.Angle(
                    result.upperArm,
                    result.forearm
                );

            // =================================================
            // M1 : ELBOW PITCH
            //
            // Robot C forearm_calibration_config.c의 elbow_pitch 계약:
            // scale=1, direction=-1, zero_offset=120, range=[20,180].
            // =================================================

            result.rawM1 = 120f - result.pitch;

            // =================================================
            // FINAL COMMAND
            // =================================================

            result.command =
                new ForearmJointCommandData
                {
                    frame_id =
                        unchecked((uint)frame.frameId),

                    valid = true,

                    elbowRoll =
                        Mathf.Clamp(
                            result.rawM0,
                            ServoMin,
                            ServoMax
                        ),

                    elbowPitch =
                        Mathf.Clamp(
                            result.rawM1,
                            PitchServoMin,
                            PitchServoMax
                        ),

                    // hand landmarks unavailable
                    wristPitch = 100f,
                    wristRoll = 87f,
                    gripper = 1f
                };

            result.saturated =
                result.rawM0 < ServoMin ||
                result.rawM0 > ServoMax ||
                result.rawM1 < PitchServoMin ||
                result.rawM1 > PitchServoMax;

            if (result.rollHeld)
                reason = "LIVE / M0 HOLD-SINGULARITY";
            else if (result.saturated)
                reason = "LIVE / servo limit";
            else
                reason = "LIVE";

            return true;
        }
    }
}
