using System;
using UnityEngine;

/// <summary>보정이 끝난 JointCommand를 즉시 시각화한다. 로봇 보정/안전/pose 연산은 하지 않는다.</summary>
[ExecuteAlways]
[DisallowMultipleComponent]
public sealed class RobotArmController : MonoBehaviour
{
    public enum InputMode { Manual, UDP }
    public enum VisualAxis { X, Y, Z }
    public enum VisualDirection { Positive = 1, Negative = -1 }

    [Serializable]
    public sealed class RobotGeometry
    {
        [Tooltip("RobotArm root 기준 Base_Yaw pivot 높이. 현재 값은 placeholder.")]
        [Min(0.01f)] public float baseHeight = 0.50f;

        [Tooltip("Base_Yaw pivot -> Shoulder_Pitch pivot 거리. 현재 값은 placeholder.")]
        [Min(0.0f)] public float shoulderOffset = 0.20f;

        [Tooltip("Shoulder_Pitch axis -> Elbow_Pitch axis 거리.")]
        [Min(0.01f)] public float upperArmLength = 1.10f;

        [Tooltip("Elbow_Pitch axis -> Wrist_Pitch axis 거리.")]
        [Min(0.01f)] public float forearmLength = 0.90f;

        [Tooltip("Wrist_Pitch axis -> Wrist_Roll axis 거리.")]
        [Min(0.01f)] public float wristLength = 0.30f;
    }

    [Serializable]
    public sealed class JointVisual
    {
        public Transform pivot;
        [Tooltip("Pivot의 local 회전축. 실제 G51 축의 실측값이 아닌 시각화 설정.")]
        public VisualAxis axis;
        [Tooltip("실제 서보 회전 방향은 미확정. 화면 표시 방향만 지정.")]
        public VisualDirection direction = VisualDirection.Positive;
        public float neutral_deg = 90f;
        [Tooltip("Builder가 연결할 때 저장한 기본 local 회전. 현재 pose를 다시 캡처하지 않는다.")]
        public Quaternion restLocalRotation = Quaternion.identity;

        public JointVisual(VisualAxis axis) { this.axis = axis; }

        public void Bind(Transform target)
        {
            pivot = target;
            restLocalRotation = target.localRotation;
        }

        public void Apply(float command)
        {
            if (pivot == null) return;
            Vector3 localAxis = axis == VisualAxis.X ? Vector3.right :
                axis == VisualAxis.Y ? Vector3.up : Vector3.forward;
            pivot.localRotation = restLocalRotation *
                Quaternion.AngleAxis((command - neutral_deg) * (int)direction, localAxis);
        }
    }

    [Header("입력 선택")]
    public InputMode inputMode = InputMode.Manual;

    [Header("최종 JointCommand 테스트 입력 (Manual Mode)")]
    public JointCommandData testCommand = JointCommandData.Neutral;

    [Header("G51 Digital Twin Geometry (현재 기본값은 placeholder)")]
    public RobotGeometry geometry = new RobotGeometry();

    [Header("시각화 축 / 방향 / 중립값 (하드웨어 실측 아님)")]
    [Tooltip("소스 참고 범위: 10~170 deg. Unity에서 제한하지 않음.")]
    public JointVisual baseYaw = new JointVisual(VisualAxis.Y);
    [Tooltip("소스 참고 범위: 20~160 deg. Unity에서 제한하지 않음.")]
    public JointVisual shoulderPitch = new JointVisual(VisualAxis.X);
    [Tooltip("소스 참고 범위: 10~170 deg. Unity에서 제한하지 않음.")]
    public JointVisual elbowPitch = new JointVisual(VisualAxis.X);
    [Tooltip("소스 참고 범위: 20~160 deg. Unity에서 제한하지 않음.")]
    public JointVisual wristPitch = new JointVisual(VisualAxis.X);
    [Tooltip("소스 참고 범위: 0~180 deg. Unity에서 제한하지 않음.")]
    public JointVisual wristRoll = new JointVisual(VisualAxis.Y);

    [Header("G51 Linkage Gripper Visual")]
    [Tooltip("사진 기반 2-finger linkage 시각화. 연결되면 legacy 선형 finger 이동보다 우선 사용.")]
    public G51GripperVisual gripperVisual;

    [Header("Legacy Gripper Fallback (이전 scene 호환)")]
    public Transform fingerLeft;
    public Transform fingerRight;
    [Tooltip("Gripper local 좌표에서 손가락 중심의 CLOSE 위치.")]
    public Vector3 fingerLeftClosed = new Vector3(-0.04f, 0.15f, 0f);
    public Vector3 fingerRightClosed = new Vector3(0.04f, 0.15f, 0f);
    [Tooltip("Gripper local X축 방향으로 손가락 각각이 벌어지는 거리.")]
    [Min(0f)] public float fingerTravel = 0.16f;

    private void OnEnable() => ApplyTestCommand();
    private void Update() => ApplyTestCommand();

    public bool ApplyTestCommand() => inputMode == InputMode.Manual && ApplyCommand(testCommand);

    /// <summary>Unity main thread에서 호출. false 입력은 Transform을 전혀 변경하지 않는다.</summary>
    public bool ApplyCommand(JointCommandData command)
    {
        if (!command.valid) return false;
        if (!Finite(command.base_deg) || !Finite(command.shoulder_deg) ||
            !Finite(command.elbow_deg) || !Finite(command.wrist_pitch_deg) ||
            !Finite(command.wrist_roll_deg) || !Finite(command.gripper_norm)) return false;

        baseYaw.Apply(command.base_deg);
        shoulderPitch.Apply(command.shoulder_deg);
        elbowPitch.Apply(command.elbow_deg);
        wristPitch.Apply(command.wrist_pitch_deg);
        wristRoll.Apply(command.wrist_roll_deg);

        if (gripperVisual != null)
        {
            gripperVisual.Apply(command.gripper_norm);
        }
        else
        {
            // 기존 placeholder scene 호환용 fallback.
            float opening = Mathf.Clamp01(command.gripper_norm) * fingerTravel;
            if (fingerLeft != null)
                fingerLeft.localPosition = fingerLeftClosed + Vector3.left * opening;
            if (fingerRight != null)
                fingerRight.localPosition = fingerRightClosed + Vector3.right * opening;
        }
        return true;
    }

    private static bool Finite(float value) => !float.IsNaN(value) && !float.IsInfinity(value);
}
