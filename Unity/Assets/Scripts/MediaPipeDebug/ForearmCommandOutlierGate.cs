using UnityEngine;

namespace HumanMotion.MediaPipeDebug
{
    // Mapper RAW 명령의 단안 추정 spike만 검토한다. Robot safety 기능이 아니다.
    [DisallowMultipleComponent]
    public sealed class ForearmCommandOutlierGate : MonoBehaviour
    {
        [Min(0f)] public float outlierAngularVelocityDegPerSec = 120f;
        [Min(0f)] public float outlierConfirmToleranceDeg = 20f;
        public bool enableOutlierGate = true;

        public ForearmJointCommandData AcceptedCommand { get; private set; }
        public string M0Status => m0.status;
        public string M1Status => m1.status;
        public float M0RawVelocityDegPerSec => m0.velocity;
        public float M1RawVelocityDegPerSec => m1.velocity;
        public long M0ReferenceFrameId => m0.referenceFrameId;
        public long M1ReferenceFrameId => m1.referenceFrameId;
        public bool HoldM0 => m0.hold;
        public bool HoldM1 => m1.hold;

        private readonly AxisState m0 = new AxisState();
        private readonly AxisState m1 = new AxisState();

        public void ResetState()
        {
            m0.Reset(); m1.Reset();
            AcceptedCommand = default;
        }

        public ForearmJointCommandData Process(ForearmJointCommandData raw, float dt, long frameId)
        {
            if (!raw.valid || !PoseLandmark.Finite(raw.elbowRoll) ||
                !PoseLandmark.Finite(raw.elbowPitch))
            {
                HoldInvalid();
                return AcceptedCommand;
            }

            var accepted = raw; // M2/M3/M4 및 frame_id는 원본 그대로 통과한다.
            accepted.elbowRoll = m0.Evaluate(raw.elbowRoll, dt, frameId,
                enableOutlierGate, outlierAngularVelocityDegPerSec, outlierConfirmToleranceDeg);
            accepted.elbowPitch = m1.Evaluate(raw.elbowPitch, dt, frameId,
                enableOutlierGate, outlierAngularVelocityDegPerSec, outlierConfirmToleranceDeg);
            AcceptedCommand = accepted;
            return accepted;
        }

        public void HoldInvalid()
        {
            m0.HoldInvalid(); m1.HoldInvalid();
            var held = AcceptedCommand;
            held.valid = false;
            AcceptedCommand = held;
        }

        private sealed class AxisState
        {
            private bool hasAccepted, suspect, fastMotion;
            private float lastAcceptedRaw, suspectRaw;
            private int suspectSamples, fastDirection;
            private long suspectFrameId;
            public string status = "WAITING";
            public float velocity = float.NaN;
            public long referenceFrameId = -1;
            public bool hold;

            public void Reset()
            {
                hasAccepted = suspect = fastMotion = hold = false;
                lastAcceptedRaw = suspectRaw = 0f;
                suspectSamples = fastDirection = 0;
                suspectFrameId = referenceFrameId = -1;
                status = "WAITING"; velocity = float.NaN;
            }

            public void HoldInvalid()
            {
                referenceFrameId = suspect ? suspectFrameId : -1;
                suspect = fastMotion = false;
                suspectSamples = fastDirection = 0;
                status = "INVALID_HOLD";
                velocity = float.NaN;
                hold = true;
            }

            public float Evaluate(float raw, float dt, long frameId, bool enabled,
                float threshold, float confirmTolerance)
            {
                hold = false;
                referenceFrameId = -1;
                velocity = float.NaN;
                if (!hasAccepted)
                {
                    hasAccepted = true;
                    lastAcceptedRaw = raw;
                    status = "STARTUP";
                    return raw;
                }

                if (PoseLandmark.Finite(dt) && dt > 0f)
                    velocity = Mathf.Abs(raw - lastAcceptedRaw) / dt;

                if (!enabled)
                {
                    lastAcceptedRaw = raw;
                    suspect = fastMotion = false;
                    status = "BYPASS";
                    return raw;
                }

                if (!PoseLandmark.Finite(velocity))
                {
                    status = "DT_UNAVAILABLE_HOLD";
                    hold = true;
                    return lastAcceptedRaw;
                }

                if (velocity <= threshold)
                {
                    status = suspect ? "REJECTED_SPIKE" : "NORMAL";
                    if (suspect) referenceFrameId = suspectFrameId;
                    lastAcceptedRaw = raw;
                    suspect = fastMotion = false;
                    return raw;
                }

                int direction = raw > lastAcceptedRaw ? 1 : -1;
                if (fastMotion && direction == fastDirection &&
                    Mathf.Abs(raw - lastAcceptedRaw) <= confirmTolerance)
                {
                    lastAcceptedRaw = raw;
                    status = "CONFIRMED_FAST_MOTION";
                    return raw;
                }

                fastMotion = false;
                if (!suspect || direction != (suspectRaw > lastAcceptedRaw ? 1 : -1))
                {
                    suspect = true;
                    suspectRaw = raw;
                    suspectFrameId = frameId;
                    suspectSamples = 1;
                }
                else
                {
                    suspectRaw = raw;
                    ++suspectSamples;
                }

                referenceFrameId = suspectFrameId;
                if (suspectSamples >= 3)
                {
                    lastAcceptedRaw = raw;
                    suspect = false;
                    fastMotion = true;
                    fastDirection = direction;
                    status = "CONFIRMED_FAST_MOTION";
                    return raw;
                }

                status = "SUSPECT";
                hold = true;
                return lastAcceptedRaw;
            }
        }
    }
}
