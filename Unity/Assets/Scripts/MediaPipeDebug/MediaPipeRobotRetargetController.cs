using UnityEngine;

namespace HumanMotion.MediaPipeDebug
{
    public sealed class MediaPipeRobotRetargetController : MonoBehaviour
    {
        public UdpMediaPipePoseSource source;
        public HumanArmDebugController humanArm;
        public ForearmArmController robot;
        public ForearmCommandOutlierGate outlierGate;
        public ForearmCommandStabilizer stabilizer;
        public bool rightArm = true;
        [Range(0, 1)] public float confidenceThreshold = .5f;
        [Header("Runtime diagnostics (read only)")]
        [SerializeField] private bool live;
        [SerializeField] private string status = "Waiting for pose";
        [SerializeField] private float humanRoll, humanPitch, elbowBend;
        [SerializeField] private ForearmJointCommandData command;
        [SerializeField] private ForearmJointCommandData rawCommand;
        [SerializeField] private ForearmJointCommandData acceptedCommand;
        public bool Live => live;
        public string Status => status;
        public float HumanRoll => humanRoll;
        public float HumanPitch => humanPitch;
        public float ElbowBend => elbowBend;
        public ForearmJointCommandData Command => command;
        public ForearmJointCommandData RawCommand => rawCommand;
        public ForearmJointCommandData AcceptedCommand => acceptedCommand;
        public bool OutlierGateEnabled => outlierGate != null && outlierGate.isActiveAndEnabled &&
            outlierGate.enableOutlierGate;
        public bool StabilizationEnabled => stabilizer != null && stabilizer.isActiveAndEnabled &&
            stabilizer.enableStabilization;
        public long AppliedFrameId { get; private set; } = -1;
        private double lastEventAt;
        private double lastPoseTimestamp;
        private bool hasAppliedCommand;
        private Renderer[] visuals;
        private bool[] visibility;
        private void Start()
        {
            if (source == null || humanArm == null || robot == null || !robot.IsConfigured)
            { status = "Invalid component references"; enabled = false; return; }
            visuals = robot.GetComponentsInChildren<Renderer>(true);
            visibility = new bool[visuals.Length];
            for (int i = 0; i < visuals.Length; i++) visibility[i] = visuals[i].enabled;
            hasAppliedCommand = false;
            lastEventAt = lastPoseTimestamp = 0;
            if (outlierGate != null) outlierGate.ResetState();
            if (stabilizer != null) stabilizer.ResetState();
            source.FrameChanged += Apply;
            Apply(source.Current);
        }
        private void Apply(PoseFrame frame)
        {
            float dt = ProcessingInterval();
            float gateDt = SourceInterval(frame, dt);
            live = MediaPipeForearmAngleMapper.TryMap(frame, humanArm.coordinateMapping, rightArm,
                confidenceThreshold, out var mapped, out status);
            if (live)
            {
                rawCommand = mapped.command;
                acceptedCommand = outlierGate != null && outlierGate.isActiveAndEnabled
                    ? outlierGate.Process(rawCommand, gateDt, frame.frameId) : rawCommand;
                var stabilizerInput = acceptedCommand;
                // 의심 축만 실제 마지막 적용값으로 고정한다. 다른 축은 계속 추종한다.
                if (outlierGate != null && outlierGate.isActiveAndEnabled && hasAppliedCommand)
                {
                    if (outlierGate.HoldM0) stabilizerInput.elbowRoll = command.elbowRoll;
                    if (outlierGate.HoldM1) stabilizerInput.elbowPitch = command.elbowPitch;
                }
                command = stabilizer != null && stabilizer.isActiveAndEnabled
                    ? stabilizer.Process(stabilizerInput, dt) : stabilizerInput;
                live = robot.ApplyCommand(command);
                if (live) { hasAppliedCommand = true; humanRoll = mapped.roll; humanPitch = mapped.pitch; elbowBend = mapped.elbowBend; AppliedFrameId = frame.frameId; }
                else status = "Robot rejected command";
            }
            if (!live)
            {
                if (outlierGate != null) outlierGate.HoldInvalid();
                rawCommand.valid = false; acceptedCommand.valid = false; command.valid = false;
                AppliedFrameId = -1; humanRoll = humanPitch = elbowBend = float.NaN;
            }
            // 무효 자세를 LIVE로 오인하지 않도록 숨긴다. 마지막 자세를 HOLD하지 않는다.
            for (int i = 0; i < visuals.Length; i++) if (visuals[i] != null) visuals[i].enabled = live && visibility[i];
        }
        private float ProcessingInterval()
        {
            double now = Time.realtimeSinceStartupAsDouble;
            double elapsed = lastEventAt > 0 ? now - lastEventAt : Time.unscaledDeltaTime;
            lastEventAt = now;
            // 수신 중단 후 재획득에서는 긴 공백을 한 번의 급격한 추종으로 합산하지 않는다.
            if (double.IsNaN(elapsed) || double.IsInfinity(elapsed) || elapsed <= 0 ||
                elapsed > source.timeoutSeconds) elapsed = Time.unscaledDeltaTime;
            return elapsed >= 0 && elapsed <= float.MaxValue ? (float)elapsed : 0f;
        }
        private float SourceInterval(PoseFrame frame, float fallback)
        {
            if (frame == null) return fallback;
            double interval = lastPoseTimestamp > 0 ? frame.timeSec - lastPoseTimestamp : fallback;
            lastPoseTimestamp = frame.timeSec;
            // CSV의 timestamp 기반 속도 분석과 동일한 시간축을 사용한다.
            if (double.IsNaN(interval) || double.IsInfinity(interval) || interval <= 0 ||
                interval > source.timeoutSeconds) return fallback;
            return (float)interval;
        }
        private void OnDestroy()
        {
            if (source != null) source.FrameChanged -= Apply;
            if (visuals == null) return;
            for (int i = 0; i < visuals.Length; i++) if (visuals[i] != null) visuals[i].enabled = visibility[i];
        }
    }
}
