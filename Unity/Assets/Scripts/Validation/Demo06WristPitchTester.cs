using HumanMotion.MediaPipeDebug;
using UnityEngine;

// Demo_06 only: exercise the existing final-command path without moving pivots.
[DisallowMultipleComponent]
public sealed class Demo06WristPitchTester : MonoBehaviour
{
    public enum M2TestValue
    {
        M2_70 = 70,
        M2_80 = 80,
        M2_90 = 90,
        M2_100 = 100,
        M2_110 = 110,
        M2_120 = 120
    }

    [Header("Demo_06 / Play Mode only")]
    public MediaPipeRobotRetargetController retarget;
    public M2TestValue selectedM2 = M2TestValue.M2_100;
    [Tooltip("Play Mode에서 켜면 현재 live command의 M2만 선택값으로 덮어써 ApplyCommand로 전달합니다.")]
    public bool applyM2Override;

    [Header("Last application (runtime read only)")]
    [SerializeField] private bool applied;
    [SerializeField] private uint frameId;
    [SerializeField] private int appliedM2;
    [SerializeField] private float appliedM3;
    [SerializeField] private float appliedM4;
    [SerializeField] private float wristPitchLocalXDeg;
    [SerializeField] private float expectedRotationErrorDeg;
    [SerializeField] private bool wristRollUnchanged;
    [SerializeField] private bool pivotUnchanged;
    [SerializeField] private bool hierarchyIntact;

    private Quaternion wristPitchRest;
    private bool wasOverriding;

    private void Awake()
    {
        if (retarget != null && retarget.robot != null && retarget.robot.wristPitch != null)
            wristPitchRest = retarget.robot.wristPitch.localRotation;
    }

    private void LateUpdate()
    {
        if (retarget == null || retarget.robot == null || !retarget.robot.IsConfigured)
        {
            applied = false;
            return;
        }

        // The live source applies M2=100 during Update. Reapply only the selected
        // M2 in LateUpdate so every webcam frame can keep M0/M1 current.
        if (!applyM2Override)
        {
            if (wasOverriding && retarget.Live && retarget.Command.valid)
                retarget.robot.ApplyCommand(retarget.Command);
            wasOverriding = false;
            applied = false;
            return;
        }

        wasOverriding = true;
        if (!retarget.Live || !retarget.Command.valid)
        {
            applied = false;
            return;
        }

        ForearmJointCommandData command = retarget.Command;
        command.wristPitch = (int)selectedM2;

        Transform wristRoll = retarget.robot.wristRoll;
        Transform wristPitch = retarget.robot.wristPitch;
        Quaternion rollBefore = wristRoll.localRotation;
        Vector3 pivotBefore = wristPitch.position;
        Vector3 localPositionBefore = wristPitch.localPosition;

        applied = retarget.robot.ApplyCommand(command);
        if (!applied) return;

        frameId = command.frame_id;
        appliedM2 = (int)selectedM2;
        appliedM3 = command.wristRoll;
        appliedM4 = command.gripper;
        wristPitchLocalXDeg = Mathf.DeltaAngle(0f, wristPitch.localEulerAngles.x);
        expectedRotationErrorDeg = Quaternion.Angle(
            wristPitchRest * Quaternion.AngleAxis(appliedM2 - 90f, Vector3.right),
            wristPitch.localRotation);
        wristRollUnchanged = Quaternion.Angle(rollBefore, wristRoll.localRotation) < 0.01f;
        pivotUnchanged = Vector3.Distance(pivotBefore, wristPitch.position) < 0.00001f &&
                         Vector3.Distance(localPositionBefore, wristPitch.localPosition) < 0.00001f;
        Transform toolMount = wristPitch.Find("ToolMount");
        hierarchyIntact = toolMount != null &&
                          retarget.robot.gripperVisual.transform.IsChildOf(toolMount);
    }
}
