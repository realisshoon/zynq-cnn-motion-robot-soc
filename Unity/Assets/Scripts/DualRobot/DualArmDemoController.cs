using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

[DefaultExecutionOrder(100)]
public sealed class DualArmDemoController : MonoBehaviour
{
    public enum DemoMode { ScriptedHandoff, CsvRightArm }
    public enum Stage { Idle, LeftApproach, LeftGrab, LeftToHandoff, RightApproach, RightGrab, Transfer, RightMoveAway, Done, Failed }
    public RobotArmController leftArm, rightArm;
    public UdpJointCommandReceiver rightReceiver;
    public BallTransferController transfer;
    public DemoMode mode;
    [Min(0.1f)] public float moveSeconds = 2f;
    [Min(0.1f)] public float gripSeconds = 0.8f;
    [Range(0, 1)] public float holdOpening;
    public JointCommandData leftIdle, leftPick, leftHandoff, rightIdle, rightHandoff, rightAway;
    public Stage CurrentStage { get; private set; }
    public JointCommandData LeftCommand { get; private set; }
    public JointCommandData RightCommand { get; private set; }
    public bool Running => CurrentStage > Stage.Idle && CurrentStage < Stage.Done;
    public string Error { get; private set; } = "";
    public string LeftState => CurrentStage.ToString();
    public string RightState => mode == DemoMode.CsvRightArm ? "CSV / UDP" : CurrentStage.ToString();
    public string CsvStatus => mode != DemoMode.CsvRightArm ? "OFF" :
        !rightReceiver.IsListening ? "NOT LISTENING" :
        rightReceiver.AppliedCount == 0 ? "WAITING" : Time.unscaledTime - lastPacketTime < 1 ? "RECEIVING" : "HOLD / NO RECENT PACKET";
    private float elapsed, lastPacketTime;
    private long observedPackets;
    private JointCommandData fromL, fromR, targetL, targetR;
    private DemoMode activeMode;
    private bool previousRunInBackground;

    private void Start()
    { previousRunInBackground = Application.runInBackground; Application.runInBackground = true; ResetDemo(); }
    private void OnDestroy() { if (Application.isPlaying) Application.runInBackground = previousRunInBackground; }
    private void Update()
    {
        if (mode != activeMode) ResetDemo();
        if (rightReceiver.AppliedCount != observedPackets)
        { observedPackets = rightReceiver.AppliedCount; lastPacketTime = Time.unscaledTime; }
#if ENABLE_INPUT_SYSTEM
        if (Keyboard.current != null && Keyboard.current.spaceKey.wasPressedThisFrame)
        { if (CurrentStage == Stage.Idle) StartDemo(); else ResetDemo(); }
#elif ENABLE_LEGACY_INPUT_MANAGER
        if (Input.GetKeyDown(KeyCode.Space))
        { if (CurrentStage == Stage.Idle) StartDemo(); else ResetDemo(); }
#endif
        Advance(Time.unscaledDeltaTime);
    }

    public void SelectMode(DemoMode selected) { mode = selected; ResetDemo(); }

    [ContextMenu("Reset Demo")]
    public void ResetDemo()
    {
        if (leftArm == null || rightArm == null || transfer == null || rightReceiver == null) return;
        rightReceiver.enabled = false;
        leftArm.inputMode = RobotArmController.InputMode.Manual;
        rightArm.inputMode = RobotArmController.InputMode.Manual;
        activeMode = mode;
        SetLeft(Open(leftIdle));
        if (mode == DemoMode.ScriptedHandoff) SetRight(Open(rightIdle));
        transfer.ResetBall();
        CurrentStage = Stage.Idle; elapsed = 0; Error = "";
        observedPackets = 0;
        if (mode == DemoMode.CsvRightArm && Application.isPlaying)
        {
            rightArm.inputMode = RobotArmController.InputMode.UDP;
            rightReceiver.enabled = true;
            rightReceiver.RestartListener();
        }
    }

