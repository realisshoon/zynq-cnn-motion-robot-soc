using UnityEngine;

namespace HumanMotion.MediaPipeDebug
{
    // Mapper의 C-contract 명령 뒤, Demo_06 Robot visual 앞에서만 동작한다.
    [DisallowMultipleComponent]
    public sealed class ForearmCommandStabilizer : MonoBehaviour
    {
        public bool enableStabilization = true;
        [Min(0f)] public float deadbandDeg = 1f;
        [Min(0f)] public float smoothingTimeConstantSec = .25f;
        [Min(0f)] public float maxAngularRateDegPerSec = 90f;

        public bool HasOutput { get; private set; }
        public ForearmJointCommandData LastOutput { get; private set; }

        public void ResetState()
        {
            HasOutput = false;
            LastOutput = default;
        }

        public ForearmJointCommandData Process(ForearmJointCommandData raw, float dt)
        {
            if (!raw.valid || !PoseLandmark.Finite(raw.elbowRoll) ||
                !PoseLandmark.Finite(raw.elbowPitch))
            {
                var held = LastOutput;
                held.valid = false; // invalid frame을 새 Robot command로 적용하지 않는다.
                return held;
            }

            var output = raw; // frame_id 및 M2/M3/M4는 항상 RAW pass-through.
            if (HasOutput && enableStabilization)
            {
                output.elbowRoll = Step(LastOutput.elbowRoll, raw.elbowRoll, dt);
                output.elbowPitch = Step(LastOutput.elbowPitch, raw.elbowPitch, dt);
            }
            LastOutput = output;
            HasOutput = true;
            return output;
        }

        private float Step(float stable, float raw, float dt)
        {
            float error = raw - stable;
            if (Mathf.Abs(error) <= deadbandDeg) return stable;
            if (!PoseLandmark.Finite(dt) || dt <= 0f) return stable;

            float alpha = smoothingTimeConstantSec > 0f
                ? 1f - Mathf.Exp(-dt / smoothingTimeConstantSec) : 1f;
            float candidate = stable + alpha * error;
            float maxStep = maxAngularRateDegPerSec * dt;
            return stable + Mathf.Clamp(candidate - stable, -maxStep, maxStep);
        }
    }
}
