using System;
using UnityEngine;

[Serializable]
public struct ForearmJointCommandData
{
    public uint frame_id;

    // C의 robot calibration이 끝난 최종 servo command다.
    [Tooltip("M0 Elbow Roll 최종 servo 각도. C 범위: 20~160 deg.")]
    public float elbowRoll;
    [Tooltip("M1 Elbow Pitch 최종 servo 각도. C 범위: 20~180 deg.")]
    public float elbowPitch;
    [Tooltip("M2 Wrist Pitch 최종 servo 각도. C 범위: 10~160 deg.")]
    public float wristPitch;
    [Tooltip("M3 Wrist Roll 최종 servo 각도. C 범위: 10~170 deg.")]
    public float wristRoll;
    [Tooltip("M4 Gripper 최종 명령. 0=close, 1=open.")]
    public float gripper;

    public bool valid;
}


[DisallowMultipleComponent]
public sealed class ForearmArmController : MonoBehaviour
{
    private static readonly Vector3 PitchVisualAxis = Vector3.forward;

    // robot_arm/src/robot_calibration/forearm_calibration_config.c가 범위의
    // source of truth다. Unity는 최종 command를 시각화하며 C 보정을 반복하지 않는다.
    private const float ElbowRollMinDeg = 20f;
    private const float ElbowRollMaxDeg = 160f;
    private const float ElbowPitchMinDeg = 20f;
    private const float ElbowPitchMaxDeg = 180f;
    private const float WristPitchMinDeg = 10f;
    private const float WristPitchMaxDeg = 160f;
    private const float WristRollMinDeg = 10f;
    private const float WristRollMaxDeg = 170f;

    [Header("Elbow origin -> Forearm -> Wrist -> Gripper")]

    [Tooltip("M0 Elbow Roll control pivot. 이 Transform 아래의 M1~M4가 함께 회전한다.")]
    public Transform elbowRoll;
    [Tooltip("M1 Elbow Pitch control pivot. M0의 child이며 M2~M4가 함께 움직인다.")]
    public Transform elbowPitch;
    [Tooltip("M3 Wrist Roll control pivot. 기존 command mapping 순서를 유지한다.")]
    public Transform wristRoll;
    [Tooltip("M2 Wrist Pitch control pivot. 기존 command mapping 순서를 유지한다.")]
    public Transform wristPitch;
    [Tooltip("M4 Gripper visual. 0=close, 1=open.")]
    public G51GripperVisual gripperVisual;


    // =========================================================
    // Unity Visual Calibration
    //
    // IMPORTANT:
    // 이 스크립트가 받는 값은 Human angle이 아니라
    // Robot C에서 calibration까지 끝난 FINAL SERVO COMMAND이다.
    //
    // 현재 C calibration (scale, direction, zero_offset_deg):
    //
    // M0 =  90 - Human Elbow Roll  (range 20..160)
    // M1 = 120 - Human Elbow Pitch (range 20..180)
    // M2 =  80 + Human Wrist Pitch (range 10..160)
    // M3 =  87 + Human Wrist Roll  (range 10..170)
    //
    // 위 zero offset/direction은 Human angle -> servo command 변환용이다.
    // Unity 입력은 이미 변환된 최종 M0~M4이므로 위 식을 다시 적용하지 않는다.
    // 아래 Visual Offset/Sign은 Scene mesh의 rest 방향만 맞추는 별도 설정이다.
    // =========================================================

    [Header("Unity Visual Calibration")]


    // ---------------------------------------------------------
    // M0 : ELBOW ROLL
    //
    // 저장된 Scene Rest Pose를 servo 90도 기준으로 사용한다.
    //
    // M0 = 90  -> Rest
    // M0 = 70  -> -20 deg
    // M0 = 110 -> +20 deg
    // ---------------------------------------------------------

    [SerializeField]
    [Tooltip("실물 calibration direction이 아니라 Unity M0 mesh 축 방향 보정값.")]
    private float elbowRollVisualSign = 1f;


    // ---------------------------------------------------------
    // M1 : ELBOW PITCH
    //
    // FINAL servo command를 그대로 시각화한다.
    //
    // Scene의 elbowPitch Rest Pose를
    // servo 90도 기준으로 사용한다.
    //
    // M1 = 90  -> Rest
    // M1 = 70  -> -20 deg
    // M1 = 105 -> +15 deg
    // M1 = 140 -> +50 deg
    //
    // 기존 +90 offset을 넣으면
    // M1=90에서도 이미 +90도 회전하므로 제거.
    // ---------------------------------------------------------

    [SerializeField]
    [Tooltip("C zero offset이 아니라 Scene mesh의 M1 rest 방향을 맞추는 시각 보정값.")]
    private float elbowPitchVisualOffsetDeg = 0f;

    [SerializeField]
    [Tooltip("실물 calibration direction이 아니라 Unity M1 mesh 축 방향 보정값.")]
    private float elbowPitchVisualSign = 1f;


    // ---------------------------------------------------------
    // M2 : WRIST PITCH
    //
    // Scene Rest를 servo 90도 기준으로 사용.
    //
    // M2 = 90  -> Rest
    // M2 = 120 -> +30 deg
    // M2 = 150 -> +60 deg
    // ---------------------------------------------------------