    [ContextMenu("Start Demo")]
    public void StartDemo()
    {
        // CSV 모드의 오른팔 소유권과 왼팔 Idle을 START/Space가 바꾸지 않는다.
        if (mode == DemoMode.CsvRightArm) return;
        ResetDemo();
        Enter(Stage.LeftApproach, Open(leftPick), Open(rightIdle));
    }

    // 검증에서도 동일한 상태 전이 경로를 호출한다. CSV 입력에는 보간을 적용하지 않는다.
    public void Advance(float deltaSeconds)
    {
        if (mode == DemoMode.CsvRightArm || !Running || deltaSeconds <= 0) return;
        elapsed += deltaSeconds;
        float duration = CurrentStage == Stage.LeftGrab || CurrentStage == Stage.RightGrab || CurrentStage == Stage.Transfer
            ? gripSeconds : moveSeconds;
        float t = Mathf.SmoothStep(0, 1, Mathf.Clamp01(elapsed / Mathf.Max(0.1f, duration)));
        SetLeft(Blend(fromL, targetL, t));
        if (mode == DemoMode.ScriptedHandoff) SetRight(Blend(fromR, targetR, t));
        if (elapsed < duration) return;
        switch (CurrentStage)
        {
            case Stage.LeftApproach: Enter(Stage.LeftGrab, Hold(leftPick), Open(rightIdle)); break;
            case Stage.LeftGrab:
                if (!transfer.GrabLeft()) { Fail(); break; }
                Enter(Stage.LeftToHandoff, Hold(leftHandoff), Open(rightIdle)); break;
            case Stage.LeftToHandoff:
                // CSV는 독립 오른팔 궤적이다. 자동 handoff를 강요하지 않고 왼팔은 전달점에서 HOLD.
                if (mode == DemoMode.CsvRightArm) CurrentStage = Stage.Done;
                else Enter(Stage.RightApproach, Hold(leftHandoff), Open(rightHandoff));
                break;
            case Stage.RightApproach: Enter(Stage.RightGrab, Hold(leftHandoff), Hold(rightHandoff)); break;
            case Stage.RightGrab:
                if (!transfer.TransferToRight()) { Fail(); break; }
                Enter(Stage.Transfer, Open(leftHandoff), Hold(rightHandoff)); break;
            case Stage.Transfer: Enter(Stage.RightMoveAway, Open(leftIdle), Hold(rightAway)); break;
            case Stage.RightMoveAway: CurrentStage = Stage.Done; break;
        }
    }

    private void Fail() { Error = transfer.LastError; CurrentStage = Stage.Failed; }
    private void Enter(Stage stage, JointCommandData left, JointCommandData right)
    { CurrentStage = stage; elapsed = 0; fromL = LeftCommand; fromR = RightCommand; targetL = left; targetR = right; }
    private void SetLeft(JointCommandData c) { LeftCommand = c; leftArm.testCommand = c; leftArm.ApplyCommand(c); }
    private void SetRight(JointCommandData c)
    {
        if (mode != DemoMode.ScriptedHandoff) return;
        RightCommand = c; rightArm.testCommand = c; rightArm.ApplyCommand(c);
    }
    private JointCommandData Hold(JointCommandData c) { c.valid = true; c.gripper_norm = holdOpening; return c; }
    private static JointCommandData Open(JointCommandData c) { c.valid = true; c.gripper_norm = 1; return c; }
    private static JointCommandData Blend(JointCommandData a, JointCommandData b, float t) => new JointCommandData
    {
        valid = true, base_deg = Mathf.Lerp(a.base_deg,b.base_deg,t),
        shoulder_deg = Mathf.Lerp(a.shoulder_deg,b.shoulder_deg,t), elbow_deg = Mathf.Lerp(a.elbow_deg,b.elbow_deg,t),
        wrist_pitch_deg = Mathf.Lerp(a.wrist_pitch_deg,b.wrist_pitch_deg,t),
        wrist_roll_deg = Mathf.Lerp(a.wrist_roll_deg,b.wrist_roll_deg,t), gripper_norm = Mathf.Lerp(a.gripper_norm,b.gripper_norm,t)
    };
}
