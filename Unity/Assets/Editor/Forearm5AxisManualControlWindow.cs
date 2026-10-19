#if UNITY_EDITOR
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;

/// <summary>
/// Play Mode-only visual preview. This window never drives the hardware and
/// deliberately does not claim to perform mechanical collision checking.
/// </summary>
public sealed class Forearm5AxisManualControlWindow : EditorWindow
{
    private const string DemoScenePath = "Assets/Scenes/Demo_02_Forearm5Axis.unity";

    private enum ArmTarget { Both, Left, Right }

    private ArmTarget armTarget = ArmTarget.Both;
    private float m0 = 90f;
    private float m1 = 70f;
    private float m2 = 100f;
    private float m3 = 90f;
    private float m4 = 0.05f;
    private DualUdpJointCommandReceiver pausedReceiver;
    private bool restoreReceiver;
    private string status = "Play Mode에서 Apply Home을 누른 뒤 슬라이더를 드래그하세요.";

    [MenuItem("Tools/Human Motion/Demo 02/Manual 5-Axis Sliders")]
    private static void Open()
    {
        GetWindow<Forearm5AxisManualControlWindow>("5-Axis Sliders");
    }

    private void OnEnable()
    {
        EditorApplication.playModeStateChanged += OnPlayModeChanged;
    }

    private void OnDisable()
    {
        EditorApplication.playModeStateChanged -= OnPlayModeChanged;
        RestoreReceiver();
    }

    private void OnPlayModeChanged(PlayModeStateChange state)
    {
        if (state == PlayModeStateChange.ExitingPlayMode)
            RestoreReceiver();
        Repaint();
    }

    private void OnGUI()
    {
        EditorGUILayout.HelpBox(
            "Unity 외형 미리보기 전용입니다. 실제 모터 출력이 없고, 부품 간 관통 검사는 아직 없습니다.",
            MessageType.Warning);

        bool ready = EditorApplication.isPlaying &&
                     SceneManager.GetActiveScene().path == DemoScenePath;
        if (!ready)
        {
            EditorGUILayout.HelpBox(
                "Demo_02_Forearm5Axis Scene을 열고 Play Mode에 들어가세요. Edit Mode에서는 관절을 변경하지 않습니다.",
                MessageType.Info);
        }

        armTarget = (ArmTarget)EditorGUILayout.EnumPopup("Target", armTarget);

        EditorGUI.BeginChangeCheck();
        m0 = EditorGUILayout.Slider("M0 Elbow Roll (deg)", m0, 20f, 160f);
        m1 = EditorGUILayout.Slider("M1 Elbow Pitch (deg)", m1, 20f, 180f);
        m2 = EditorGUILayout.Slider("M2 Wrist Pitch (deg)", m2, 10f, 160f);
        m3 = EditorGUILayout.Slider("M3 Wrist Roll (deg)", m3, 10f, 170f);
        m4 = EditorGUILayout.Slider("M4 Gripper (0=close, 1=open)", m4, 0f, 1f);
        bool sliderChanged = EditorGUI.EndChangeCheck();

        using (new EditorGUI.DisabledScope(!ready))
        {
            if (sliderChanged)
                ApplyPose();

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("Apply current values"))
                ApplyPose();
            if (GUILayout.Button("Apply Home"))
            {
                m0 = 90f;
                m1 = 70f;
                m2 = 100f;
                m3 = 90f;
                m4 = 0.05f;
                ApplyPose();
            }
            EditorGUILayout.EndHorizontal();
        }

        EditorGUILayout.HelpBox(status, MessageType.None);
    }

    private void ApplyPose()
    {
        if (!EditorApplication.isPlaying ||
            SceneManager.GetActiveScene().path != DemoScenePath)
            return;

        ForearmArmController left = null;
        ForearmArmController right = null;
        foreach (ForearmArmController arm in Object.FindObjectsByType<ForearmArmController>(
                     FindObjectsInactive.Include, FindObjectsSortMode.None))
        {
            if (arm.gameObject.name == "RobotArm_L") left = arm;
            else if (arm.gameObject.name == "RobotArm_R") right = arm;
        }

        if ((armTarget != ArmTarget.Right && (left == null || !left.IsConfigured)) ||
            (armTarget != ArmTarget.Left && (right == null || !right.IsConfigured)))
        {
            status = "대상 팔의 ForearmArmController 참조/관절 계층을 확인하세요. 적용하지 않았습니다.";
            return;
        }

        // The UDP receiver otherwise writes another pose over a dragged slider.
        // This change exists only during Play Mode and is restored on close/exit.
        if (pausedReceiver == null)
        {
            pausedReceiver = Object.FindFirstObjectByType<DualUdpJointCommandReceiver>(
                FindObjectsInactive.Include);
            restoreReceiver = pausedReceiver != null && pausedReceiver.enabled;
            if (restoreReceiver) pausedReceiver.enabled = false;
        }

        var command = new ForearmJointCommandData
        {
            elbowRoll = m0,
            elbowPitch = m1,
            wristPitch = m2,
            wristRoll = m3,
            gripper = m4,
            valid = true
        };

        bool applied = true;
        if (armTarget != ArmTarget.Right) applied &= left.ApplyCommand(command);
        if (armTarget != ArmTarget.Left) applied &= right.ApplyCommand(command);
        status = applied
            ? "수동 미리보기 적용됨. 5축 값은 함께 유지됩니다. UDP 수신은 창을 닫거나 Play 종료 시 복구됩니다."
            : "관절 명령 적용 실패. ForearmArmController 설정을 확인하세요.";
        SceneView.RepaintAll();
    }

    private void RestoreReceiver()
    {
        if (restoreReceiver && pausedReceiver != null && EditorApplication.isPlaying)
            pausedReceiver.enabled = true;
        restoreReceiver = false;
        pausedReceiver = null;
    }
}
#endif