    [SerializeField]
    [Tooltip("C zero offset이 아니라 Scene mesh의 M2 rest 방향을 맞추는 시각 보정값.")]
    private float wristPitchVisualOffsetDeg = 0f;

    [SerializeField]
    [Tooltip("실물 calibration direction이 아니라 Unity M2 mesh 축 방향 보정값.")]
    private float wristPitchVisualSign = 1f;


    // =========================================================
    // Rest Pose
    // =========================================================

    private Quaternion elbowRollRest;
    private Quaternion elbowPitchRest;
    private Quaternion wristRollRest;
    private Quaternion wristPitchRest;


    // =========================================================
    // Configuration Check
    // =========================================================

    public bool IsConfigured
    {
        get
        {
            if (elbowRoll == null || elbowPitch == null ||
                wristPitch == null || wristRoll == null || gripperVisual == null)
            {
                return false;
            }

            Transform toolMount = wristRoll.Find("ToolMount");
            return toolMount != null &&
                   elbowRoll.IsChildOf(transform) &&
                   elbowPitch.IsChildOf(elbowRoll) &&
                   wristPitch.IsChildOf(elbowPitch) &&
                   wristRoll.IsChildOf(wristPitch) &&
                   gripperVisual.transform.IsChildOf(toolMount);
        }
    }


    // =========================================================
    // Awake
    // =========================================================

    private void Awake()
    {
        if (!IsConfigured)
            return;

        // Scene에 배치되어 있는 현재 자세를 Rest Pose로 저장
        elbowRollRest = elbowRoll.localRotation;
        elbowPitchRest = elbowPitch.localRotation;
        wristRollRest = wristRoll.localRotation;
        wristPitchRest = wristPitch.localRotation;
    }


    // =========================================================
    // Apply Command
    // =========================================================

    public bool ApplyCommand(ForearmJointCommandData command)
    {
        if (!IsConfigured ||
            !command.valid ||
            !Finite(command.elbowRoll) ||
            !Finite(command.elbowPitch) ||
            !Finite(command.wristPitch) ||
            !Finite(command.wristRoll) ||
            !Finite(command.gripper))
        {
            return false;
        }


        // =====================================================
        // Robot command safety range
        //
        // 현재 Git 기준:
        //
        // M0 : 20 ~ 160
        // M1 : 20 ~ 180
        // M2 : 10 ~ 160
        // M3 : 10 ~ 170
        // M4 : 0 ~ 1
        // =====================================================

        float m0 = Mathf.Clamp(
            command.elbowRoll,
            ElbowRollMinDeg,
            ElbowRollMaxDeg
        );

        float m1 = Mathf.Clamp(
            command.elbowPitch,
            ElbowPitchMinDeg,
            ElbowPitchMaxDeg
        );

        float m2 = Mathf.Clamp(
            command.wristPitch,
            WristPitchMinDeg,
            WristPitchMaxDeg
        );

        float m3 = Mathf.Clamp(
            command.wristRoll,
            WristRollMinDeg,
            WristRollMaxDeg
        );

        float m4 = Mathf.Clamp01(
            command.gripper
        );


        // =====================================================
        // M0 : ELBOW ROLL
        // =====================================================

        float elbowRollVisualDeg =
            elbowRollVisualSign *
            (m0 - 90f);

        elbowRoll.localRotation =
            elbowRollRest *
            Quaternion.AngleAxis(
                elbowRollVisualDeg,
                Vector3.up
            );


        // =====================================================
        // M1 : ELBOW PITCH
        //
        // 중요:
        //
        // 기존:
        //
        //   +90 + (M1 - 90)
        //   = M1
        //
        // 였기 때문에 M1=70만 들어와도
        // Scene Rest에서 +70도를 추가로 돌리고 있었음.
        //
        // 현재:
        //
        //   0 + (M1 - 90)
        //
        // M1=90을 Unity Rest 기준으로 사용.
        // =====================================================

        float elbowPitchVisualDeg =
            elbowPitchVisualOffsetDeg +
            elbowPitchVisualSign *
            (m1 - 90f);

        elbowPitch.localRotation =
            elbowPitchRest *
            Quaternion.AngleAxis(
                elbowPitchVisualDeg,
                PitchVisualAxis
            );


        // =====================================================
        // M3 : WRIST ROLL
        //
        // 현재 Unity hierarchy의 축 방향 때문에
        // servo command 변화 방향을 반대로 적용.
        // =====================================================

        wristRoll.localRotation =
            wristRollRest *
            Quaternion.AngleAxis(
                -(m3 - 90f),
                Vector3.up
            );


        // =====================================================
        // M2 : WRIST PITCH
        // =====================================================

        float wristPitchVisualDeg =
            wristPitchVisualOffsetDeg +
            wristPitchVisualSign *
            (m2 - 90f);

        wristPitch.localRotation =
            wristPitchRest *
            Quaternion.AngleAxis(
                wristPitchVisualDeg,
                PitchVisualAxis
            );


        // =====================================================
        // M4 : GRIPPER
        // =====================================================

        gripperVisual.Apply(
            m4
        );


        return true;
    }


    // =========================================================
    // Utility
    // =========================================================

    private static bool Finite(float value)
    {
        return
            !float.IsNaN(value) &&
            !float.IsInfinity(value);
    }
}
