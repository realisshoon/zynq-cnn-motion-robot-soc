using UnityEngine;

/// <summary>
/// XYZ telemetry -> Digital Twin Forearm M0/M1 adapter.
///
/// Human RIGHT -> RobotArm_L
/// Human LEFT  -> RobotArm_R
///
/// UDP/pose target은 약 10 FPS로 갱신되지만
/// Unity에서는 매 Update마다 SmoothDampAngle로 보간한다.
///
/// Webcam mock에는 finger landmark가 없으므로
/// wrist/gripper는 HOME 자세로 유지한다.
/// </summary>
public sealed class XyzTelemetryForearmDriver : MonoBehaviour
{
    [Header("Source")]
    public XyzTelemetryUdpReceiver receiver;

    [Header("Digital Twin Targets")]
    public ForearmArmController robotArmL;
    public ForearmArmController robotArmR;

    [Header("Elbow Calibration")]
    [SerializeField] private float elbowZeroDeg = 90f;
    [SerializeField] private float elbowMinDeg = 20f;
    [SerializeField] private float elbowMaxDeg = 160f;

    [Header("Visual Smoothing")]
    [Tooltip("작을수록 빠르게 따라감. 0.06~0.15 정도 권장")]
    [SerializeField] private float smoothTime = 0.10f;

    [Tooltip("Unity Digital Twin 최대 회전속도")]
    [SerializeField] private float maxSpeedDegPerSec = 360f;

    [Header("Held Axes - no finger landmarks")]
    [SerializeField] private float wristPitchHome = 100f;
    [SerializeField] private float wristRollHome = 90f;
    [SerializeField, Range(0f, 1f)]
    private float gripperHome = 0.05f;

    [Header("Runtime - Human RIGHT -> RobotArm_L")]
    [SerializeField] private float humanRightRoll;
    [SerializeField] private float humanRightPitch;

    [SerializeField] private float targetLRoll;
    [SerializeField] private float targetLPitch;

    [SerializeField] private float currentLRoll;
    [SerializeField] private float currentLPitch;

    [Header("Runtime - Human LEFT -> RobotArm_R")]
    [SerializeField] private float humanLeftRoll;
    [SerializeField] private float humanLeftPitch;

    [SerializeField] private float targetRRoll;
    [SerializeField] private float targetRPitch;

    [SerializeField] private float currentRRoll;
    [SerializeField] private float currentRPitch;

    [Header("Runtime")]
    [SerializeField] private uint lastProcessedFrame;

    private float velocityLRoll;
    private float velocityLPitch;

    private float velocityRRoll;
    private float velocityRPitch;

    private bool leftInitialized;
    private bool rightInitialized;


    private void Reset()
    {
        receiver = GetComponent<XyzTelemetryUdpReceiver>();
    }


    private void Update()
    {
        if (receiver == null ||
            robotArmL == null ||
            robotArmR == null)
            return;

        uint frameId = receiver.LastFrameId;

        /*
         * 새 UDP frame이 들어왔을 때만
         * 새로운 target 각도를 계산한다.
         */
        if (frameId != 0 &&
            frameId != lastProcessedFrame)
        {
            UpdateTargets(frameId);

            lastProcessedFrame = frameId;
        }

        /*
         * 중요:
         *
         * UDP frame이 새로 안 들어와도
         * Unity Update마다 계속 실행한다.
         *
         * 그래서 10 FPS target을
         * 60 FPS 정도로 부드럽게 따라간다.
         */
        ApplySmoothedCommands(frameId);
    }


    private void UpdateTargets(uint frameId)
    {
        if (!receiver.LeftValid ||
            !receiver.RightValid)
            return;

        Vector3 bodyX;
        Vector3 bodyY;
        Vector3 bodyZ;

        if (!BuildBodyFrame(
                receiver.LeftShoulder,
                receiver.RightShoulder,
                out bodyX,
                out bodyY,
                out bodyZ))
            return;


        // =====================================================
        // Human RIGHT -> RobotArm_L
        // =====================================================

        if (receiver.RightFresh)
        {
            Vector3 forearm =
                receiver.RightWrist -
                receiver.RightElbow;

            if (CalculateAngles(
                    forearm,
                    bodyX,
                    bodyY,
                    bodyZ,
                    out humanRightRoll,
                    out humanRightPitch))
            {
                targetLRoll = Mathf.Clamp(
                    elbowZeroDeg - humanRightRoll,
                    elbowMinDeg,
                    elbowMaxDeg
                );

                targetLPitch = Mathf.Clamp(
                    elbowZeroDeg - humanRightPitch,
                    elbowMinDeg,
                    elbowMaxDeg
                );

                /*
                 * 첫 정상 frame에서는 현재값을 target으로 맞춰
                 * 시작할 때 큰 점프가 생기지 않게 한다.
                 */
                if (!leftInitialized)
                {
                    currentLRoll = targetLRoll;
                    currentLPitch = targetLPitch;

                    leftInitialized = true;
                }
            }
        }


        // =====================================================
        // Human LEFT -> RobotArm_R
        // =====================================================

        if (receiver.LeftFresh)
        {
            Vector3 forearm =
                receiver.LeftWrist -
                receiver.LeftElbow;

            if (CalculateAngles(
                    forearm,
                    bodyX,
                    bodyY,
                    bodyZ,
                    out humanLeftRoll,
                    out humanLeftPitch))
            {
                targetRRoll = Mathf.Clamp(
                    elbowZeroDeg - humanLeftRoll,
                    elbowMinDeg,
                    elbowMaxDeg
                );

                targetRPitch = Mathf.Clamp(
                    elbowZeroDeg - humanLeftPitch,
                    elbowMinDeg,
                    elbowMaxDeg
                );

                if (!rightInitialized)
                {
                    currentRRoll = targetRRoll;
                    currentRPitch = targetRPitch;

                    rightInitialized = true;
                }
            }
        }
    }


