using System;
using UnityEngine;

[Serializable]
public struct ForearmJointCommandData
{
    public uint frame_id;

    // Final robot servo command
    public float elbowRoll;     // M0
    public float elbowPitch;    // M1
    public float wristPitch;    // M2
    public float wristRoll;     // M3
    public float gripper;       // M4

    public bool valid;
}


[DisallowMultipleComponent]
public sealed class ForearmArmController : MonoBehaviour
{
    [Header("Elbow origin -> Forearm -> Wrist -> Gripper")]

    public Transform elbowRoll;
    public Transform elbowPitch;
    public Transform wristRoll;
    public Transform wristPitch;
    public G51GripperVisual gripperVisual;


    // =========================================================
    // Unity Visual Calibration
    //
    // IMPORTANT:
    // 이 스크립트가 받는 값은 Human angle이 아니라
    // Robot C에서 calibration까지 끝난 FINAL SERVO COMMAND이다.
    //
    // 현재 Git calibration:
    //
    // M0 =  90 - Human Forearm Azimuth
    // M1 = 120 - Human Forearm Elevation
    // M2 =  80 + Human Wrist Pitch
    // M3 =  87 + Human Wrist Roll
    //
    // Unity에서는 위 식을 다시 계산하지 않는다.
    // 전달받은 최종 M0~M4를 시각화만 한다.
    // =========================================================

    [Header("Unity Visual Calibration")]


    // ---------------------------------------------------------
    // M0 : ELBOW ROLL
    //
    // 기존 Unity 모델에서 실측/확인된 visual calibration.
    //
    // M0 = 90일 때 forward가 되도록
    // Scene Rest에서 -90도 보정이 필요함.
    // ---------------------------------------------------------

    [SerializeField]
    private float elbowRollVisualOffsetDeg = -90f;

    [SerializeField]
    private float elbowRollVisualSign = 1f;


    // ---------------------------------------------------------
    // M1 : ELBOW PITCH
    //
    // FINAL servo command를 그대로 시각화한다.
    //
    // 아래 값은 컴포넌트 기본값이다. Demo_06은 Inspector에서
    // 실물 방향에 맞춘 별도 offset/sign을 직렬화한다.
    // ---------------------------------------------------------

    [SerializeField]
    private float elbowPitchVisualOffsetDeg = 0f;

    [SerializeField]
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
    private float wristPitchVisualOffsetDeg = 0f;

    [SerializeField]
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

    public bool IsConfigured =>
        elbowRoll != null &&
        elbowPitch != null &&
        wristRoll != null &&
        wristPitch != null &&
        gripperVisual != null &&

        elbowRoll.IsChildOf(transform) &&
        elbowPitch.IsChildOf(elbowRoll) &&
        wristRoll.IsChildOf(elbowPitch) &&
        wristPitch.IsChildOf(wristRoll) &&
        gripperVisual.transform.IsChildOf(wristPitch);


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
        // 현재 Robot C forearm_calibration_config.c 기준:
        //
        // M0 : 20 ~ 160
        // M1 : 20 ~ 180
        // M2 : 10 ~ 160
        // M3 : 10 ~ 170
        // M4 : 0 ~ 1
        // =====================================================

        float m0 = Mathf.Clamp(command.elbowRoll, 20f, 160f);
        float m1 = Mathf.Clamp(command.elbowPitch, 20f, 180f);
        float m2 = Mathf.Clamp(command.wristPitch, 10f, 160f);
        float m3 = Mathf.Clamp(command.wristRoll, 10f, 170f);

        float m4 = Mathf.Clamp01(
            command.gripper
        );


        // =====================================================
        // M0 : ELBOW ROLL
        // =====================================================

        float elbowRollVisualDeg =
            elbowRollVisualOffsetDeg +
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
        // FINAL SERVO COMMAND만 시각화한다. Human pitch나
        // Robot C calibration을 여기서 다시 계산하지 않는다.
        // =====================================================

        float elbowPitchVisualDeg =
            elbowPitchVisualOffsetDeg +
            elbowPitchVisualSign *
            (m1 - 90f);

        elbowPitch.localRotation =
            elbowPitchRest *
            Quaternion.AngleAxis(
                elbowPitchVisualDeg,
                Vector3.right
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
                Vector3.right
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
