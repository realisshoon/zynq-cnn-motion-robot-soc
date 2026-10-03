#if UNITY_EDITOR
using System;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class BuildForearm5AxisVisuals
{
    private const string ScenePath = "Assets/Scenes/Demo_02_Forearm5Axis.unity";
    private const string BlackPath = "Assets/Materials/MAT_RobotBlack.mat";
    private const string MatteBlackPath = "Assets/Materials/MAT_RobotMatteBlack.mat";
    private const string RecessBlackPath = "Assets/Materials/MAT_RobotRecessBlack.mat";
    private const string SilverPath = "Assets/Materials/MAT_MetalSilver.mat";
    private const string ServoGreenPath = "Assets/Materials/MAT_ServoGreen20KG.mat";
    private const string BrassPath = "Assets/Materials/MAT_BrassGold.mat";

    // ---------------------------------------------------------------------
    // Visual placeholder dimensions (Unity metres)
    // ---------------------------------------------------------------------
    // 2026-10 실측값을 Unity metre 단위로 옮긴 visual 치수다.
    // Control Transform은 움직이지 않고 이 구역과 builder-owned geometry만 조정한다.
    private static readonly Vector3 BasePlateSize = new Vector3(0.235f, 0.012f, 0.20f);
    private const float BasePlateCenterY = -0.118f;

    // M0/M2/M3/M4는 같은 MG996R급 servo이므로 반드시 같은 실제 크기를 쓴다.
    // 각 조립 위치에서 orientation만 달라지며 root scale로 크기를 보정하지 않는다.
    private static readonly Vector3 StandardServoBodySize =
        new Vector3(0.040f, 0.040f, 0.020f);
    private const float ElbowPitchServoSizeMultiplier = 1.00f;

    private const float FixedPlateRadius = 0.055f;
    private const float FixedPlateThickness = 0.008f;
    private const float BearingPlateRadius = 0.055f;
    private const float BearingPlateThickness = 0.006f;
    private const float BearingOuterRadius = 0.050f;
    private const float BearingThickness = 0.014f;
    // 최신 사진에서 식별되는 장거리 spacer 수. 제품 사양 확정값이 아니며
    // 추가 각도 사진/실측이 들어오면 이 parameter만 조정한다.
    private const int PlatformStandoffCount = 4;
    private const float PlatformStandoffOrbit = 0.043f;
    private const float PlatformStandoffWidth = 0.006f;
    private const float PlatformStandoffHeight = 0.055f;
    private const int PlatformBoltCount = 6;
    private const float RotatingPlateRadius = 0.055f;
    private const float RotatingPlateThickness = 0.009f;

    private const float PitchBracketWidth = 0.050f;
    private const float PitchBracketHeight = 0.056f;
    // RobotArm_L Scene에서 손으로 맞춘 M1 ㄷ틀의 앞뒤 폭(local Z).
    // M2 ㄷ틀에도 같은 판별 치수를 적용한다.
    private const float PitchBracketLeftPlateDepth = 0.031081f;
    private const float PitchBracketRightPlateDepth = 0.0262f;
    private const float PitchBracketBridgeDepth = 0.027391499f;
    private const float PitchBracketPlateThickness = 0.003f;
    private const float PitchBracketBridgeThickness = 0.004f;
    private const float PitchBracketOverallHeight = 0.060f;
    // RobotArm_L에서 확정한 수평 배치. M1 servo case와 M1 구동 ㄷ틀을
    // 같은 local Y -90도 방향으로 돌려 M2 조립체와 나란한 방향을 유지한다.
    // ElbowPitch control Transform 자체의 위치/회전에는 적용하지 않는다.
    private static readonly Quaternion M1VisualRotation =
        Quaternion.Euler(0f, -90f, 0f);
    // 최신 실측에서 M1도 MG servo와 같은 40 x 40 x 20 mm 외곽이다.
    // M1은 넓은 40 x 40 mm 면으로 눕는다. 따라서 높이는 20 mm이다.
    private static readonly Vector3 PitchServoBodySize = new Vector3(
        StandardServoBodySize.y * ElbowPitchServoSizeMultiplier,
        StandardServoBodySize.z * ElbowPitchServoSizeMultiplier,
        StandardServoBodySize.x * ElbowPitchServoSizeMultiplier);
    private static readonly Vector3 PitchServoEndCaseSize = new Vector3(
        0.006f * ElbowPitchServoSizeMultiplier,
        0.022f * ElbowPitchServoSizeMultiplier,
        0.042f * ElbowPitchServoSizeMultiplier);
    private const float PitchServoEndCaseOffset =
        0.018f * ElbowPitchServoSizeMultiplier;

    private const float ForearmRadius = 0.007f;
    private const float ForearmShaftLength = 0.060f;
    private const float ForearmFlangeRadius = 0.012f;
    private const float ForearmFlangeThickness = 0.006f;
    private const float M2ShaftCollarRadius = 0.018f;
    private const float M2ShaftCollarThickness = 0.008f;
    private const float M2BracketBaseDrop = 0.030f;
    // M1과 M2 U-frame의 외곽 크기와 수평 방향은 같다.
    private const float M2BracketBaseThickness = PitchBracketBridgeThickness;
    private const float M2BracketSideHeight =
        PitchBracketOverallHeight - M2BracketBaseThickness;
    private const float M2BracketWidth = PitchBracketWidth;
    private const float M2BracketBaseDepth = PitchBracketBridgeDepth;
    // 실제 U-frame은 output pivot이 아니라 비대칭 servo case를 기준으로 감싼다.
    // Body와 양쪽 side plate 사이에 남길 최소 visual 조립 간격이다.
    private const float M2ServoSideClearance = 0.001f;
    private static readonly Quaternion M2BracketRotation =
        Quaternion.Euler(0f, -90f, 0f);
    // Stack from the M2 pivot downward:
    // U-frame bottom plate -> silver collar -> chrome shaft.
    private const float M2ShaftCollarCenterDrop =
        M2BracketBaseDrop + M2BracketBaseThickness * 0.5f +
        M2ShaftCollarThickness * 0.5f;
    private const float M2ChromeShaftEndDrop =
        M2BracketBaseDrop + M2BracketBaseThickness * 0.5f +
        M2ShaftCollarThickness;
    // Legacy wrist visual은 원래 chrome shaft 끝보다 local Y -0.11에 생성됐다.
    // 이 공통 mount만 올려서 bracket/servo/bolt/link가 한 조립체로 함께 이동한다.
    private const float LegacyWristAssemblyLift = 0.110f;
    private static readonly Vector3 WristServoCaseSize = new Vector3(0.086f, 0.050f, 0.055f);

    // G51 source geometry는 Gripper control의 0.16 scale 아래에 있다. M4 local
    // 치수에 그 역수를 적용해 최종 world 크기가 M0/M2/M3와 정확히 같게 한다.
    // M4 source mesh는 gripper plate의 local Z 방향으로 서 있다. 크기는 다른
    // MG996R과 같고 축 순서만 plate 좌표계에 맞게 바꾼다.
    private const float GripperSourceScale = 0.16f;
    private static readonly Vector3 GripperServoBodySize = new Vector3(
        StandardServoBodySize.x / GripperSourceScale,
        StandardServoBodySize.z / GripperSourceScale,
        StandardServoBodySize.y / GripperSourceScale);
    private static readonly Vector3 GripperServoEndCaseSize = new Vector3(
        0.042f / GripperSourceScale,
        0.022f / GripperSourceScale,
        0.006f / GripperSourceScale);
    private const float GripperServoEndCaseOffset = 0.018f / GripperSourceScale;
    // 실제 M4 servo는 집게의 왼쪽 drive gear 축 위에 선다.
    private const float GripperServoCenterX = -0.095f;
    // 외형 전체의 M3 mount 배치는 이전에 검증한 위치를 그대로 유지한다.
    private const float GripperPresentationAnchorX = 0.095f;
    private const float GripperServoCenterY = 0.095f;
    private const float GripperServoCenterZ = 0.210f;
    private const float GripperFingerLength = 0.490f;
    private const float GripperJawPivotX = 0.300f;
    private const float GripperFingerLateralScale = 0.82f;
    private const float GripperLinkThickness = 0.026f;
    private const float GripperLinkDepth = 0.028f;
    private const float GripperCoverDepth = 0.002f / GripperSourceScale;
    private const float GripperBoltDiameter = 0.010f;
    private const float M3GripperCouplerEnd = 0.012f;
    // RobotArm_L에서 저장한 motor Body 위치. 크기와 control axis는 그대로 두고
    // case 부속품만 이 위치에 맞춰 조립한다.
    private static readonly Vector3 M2CaseVisualOffset =
        new Vector3(0.0042f, 0.0189f, -0.0116f);
    private static readonly Vector3 M3CaseVisualOffset =
        new Vector3(-0.001f, 0f, -0.0349f);
    // RobotArm_L Scene에서 수동으로 고정한 M2-M3 ㄱ자 옆면과 M3 아래 판.
    // Builder가 기존 수동 배치를 원래 두 평행 rail로 되돌리지 않게 한다.
    private static readonly Vector3 M3BracketBaseHandPosition =
        new Vector3(-0.0041f, 0.0284f, 0.0025f);
    private static readonly Vector3 M3BracketConnectorHandPosition =
        new Vector3(-0.02146f, 0.0126f, 0.0112f);
    private static readonly Vector3 M3LowerPlateHandPosition =
        new Vector3(-0.0499f, -0.0051f, 0.01315f);
    // Shift only the displayed mechanism so its M4 case starts beyond the
    // coupler mount. The presentation root and Gripper control stay at zero.
    private const float GripperVisualForwardFromMount = 0.005f;
    // M2 horn faces local -X. The M3 output sits beyond it on that same side;
    // this is a photo-proportion offset, not a measured hardware dimension.
    private const float M3RollPivotOffsetFromM2 = 0.035f;
    private static readonly Quaternion M3RollPivotRotation =
        Quaternion.Euler(0f, 0f, 90f);

    private struct ArmParts
    {
        public Transform root;
        public Transform elbowRoll;
        public Transform elbowPitch;
        public Transform wristRoll;
        public Transform wristPitch;
        public Transform toolMount;
        public Transform gripper;
    }

    [MenuItem("Tools/Human Motion/Demo 02/Build or Refresh 5-Axis Robot Visuals")]
    public static void Build()
    {
        Scene scene = SceneManager.GetActiveScene();
        if (EditorApplication.isPlayingOrWillChangePlaymode ||
            !scene.IsValid() ||
            !string.Equals(scene.path, ScenePath, StringComparison.OrdinalIgnoreCase))
        {
            Warn("Demo_02_Forearm5Axis.unity를 Edit Mode에서 열고 실행하세요.");
            return;
        }

        Transform system = FindRoot(scene, "DualRobotSystem");
        Transform table = system != null ? system.Find("Table") : null;
        Renderer tableRenderer = table != null ? table.GetComponent<Renderer>() : null;
        ArmParts left, right;
        string validationError;
        if (system == null)
        {
            Warn("DualRobotSystem을 찾지 못했습니다. 변경하지 않았습니다.");
            return;
        }
        if (tableRenderer == null)
        {
            Warn("DualRobotSystem/Table의 Renderer를 찾지 못했습니다. 변경하지 않았습니다.");
            return;
        }
        if (!TryGetArm(system, "RobotArm_L", out left, out validationError))
        {
            Warn("RobotArm_L 검증 실패: " + validationError + " 변경하지 않았습니다.");
            return;
        }
        if (!TryGetArm(system, "RobotArm_R", out right, out validationError))
        {
            Warn("RobotArm_R 검증 실패: " + validationError + " 변경하지 않았습니다.");
            return;
        }
        if (!ReceiverMatches(system, left.root, right.root, out validationError))
        {
            Warn("DualUdpJointCommandReceiver 검증 실패: " + validationError +
                " 변경하지 않았습니다.");
            return;
        }

        Material black = GetOrCreateMaterial(BlackPath, new Color(0.035f, 0.038f, 0.042f), 0.12f, 0.3f);
        Material matteBlack = GetOrCreateMaterial(
            MatteBlackPath, new Color(0.018f, 0.019f, 0.021f), 0f, 0.07f);
        Material recessBlack = GetOrCreateMaterial(
            RecessBlackPath, new Color(0.006f, 0.007f, 0.008f), 0f, 0.02f);
        Material silver = GetOrCreateMaterial(SilverPath, new Color(0.72f, 0.75f, 0.78f), 0.8f, 0.65f);
        Material servoGreen = GetOrCreateMaterial(
            ServoGreenPath, new Color(0.075f, 0.36f, 0.15f), 0.72f, 0.48f);
        Material brass = GetOrCreateMaterial(
            BrassPath, new Color(0.56f, 0.40f, 0.12f), 0.72f, 0.42f);
        if (black == null || matteBlack == null || recessBlack == null ||
            silver == null || servoGreen == null || brass == null)
        {
            Warn("Robot visual Material을 준비할 수 없습니다. 변경하지 않았습니다.");
            return;
        }

        int undoGroup = Undo.GetCurrentGroup();
        Undo.SetCurrentGroupName("Build 5-Axis Robot Visuals");

        // 두 팔은 테이블 위의 좌우 위치만 다르고 같은 방향을 바라본다.
        // 오른팔 root 아래의 control/visual local Transform은 그대로 유지하고,
        // root 회전만 왼팔과 같게 맞춰 조립체 전체를 180도 돌린다.
        Undo.RecordObject(right.root, "Align right robot arm facing with left");
        right.root.localRotation = left.root.localRotation;

        BuildArm(left, black, matteBlack, recessBlack, silver, servoGreen, brass);

        // RobotArm_L에서 확정한 M4 case 부착 위치를 RobotArm_R에도 그대로 쓴다.
        // 각 팔의 예전 수동 보정을 따로 보존하면 같은 Builder를 실행해도 좌우
        // gripper motor가 서로 다른 위치에 붙을 수 있다.
        Vector3 leftM4CaseLocalPosition;
        bool hasLeftM4CasePosition = TryGetM4CaseLocalPosition(
            left, out leftM4CaseLocalPosition);
        BuildArm(right, black, matteBlack, recessBlack, silver, servoGreen, brass,
            hasLeftM4CasePosition
                ? (Vector3?)leftM4CaseLocalPosition
                : null);
        Undo.CollapseUndoOperations(undoGroup);
        EditorSceneManager.MarkSceneDirty(scene);
        Debug.Log("Demo 02 5-axis visual 생성/갱신 완료: RobotArm_L, RobotArm_R. Scene은 저장하지 않았습니다.");
    }

    // CI/batch validation entry point. 일반 메뉴 Build와 같은 검증을 거친 뒤
    // Demo_02만 저장하며 다른 Scene은 열거나 저장하지 않는다.
    public static void BuildAndSaveScene()
    {
        Scene scene = EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Single);
        Build();
        if (!EditorSceneManager.SaveScene(scene, ScenePath))
            throw new InvalidOperationException("Demo_02_Forearm5Axis.unity 저장에 실패했습니다.");
    }

    private static Transform FindRoot(Scene scene, string name)
    {
        foreach (GameObject root in scene.GetRootGameObjects())
            if (root.name == name) return root.transform;
        return null;
    }

    private static bool TryGetArm(
        Transform system, string name, out ArmParts arm, out string validationError)
    {
        arm = new ArmParts();
        validationError = string.Empty;
        if (system == null)
        {
            validationError = "DualRobotSystem이 null입니다.";
            return false;
        }
        arm.root = system.Find(name);
        if (arm.root == null)
        {
            validationError = name + " Transform을 찾지 못했습니다.";
            return false;
        }
        ForearmArmController control = arm.root.GetComponent<ForearmArmController>();
        if (control == null)
        {
            validationError = "ForearmArmController가 없습니다.";
            return false;
        }
        if (!control.enabled)
        {
            validationError = "ForearmArmController가 비활성화되어 있습니다.";
            return false;
        }

        // Controller reference가 실제 M0~M4 계약의 source of truth다. Visual mount가
        // 중간에 추가돼도 이름 기반 direct Find 때문에 정상 hierarchy를 거부하지 않는다.
        arm.elbowRoll = control.elbowRoll;
        arm.elbowPitch = control.elbowPitch;
        arm.wristRoll = control.wristRoll;
        arm.wristPitch = control.wristPitch;
        arm.gripper = control.gripperVisual != null
            ? control.gripperVisual.transform
            : null;
        arm.toolMount = arm.wristRoll != null
            ? FindDescendantByName(arm.wristRoll, "ToolMount")
            : null;

        if (arm.elbowRoll == null || arm.elbowPitch == null || arm.wristRoll == null ||
            arm.wristPitch == null || arm.toolMount == null || arm.gripper == null)
        {
            validationError = "ForearmArmController의 M0~M4 reference 중 null이 있습니다.";
            return false;
        }

        if (!arm.elbowRoll.IsChildOf(arm.root) ||
            !arm.elbowPitch.IsChildOf(arm.elbowRoll) ||
            !arm.wristPitch.IsChildOf(arm.elbowPitch) ||
            !arm.wristRoll.IsChildOf(arm.wristPitch) ||
            !arm.toolMount.IsChildOf(arm.wristRoll) ||
            !arm.gripper.IsChildOf(arm.toolMount))
        {
            validationError =
                "M1 ElbowPitch → M2 WristPitch → M3 WristRoll → ToolMount → M4 Gripper parent 관계가 다릅니다.";
            return false;
        }

        Vector3 wrist = arm.elbowPitch.InverseTransformPoint(arm.wristPitch.position);
        if (wrist.y <= 0.01f || Mathf.Abs(wrist.x) >= 0.01f || Mathf.Abs(wrist.z) >= 0.01f)
        {
            validationError =
                "M2 WristPitch pivot이 M1 ElbowPitch의 chrome shaft 끝에 있지 않습니다: " + wrist;
            return false;
        }
        Vector3 rollLocal = arm.wristPitch.InverseTransformPoint(arm.wristRoll.position);
        Vector3 rollAxisLocal = arm.wristPitch.InverseTransformDirection(
            arm.wristRoll.up).normalized;
        if (Vector3.Distance(rollLocal,
                Vector3.left * M3RollPivotOffsetFromM2) > 0.002f ||
            Vector3.Angle(rollAxisLocal, Vector3.left) > 1f ||
            Quaternion.Angle(arm.wristRoll.localRotation,
                M3RollPivotRotation) > 1f)
        {
            validationError =
                "M3 WristRoll pivot은 M2 기준 local (-0.035, 0, 0), " +
                "localRotation (0, 0, 90), roll axis local -X여야 합니다. 현재: " +
                rollLocal + ", axis " + rollAxisLocal +
                ". Unity Editor에서 control pivot을 먼저 맞추세요.";
            return false;
        }
        return true;
    }

    private static bool ReceiverMatches(
        Transform system, Transform left, Transform right, out string validationError)
    {
        validationError = string.Empty;
        DualUdpJointCommandReceiver[] receivers =
            system.GetComponentsInChildren<DualUdpJointCommandReceiver>(true);
        if (receivers.Length != 1)
        {
            validationError = "receiver 개수가 1이 아닙니다: " + receivers.Length;
            return false;
        }
        if (!receivers[0].enabled || receivers[0].port != 5005)
        {
            validationError = "receiver enabled 또는 port 5005 설정이 다릅니다.";
            return false;
        }
        if (receivers[0].leftArm != left.GetComponent<ForearmArmController>() ||
            receivers[0].rightArm != right.GetComponent<ForearmArmController>())
        {
            validationError = "Left/Right ForearmArmController reference가 다릅니다.";
            return false;
        }
        return true;
    }

    private static Material GetOrCreateMaterial(string path, Color color, float metallic, float smoothness)
    {
        Material material = AssetDatabase.LoadAssetAtPath<Material>(path);
        if (material != null) return material;

        Shader shader = Shader.Find("Universal Render Pipeline/Lit");
        if (shader == null) shader = Shader.Find("Standard");
        if (shader == null) return null;
        if (!AssetDatabase.IsValidFolder("Assets/Materials"))
            AssetDatabase.CreateFolder("Assets", "Materials");

        material = new Material(shader) { name = System.IO.Path.GetFileNameWithoutExtension(path) };
        if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
        if (material.HasProperty("_Color")) material.SetColor("_Color", color);
        if (material.HasProperty("_Metallic")) material.SetFloat("_Metallic", metallic);
        if (material.HasProperty("_Smoothness")) material.SetFloat("_Smoothness", smoothness);
        if (material.HasProperty("_Glossiness")) material.SetFloat("_Glossiness", smoothness);
        AssetDatabase.CreateAsset(material, path);
        return material;
    }

    private static void BuildArm(
        ArmParts arm, Material black, Material matteBlack, Material recessBlack,
        Material silver,
        Material servoGreen, Material brass,
        Vector3? m4CaseLocalPositionOverride = null)
    {
        Vector3 elbow = arm.elbowRoll.position;

        // 이전 builder가 만든 사각 base/servo placeholder를 제거한다. 이름이
        // 정확히 일치하는 builder 소유 visual만 대상으로 하며 control pivot은 건드리지 않는다.
        DestroyGeneratedChild(arm.root, "RobotBasePlate_Visual");
        DestroyGeneratedChild(arm.root, "ElbowVisualMount");

        // Yahboom 2DOF platform의 고정측. RobotArm root 아래에 있으므로 M0가
        // 회전해도 base plate, bearing structure와 실제 하부 servo body는 움직이지 않는다.
        Transform fixedBase = EnsureEmpty(arm.root, "ElbowRollFixedBase");
        PlaceWorld(fixedBase, elbow, Vector3.one);

        Transform basePlate = EnsurePrimitive(
            fixedBase, "ElbowRollBasePlate", PrimitiveType.Cube);
        PlaceLocal(basePlate, new Vector3(0f, BasePlateCenterY, 0f), BasePlateSize);
        SetMaterial(basePlate, black);

        float fixedPlateCenterY = -0.105f;
        Transform fixedPlate = EnsurePrimitive(
            fixedBase, "ElbowRollFixedLowerPlate", PrimitiveType.Cylinder);
        PlaceLocal(fixedPlate, new Vector3(0f, fixedPlateCenterY, 0f),
            CylinderScale(FixedPlateRadius, FixedPlateThickness));
        SetMaterial(fixedPlate, black);

        // 사진에서 확인되지 않은 M0 외부 servo placeholder와 이전 케이크형
        // support 원반을 정리한다. M0는 확인 가능한 bearing/platform만 표현한다.
        DestroyGeneratedChild(fixedBase, "ElbowRollBearingSupport");
        DestroyGeneratedChild(fixedBase, "ElbowRollLowerServoMount");
        DestroyGeneratedChild(fixedBase, "ElbowRollLowerServoOutput");
        DestroyGeneratedChild(fixedBase, "ElbowRollServoEar_L");
        DestroyGeneratedChild(fixedBase, "ElbowRollServoEar_R");

        // 추가 조립 사진에서 확인된 M0 검은 servo body를 fixed side에 표현한다.
        // 중앙 output shaft/hub는 아래에서 rotating side child로 따로 구성한다.
        DestroyGeneratedChild(fixedBase, "M0ServoVisual");
        BuildMg996RServoVisual(
            fixedBase, "M0ServoVisual", new Vector3(0f, -0.082f, 0f),
            Quaternion.identity, black, silver);

        BuildPlatformStandoffs(fixedBase, brass);
        BuildBearingAssembly(fixedBase, black, silver);

        // Bearing 위 회전측. 기존 ElbowRoll control pivot의 child이므로 별도
        // DOF를 만들지 않고 M0 명령과 정확히 함께 회전한다.
        Transform rotatingPlate = EnsurePrimitive(
            arm.elbowRoll, "ElbowRollRotatingPlate", PrimitiveType.Cylinder);
        PlaceWorld(rotatingPlate, elbow + Vector3.up * -0.043f,
            CylinderScale(RotatingPlateRadius, RotatingPlateThickness));
        SetMaterial(rotatingPlate, black);

        BuildM0RotatingOutput(arm.elbowRoll, elbow, silver);

        DestroyGeneratedChild(arm.elbowRoll, "ElbowRollRotatingHub");
        DestroyGeneratedChild(arm.elbowRoll, "ElbowRollUpperSupport");
        BuildPlatformBoltHeads(arm.elbowRoll, elbow, silver);
        BuildElbowPitchServoVisual(
            arm.elbowRoll, arm.elbowPitch, black, silver, servoGreen);

        // 실제 직렬 순서에서 chrome shaft 끝의 첫 관절은 M2 WristPitch다.
        // Control Transform은 이동하지 않고 shaft visual의 끝점만 해당 pivot에 맞춘다.
        Vector3 wristLocal = arm.elbowPitch.InverseTransformPoint(arm.wristPitch.position);
        Transform shaft = EnsurePrimitive(arm.elbowPitch, "ForearmShaft_Visual", PrimitiveType.Cylinder);
        Undo.RecordObject(shaft, "Place forearm shaft");
        Vector3 shaftDirection = wristLocal.normalized;
        // The chrome tube terminates at the underside of the M2 U-frame,
        // rather than visually passing through the servo/output pivot.
        Vector3 shaftEnd =
            wristLocal - shaftDirection * M2ChromeShaftEndDrop;
        Vector3 shaftStart = shaftEnd - shaftDirection * ForearmShaftLength;
        Vector3 shaftVector = shaftEnd - shaftStart;
        shaft.localPosition = shaftStart + shaftVector * 0.5f;
        shaft.localRotation = Quaternion.FromToRotation(Vector3.up, shaftVector.normalized);
        // M1 output flange에서 M2 WristPitch pivot까지 이어지는 실제 chrome shaft다.
        float visualForearmLength = shaftVector.magnitude;
        shaft.localScale = CylinderScale(ForearmRadius, visualForearmLength);
        SetMaterial(shaft, silver);

        Transform flange = EnsurePrimitive(
            arm.elbowPitch, "ForearmShaftLowerFlange", PrimitiveType.Cylinder);
        PlaceLocal(flange, shaftStart,
            Quaternion.FromToRotation(Vector3.up, shaftDirection),
            CylinderScale(ForearmFlangeRadius, ForearmFlangeThickness));
        SetMaterial(flange, silver);

        BuildWristAndGripperDetails(
            arm, black, matteBlack, recessBlack, silver,
            m4CaseLocalPositionOverride);
        ApplyStructuralMatte(arm.root, black, matteBlack);

        // 새 은색 shaft가 기존 검은 링크에 가려지지 않게 시각 자식만 끈다.
        Transform oldForearm = arm.elbowPitch.Find("ForeArm");
        if (oldForearm != null && oldForearm.childCount == 0 && oldForearm.gameObject.activeSelf)
        {
            Undo.RecordObject(oldForearm.gameObject, "Hide legacy forearm visual");
            oldForearm.gameObject.SetActive(false);
        }
    }

    private static void BuildPlatformStandoffs(Transform fixedBase, Material brass)
    {
        DestroyGeneratedChild(fixedBase, "ElbowRollPlatformStandoffs");
        Transform group = EnsureEmpty(fixedBase, "ElbowRollPlatformStandoffs");
        PlaceLocal(group, Vector3.zero, Vector3.one);
        for (int i = 0; i < PlatformStandoffCount; ++i)
        {
            float angle = i * Mathf.PI * 2f / PlatformStandoffCount;
            Transform spacer = EnsurePrimitive(
                group, "PlatformHexStandoff_" + i.ToString("00"), PrimitiveType.Cube);
            PlaceLocal(spacer,
                new Vector3(Mathf.Cos(angle) * PlatformStandoffOrbit, -0.074f,
                    Mathf.Sin(angle) * PlatformStandoffOrbit),
                new Vector3(
                    PlatformStandoffWidth, PlatformStandoffHeight, PlatformStandoffWidth));
            SetMaterial(spacer, brass);
        }
    }

    private static void BuildBearingAssembly(
        Transform fixedBase, Material black, Material silver)
    {
        DestroyGeneratedChild(fixedBase, "ElbowRollDeepGrooveBearing");
        Transform lowerPlate = EnsurePrimitive(
            fixedBase, "ElbowRollBearingLowerPlate", PrimitiveType.Cylinder);
        PlaceLocal(lowerPlate, new Vector3(0f, -0.064f, 0f),
            CylinderScale(BearingPlateRadius, BearingPlateThickness));
        SetMaterial(lowerPlate, black);

        // 실물 사진에서 외부로 보이는 bearing outer race는 끊긴 조각이 아니라
        // 연속된 silver band다. 내부는 검은 plate에 가려지므로 한 원통 band로 표현한다.
        Transform bearing = EnsurePrimitive(
            fixedBase, "ElbowRollDeepGrooveBearing", PrimitiveType.Cylinder);
        PlaceLocal(bearing, new Vector3(0f, -0.053f, 0f),
            CylinderScale(BearingOuterRadius, BearingThickness));
        SetMaterial(bearing, silver);
    }

    private static void BuildM0RotatingOutput(
        Transform elbowRoll, Vector3 elbowWorld, Material silver)
    {
        Transform shaft = EnsurePrimitive(
            elbowRoll, "M0ServoOutputShaft", PrimitiveType.Cylinder);
        PlaceWorld(shaft, elbowWorld + Vector3.up * -0.075f,
            CylinderScale(0.007f, 0.040f));
        SetMaterial(shaft, silver);

        Transform hub = EnsurePrimitive(
            elbowRoll, "M0ServoOutputHub", PrimitiveType.Cylinder);
        PlaceWorld(hub, elbowWorld + Vector3.up * -0.051f,
            CylinderScale(0.015f, 0.008f));
        SetMaterial(hub, silver);
    }

    private static void BuildPlatformBoltHeads(
        Transform elbowRoll, Vector3 elbowWorld, Material silver)
    {
        DestroyGeneratedChild(elbowRoll, "ElbowRollPlatformBolts");
        Transform group = EnsureEmpty(elbowRoll, "ElbowRollPlatformBolts");
        PlaceWorld(group, elbowWorld, Vector3.one);
        for (int i = 0; i < PlatformBoltCount; ++i)
        {
            float angle = i * Mathf.PI * 2f / PlatformBoltCount;
            Transform bolt = EnsurePrimitive(
                group, "PlatformBolt_" + i.ToString("00"), PrimitiveType.Cylinder);
            PlaceLocal(bolt,
                new Vector3(Mathf.Cos(angle) * PlatformStandoffOrbit, -0.037f,
                    Mathf.Sin(angle) * PlatformStandoffOrbit),
                CylinderScale(0.003f, 0.004f));
            SetMaterial(bolt, silver);
        }
    }

    private static void BuildElbowPitchServoVisual(
        Transform elbowRoll, Transform elbowPitch,
        Material black, Material silver, Material servoGreen)
    {
        // 기존 G51/MG996R mesh만 숨긴다. control Transform과 linkage는 유지한다.
        SetChildActive(elbowPitch, "__G51V2_Servo_Elbow_Pitch", false);
        SetChildActive(elbowPitch, "__G51V2_Bracket_Elbow_Pitch", false);
        // 구형 forearm placeholder 두 개는 shaft 아래에 사각 기둥으로 남는다.
        // 새 M1 ㄷ틀과 chrome shaft가 이를 대체하므로 mesh root만 숨긴다.
        SetChildActive(elbowPitch, "__G51V2_LinkA_Elbow_Pitch", false);
        SetChildActive(elbowPitch, "__G51V2_LinkB_Elbow_Pitch", false);

        // Servo case와 U bracket은 M0 회전 상판에 고정된 부품이다. 따라서 M1이
        // 움직일 때는 정지하고 M0가 움직일 때만 상판과 함께 회전한다.
        Transform fixedAssembly = EnsureEmpty(elbowRoll, "ElbowPitchFixedAssembly");
        PlaceLocal(fixedAssembly,
            Vector3.zero, M1VisualRotation, Vector3.one);

        Transform body = EnsurePrimitive(
            fixedAssembly, "ElbowPitchServoBody", PrimitiveType.Cube);
        // 실물 green 20KG servo는 platform 위에 40 x 40 mm 면으로 눕는다.
        // fixedAssembly의 Y 회전은 수평 방향만 바꾸고 20 mm 높이는 유지한다.
        PlaceLocal(body, new Vector3(0f, -0.015f, 0f), PitchServoBodySize);
        SetMaterial(body, servoGreen);

        DestroyGeneratedChild(fixedAssembly, "ElbowPitchServoTop");
        DestroyGeneratedChild(fixedAssembly, "ElbowPitchServoEndCase_Upper");
        DestroyGeneratedChild(fixedAssembly, "ElbowPitchServoEndCase_Lower");
        DestroyGeneratedChild(fixedAssembly, "ElbowPitchServoEndCase_Front");
        DestroyGeneratedChild(fixedAssembly, "ElbowPitchServoEndCase_Rear");
        DestroyGeneratedChild(fixedAssembly, "ElbowPitchServoEndCase_L");
        DestroyGeneratedChild(fixedAssembly, "ElbowPitchServoEndCase_R");
        Transform leftCase = EnsurePrimitive(
            fixedAssembly, "ElbowPitchServoEndCase_Front", PrimitiveType.Cube);
        PlaceLocal(leftCase,
            new Vector3(-PitchServoEndCaseOffset, -0.015f, 0f),
            PitchServoEndCaseSize);
        SetMaterial(leftCase, black);

        Transform rightCase = EnsurePrimitive(
            fixedAssembly, "ElbowPitchServoEndCase_Rear", PrimitiveType.Cube);
        PlaceLocal(rightCase,
            new Vector3(PitchServoEndCaseOffset, -0.015f, 0f),
            PitchServoEndCaseSize);
        SetMaterial(rightCase, black);

        // 이전 구현은 ㄷ자 bracket을 fixed servo case와 한 덩어리로 만들었다.
        // 실제로는 servo output에 물려 움직이는 yoke이므로 fixed 쪽 판을 제거한다.
        DestroyGeneratedChild(fixedAssembly, "PitchBracketPlate_L");
        DestroyGeneratedChild(fixedAssembly, "PitchBracketPlate_R");

        Transform earL = EnsurePrimitive(
            fixedAssembly, "ServoMountEar_L", PrimitiveType.Cube);
        PlaceLocal(earL,
            new Vector3(-0.026f * ElbowPitchServoSizeMultiplier, -0.015f, 0f),
            new Vector3(
                0.006f * ElbowPitchServoSizeMultiplier,
                0.024f * ElbowPitchServoSizeMultiplier,
                0.050f * ElbowPitchServoSizeMultiplier));
        SetMaterial(earL, black);

        Transform earR = EnsurePrimitive(
            fixedAssembly, "ServoMountEar_R", PrimitiveType.Cube);
        PlaceLocal(earR,
            new Vector3(0.026f * ElbowPitchServoSizeMultiplier, -0.015f, 0f),
            new Vector3(
                0.006f * ElbowPitchServoSizeMultiplier,
                0.024f * ElbowPitchServoSizeMultiplier,
                0.050f * ElbowPitchServoSizeMultiplier));
        SetMaterial(earR, black);

        // M1 모터 밑면과 M0 회전 상판 사이의 실제 빈 공간을 받치는 고정측 받침.
        // 두 기준면을 읽어 맞추므로 기존 motor/control 위치는 이동시키지 않는다.
        Transform rotatingPlate = elbowRoll.Find("ElbowRollRotatingPlate");
        if (rotatingPlate != null)
        {
            float platformTop = fixedAssembly.InverseTransformPoint(
                rotatingPlate.TransformPoint(Vector3.up)).y;
            float motorBottom = body.localPosition.y - body.localScale.y * 0.5f;
            const float footThickness = 0.003f;
            float railHeight = motorBottom - platformTop - footThickness;
            if (railHeight > 0.001f)
            {
                Transform foot = EnsurePrimitive(
                    fixedAssembly, "M1ServoSupportFoot_Visual", PrimitiveType.Cube);
                PlaceLocal(foot,
                    new Vector3(0f, platformTop + footThickness * 0.5f, 0f),
                    new Vector3(0.042f, footThickness, 0.042f));
                SetMaterial(foot, black);

                for (int side = -1; side <= 1; side += 2)
                {
                    Transform rail = EnsurePrimitive(fixedAssembly,
                        side < 0 ? "M1ServoSupportRail_L" : "M1ServoSupportRail_R",
                        PrimitiveType.Cube);
                    PlaceLocal(rail,
                        new Vector3(side * 0.012f,
                            platformTop + footThickness + railHeight * 0.5f, 0f),
                        new Vector3(0.008f, railHeight, 0.038f));
                    SetMaterial(rail, black);
                }
            }
        }

        BuildServo20Mark(fixedAssembly, silver);

        // M1 구동측. 이 visual만 ElbowPitch 아래에 두어 output hub와 forearm yoke가
        // 실제 servo horn처럼 M1 이후 링크와 함께 회전한다.
        Transform servoVisual = EnsureEmpty(elbowPitch, "ElbowPitchServoVisual");
        PlaceLocal(servoVisual,
            Vector3.zero, M1VisualRotation, Vector3.one);

        // 이전 버전에서 M1 아래에 있던 fixed case visual만 정리한다.
        DestroyGeneratedChild(servoVisual, "MetalServoBody");
        DestroyGeneratedChild(servoVisual, "MetalServoTopCase");
        DestroyGeneratedChild(servoVisual, "ServoMountEar_L");
        DestroyGeneratedChild(servoVisual, "ServoMountEar_R");

        Transform outputHub = EnsurePrimitive(
            servoVisual, "MetalServoOutputHub", PrimitiveType.Cylinder);
        PlaceLocal(outputHub, Vector3.zero,
            Quaternion.Euler(0f, 0f, 90f), CylinderScale(0.010f, 0.012f));
        SetMaterial(outputHub, silver);

        DestroyGeneratedChild(servoVisual, "PitchDrivenYoke");
        // 사진의 U bracket은 servo 앞/뒤 면이 아니라 output axis의 좌우 면을 잡는다.
        // 따라서 기존 Z측 plate를 X측 plate로 90도 돌려 배치한다.
        float bracketX = PitchBracketWidth * 0.5f - PitchBracketPlateThickness * 0.5f;
        for (int side = -1; side <= 1; side += 2)
        {
            string suffix = side < 0 ? "Left" : "Right";
            Transform sidePlate = EnsurePrimitive(
                servoVisual, "PitchOutputBracketPlate_" + suffix, PrimitiveType.Cube);
            PlaceLocal(sidePlate, new Vector3(side * bracketX, 0.002f, 0f),
                new Vector3(PitchBracketPlateThickness, PitchBracketHeight,
                    side < 0 ? PitchBracketLeftPlateDepth : PitchBracketRightPlateDepth));
            SetMaterial(sidePlate, black);

            Transform pivotBolt = EnsurePrimitive(
                servoVisual, "PitchOutputPivotBolt_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(pivotBolt, new Vector3(side * (bracketX + 0.004f), 0f, 0f),
                Quaternion.Euler(0f, 0f, 90f), CylinderScale(0.010f, 0.012f));
            SetMaterial(pivotBolt, silver);
        }

        DestroyGeneratedChild(servoVisual, "PitchOutputBracketPlate_Front");
        DestroyGeneratedChild(servoVisual, "PitchOutputBracketPlate_Rear");
        DestroyGeneratedChild(servoVisual, "PitchOutputPivotBolt_Front");
        DestroyGeneratedChild(servoVisual, "PitchOutputPivotBolt_Rear");

        Transform yokeBridge = EnsurePrimitive(
            servoVisual, "PitchOutputBracketBridge", PrimitiveType.Cube);
        PlaceLocal(yokeBridge, new Vector3(0f, 0.032f, 0f),
            new Vector3(PitchBracketWidth, PitchBracketBridgeThickness, PitchBracketBridgeDepth));
        SetMaterial(yokeBridge, black);

        DestroyGeneratedChild(servoVisual, "PitchPivotBolt_L");
        DestroyGeneratedChild(servoVisual, "PitchPivotBolt_R");
    }

    private static void BuildServo20Mark(Transform fixedAssembly, Material silver)
    {
        DestroyGeneratedChild(fixedAssembly, "Servo20Mark");
        Transform mark = EnsureEmpty(fixedAssembly, "Servo20Mark");
        PlaceLocal(mark, new Vector3(0f, -0.0035f, 0f),
            Quaternion.Euler(90f, 0f, 0f), Vector3.one);

        const float digitWidth = 0.014f;
        const float digitHeight = 0.022f;
        const float stroke = 0.003f;
        const float depth = 0.002f;
        float leftX = -0.010f;
        float rightX = 0.010f;
        float topY = digitHeight * 0.5f;
        float bottomY = -digitHeight * 0.5f;

        CreateMarkBar(mark, "TwoTop", new Vector3(leftX, topY, 0f),
            new Vector3(digitWidth, stroke, depth), silver);
        CreateMarkBar(mark, "TwoUpperRight", new Vector3(leftX + digitWidth * 0.5f, topY * 0.5f, 0f),
            new Vector3(stroke, digitHeight * 0.5f, depth), silver);
        CreateMarkBar(mark, "TwoMiddle", new Vector3(leftX, 0f, 0f),
            new Vector3(digitWidth, stroke, depth), silver);
        CreateMarkBar(mark, "TwoLowerLeft", new Vector3(leftX - digitWidth * 0.5f, bottomY * 0.5f, 0f),
            new Vector3(stroke, digitHeight * 0.5f, depth), silver);
        CreateMarkBar(mark, "TwoBottom", new Vector3(leftX, bottomY, 0f),
            new Vector3(digitWidth, stroke, depth), silver);

        CreateMarkBar(mark, "ZeroTop", new Vector3(rightX, topY, 0f),
            new Vector3(digitWidth, stroke, depth), silver);
        CreateMarkBar(mark, "ZeroBottom", new Vector3(rightX, bottomY, 0f),
            new Vector3(digitWidth, stroke, depth), silver);
        CreateMarkBar(mark, "ZeroLeft", new Vector3(rightX - digitWidth * 0.5f, 0f, 0f),
            new Vector3(stroke, digitHeight, depth), silver);
        CreateMarkBar(mark, "ZeroRight", new Vector3(rightX + digitWidth * 0.5f, 0f, 0f),
            new Vector3(stroke, digitHeight, depth), silver);
    }

    private static void CreateMarkBar(
        Transform parent, string name, Vector3 position, Vector3 size, Material material)
    {
        Transform bar = EnsurePrimitive(parent, name, PrimitiveType.Cube);
        PlaceLocal(bar, position, size);
        SetMaterial(bar, material);
    }

    private static void BuildWristAndGripperDetails(
        ArmParts arm, Material black, Material matteBlack,
        Material recessBlack, Material silver,
        Vector3? m4CaseLocalPositionOverride)
    {
        G51GripperVisual linkage = arm.gripper.GetComponent<G51GripperVisual>();
        bool preserveSavedSolverRest = linkage != null && linkage.visualRoot != null;
        Quaternion visualTurn = Quaternion.Euler(0f, 0f, -90f);
        Quaternion gripperAxisAlignment = Quaternion.FromToRotation(
            visualTurn * Vector3.down, Vector3.up);
        // Keep the fingers pointing along the M3 output (+Y), then roll the
        // complete presentation around that same axis so the linkage plate is
        // horizontal instead of standing edge-on at right angles to the arm.
        Quaternion presentationTurn =
            Quaternion.AngleAxis(90f, Vector3.up) * gripperAxisAlignment;
        Transform previousMount = arm.wristRoll.Find("GripperVisualMount_Visual");
        Transform previousOutput = arm.wristRoll.Find("M3GripperMountPoint_Visual");
        if (previousMount == null && previousOutput != null)
            previousMount = previousOutput.Find("GripperVisualMount_Visual");
        Transform oldPresentation = arm.gripper.Find("GripperPresentation_Visual");
        if (oldPresentation == null && previousMount != null)
            oldPresentation = previousMount.Find("GripperPresentation_Visual");
        // M4 Body를 Scene에서 따로 옮긴 경우 그 world 위치를 기억한다.
        // 새 M3 output에 집게 전체를 맞춘 뒤에도 M4 case만 손으로 잡은
        // 위치에 남기며, gear/finger pivot에는 이 보정을 적용하지 않는다.
        bool preserveMovedM4Case = false;
        Vector3 movedM4WorldPosition = Vector3.zero;
        Transform oldMechanismForM4 = oldPresentation != null
            ? oldPresentation.Find("GripperMechanismOffset_Visual") : null;
        Transform oldM4Body = oldMechanismForM4 != null
            ? oldMechanismForM4.Find("GripperServoBody") : null;
        if (oldM4Body != null)
        {
            Vector3 defaultM4Position = visualTurn * new Vector3(
                GripperServoCenterX, GripperServoCenterY, GripperServoCenterZ);
            preserveMovedM4Case =
                (oldM4Body.localPosition - defaultM4Position).sqrMagnitude >
                0.005f * 0.005f;
            movedM4WorldPosition = oldM4Body.position;
        }

        // Restore builder-owned visual parts to their source local coordinates.
        // This also handles an earlier presentation under Gripper control with a
        // large inverse offset, and makes repeated Builder runs deterministic.
        if (linkage != null && linkage.visualRoot != null)
        {
            Undo.RecordObject(linkage, "Clear previous gripper visual frame");
            linkage.visualRoot = null;
        }
        if (oldPresentation != null)
        {
            bool rotatedChildren =
                Quaternion.Angle(oldPresentation.localRotation, Quaternion.identity) < 1f ||
                Quaternion.Angle(oldPresentation.localRotation, gripperAxisAlignment) < 1f ||
                Quaternion.Angle(oldPresentation.localRotation, presentationTurn) < 1f;
            Quaternion reverseTurn = Quaternion.Inverse(visualTurn);
            Transform oldMechanism = oldPresentation.Find("GripperMechanismOffset_Visual");
            Transform restoreRoot = oldMechanism != null
                ? oldMechanism : oldPresentation;
            while (restoreRoot.childCount > 0)
            {
                Transform child = restoreRoot.GetChild(0);
                Vector3 restoredPosition = rotatedChildren
                    ? reverseTurn * child.localPosition : child.localPosition;
                Quaternion restoredRotation = rotatedChildren
                    ? reverseTurn * child.localRotation : child.localRotation;
                Vector3 restoredScale = child.localScale;
                Undo.SetTransformParent(child, arm.gripper,
                    "Restore gripper visual for refresh");
                Undo.RecordObject(child, "Restore gripper visual local pose");
                child.localPosition = restoredPosition;
                child.localRotation = restoredRotation;
                child.localScale = restoredScale;
            }
            Undo.DestroyObjectImmediate(oldPresentation.gameObject);
        }
        if (previousMount != null && previousMount.parent == arm.wristRoll &&
            previousMount.childCount == 0)
            Undo.DestroyObjectImmediate(previousMount.gameObject);

        BuildWristServoDetails(arm, black, silver);

        DestroyGeneratedChild(arm.wristRoll, "WristJointCollar_Visual");
        DestroyGeneratedChild(arm.wristPitch, "WristJointCollar_Visual");
        Transform wristCollar = EnsurePrimitive(
            arm.elbowPitch, "WristJointCollar_Visual", PrimitiveType.Cylinder);
        Vector3 wristPitchLocal =
            arm.elbowPitch.InverseTransformPoint(arm.wristPitch.position);
        Vector3 wristDirection = wristPitchLocal.normalized;
        PlaceLocal(wristCollar,
            wristPitchLocal - wristDirection * M2ShaftCollarCenterDrop,
            Quaternion.FromToRotation(Vector3.up, wristDirection),
            CylinderScale(M2ShaftCollarRadius, M2ShaftCollarThickness));
        SetMaterial(wristCollar, silver);

        // Gripper control position, rotation, and scale remain exactly as found.
        // Only the builder-owned display meshes are resized below.

        Transform gripperServo = arm.gripper.Find("GripperServoBody");
        if (gripperServo != null)
        {
            Undo.RecordObject(gripperServo, "Size gripper servo body");
            gripperServo.localPosition =
                new Vector3(GripperServoCenterX,
                    GripperServoCenterY, GripperServoCenterZ);
            gripperServo.localRotation = Quaternion.identity;
            gripperServo.localScale = GripperServoBodySize;
            SetMaterial(gripperServo, black);
        }

        BuildDetailedGripperHardware(
            arm.gripper, black, matteBlack, recessBlack, silver);

        if (linkage != null)
        {
            Undo.RecordObject(linkage, "Configure linkage gripper visual");
            linkage.linkThickness = GripperLinkThickness;
            linkage.linkDepth = GripperLinkDepth;
            // 저장된 finger pivot의 위치/회전/scale은 solver 기준 그대로 둔다.
            // 폭과 두께는 pivot 자식 visual과 dynamic link renderer만 조정한다.

            if (linkage.leftLinkBar != null) SetMaterial(linkage.leftLinkBar, black);
            if (linkage.rightLinkBar != null) SetMaterial(linkage.rightLinkBar, black);

            Transform[] jointAnchors = {
                linkage.leftJawPivot, linkage.rightJawPivot,
                linkage.leftJawLinkAnchor, linkage.rightJawLinkAnchor,
                linkage.leftDrivePin, linkage.rightDrivePin
            };
            for (int i = 0; i < jointAnchors.Length; ++i)
            {
                Transform anchor = jointAnchors[i];
                if (anchor == null) continue;
                Transform jointBolt = EnsurePrimitive(
                    anchor, "PhotoJointBolt_" + i.ToString("00"), PrimitiveType.Cylinder);
                PlaceLocal(jointBolt, new Vector3(0f, 0f, -0.040f),
                    Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.012f, 0.010f));
                SetMaterial(jointBolt, silver);
            }

            // Finger geometry is already linkage-based. Keep its expected hierarchy intact
            // instead of replacing the M4 mechanism with direct cube motion.
            Transform leftStem = linkage.leftJawPivot != null
                ? linkage.leftJawPivot.Find("JawStem_L")
                : null;
            if (leftStem == null || GripperFingerLength <= 0f)
                Debug.LogWarning("Linkage gripper visual이 예상 hierarchy와 다릅니다.", arm.root);
        }

        // The visible linkage is mounted directly at the M3 coupler endpoint.
        // G51GripperVisual stays on the original Gripper control and keeps all
        // of its pivot references, while its optional solver frame follows this
        // presentation under the rotating WristRoll side.
        Transform outputMount = arm.wristRoll.Find("M3GripperMountPoint_Visual");
        Transform visualMount = EnsureEmpty(
            outputMount, "GripperVisualMount_Visual");
        PlaceLocal(visualMount, Vector3.zero,
            Quaternion.identity, arm.gripper.localScale);
        Transform presentation = EnsureEmpty(
            visualMount, "GripperPresentation_Visual");
        // The original gripper's fingers extend along local -Y. The combined
        // child/presentation basis points them along WristRoll local +Y, now
        // the horizontal M3 output axis in the WristPitch frame.
        PlaceLocal(presentation, Vector3.zero, presentationTurn, Vector3.one);
        Transform mechanismOffset = EnsureEmpty(
            presentation, "GripperMechanismOffset_Visual");
        Vector3 forwardInPresentation =
            Quaternion.Inverse(presentationTurn) * Vector3.up;
        Vector3 baseMechanismOffset =
            forwardInPresentation *
            (GripperVisualForwardFromMount / arm.gripper.localScale.y);

        // 기존 M3 mount 기준을 유지하면서 집게는 바깥쪽으로 뻗는다.
        // 좌우 root가 같은 방향이므로 집게 visual의 local 조립 방향도 동일하다.
        Quaternion outwardDirectionTurn = Quaternion.Euler(0f, 0f, 180f);
        Vector3 servoPivotAfterVisualTurn = visualTurn * new Vector3(
            GripperPresentationAnchorX, GripperServoCenterY, GripperServoCenterZ);
        Vector3 outwardMechanismOffset =
            baseMechanismOffset + servoPivotAfterVisualTurn -
            outwardDirectionTurn * servoPivotAfterVisualTurn;
        PlaceLocal(mechanismOffset,
            outwardMechanismOffset, outwardDirectionTurn, Vector3.one);
        Transform[] visualChildren = new Transform[arm.gripper.childCount];
        int visualIndex = 0;
        foreach (Transform child in arm.gripper)
            visualChildren[visualIndex++] = child;
        foreach (Transform child in visualChildren)
        {
            Vector3 sourcePosition = child.localPosition;
            Quaternion sourceRotation = child.localRotation;
            Vector3 sourceScale = child.localScale;
            Undo.SetTransformParent(child, mechanismOffset, "Group M4 visual mechanism");
            Undo.RecordObject(child, "Turn M4 visual mechanism sideways");
            child.localPosition = visualTurn * sourcePosition;
            child.localRotation = visualTurn * sourceRotation;
            child.localScale = sourceScale;
        }

        Transform newM4Body = mechanismOffset.Find("GripperServoBody");
        if (newM4Body != null &&
            (m4CaseLocalPositionOverride.HasValue || preserveMovedM4Case))
        {
            Vector3 targetM4CaseLocalPosition =
                m4CaseLocalPositionOverride.HasValue
                    ? m4CaseLocalPositionOverride.Value
                    : mechanismOffset.InverseTransformPoint(movedM4WorldPosition);
            Vector3 caseShift = targetM4CaseLocalPosition - newM4Body.localPosition;
            foreach (Transform child in mechanismOffset)
            {
                if (!IsM4CaseFollower(child.name)) continue;
                Undo.RecordObject(child, "Align M4 assembly with left arm");
                child.localPosition += caseShift;
            }
        }

        // M4/gear/finger pivot은 고정. 서로 벌어진 거리만 visual plate가 잇는다.
        BuildGripperM4Bridge(mechanismOffset, newM4Body,
            matteBlack, recessBlack, silver);

        if (linkage != null)
        {
            Undo.RecordObject(linkage, "Use M3 output as gripper visual frame");
            linkage.visualRoot = mechanismOffset;
            // 저장된 gear rest/4-bar 길이/branch 기준은 다시 계산하지 않는다.
            // 처음 visualRoot를 만드는 Scene에서만 초기 기준을 잡는다.
            if (!preserveSavedSolverRest)
                linkage.CaptureRest();
        }
    }

    private static bool TryGetM4CaseLocalPosition(
        ArmParts arm, out Vector3 localPosition)
    {
        localPosition = Vector3.zero;
        G51GripperVisual linkage =
            arm.gripper != null ? arm.gripper.GetComponent<G51GripperVisual>() : null;
        Transform mechanism = linkage != null ? linkage.visualRoot : null;
        Transform body = mechanism != null
            ? mechanism.Find("GripperServoBody")
            : null;
        if (body == null)
            return false;

        localPosition = body.localPosition;
        return true;
    }

    private static void BuildWristServoDetails(
        ArmParts arm, Material black, Material silver)
    {
        // 사진과 맞지 않는 기존 wrist placeholder가 새 조립체와 겹치거나 공중에
        // 남지 않도록 renderer root만 비활성화한다. Control Transform은 유지한다.
        foreach (string legacyName in new[] {
            "__G51V2_Bracket_Wrist_Pitch", "__G51V2_Servo_Wrist_Pitch",
            "__G51V3_Bolt_Wrist_Pitch_A", "__G51V3_Bolt_Wrist_Pitch_B",
            "__G51V2_LinkA_Wrist_Pitch", "__G51V2_LinkB_Wrist_Pitch",
            "WristLink", "__G51V2_Servo_Wrist_Roll",
            "__G51V2_Bracket_Wrist_Roll", "__G51V3_Bolt_Wrist_Roll_A",
            "__G51V3_Bolt_Wrist_Roll_B"
        })
            SetDescendantActive(arm.elbowPitch, legacyName, false);

        // 이전 배치에서 M2/M3가 반대 parent에 생성됐던 visual만 정리한다.
        // 실제 control Transform과 legacy mesh 원본은 삭제하지 않는다.
        foreach (string generatedName in new[] {
            "WristShaftClamp_Visual", "WristUpperBracketBridge_Visual",
            "WristRollMG996R_Visual", "WristPitchMG996R_Visual",
            "WristPitchOutputHub_Visual", "WristRollOutputHub_Visual",
            "WristRollToToolMountLink_Visual",
            "M2WristPitchFixedAssembly_Visual", "M3WristRollFixedAssembly_Visual"
        })
        {
            DestroyGeneratedChild(arm.elbowPitch, generatedName);
            DestroyGeneratedChild(arm.wristPitch, generatedName);
            DestroyGeneratedChild(arm.wristRoll, generatedName);
        }
        foreach (string generatedName in new[] {
            "WristUpperBracketPlate_Front", "WristUpperBracketPlate_Rear",
            "WristUpperPivotBolt_Front", "WristUpperPivotBolt_Rear"
        })
            DestroyGeneratedChild(arm.wristRoll, generatedName);

        // M2 WristPitch servo body는 M1 output link에 고정된다. 따라서 M2가
        // 회전해도 case는 정지하며, chrome shaft 끝에 조밀한 U bracket으로 결합된다.
        Vector3 m2Pivot = arm.elbowPitch.InverseTransformPoint(arm.wristPitch.position);
        Transform m2Assembly = EnsureEmpty(
            arm.elbowPitch, "M2WristPitchFixedAssembly_Visual");
        PlaceLocal(m2Assembly, m2Pivot, Vector3.one);

        // 이전 검은 사각 clamp는 silver collar와 bottom plate 사이를 가리고
        // shaft 중심도 흐려 보이게 했다. 실물처럼 원형 collar가 바닥판에
        // 직접 닿도록 builder 소유 placeholder를 제거한다.
        DestroyGeneratedChild(m2Assembly, "WristShaftClamp_Visual");

        // Replace the former top bridge/two-strip silhouette with the real
        // bottom + two-side U-frame. The chrome shaft meets this bottom plate.
        DestroyGeneratedChild(m2Assembly, "WristPitchBracketBridge_Visual");
        // MG996R visual의 local Z가 output axis, local Y가 body 장축이다.
        // 먼저 output을 M2 +X에 맞춘 뒤, 그 output 축(local Z) 둘레로
        // 사진의 파란 화살표 방향인 -90도로 눕혀 body 장축을 수평으로 둔다.
        // Post-multiply이므로 horn/output 방향은 바뀌지 않는다.
        Quaternion m2ServoOrientation =
            Quaternion.LookRotation(Vector3.right, Vector3.down) *
            Quaternion.Euler(0f, 0f, -90f);
        Transform oldM2Servo = m2Assembly.Find("WristPitchMG996R_Visual");
        bool m2LowerEndCaseRemoved = oldM2Servo != null &&
            oldM2Servo.Find("EndCase_Lower") == null;
        bool m2LowerEarRemoved = oldM2Servo != null &&
            oldM2Servo.Find("MountingEar_Lower") == null;
        Transform m2Servo = BuildMg996RServoVisual(
            m2Assembly, "WristPitchMG996R_Visual",
            Vector3.zero, m2ServoOrientation,
            black, silver);
        Transform m2Horn = m2Servo.Find("OutputHorn");
        if (m2Horn != null)
        {
            Vector3 hornInAssembly =
                m2Assembly.InverseTransformPoint(m2Horn.position);
            Undo.RecordObject(m2Servo, "Align M2 servo horn to WristPitch pivot");
            m2Servo.localPosition -= hornInAssembly;
        }
        OffsetServoCaseVisual(m2Servo, M2CaseVisualOffset, false);
        if (m2LowerEndCaseRemoved)
            DestroyGeneratedChild(m2Servo, "EndCase_Lower");
        if (m2LowerEarRemoved)
        {
            DestroyGeneratedChild(m2Servo, "MountingEar_Lower");
            DestroyGeneratedChild(m2Servo, "EarBolt_Lower_L");
            DestroyGeneratedChild(m2Servo, "EarBolt_Lower_R");
        }

        // Build the photographed U-frame around the final laid-down servo pose,
        // rather than around a second independent offset. The base remains on
        // the chrome-shaft side and both legs open toward the servo/output.
        Transform oldM2BracketFrame = m2Assembly.Find("M2BracketFrame_Visual");
        bool m2RightPlateRemoved = oldM2BracketFrame != null &&
            oldM2BracketFrame.Find("WristPitchBracketPlate_Left") != null &&
            oldM2BracketFrame.Find("WristPitchBracketPlate_Right") == null;
        Transform m2BracketFrame = EnsureEmpty(
            m2Assembly, "M2BracketFrame_Visual");
        PlaceLocal(m2BracketFrame,
            Vector3.zero,
            M2BracketRotation, Vector3.one);

        Transform m2Body = m2Servo.Find("Body");
        float m2BodyCenterX =
            m2BracketFrame.InverseTransformPoint(m2Body.position).x;
        float m2BodyHalfWidth = StandardServoBodySize.y * 0.5f;

        Transform m2BracketBase = EnsurePrimitive(
            m2BracketFrame, "WristPitchBracketBase_Visual", PrimitiveType.Cube);
        PlaceLocal(m2BracketBase,
            new Vector3(m2BodyCenterX, -M2BracketBaseDrop, 0f),
            new Vector3(M2BracketWidth, M2BracketBaseThickness, M2BracketBaseDepth));
        SetMaterial(m2BracketBase, black);

        // Two tall side plates rise from the bottom plate and hold the M2
        // output-side and rear-side faces, matching the photographed U-frame.
        // MG996R의 output은 case 중앙에서 치우쳐 있으므로 plate pair도 pivot(0)이
        // 아니라 고정된 servo body 중심을 기준으로 잡아야 한쪽 판과 겹치지 않는다.
        // Servo transform은 건드리지 않고 두 판에 똑같은 1 mm 간격을 준다.
        for (int side = -1; side <= 1; side += 2)
        {
            string suffix = side < 0 ? "Left" : "Right";
            if (side > 0 && m2RightPlateRemoved)
            {
                foreach (string removedPart in new[] {
                    "WristPitchPivotBolt_Right", "WristPitchBearingSeat_Right",
                    "WristPitchBearingCollar_Right", "WristPitchOutputBolt_Right_A",
                    "WristPitchOutputBolt_Right_B", "WristPitchFrameBolt_Right_Lower",
                    "WristPitchFrameBolt_Right_Upper"
                })
                    DestroyGeneratedChild(m2BracketFrame, removedPart);
                continue;
            }
            float plateX = m2BodyCenterX + side *
                (m2BodyHalfWidth + M2ServoSideClearance +
                 PitchBracketPlateThickness * 0.5f);
            Transform plate = EnsurePrimitive(
                m2BracketFrame, "WristPitchBracketPlate_" + suffix, PrimitiveType.Cube);
            PlaceLocal(plate,
                new Vector3(plateX,
                    -M2BracketBaseDrop + M2BracketBaseThickness * 0.5f +
                    M2BracketSideHeight * 0.5f,
                    0f),
                new Vector3(PitchBracketPlateThickness,
                    M2BracketSideHeight,
                    side < 0 ? PitchBracketLeftPlateDepth : PitchBracketRightPlateDepth));
            SetMaterial(plate, black);

            Transform pivotBolt = EnsurePrimitive(
                m2BracketFrame, "WristPitchPivotBolt_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(pivotBolt,
                new Vector3(plateX + side * 0.002f, 0f, 0f),
                Quaternion.Euler(0f, 0f, 90f), CylinderScale(0.003f, 0.003f));
            SetMaterial(pivotBolt, silver);

            // U-frame bearing washer는 servo body보다 훨씬 작다. 출력 spline은
            // MG996R 실물 사진의 작은 축 크기로 표현한다.
            Transform bearingSeat = EnsurePrimitive(
                m2BracketFrame, "WristPitchBearingSeat_" + suffix,
                PrimitiveType.Cylinder);
            PlaceLocal(bearingSeat,
                new Vector3(plateX + side * 0.003f, 0f, 0f),
                Quaternion.Euler(0f, 0f, 90f), CylinderScale(0.007f, 0.003f));
            SetMaterial(bearingSeat, black);

            Transform bearingCollar = EnsurePrimitive(
                m2BracketFrame, "WristPitchBearingCollar_" + suffix,
                PrimitiveType.Cylinder);
            PlaceLocal(bearingCollar,
                new Vector3(plateX + side * 0.005f, 0f, 0f),
                Quaternion.Euler(0f, 0f, 90f), CylinderScale(0.004f, 0.004f));
            SetMaterial(bearingCollar, silver);

            // 실물 M2 output plate의 작은 축 양옆 체결 볼트 두 개.
            foreach (int boltSide in new[] { -1, 1 })
            {
                Transform outputBolt = EnsurePrimitive(
                    m2BracketFrame,
                    "WristPitchOutputBolt_" + suffix +
                    (boltSide < 0 ? "_A" : "_B"), PrimitiveType.Cylinder);
                PlaceLocal(outputBolt,
                    new Vector3(plateX + side * 0.006f,
                        boltSide * 0.010f, 0f),
                    Quaternion.Euler(0f, 0f, 90f),
                    CylinderScale(0.0025f, 0.003f));
                SetMaterial(outputBolt, silver);
            }

            // 실물 perforated plate의 모서리 체결점을 얕은 bolt head로 표현한다.
            foreach (int vertical in new[] { -1, 1 })
            {
                Transform frameBolt = EnsurePrimitive(
                    m2BracketFrame,
                    "WristPitchFrameBolt_" + suffix +
                    (vertical < 0 ? "_Lower" : "_Upper"),
                    PrimitiveType.Cylinder);
                PlaceLocal(frameBolt,
                    new Vector3(plateX + side * 0.005f,
                        vertical < 0 ? -0.020f : 0.020f,
                        -M2BracketBaseDepth * 0.28f),
                    Quaternion.Euler(0f, 0f, 90f),
                    CylinderScale(0.003f, 0.003f));
                SetMaterial(frameBolt, silver);
            }
        }

        // M2 output hub는 WristPitch control 아래에 두어 M2 command와 함께 회전한다.
        Transform pitchHub = EnsurePrimitive(
            arm.wristPitch, "WristPitchOutputHub_Visual", PrimitiveType.Cylinder);
        PlaceLocal(pitchHub, Vector3.zero, Quaternion.Euler(0f, 0f, 90f),
            CylinderScale(0.005f, 0.004f));
        SetMaterial(pitchHub, silver);

        // M3 WristRoll servo case는 M2 output측에 고정한다. Roll output hub만
        // WristRoll control 아래에 두므로 두 검은 servo가 한 박스로 겹쳐 보이지 않는다.
        Transform m3Assembly = EnsureEmpty(
            arm.wristPitch, "M3WristRollFixedAssembly_Visual");
        PlaceLocal(m3Assembly, Vector3.zero, Vector3.one);
        // RobotArm_L에서 추가한 세 번째 절곡판이 있으면 그 판과 함께
        // 손으로 맞춘 기존 두 판의 현재 Transform도 그대로 유지한다.
        bool preserveHandM3Bracket =
            m3Assembly.Find("M3BracketBase_Visual (1)") != null &&
            m3Assembly.Find("M3BracketBase_Visual") != null &&
            m3Assembly.Find("M3BracketConnector_Visual") != null;

        // Use the actual M3 control axis in this arm's local frame. Mirrored arm
        // roots therefore mirror the whole assembly without world-left/right cases.
        Vector3 rollPivot = m3Assembly.InverseTransformPoint(arm.wristRoll.position);
        Vector3 rollAxis =
            m3Assembly.InverseTransformDirection(arm.wristRoll.up).normalized;
        // The M3 output is along M2 local -X. Use the shaft's local up as the
        // bracket radial: using WristPitch.right here would be parallel to the
        // new roll axis and would make Cross(radial, rollAxis) degenerate.
        Vector3 radial =
            m3Assembly.InverseTransformDirection(arm.wristPitch.up).normalized;
        Vector3 bracketSide = Vector3.Cross(radial, rollAxis).normalized;
        Quaternion bracketFrame =
            Quaternion.LookRotation(bracketSide, rollAxis);

        // The MG996R helper's horn points along local -Z. Point it along the
        // WristRoll axis towards the gripper, then move the fixed case until the
        // horn center coincides exactly with the existing control pivot.
        Quaternion m3ServoOrientation =
            Quaternion.LookRotation(-rollAxis, radial) *
            Quaternion.Euler(0f, 0f, -90f);
        bool m3UpperPlateRemoved =
            m3Assembly.Find("M3ServoSidePlate_Lower") != null &&
            m3Assembly.Find("M3ServoSidePlate_Upper") == null;
        Transform m3Servo = BuildMg996RServoVisual(
            m3Assembly, "WristRollMG996R_Visual",
            rollPivot, m3ServoOrientation, black, silver);
        // M3도 M0/M2와 같은 servo 크기를 유지한다. 수평 외형은 위 orientation
        // 만으로 만들며 비균등 root scale을 적용하지 않는다.
        Transform m3Horn = m3Servo.Find("OutputHorn");
        Vector3 hornOffset =
            m3Assembly.InverseTransformPoint(m3Horn.position) - rollPivot;
        Undo.RecordObject(m3Servo, "Align M3 servo output with WristRoll pivot");
        m3Servo.localPosition -= hornOffset;
        OffsetServoCaseVisual(m3Servo, M3CaseVisualOffset, true);
        Transform m3Body = m3Servo.Find("Body");
        Vector3 m3BodyCenter =
            m3Assembly.InverseTransformPoint(m3Body.position);

        // 사진 3/4의 M3는 서보 case가 노출된 채 얇은 상/하 고정판 사이에
        // 들어간다. Plate는 fixed side에 두어 M3 roll과 함께 공전하지 않는다.
        foreach (int side in new[] { -1, 1 })
        {
            string suffix = side < 0 ? "Lower" : "Upper";
            if (side > 0 && m3UpperPlateRemoved)
            {
                DestroyGeneratedChild(m3Assembly, "M3ServoSidePlate_Upper");
                DestroyGeneratedChild(m3Assembly, "M3ServoPlateBolt_Upper_A");
                DestroyGeneratedChild(m3Assembly, "M3ServoPlateBolt_Upper_B");
                continue;
            }
            Transform servoPlate = EnsurePrimitive(
                m3Assembly, "M3ServoSidePlate_" + suffix, PrimitiveType.Cube);
            if (side < 0)
                PlaceLocal(servoPlate, M3LowerPlateHandPosition,
                    bracketFrame, new Vector3(0.003f, 0.019048f, 0.041511f));
            else
                PlaceLocal(servoPlate,
                    m3BodyCenter + radial * (side * 0.023f) +
                    bracketSide * 0.004f,
                    bracketFrame, new Vector3(0.003f, 0.050f, 0.030f));
            SetMaterial(servoPlate, black);

            foreach (int end in new[] { -1, 1 })
            {
                Transform plateBolt = EnsurePrimitive(
                    m3Assembly,
                    "M3ServoPlateBolt_" + suffix + (end < 0 ? "_A" : "_B"),
                    PrimitiveType.Cylinder);
                PlaceLocal(plateBolt,
                    m3BodyCenter + radial * (side * 0.025f) +
                    rollAxis * (end * 0.018f) + bracketSide * 0.013f,
                    bracketFrame * Quaternion.Euler(0f, 0f, 90f),
                    CylinderScale(0.003f, 0.004f));
                SetMaterial(plateBolt, silver);
            }
        }

        // 지붕은 실제 M2 body의 윗면에 맞추고, 수동으로 맞춘 ㄱ자 판은
        // 아래에서 그 판의 전방 절곡면에 이어지도록 유지한다.
        Vector3 m2BodyCenter = m3Assembly.InverseTransformPoint(m2Body.position);
        float m2BodyAlongRoll = Vector3.Dot(m2BodyCenter, rollAxis);
        float m2BodyAlongRadial = Vector3.Dot(m2BodyCenter, radial);
        float m2BodyAlongSide = Vector3.Dot(m2BodyCenter, bracketSide);
        float m2BodyHalfAlongRoll = ProjectedHalfExtentInLocalDirection(
            m2Body, m3Assembly, rollAxis);
        float m2BodyHalfRadial = ProjectedHalfExtentInLocalDirection(
            m2Body, m3Assembly, radial);
        // 3 mm 지붕판의 안쪽 면이 M2 case 윗면에 닿는다.
        float roofRadial = m2BodyAlongRadial + m2BodyHalfRadial + 0.0015f;
        float linkStart = m2BodyAlongRoll + m2BodyHalfAlongRoll + 0.002f;

        DestroyGeneratedChild(m3Assembly, "M2ToM3Collar_Visual");
        DestroyGeneratedChild(m3Assembly, "M2ToM3Flange_Visual");

        // These fixed-side helper parts only locate the output while building.
        // The visible shaft and flange below are children of the rotating side.
        SetChildActive(m3Servo, "OutputShaft", false);
        SetChildActive(m3Servo, "OutputHorn", false);

        // 두 판의 위치/회전/길이는 RobotArm_L에서 손으로 맞춘 ㄱ자 옆면값이다.
        // Builder가 두 평행 rail로 되돌리지 않게 그대로 생성한다.
        const float railRadial = -0.023f;
        DestroyGeneratedChild(m3Assembly, "M3BracketUpright_Visual");
        foreach (int side in new[] { -1, 1 })
        {
            string suffix = side < 0 ? "Left" : "Right";
            Vector3 sideOffset = bracketSide * (m2BodyAlongSide + side * 0.010f);
            Transform longRail = EnsurePrimitive(
                m3Assembly,
                side < 0 ? "M3BracketBase_Visual" : "M3BracketConnector_Visual",
                PrimitiveType.Cube);
            if (!preserveHandM3Bracket)
            {
                if (side < 0)
                    PlaceLocal(longRail, M3BracketBaseHandPosition,
                        bracketFrame, new Vector3(0.003f, 0.03010965f, 0.014f));
                else
                    PlaceLocal(longRail, M3BracketConnectorHandPosition,
                        Quaternion.Euler(0f, 0f, 178.522f),
                        new Vector3(0.003f, 0.035456985f, 0.014f));
            }
            SetMaterial(longRail, black);

            // 실물의 좌우 반전된 ㄱ틀은 M2 쪽에서 꺾여 roof와 rail을 잇는다.
            Transform returnPlate = EnsurePrimitive(
                m3Assembly, "M2ToM3LBracketReturn_" + suffix,
                PrimitiveType.Cube);
            PlaceLocal(returnPlate,
                rollAxis * linkStart +
                radial * ((roofRadial + railRadial) * 0.5f) + sideOffset,
                bracketFrame,
                new Vector3(roofRadial - railRadial + 0.003f,
                    0.003f, 0.014f));
            SetMaterial(returnPlate, black);

            Transform returnBolt = EnsurePrimitive(
                m3Assembly, "M2ToM3LBracketBolt_" + suffix,
                PrimitiveType.Cylinder);
            PlaceLocal(returnBolt,
                rollAxis * linkStart + radial * (roofRadial - 0.007f) + sideOffset,
                bracketFrame * Quaternion.Euler(90f, 0f, 0f),
                CylinderScale(0.003f, 0.004f));
            SetMaterial(returnBolt, silver);
        }

        // M2 case를 덮는 판은 실물처럼 M3 쪽 끝이 반원이다. 뒤쪽 절반은
        // 얇은 직사각형, 앞쪽은 같은 폭의 타원 원판으로 겹쳐 한 장의
        // 평평한 ㄱ틀 지붕처럼 보이게 한다. 두 roof bolt는 그대로 둔다.
        Vector3 roofCenter =
            rollAxis * m2BodyAlongRoll + radial * roofRadial +
            bracketSide * m2BodyAlongSide;
        Transform m2Roof = EnsurePrimitive(
            m3Assembly, "M2ServoRoof_Visual", PrimitiveType.Cube);
        PlaceLocal(m2Roof, roofCenter - rollAxis * 0.010f,
            bracketFrame, new Vector3(0.003f, 0.020f, 0.050f));
        SetMaterial(m2Roof, black);
        Transform roundedRoofEnd = EnsurePrimitive(
            m3Assembly, "M2ServoRoofRound_Visual", PrimitiveType.Cylinder);
        PlaceLocal(roundedRoofEnd, roofCenter,
            bracketFrame * Quaternion.Euler(0f, 0f, 90f),
            new Vector3(0.040f, 0.0014f, 0.050f));
        SetMaterial(roundedRoofEnd, black);

        for (int boltIndex = 0; boltIndex < 2; ++boltIndex)
        {
            Transform roofBolt = EnsurePrimitive(
                m3Assembly, "M2ServoRoofBolt_" + boltIndex.ToString("00"),
                PrimitiveType.Cylinder);
            PlaceLocal(roofBolt,
                rollAxis * (m2BodyAlongRoll + (boltIndex == 0 ? -0.012f : 0.012f)) +
                radial * (roofRadial + 0.0025f) +
                bracketSide * m2BodyAlongSide,
                Quaternion.FromToRotation(Vector3.up, radial),
                CylinderScale(0.002f, 0.002f));
            SetMaterial(roofBolt, silver);
        }

        foreach (int side in new[] { -1, 1 })
        {
            string suffix = side < 0 ? "Left" : "Right";
            Transform earBolt = EnsurePrimitive(
                m3Assembly, "M3MountingEarBolt_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(earBolt,
                m3BodyCenter + radial * (side * 0.020f) +
                bracketSide * 0.015f,
                bracketFrame * Quaternion.Euler(90f, 0f, 0f),
                CylinderScale(0.003f, 0.005f));
            SetMaterial(earBolt, silver);
            DestroyGeneratedChild(m3Assembly, "M3UprightBolt_" + suffix);
        }

        // M3 case를 축방향으로 옮긴 만큼 output ring과 gripper mount도 같은
        // 축 위로 옮긴다. WristRoll control pivot/axis 자체는 그대로다.
        float rollOutputShift =
            arm.wristRoll.InverseTransformPoint(m3Horn.position).y;
        Vector3 rollOutputOffset = Vector3.up * rollOutputShift;
        Transform rollHub = EnsurePrimitive(
            arm.wristRoll, "WristRollOutputHub_Visual", PrimitiveType.Cylinder);
        PlaceLocal(rollHub, rollOutputOffset, CylinderScale(0.010f, 0.006f));
        SetMaterial(rollHub, silver);

        Transform outputSpindle = EnsurePrimitive(
            arm.wristRoll, "M3OutputSpindle_Visual", PrimitiveType.Cylinder);
        PlaceLocal(outputSpindle, rollOutputOffset + Vector3.down * 0.005f,
            CylinderScale(0.004f, 0.018f));
        SetMaterial(outputSpindle, silver);

        Transform outputFlange = EnsurePrimitive(
            arm.wristRoll, "M3OutputFlange_Visual", PrimitiveType.Cylinder);
        PlaceLocal(outputFlange, rollOutputOffset + Vector3.up * 0.004f,
            CylinderScale(0.011f, 0.005f));
        SetMaterial(outputFlange, silver);

        // 사진 3/6에서 보이는 output flange의 네 체결점. WristRoll 아래에 두어
        // flange와 함께 M3 축을 중심으로 회전한다.
        for (int boltIndex = 0; boltIndex < 4; ++boltIndex)
        {
            float angle = boltIndex * Mathf.PI * 0.5f + Mathf.PI * 0.25f;
            Transform flangeBolt = EnsurePrimitive(
                arm.wristRoll,
                "M3OutputFlangeBolt_" + boltIndex.ToString("00"),
                PrimitiveType.Cylinder);
            PlaceLocal(flangeBolt,
                rollOutputOffset + new Vector3(Mathf.Cos(angle) * 0.008f,
                    0.007f, Mathf.Sin(angle) * 0.008f),
                CylinderScale(0.002f, 0.003f));
            SetMaterial(flangeBolt, black);
        }

        Transform outputCoupler = EnsurePrimitive(
            arm.wristRoll, "M3OutputCoupler_Visual", PrimitiveType.Cylinder);
        float outputCouplerStart = 0.006f;
        float outputCouplerLength = M3GripperCouplerEnd - outputCouplerStart;
        PlaceLocal(outputCoupler,
            rollOutputOffset +
            Vector3.up * (outputCouplerStart + outputCouplerLength * 0.5f),
            CylinderScale(0.008f, outputCouplerLength));
        SetMaterial(outputCoupler, silver);

        Transform outputMount = EnsureEmpty(
            arm.wristRoll, "M3GripperMountPoint_Visual");
        PlaceLocal(outputMount,
            rollOutputOffset + Vector3.up * M3GripperCouplerEnd, Vector3.one);

        // ToolMount and the M4 presentation use this same coupler endpoint.
        // The control hierarchy stays untouched; only the visible mount moves.
        Vector3 gripperMountLocal =
            arm.toolMount.InverseTransformPoint(outputMount.position);
        Transform toolBridge = EnsurePrimitive(
            arm.toolMount, "WristToGripperBridge_Visual", PrimitiveType.Cube);
        PlaceLocal(toolBridge, gripperMountLocal,
            new Vector3(0.020f, 0.003f, 0.020f));
        SetMaterial(toolBridge, black);

        DestroyGeneratedChild(arm.toolMount, "ToolToGripperConnector_Visual");
        DestroyGeneratedChild(arm.toolMount, "GripperMountFlange_Visual");
        Transform mountPlate = EnsurePrimitive(
            arm.toolMount, "GripperMountPlate_Visual", PrimitiveType.Cube);
        PlaceLocal(mountPlate, gripperMountLocal,
            Quaternion.Inverse(arm.toolMount.rotation) * arm.wristRoll.rotation,
            new Vector3(0.003f, 0.034f, 0.040f));
        SetMaterial(mountPlate, black);
    }

    private static void AssembleLegacyWristPitchVisuals(Transform wristRoll)
    {
        Transform mount = EnsureEmpty(wristRoll, "WristPitchVisualMount");
        PlaceLocal(mount, new Vector3(0f, LegacyWristAssemblyLift, 0f), Vector3.one);

        // M2 fixed-side visual 전체를 공통 mount 아래로 묶는다. bracket와 servo만
        // 따로 올렸을 때 bolt/link가 아래에 남아 분해되어 보이던 문제를 방지한다.
        // 원래의 -0.11 위치를 복원한 뒤 mount를 +0.11 올리므로 bracket 기준점은
        // 정확히 WristRoll 원점, 즉 chrome shaft 끝에 놓인다.
        PlaceLegacyWristPart(mount, wristRoll, "__G51V2_Bracket_Wrist_Pitch",
            new Vector3(0f, -0.110f, 0f));
        PlaceLegacyWristPart(mount, wristRoll, "__G51V2_Servo_Wrist_Pitch",
            new Vector3(0f, -0.110f, 0f));
        PlaceLegacyWristPart(mount, wristRoll, "__G51V3_Bolt_Wrist_Pitch_A",
            new Vector3(0.02447032f, -0.110f, 0.01141071f));
        PlaceLegacyWristPart(mount, wristRoll, "__G51V3_Bolt_Wrist_Pitch_B",
            new Vector3(-0.02447032f, -0.110f, -0.01141068f));
        PlaceLegacyWristPart(mount, wristRoll, "__G51V2_LinkA_Wrist_Pitch",
            new Vector3(-0.01182928f, -0.091519f, 0.02536795f));
        PlaceLegacyWristPart(mount, wristRoll, "__G51V2_LinkB_Wrist_Pitch",
            new Vector3(-0.00084932f, -0.076519f, 0.00182137f));

        // 다음 wrist motor와 연결 link도 같은 0.11 m만큼 위로 맞춘다.
        // Parent는 그대로 WristRoll에 두어 기존 M2/M3 제어 계약을 유지한다.
        PlaceLegacyWristPartAtCurrentParent(
            wristRoll, "WristLink", new Vector3(-0.00633928f, 0.02598083f, 0.01359463f));
        PlaceLegacyWristPartAtCurrentParent(
            wristRoll, "__G51V2_Servo_Wrist_Roll",
            new Vector3(-0.01267859f, 0.05196172f, 0.02718929f));
    }

    private static void PlaceLegacyWristPart(
        Transform mount, Transform searchRoot, string name, Vector3 localPosition)
    {
        Transform visual = FindDescendantByName(searchRoot, name);
        if (visual == null) return;

        if (visual.parent != mount)
            Undo.SetTransformParent(visual, mount, "Group " + name + " at chrome shaft end");
        Undo.RecordObject(visual, "Assemble " + name + " at chrome shaft end");
        visual.localPosition = localPosition;
    }

    private static void PlaceLegacyWristPartAtCurrentParent(
        Transform searchRoot, string name, Vector3 localPosition)
    {
        Transform visual = FindDescendantByName(searchRoot, name);
        if (visual == null) return;
        Undo.RecordObject(visual, "Lift connected " + name);
        visual.localPosition = localPosition;
    }

    private static Transform FindDescendantByName(Transform root, string name)
    {
        if (root == null) return null;
        foreach (Transform child in root)
        {
            if (child.name == name) return child;
            Transform nested = FindDescendantByName(child, name);
            if (nested != null) return nested;
        }
        return null;
    }

    private static void SetDescendantActive(Transform root, string name, bool active)
    {
        Transform found = FindDescendantByName(root, name);
        if (found == null || found.gameObject.activeSelf == active) return;
        Undo.RecordObject(found.gameObject, "Set " + name + " active");
        found.gameObject.SetActive(active);
    }

    private static Transform BuildMg996RServoVisual(
        Transform parent, string name, Vector3 position, Quaternion rotation,
        Material black, Material silver)
    {
        Transform root = EnsureEmpty(parent, name);
        PlaceLocal(root, position, rotation, Vector3.one);

        Transform body = EnsurePrimitive(root, "Body", PrimitiveType.Cube);
        PlaceLocal(body, Vector3.zero, StandardServoBodySize);
        SetMaterial(body, black);

        foreach (int seamSide in new[] { -1, 1 })
        {
            Transform seam = EnsurePrimitive(
                root, "CaseSeam_" + (seamSide < 0 ? "Lower" : "Upper"),
                PrimitiveType.Cube);
            PlaceLocal(seam, new Vector3(0f, seamSide * 0.017f, -0.011f),
                new Vector3(0.042f, 0.0015f, 0.001f));
            SetMaterial(seam, black);
        }

        foreach (int end in new[] { -1, 1 })
        {
            string suffix = end < 0 ? "Lower" : "Upper";
            Transform endCase = EnsurePrimitive(
                root, "EndCase_" + suffix, PrimitiveType.Cube);
            PlaceLocal(endCase, new Vector3(0f, end * 0.018f, 0f),
                new Vector3(0.042f, 0.006f, 0.022f));
            SetMaterial(endCase, black);

            Transform ear = EnsurePrimitive(
                root, "MountingEar_" + suffix, PrimitiveType.Cube);
            PlaceLocal(ear, new Vector3(0f, end * 0.025f, 0f),
                new Vector3(0.050f, 0.006f, 0.024f));
            SetMaterial(ear, black);

            foreach (int side in new[] { -1, 1 })
            {
                Transform screw = EnsurePrimitive(
                    root, "EarBolt_" + suffix + (side < 0 ? "_L" : "_R"),
                    PrimitiveType.Cylinder);
                PlaceLocal(screw, new Vector3(side * 0.017f, end * 0.025f, -0.013f),
                    Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.0025f, 0.003f));
                SetMaterial(screw, silver);
            }
        }

        // MG996R 계열의 output boss는 넓은 전면 중앙이 아니라 한쪽 끝에 치우쳐 있다.
        Transform boss = EnsurePrimitive(root, "OutputBoss", PrimitiveType.Cylinder);
        PlaceLocal(boss, new Vector3(0f, 0.012f, -0.012f),
            Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.008f, 0.004f));
        SetMaterial(boss, black);

        Transform shaft = EnsurePrimitive(root, "OutputShaft", PrimitiveType.Cylinder);
        PlaceLocal(shaft, new Vector3(0f, 0.012f, -0.016f),
            Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.0045f, 0.006f));
        SetMaterial(shaft, silver);

        Transform horn = EnsurePrimitive(root, "OutputHorn", PrimitiveType.Cylinder);
        PlaceLocal(horn, new Vector3(0f, 0.012f, -0.020f),
            Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.010f, 0.0025f));
        SetMaterial(horn, silver);

        Transform cableStub = EnsurePrimitive(root, "CableExit", PrimitiveType.Cylinder);
        PlaceLocal(cableStub, new Vector3(-0.012f, -0.024f, 0f),
            Quaternion.Euler(0f, 0f, 90f), CylinderScale(0.003f, 0.010f));
        SetMaterial(cableStub, black);
        return root;
    }

    private static void OffsetServoCaseVisual(Transform servo, Vector3 offset,
        bool includeOutput)
    {
        // Body만 이동시키면 end case/ear/cable이 옛 위치에 남는다. M2의
        // output은 control pivot에 남기고, M3의 output은 body와 함께 축방향으로
        // 옮긴다. 어느 쪽도 servo 크기나 control Transform은 변경하지 않는다.
        foreach (Transform child in servo)
        {
            bool output = child.name == "OutputBoss" ||
                          child.name == "OutputShaft" ||
                          child.name == "OutputHorn";
            if (output && !includeOutput) continue;
            Undo.RecordObject(child, "Align servo case visual");
            child.localPosition += offset;
        }
    }

    private static bool IsM4CaseFollower(string name)
    {
        return name.StartsWith("GripperServo", StringComparison.Ordinal) ||
               name.StartsWith("GripperM4Upper", StringComparison.Ordinal);
    }

    private static void BuildGripperM4Bridge(
        Transform mechanism, Transform m4Body, Material matteBlack,
        Material recessBlack, Material silver)
    {
        Transform gearL = mechanism.Find("Gear_L");
        Transform gearR = mechanism.Find("Gear_R");
        Transform fingerL = mechanism.Find("Finger_L");
        Transform fingerR = mechanism.Find("Finger_R");
        if (m4Body == null || gearL == null || gearR == null ||
            fingerL == null || fingerR == null)
            return;

        // 저장된 M4 Body와 gear/finger pivot은 읽기만 한다.
        // 그 사이 거리 변화는 판의 길이와 각도만으로 흡수한다.
        float plateZ = Mathf.Min(0.048f,
            m4Body.localPosition.z - m4Body.localScale.z * 0.5f - 0.012f);
        Vector3 gearMiddle = (gearL.localPosition + gearR.localPosition) * 0.5f;
        Vector3 gearEnd = new Vector3(gearMiddle.x, gearMiddle.y, plateZ);
        Vector3 motorEnd = new Vector3(
            m4Body.localPosition.x, m4Body.localPosition.y, plateZ);
        Vector3 shoulderEnd = Vector3.Lerp(gearEnd, motorEnd, 0.48f);

        Transform spine = EnsurePrimitive(
            mechanism, "GripperM4BridgeSpine_Visual", PrimitiveType.Cube);
        PlaceBar(spine, gearEnd, motorEnd, 0.140f, 0.014f);
        SetMaterial(spine, matteBlack);

        Transform[] fingers = { fingerL, fingerR };
        for (int i = 0; i < fingers.Length; ++i)
        {
            string suffix = i == 0 ? "L" : "R";
            Vector3 contact = new Vector3(
                fingers[i].localPosition.x, fingers[i].localPosition.y, plateZ);
            Transform shoulder = EnsurePrimitive(mechanism,
                "GripperM4BridgeShoulder_" + suffix, PrimitiveType.Cube);
            PlaceBar(shoulder, contact, shoulderEnd, 0.090f, 0.014f);
            SetMaterial(shoulder, matteBlack);

            // 기존 집게 판과 만나는 두 접점의 작은 와셔/볼트.
            Transform washer = EnsurePrimitive(mechanism,
                "GripperM4BridgeWasher_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(washer, contact + Vector3.back * 0.009f,
                Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.020f, 0.003f));
            SetMaterial(washer, silver);
            Transform bolt = EnsurePrimitive(mechanism,
                "GripperM4BridgeBolt_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(bolt, contact + Vector3.back * 0.012f,
                Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.011f, 0.003f));
            SetMaterial(bolt, silver);
        }

        // 판 표면과 거의 같은 높이의 아주 얇은 암부: 돌출 캡이 아닌 hole 표현.
        Transform recess = EnsurePrimitive(mechanism,
            "GripperM4BridgeRecess_Visual", PrimitiveType.Cylinder);
        Vector3 holeCenter = Vector3.Lerp(gearEnd, motorEnd, 0.57f);
        PlaceLocal(recess, holeCenter + Vector3.back * 0.0074f,
            Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.030f, 0.0005f));
        SetMaterial(recess, recessBlack);
    }

    private static void BuildDetailedGripperHardware(
        Transform gripper, Material black, Material matteBlack,
        Material recessBlack, Material silver)
    {
        // 사진 1의 underside에서는 두 gear와 teeth가 실제로 노출된다. 기존 gear
        // pivot/solver는 그대로 사용하고 표시 크기만 사진 비율로 확장한다.
        foreach (Renderer renderer in gripper.GetComponentsInChildren<Renderer>(true))
        {
            string objectName = renderer.gameObject.name;
            if (IsMotorCasePart(renderer.transform))
            {
                SetMaterial(renderer.transform, black);
                continue;
            }
            if (objectName.StartsWith("GearDisc_", StringComparison.Ordinal))
            {
                Undo.RecordObject(renderer, "Show photographed gripper gear disc");
                Undo.RecordObject(renderer.transform, "Size photographed gripper gear disc");
                renderer.enabled = true;
                renderer.transform.localScale = new Vector3(0.180f, 0.022f, 0.180f);
                SetMaterial(renderer.transform, matteBlack);
                continue;
            }
            if (objectName.StartsWith("GearTooth_", StringComparison.Ordinal))
            {
                Undo.RecordObject(renderer, "Show photographed gripper gear teeth");
                Undo.RecordObject(renderer.transform, "Size photographed gripper gear teeth");
                renderer.enabled = true;
                Vector3 radial = renderer.transform.localPosition;
                radial.z = 0f;
                if (radial.sqrMagnitude > 0.000001f)
                    renderer.transform.localPosition = radial.normalized * 0.103f;
                renderer.transform.localScale = new Vector3(0.030f, 0.018f, 0.060f);
                SetMaterial(renderer.transform, matteBlack);
                continue;
            }

            bool silverPart = objectName.Contains("Bolt") ||
                              objectName.StartsWith("DrivePin", StringComparison.Ordinal);
            SetMaterial(renderer.transform, silverPart ? silver : matteBlack);
        }

        // Gear pivot의 자식이므로 회전할 때 구멍/중앙 나사도 gear와 함께 움직인다.
        // 얇은 검정 원반은 관통 가공을 표현하는 recessed inset이다.
        foreach (string suffix in new[] { "L", "R" })
        {
            Transform gear = FindDescendantByName(gripper, "Gear_" + suffix);
            if (gear == null) continue;
            Transform centerWasher = EnsurePrimitive(
                gear, "PhotoGearCenterWasher_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(centerWasher, new Vector3(0f, 0f, -0.019f),
                Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.023f, 0.004f));
            SetMaterial(centerWasher, silver);
            Transform centerScrew = EnsurePrimitive(
                gear, "PhotoGearCenterScrew_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(centerScrew, new Vector3(0f, 0f, -0.022f),
                Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.011f, 0.003f));
            SetMaterial(centerScrew, silver);

            for (int hole = 0; hole < 3; ++hole)
            {
                float angle = (hole * 120f + 30f) * Mathf.Deg2Rad;
                Transform inset = EnsurePrimitive(gear,
                    "PhotoGearRecess_" + suffix + "_" + hole.ToString("00"),
                    PrimitiveType.Cylinder);
                PlaceLocal(inset,
                    new Vector3(Mathf.Cos(angle) * 0.055f,
                        Mathf.Sin(angle) * 0.055f, -0.018f),
                    Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.009f, 0.002f));
                SetMaterial(inset, recessBlack);
            }
        }

        Transform cover = EnsurePrimitive(
            gripper, "GripperServoFrontCover_Visual", PrimitiveType.Cube);
        float gripperServoFrontZ =
            GripperServoCenterZ + GripperServoBodySize.z * 0.5f;
        PlaceLocal(cover,
            new Vector3(GripperServoCenterX, GripperServoCenterY,
                gripperServoFrontZ + GripperCoverDepth * 0.5f),
            new Vector3(
                GripperServoBodySize.x,
                GripperServoBodySize.y,
                GripperCoverDepth));
        SetMaterial(cover, black);

        // 세워진 M4 case의 위/아래 end case와 seam. 전체 치수는 같은 MG996R이고
        // local Z가 body 장축이 되도록 축만 바뀐다.
        foreach (int end in new[] { -1, 1 })
        {
            string suffix = end < 0 ? "Lower" : "Upper";
            Transform endCase = EnsurePrimitive(
                gripper, "GripperServoEndCase_" + suffix, PrimitiveType.Cube);
            PlaceLocal(endCase,
                new Vector3(GripperServoCenterX, GripperServoCenterY,
                    GripperServoCenterZ + end * GripperServoEndCaseOffset),
                GripperServoEndCaseSize);
            SetMaterial(endCase, black);

            Transform seam = EnsurePrimitive(
                gripper, "GripperServoCaseSeam_" + suffix, PrimitiveType.Cube);
            PlaceLocal(seam,
                new Vector3(GripperServoCenterX, GripperServoCenterY,
                    GripperServoCenterZ + end * GripperServoEndCaseOffset),
                new Vector3(
                    0.042f / GripperSourceScale,
                    0.022f / GripperSourceScale,
                    0.0015f / GripperSourceScale));
            SetMaterial(seam, black);
        }

        // 이전 눕힌 case 이름은 반복 Builder 실행 시 남지 않게 정리한다.
        DestroyGeneratedChild(gripper, "GripperServoEndCase_L");
        DestroyGeneratedChild(gripper, "GripperServoEndCase_R");
        DestroyGeneratedChild(gripper, "GripperServoCaseSeam_L");
        DestroyGeneratedChild(gripper, "GripperServoCaseSeam_R");

        // 실제 servo 전면의 네 모서리 체결 나사.
        int screwIndex = 0;
        foreach (float xOffset in new[] { -0.085f, 0.085f })
        foreach (float yOffset in new[] { -0.040f, 0.040f })
        {
            Transform screw = EnsurePrimitive(
                gripper, "GripperServoCoverBolt_" + screwIndex.ToString("00"),
                PrimitiveType.Cylinder);
            PlaceLocal(screw,
                new Vector3(GripperServoCenterX + xOffset,
                    GripperServoCenterY + yOffset,
                    gripperServoFrontZ + GripperCoverDepth * 1.5f),
                Quaternion.Euler(90f, 0f, 0f),
                CylinderScale(GripperBoltDiameter, 0.014f));
            SetMaterial(screw, silver);
            ++screwIndex;
        }

        // 사진의 주황 표시: M4 아래에 붙는 얇은 판과 작은 체결 볼트 두 개.
        // M4 case와 같은 기준에서 만들고, 저장된 Body 이동량을 아래에서 함께 적용한다.
        float m4PlateZ =
            GripperServoCenterZ - GripperServoBodySize.z * 0.5f - 0.012f;
        Transform m4UpperPlate = EnsurePrimitive(
            gripper, "GripperM4UpperPlate_Visual", PrimitiveType.Cube);
        PlaceLocal(m4UpperPlate,
            new Vector3(GripperServoCenterX, GripperServoCenterY, m4PlateZ),
            new Vector3(0.320f, 0.115f, 0.014f));
        SetMaterial(m4UpperPlate, matteBlack);
        for (int bolt = -1; bolt <= 1; bolt += 2)
        {
            string suffix = bolt < 0 ? "L" : "R";
            Transform washer = EnsurePrimitive(
                gripper, "GripperM4UpperWasher_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(washer,
                new Vector3(GripperServoCenterX + bolt * 0.110f,
                    GripperServoCenterY, m4PlateZ - 0.009f),
                Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.017f, 0.003f));
            SetMaterial(washer, silver);
            Transform plateBolt = EnsurePrimitive(
                gripper, "GripperM4UpperBolt_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(plateBolt,
                new Vector3(GripperServoCenterX + bolt * 0.110f,
                    GripperServoCenterY, m4PlateZ - 0.011f),
                Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.010f, 0.003f));
            SetMaterial(plateBolt, silver);
        }

        // Servo 양쪽 mounting ear와 중앙 linkage base plate.
        for (int side = -1; side <= 1; side += 2)
        {
            string suffix = side < 0 ? "L" : "R";
            Transform ear = EnsurePrimitive(
                gripper, "GripperServoMountEar_" + suffix, PrimitiveType.Cube);
            PlaceLocal(ear,
                new Vector3(
                    GripperServoCenterX + side * 0.150f,
                    GripperServoCenterY,
                    GripperServoCenterZ - GripperServoBodySize.z * 0.5f + 0.025f),
                new Vector3(0.060f, 0.180f, 0.025f));
            SetMaterial(ear, black);

            Transform sidePlate = EnsurePrimitive(
                gripper, "GripperLinkageSidePlate_" + suffix, PrimitiveType.Cube);
            PlaceBar(sidePlate,
                new Vector3(side * 0.285f, 0.205f, 0.010f),
                new Vector3(side * 0.105f, 0.080f, 0.010f),
                0.032f, 0.020f);
            SetMaterial(sidePlate, black);

            Transform baseBolt = EnsurePrimitive(
                gripper, "GripperSidePlateBolt_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(baseBolt, new Vector3(side * 0.285f, 0.070f, -0.038f),
                Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.018f, 0.012f));
            SetMaterial(baseBolt, silver);
        }

        // 중앙 carrier 위에 중복으로 얹혀 있던 두꺼운 사각 rail은 실물에 없다.
        DestroyGeneratedChild(gripper, "GripperUpperMountRail_Visual");

        // 사진의 gear housing은 단순 직사각형 한 장이 아니라 양쪽 jaw pivot까지
        // 뻗는 얇은 중앙 carrier와 겹쳐진다. 기존 dynamic gear 위가 아니라
        // back side에 두어 underside에서 두 gear가 계속 보이게 한다.
        Transform gearCarrier = EnsurePrimitive(
            gripper, "GripperGearCarrier_Visual", PrimitiveType.Cube);
        PlaceLocal(gearCarrier, new Vector3(0f, 0.065f, 0.058f),
            new Vector3(0.360f, 0.045f, 0.012f));
        SetMaterial(gearCarrier, black);

        Transform lowerGearCarrier = EnsurePrimitive(
            gripper, "GripperLowerGearCarrier_Visual", PrimitiveType.Cube);
        PlaceLocal(lowerGearCarrier, new Vector3(0f, 0.065f, -0.058f),
            new Vector3(0.360f, 0.045f, 0.012f));
        SetMaterial(lowerGearCarrier, black);

        foreach (int side in new[] { -1, 1 })
        {
            string suffix = side < 0 ? "L" : "R";
            Transform carrierArm = EnsurePrimitive(
                gripper, "GripperCarrierArm_" + suffix, PrimitiveType.Cube);
            PlaceBar(carrierArm,
                new Vector3(side * 0.285f, 0.070f, 0.058f),
                new Vector3(side * 0.090f, 0.105f, 0.058f),
                0.032f, 0.015f);
            SetMaterial(carrierArm, black);

            Transform lowerCarrierArm = EnsurePrimitive(
                gripper, "GripperLowerCarrierArm_" + suffix, PrimitiveType.Cube);
            PlaceBar(lowerCarrierArm,
                new Vector3(side * 0.285f, 0.070f, -0.058f),
                new Vector3(side * 0.090f, 0.105f, -0.058f),
                0.032f, 0.015f);
            SetMaterial(lowerCarrierArm, black);
        }

        Transform servoBoss = EnsurePrimitive(
            gripper, "GripperServoOutputBoss_Visual", PrimitiveType.Cylinder);
        PlaceLocal(servoBoss, new Vector3(GripperServoCenterX, 0.095f, 0.040f),
            Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.060f, 0.028f));
        SetMaterial(servoBoss, black);

        Transform servoShaft = EnsurePrimitive(
            gripper, "GripperServoOutputShaft_Visual", PrimitiveType.Cylinder);
        PlaceLocal(servoShaft, new Vector3(GripperServoCenterX, 0.095f, 0.010f),
            Quaternion.Euler(90f, 0f, 0f), CylinderScale(0.026f, 0.026f));
        SetMaterial(servoShaft, silver);

        // 기존 움직이는 jaw/rod는 그대로 사용하되 검은 판과 은색 joint가
        // 확실히 구분되도록 재질을 정리한다.
        foreach (string name in new[] {
            "Finger_L", "Finger_R", "TopLink_L", "TopLink_R",
            "GearBackPlate", "ServoNeck" })
        {
            Transform part = gripper.Find(name);
            if (part == null) continue;
            foreach (Renderer renderer in part.GetComponentsInChildren<Renderer>(true))
                SetMaterial(renderer.transform, black);
        }

        Transform gearBackPlate = gripper.Find("GearBackPlate");
        if (gearBackPlate != null)
            PlaceLocal(gearBackPlate, new Vector3(0f, 0.090f, 0.068f),
                new Vector3(0.320f, 0.080f, 0.012f));

        Transform servoNeck = gripper.Find("ServoNeck");
        if (servoNeck != null)
        {
            // 원본 Transform은 보존하되 중복된 박스형 neck mesh만 숨긴다.
            foreach (Renderer renderer in servoNeck.GetComponentsInChildren<Renderer>(true))
            {
                Undo.RecordObject(renderer, "Hide duplicate gripper neck box");
                renderer.enabled = false;
            }
        }

        foreach (string name in new[] { "Finger_L", "Finger_R" })
        {
            Transform jaw = FindDescendantByName(gripper, name);
            if (jaw == null) continue;
            string suffix = name.EndsWith("_L", StringComparison.Ordinal) ? "L" : "R";

            // 실제 사진은 JawLower/JawStem/JawHook의 꺾인 다단 평판 구조다.
            // 이전 단순 긴 cube 대체물은 제거하고 원래 linkage plate를 다시 표시한다.
            DestroyGeneratedChild(jaw, "PhotoFinger_Plate_" + suffix);
            DestroyGeneratedChild(jaw, "PhotoFinger_Tip_" + suffix);
            DestroyGeneratedChild(jaw, "FingerTipBolt_" + suffix);
            foreach (Renderer renderer in jaw.GetComponentsInChildren<Renderer>(true))
            {
                Undo.RecordObject(renderer, "Show photographed articulated finger plate");
                renderer.enabled = true;
                SetMaterial(renderer.transform,
                    renderer.gameObject.name.Contains("Bolt") ? silver : matteBlack);
            }

            float inward = suffix == "L" ? 1f : -1f;
            Vector3 lowerPivot = new Vector3(0f, 0f, 0f);
            // JawLinkAnchor는 solver 기준점이므로 첫 segment 끝은 기존 값과
            // 정확히 같은 위치를 유지한다.
            Vector3 linkPivot = new Vector3(inward * 0.060f, 0.150f, 0f);
            Vector3 stemEnd = new Vector3(inward * 0.118f, 0.430f, 0f);
            Vector3 hookBend = new Vector3(inward * 0.170f, 0.505f, 0f);
            // 저장된 finger tip 위치를 기준으로 외형 폭만 늘린다.
            Transform fingerPad = jaw.Find("FingerPad_" + suffix);
            Vector3 hookEnd = fingerPad != null
                ? fingerPad.localPosition
                : new Vector3(inward * 0.205f, 0.560f, 0f);

            Transform jawLower = jaw.Find("JawLower_" + suffix);
            Transform jawStem = jaw.Find("JawStem_" + suffix);
            Transform jawHook = jaw.Find("JawHook_" + suffix);
            if (jawLower != null)
                PlaceBar(jawLower,
                    lowerPivot + Vector3.back * 0.035f,
                    linkPivot + Vector3.back * 0.035f, 0.062f, 0.024f);
            if (jawStem != null)
                PlaceBar(jawStem,
                    linkPivot + Vector3.back * 0.035f,
                    stemEnd + Vector3.back * 0.035f, 0.050f, 0.024f);
            if (jawHook != null)
                PlaceBar(jawHook,
                    stemEnd + Vector3.back * 0.035f,
                    hookBend + Vector3.back * 0.035f, 0.042f, 0.024f);

            Transform hookTip = EnsurePrimitive(
                jaw, "PhotoJawHookTip_" + suffix, PrimitiveType.Cube);
            PlaceBar(hookTip,
                hookBend + Vector3.back * 0.035f,
                hookEnd + Vector3.back * 0.035f, 0.037f, 0.024f);
            SetMaterial(hookTip, matteBlack);

            // 실물의 판재 끝과 관절은 각진 cube 끝이 아니라 짧은 둥근 윤곽이다.
            // 모든 detail은 jaw pivot의 자식이라 기존 4-bar 동작을 그대로 따른다.
            Vector3[] roundedEnds = { linkPivot, stemEnd, hookBend, hookEnd };
            float[] endRadii = { 0.031f, 0.025f, 0.021f, 0.019f };
            for (int detail = 0; detail < roundedEnds.Length; ++detail)
            {
                Transform roundEnd = EnsurePrimitive(jaw,
                    "PhotoJawRoundEnd_" + suffix + "_" + detail.ToString("00"),
                    PrimitiveType.Cylinder);
                PlaceLocal(roundEnd, roundedEnds[detail] + Vector3.back * 0.035f,
                    Quaternion.Euler(90f, 0f, 0f),
                    CylinderScale(endRadii[detail], 0.024f));
                SetMaterial(roundEnd, matteBlack);
            }

            // 맞물리는 끝 안쪽의 작은 grip serration. 손가락 움직임/축은 불변.
            for (int tooth = 0; tooth < 3; ++tooth)
            {
                float t = 0.36f + tooth * 0.21f;
                Vector3 onTip = Vector3.Lerp(hookBend, hookEnd, t);
                Transform serration = EnsurePrimitive(jaw,
                    "PhotoJawGripTooth_" + suffix + "_" + tooth.ToString("00"),
                    PrimitiveType.Cube);
                PlaceLocal(serration,
                    onTip + new Vector3(inward * 0.013f, 0f, -0.035f),
                    new Vector3(0.016f, 0.014f, 0.024f));
                SetMaterial(serration, matteBlack);
            }

            // 측면 사진에서는 각 finger의 주판 한 장과 가는 4-bar rod가 분리돼
            // 보인다. 이전 중복 판은 제거하고 solver가 움직이는 본판을 사용한다.
            DestroyGeneratedChild(jaw, "PhotoJawLowerUpper_" + suffix);
            DestroyGeneratedChild(jaw, "PhotoJawStemUpper_" + suffix);
            DestroyGeneratedChild(jaw, "PhotoJawHookUpper_" + suffix);

            if (fingerPad != null)
            {
                Undo.RecordObject(fingerPad, "Thicken saved finger tip pad");
                fingerPad.localScale = new Vector3(0.035f, 0.026f, 0.016f);
            }

            Transform tipBolt = EnsurePrimitive(
                jaw, "PhotoFingerEndBolt_" + suffix, PrimitiveType.Cylinder);
            PlaceLocal(tipBolt, hookEnd,
                Quaternion.Euler(90f, 0f, 0f),
                CylinderScale(0.012f, 0.012f));
            SetMaterial(tipBolt, silver);
        }
    }

    private static float ProjectedHalfExtentInLocalDirection(
        Transform box, Transform referenceFrame, Vector3 localDirection)
    {
        Vector3 direction = localDirection.normalized;
        Vector3 halfX = referenceFrame.InverseTransformVector(
            box.TransformVector(Vector3.right * 0.5f));
        Vector3 halfY = referenceFrame.InverseTransformVector(
            box.TransformVector(Vector3.up * 0.5f));
        Vector3 halfZ = referenceFrame.InverseTransformVector(
            box.TransformVector(Vector3.forward * 0.5f));
        return Mathf.Abs(Vector3.Dot(direction, halfX)) +
               Mathf.Abs(Vector3.Dot(direction, halfY)) +
               Mathf.Abs(Vector3.Dot(direction, halfZ));
    }

    private static void PlaceBar(
        Transform bar, Vector3 start, Vector3 end, float width, float depth)
    {
        Vector3 delta = end - start;
        PlaceLocal(bar, (start + end) * 0.5f,
            Quaternion.FromToRotation(Vector3.up, delta.normalized),
            new Vector3(width, delta.magnitude, depth));
    }

    private static void DestroyGeneratedChild(Transform parent, string name)
    {
        Transform found = parent.Find(name);
        if (found != null)
            Undo.DestroyObjectImmediate(found.gameObject);
    }

    private static void SetChildActive(Transform parent, string name, bool active)
    {
        Transform found = parent.Find(name);
        if (found == null || found.gameObject.activeSelf == active) return;
        Undo.RecordObject(found.gameObject, "Set " + name + " active");
        found.gameObject.SetActive(active);
    }

    private static Transform EnsureEmpty(Transform parent, string name)
    {
        Transform found = parent.Find(name);
        if (found != null) return found;
        GameObject created = new GameObject(name);
        Undo.RegisterCreatedObjectUndo(created, "Create " + name);
        Undo.SetTransformParent(created.transform, parent, "Parent " + name);
        return created.transform;
    }

    private static Transform EnsurePrimitive(Transform parent, string name, PrimitiveType type)
    {
        Transform found = parent.Find(name);
        if (found != null) return found;
        GameObject created = GameObject.CreatePrimitive(type);
        created.name = name;
        Undo.RegisterCreatedObjectUndo(created, "Create " + name);
        Undo.SetTransformParent(created.transform, parent, "Parent " + name);
        Collider collider = created.GetComponent<Collider>();
        if (collider != null) Undo.DestroyObjectImmediate(collider);
        return created.transform;
    }

    private static void PlaceWorld(Transform visual, Vector3 position, Vector3 size)
    {
        Undo.RecordObject(visual, "Place " + visual.name);
        visual.position = position;
        visual.rotation = Quaternion.identity;
        visual.localScale = size;
    }

    private static void PlaceLocal(Transform visual, Vector3 position, Vector3 size)
    {
        PlaceLocal(visual, position, Quaternion.identity, size);
    }

    private static void PlaceLocal(
        Transform visual, Vector3 position, Quaternion rotation, Vector3 size)
    {
        Undo.RecordObject(visual, "Place " + visual.name);
        visual.localPosition = position;
        visual.localRotation = rotation;
        visual.localScale = size;
    }

    private static Vector3 CylinderScale(float radius, float height)
    {
        // Unity primitive cylinder: radius 0.5, total height 2.0.
        return new Vector3(radius * 2f, height * 0.5f, radius * 2f);
    }

    private static void SetMaterial(Transform visual, Material material)
    {
        Renderer renderer = visual.GetComponent<Renderer>();
        if (renderer == null) return;
        Undo.RecordObject(renderer, "Set " + visual.name + " material");
        renderer.sharedMaterial = material;
    }

    private static void ApplyStructuralMatte(
        Transform armRoot, Material motorBlack, Material matteBlack)
    {
        foreach (Renderer renderer in armRoot.GetComponentsInChildren<Renderer>(true))
        {
            if (renderer.sharedMaterial != motorBlack || IsMotorCasePart(renderer.transform))
                continue;
            SetMaterial(renderer.transform, matteBlack);
        }
    }

    private static bool IsMotorCasePart(Transform part)
    {
        for (Transform current = part; current != null; current = current.parent)
        {
            string name = current.name;
            if (name == "M0ServoVisual" || name == "ElbowPitchServoVisual" ||
                name == "WristPitchMG996R_Visual" ||
                name == "WristRollMG996R_Visual" ||
                name.StartsWith("ElbowPitchServo", StringComparison.Ordinal) ||
                name.StartsWith("GripperServo", StringComparison.Ordinal) ||
                name.StartsWith("ServoMountEar_", StringComparison.Ordinal))
                return true;
        }
        return false;
    }

    private static void Warn(string message)
    {
        Debug.LogWarning(message);
        EditorUtility.DisplayDialog("5-Axis Robot Visuals", message, "OK");
    }
}
#endif