    private void ApplySmoothedCommands(uint frameId)
    {
        float dt = Time.deltaTime;

        if (dt <= 0f)
            return;


        // =====================================================
        // RobotArm_L
        // =====================================================

        if (leftInitialized)
        {
            currentLRoll =
                Mathf.SmoothDampAngle(
                    currentLRoll,
                    targetLRoll,
                    ref velocityLRoll,
                    smoothTime,
                    maxSpeedDegPerSec,
                    dt
                );

            currentLPitch =
                Mathf.SmoothDampAngle(
                    currentLPitch,
                    targetLPitch,
                    ref velocityLPitch,
                    smoothTime,
                    maxSpeedDegPerSec,
                    dt
                );

            ForearmJointCommandData commandL =
                new ForearmJointCommandData
                {
                    frame_id = frameId,

                    elbowRoll = currentLRoll,
                    elbowPitch = currentLPitch,

                    wristPitch = wristPitchHome,
                    wristRoll = wristRollHome,
                    gripper = gripperHome,

                    valid = true
                };

            robotArmL.ApplyCommand(commandL);
        }


        // =====================================================
        // RobotArm_R
        // =====================================================

        if (rightInitialized)
        {
            currentRRoll =
                Mathf.SmoothDampAngle(
                    currentRRoll,
                    targetRRoll,
                    ref velocityRRoll,
                    smoothTime,
                    maxSpeedDegPerSec,
                    dt
                );

            currentRPitch =
                Mathf.SmoothDampAngle(
                    currentRPitch,
                    targetRPitch,
                    ref velocityRPitch,
                    smoothTime,
                    maxSpeedDegPerSec,
                    dt
                );

            ForearmJointCommandData commandR =
                new ForearmJointCommandData
                {
                    frame_id = frameId,

                    elbowRoll = currentRRoll,
                    elbowPitch = currentRPitch,

                    wristPitch = wristPitchHome,
                    wristRoll = wristRollHome,
                    gripper = gripperHome,

                    valid = true
                };

            robotArmR.ApplyCommand(commandR);
        }
    }


    /*
     * Body coordinate:
     *
     * X = anatomical Left Shoulder -> Right Shoulder
     * Y = projected camera up
     * Z = X cross Y
     */
    private static bool BuildBodyFrame(
        Vector3 shoulderL,
        Vector3 shoulderR,
        out Vector3 bodyX,
        out Vector3 bodyY,
        out Vector3 bodyZ)
    {
        bodyX = shoulderR - shoulderL;

        if (bodyX.sqrMagnitude < 1e-8f)
        {
            bodyY = Vector3.zero;
            bodyZ = Vector3.zero;

            return false;
        }

        bodyX.Normalize();

        Vector3 cameraUp =
            Vector3.up;

        bodyY =
            cameraUp -
            Vector3.Dot(
                cameraUp,
                bodyX
            ) * bodyX;

        if (bodyY.sqrMagnitude < 1e-8f)
        {
            bodyZ = Vector3.zero;

            return false;
        }

        bodyY.Normalize();

        bodyZ =
            Vector3.Cross(
                bodyX,
                bodyY
            );

        if (bodyZ.sqrMagnitude < 1e-8f)
            return false;

        bodyZ.Normalize();

        bodyY =
            Vector3.Cross(
                bodyZ,
                bodyX
            ).normalized;

        return true;
    }


    /*
     * Forearm direction -> human elbow angles
     *
     * fx = dot(forearm, Body X)
     * fy = dot(forearm, Body Y)
     * fz = dot(forearm, Body Z)
     *
     * roll:
     * atan2(fx, fz)
     *
     * pitch:
     * atan2(fy, sqrt(fx^2 + fz^2))
     */
    private static bool CalculateAngles(
        Vector3 forearm,
        Vector3 bodyX,
        Vector3 bodyY,
        Vector3 bodyZ,
        out float rollDeg,
        out float pitchDeg)
    {
        rollDeg = 0f;
        pitchDeg = 0f;

        if (forearm.sqrMagnitude < 1e-8f)
            return false;

        forearm.Normalize();

        float fx =
            Vector3.Dot(
                forearm,
                bodyX
            );

        float fy =
            Vector3.Dot(
                forearm,
                bodyY
            );

        float fz =
            Vector3.Dot(
                forearm,
                bodyZ
            );

        float horizontal =
            Mathf.Sqrt(
                fx * fx +
                fz * fz
            );

        /*
         * Forearm이 Body Y에 거의 평행하면
         * azimuth singularity.
         *
         * 이 경우 target을 갱신하지 않고 HOLD.
         */
        if (horizontal < 0.01f)
            return false;

        rollDeg =
            Mathf.Atan2(
                fx,
                fz
            ) * Mathf.Rad2Deg;

        pitchDeg =
            Mathf.Atan2(
                fy,
                horizontal
            ) * Mathf.Rad2Deg;

        return true;
    }
}
