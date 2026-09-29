using System;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class RobotArmBuilder
{
    public const string MenuPath = "Tools/Human Motion/Build Robot Arm";

    [MenuItem(MenuPath)]
    public static void BuildRobotArm()
    {
        Undo.IncrementCurrentGroup();
        int group = Undo.GetCurrentGroup();
        Undo.SetCurrentGroupName("Build Robot Arm");
        try
        {
            var controller = BuildInScene(SceneManager.GetActiveScene(), true);
            Selection.activeGameObject = controller.gameObject;
            Undo.CollapseUndoOperations(group);
            SceneView.RepaintAll();
            Debug.Log("RobotArm 준비 완료: geometry/reference 갱신. 실제 G51 치수/축은 실측값으로 교체하세요.", controller);
        }
        catch (Exception exception)
        {
            Undo.RevertAllDownToGroup(group);
            Debug.LogException(exception);
        }
    }

    [MenuItem(MenuPath, true)]
    private static bool CanBuild() => !EditorApplication.isPlayingOrWillChangePlaymode &&
        !EditorApplication.isCompiling && PrefabStageUtility.GetCurrentPrefabStage() == null;

    [MenuItem("Tools/Human Motion/Apply Robot Geometry")]
    public static void ApplySelectedGeometry()
    {
        if (Selection.activeGameObject == null) return;
        var controller = Selection.activeGameObject.GetComponent<RobotArmController>();
        if (controller == null) controller = Selection.activeGameObject.GetComponentInParent<RobotArmController>();
        if (controller == null)
        {
            Debug.LogWarning("RobotArmController가 있는 RobotArm을 선택하세요.");
            return;
        }

        ApplyGeometry(controller, true);
        EditorSceneManager.MarkSceneDirty(controller.gameObject.scene);
        SceneView.RepaintAll();
        Debug.Log("Robot geometry 적용 완료.", controller);
    }

    [MenuItem("Tools/Human Motion/Apply Robot Geometry", true)]
    private static bool CanApplySelectedGeometry() =>
        CanBuild() && Selection.activeGameObject != null &&
        (Selection.activeGameObject.GetComponent<RobotArmController>() != null ||
         Selection.activeGameObject.GetComponentInParent<RobotArmController>() != null);

    [MenuItem("Tools/Human Motion/Repair Robot References")]
    public static void RepairSelectedReferences()
    {
        if (Selection.activeGameObject == null) return;
        var controller = Selection.activeGameObject.GetComponent<RobotArmController>();
        if (controller == null) controller = Selection.activeGameObject.GetComponentInParent<RobotArmController>();
        if (controller == null)
        {
            Debug.LogWarning("RobotArmController가 있는 RobotArm을 선택하세요.");
            return;
        }

        RepairReferences(controller, true);
        EditorUtility.SetDirty(controller);
        EditorSceneManager.MarkSceneDirty(controller.gameObject.scene);
        SceneView.RepaintAll();
        Debug.Log("RobotArm joint reference 복구 완료.", controller);
    }

    [MenuItem("Tools/Human Motion/Repair Robot References", true)]
    private static bool CanRepairSelectedReferences() =>
        CanBuild() && Selection.activeGameObject != null &&
        (Selection.activeGameObject.GetComponent<RobotArmController>() != null ||
         Selection.activeGameObject.GetComponentInParent<RobotArmController>() != null);

    [MenuItem("Tools/Human Motion/Rebuild G51 Gripper Visual")]
    public static void RebuildSelectedGripper()
    {
        if (Selection.activeGameObject == null) return;
        var controller = Selection.activeGameObject.GetComponent<RobotArmController>();
        if (controller == null) controller = Selection.activeGameObject.GetComponentInParent<RobotArmController>();
        if (controller == null)
        {
            Debug.LogWarning("RobotArmController가 있는 RobotArm을 선택하세요.");
            return;
        }

        RebuildGripperVisual(controller, true);
        EditorUtility.SetDirty(controller);
        EditorSceneManager.MarkSceneDirty(controller.gameObject.scene);
        SceneView.RepaintAll();
        Debug.Log("사진 기반 G51 linkage gripper visual 재생성 완료.", controller);
    }

    [MenuItem("Tools/Human Motion/Rebuild G51 Gripper Visual", true)]
    private static bool CanRebuildSelectedGripper() =>
        CanBuild() && Selection.activeGameObject != null &&
        (Selection.activeGameObject.GetComponent<RobotArmController>() != null ||
         Selection.activeGameObject.GetComponentInParent<RobotArmController>() != null);

    [MenuItem("Tools/Human Motion/Enable UDP Input")]
    public static void EnableUdpInput()
    {
        BuildRobotArm();
        var controller = Selection.activeGameObject != null ? Selection.activeGameObject.GetComponent<RobotArmController>() : null;
        if (controller == null) return;
        Undo.RecordObject(controller, "Enable UDP Input");
        controller.inputMode = RobotArmController.InputMode.UDP;
        EditorUtility.SetDirty(controller);
        EditorSceneManager.MarkSceneDirty(controller.gameObject.scene);
    }

    [MenuItem("Tools/Human Motion/Enable UDP Input", true)]
    private static bool CanEnableUdp() => CanBuild();

    private static void EnsureReceiver(RobotArmController controller, bool undo)
    {
        if (controller.TryGetComponent<UdpJointCommandReceiver>(out _)) return;
        var receiver = undo ? Undo.AddComponent<UdpJointCommandReceiver>(controller.gameObject) :
            controller.gameObject.AddComponent<UdpJointCommandReceiver>();
        receiver.controller = controller;
        EditorUtility.SetDirty(receiver);
        EditorSceneManager.MarkSceneDirty(controller.gameObject.scene);
    }

    public static RobotArmController BuildInScene(Scene scene, bool recordUndo)
    {
        if (!scene.IsValid() || !scene.isLoaded) throw new ArgumentException("열린 Scene이 필요합니다.");

        Transform root = null;
        foreach (var sceneRoot in scene.GetRootGameObjects())
        foreach (var candidate in sceneRoot.GetComponentsInChildren<Transform>(true))
        {
            if (candidate.name != "RobotArm" && candidate.GetComponent<RobotArmController>() == null) continue;
            if (root != null) throw new InvalidOperationException("RobotArm 후보가 여러 개입니다. 중복 생성하지 않습니다.");
            root = candidate;
        }

        if (root != null && root.TryGetComponent<RobotArmController>(out var existing))
        {
            EnsureReceiver(existing, recordUndo);
            RepairReferences(existing, recordUndo);
            if (existing.gripperVisual == null)
                RebuildGripperVisual(existing, recordUndo);
            ApplyGeometry(existing, recordUndo);
            return existing;
        }

        if (root == null)
        {
            var go = new GameObject("RobotArm");
            SceneManager.MoveGameObjectToScene(go, scene);
            root = go.transform;
            if (recordUndo) Undo.RegisterCreatedObjectUndo(go, "Create RobotArm");
        }

        // 최초 생성값은 현재 placeholder baseline. 생성 직후 ApplyGeometry()가 controller.geometry로 다시 맞춘다.
        Mesh(root, "BaseMesh", PrimitiveType.Cylinder, new Vector3(0, 0.25f, 0), new Vector3(1, 0.25f, 1), recordUndo);
        var yaw = Pivot(root, "Base_Yaw", new Vector3(0, 0.5f, 0), recordUndo);
        Mesh(yaw, "BaseLink", PrimitiveType.Cube, new Vector3(0, 0.1f, 0), new Vector3(0.5f, 0.2f, 0.4f), recordUndo);
        var shoulder = Pivot(yaw, "Shoulder_Pitch", new Vector3(0, 0.2f, 0), recordUndo);
        Mesh(shoulder, "UpperArm", PrimitiveType.Cube, new Vector3(0, 0.55f, 0), new Vector3(0.22f, 1.1f, 0.26f), recordUndo);
        var elbow = Pivot(shoulder, "Elbow_Pitch", new Vector3(0, 1.1f, 0), recordUndo);
        Mesh(elbow, "ForeArm", PrimitiveType.Cube, new Vector3(0, 0.45f, 0), new Vector3(0.18f, 0.9f, 0.22f), recordUndo);
        var pitch = Pivot(elbow, "Wrist_Pitch", new Vector3(0, 0.9f, 0), recordUndo);
        Mesh(pitch, "WristLink", PrimitiveType.Cylinder, new Vector3(0, 0.15f, 0), new Vector3(0.2f, 0.15f, 0.2f), recordUndo);
        var roll = Pivot(pitch, "Wrist_Roll", new Vector3(0, 0.3f, 0), recordUndo);
        Mesh(roll, "Hand", PrimitiveType.Cube, new Vector3(0, 0.12f, 0), new Vector3(0.48f, 0.24f, 0.2f), recordUndo);
        var gripper = Pivot(roll, "Gripper", new Vector3(0, 0.24f, 0), recordUndo);
        var left = Mesh(gripper, "Finger_L", PrimitiveType.Cube, new Vector3(-0.2f, 0.15f, 0), new Vector3(0.08f, 0.3f, 0.14f), recordUndo);
        var right = Mesh(gripper, "Finger_R", PrimitiveType.Cube, new Vector3(0.2f, 0.15f, 0), new Vector3(0.08f, 0.3f, 0.14f), recordUndo);

        var controller = recordUndo ? Undo.AddComponent<RobotArmController>(root.gameObject) :
            root.gameObject.AddComponent<RobotArmController>();

        controller.baseYaw.Bind(yaw);
        controller.shoulderPitch.Bind(shoulder);
        controller.elbowPitch.Bind(elbow);
        controller.wristPitch.Bind(pitch);
        controller.wristRoll.Bind(roll);
        controller.fingerLeft = left;
        controller.fingerRight = right;
        controller.fingerLeftClosed = new Vector3(-0.04f, left.localPosition.y, left.localPosition.z);
        controller.fingerRightClosed = new Vector3(0.04f, right.localPosition.y, right.localPosition.z);
        controller.testCommand = JointCommandData.Neutral;

        RebuildGripperVisual(controller, recordUndo);
        ApplyGeometry(controller, recordUndo);

        if (recordUndo)
            Undo.RecordObjects(new UnityEngine.Object[] { yaw, shoulder, elbow, pitch, roll, left, right }, "Initialize RobotArm Pose");

        controller.ApplyTestCommand();
        EnsureReceiver(controller, recordUndo);
        EditorUtility.SetDirty(controller);
        EditorSceneManager.MarkSceneDirty(scene);
        return controller;
    }

    /// <summary>
    /// 기존 RobotArm hierarchy의 Transform을 RobotArmController JointVisual에 다시 연결한다.
    /// 기존 restLocalRotation은 보존하여 현재 calibration baseline을 덮어쓰지 않는다.
    /// </summary>
    public static void RepairReferences(RobotArmController controller, bool recordUndo)
    {
        if (controller == null) throw new ArgumentNullException(nameof(controller));

        Transform root = controller.transform;
        Transform yaw = RequiredChild(root, "Base_Yaw");
        Transform shoulder = RequiredChild(yaw, "Shoulder_Pitch");
        Transform elbow = RequiredChild(shoulder, "Elbow_Pitch");
        Transform pitch = RequiredChild(elbow, "Wrist_Pitch");
        Transform roll = RequiredChild(pitch, "Wrist_Roll");
        Transform gripper = RequiredChild(roll, "Gripper");
        Transform left = Child(gripper, "Finger_L");
        Transform right = Child(gripper, "Finger_R");

        if (recordUndo) Undo.RecordObject(controller, "Repair RobotArm References");

        controller.baseYaw.pivot = yaw;
        controller.shoulderPitch.pivot = shoulder;
        controller.elbowPitch.pivot = elbow;
        controller.wristPitch.pivot = pitch;
        controller.wristRoll.pivot = roll;
        controller.fingerLeft = left;
        controller.fingerRight = right;
        controller.gripperVisual = gripper.GetComponent<G51GripperVisual>();

        EditorUtility.SetDirty(controller);
    }

    /// <summary>
    /// 사용자가 제공한 실물 gripper 사진 + RoboCon/G51 product reference를 기준으로
    /// gear/crank/link/jaw 구조를 시각화한다.
    /// 물리 치수는 아직 실측값이 아니며, 사진 비율 기반 VISUAL PLACEHOLDER다.
    /// </summary>
    public static void RebuildGripperVisual(RobotArmController controller, bool recordUndo)
    {
        if (controller == null) throw new ArgumentNullException(nameof(controller));

        Transform roll = RequiredChild(
            RequiredChild(
                RequiredChild(
                    RequiredChild(
                        RequiredChild(controller.transform, "Base_Yaw"),
                        "Shoulder_Pitch"),
                    "Elbow_Pitch"),
                "Wrist_Pitch"),
            "Wrist_Roll");
        Transform gripper = RequiredChild(roll, "Gripper");

        // Gripper 아래는 visual 전용이다. 이전 단순 finger / v3 placeholder를 모두 지우고 다시 만든다.
        for (int i = gripper.childCount - 1; i >= 0; --i)
        {
            GameObject child = gripper.GetChild(i).gameObject;
            if (recordUndo) Undo.DestroyObjectImmediate(child);
            else UnityEngine.Object.DestroyImmediate(child);
        }

        var oldVisual = gripper.GetComponent<G51GripperVisual>();
        if (oldVisual != null)
        {
            if (recordUndo) Undo.DestroyObjectImmediate(oldVisual);
            else UnityEngine.Object.DestroyImmediate(oldVisual);
        }

        // ------------------------------------------------------------------
        // 1) Servo + central plate
        // 실제 사진에서 gripper drive servo가 gear housing 바로 뒤/아래에 붙는다.
        // ------------------------------------------------------------------
        Mesh(gripper, "GripperServoBody", PrimitiveType.Cube,
             new Vector3(0f, -0.17f, 0.015f), new Vector3(0.28f, 0.30f, 0.18f), recordUndo);
        Mesh(gripper, "ServoNeck", PrimitiveType.Cube,
             new Vector3(0f, -0.015f, 0f), new Vector3(0.15f, 0.08f, 0.12f), recordUndo);
        Mesh(gripper, "GearBackPlate", PrimitiveType.Cube,
             new Vector3(0f, 0.09f, 0.025f), new Vector3(0.48f, 0.20f, 0.075f), recordUndo);

        Transform hub = Mesh(gripper, "DriveHub", PrimitiveType.Cylinder,
             new Vector3(0f, -0.005f, -0.015f), new Vector3(0.11f, 0.028f, 0.11f), recordUndo);
        hub.localRotation = Quaternion.Euler(90f, 0f, 0f);

        // ------------------------------------------------------------------
        // 2) Meshed gears + moving crank pins
        // v3에서는 inner anchor가 body에 고정돼 있었지만, 실물은 gear/crank와 함께 움직인다.
        // ------------------------------------------------------------------
        Transform leftGear = Pivot(gripper, "Gear_L", new Vector3(-0.095f, 0.095f, 0f), recordUndo);
        Transform rightGear = Pivot(gripper, "Gear_R", new Vector3(0.095f, 0.095f, 0f), recordUndo);
        CreateGearVisual(leftGear, "L", 0.095f, 14, recordUndo);
        CreateGearVisual(rightGear, "R", 0.095f, 14, recordUndo);

        Transform leftDrivePin = Pivot(leftGear, "DrivePin_L", new Vector3(0.055f, 0.140f, 0f), recordUndo);
        Transform rightDrivePin = Pivot(rightGear, "DrivePin_R", new Vector3(-0.055f, 0.140f, 0f), recordUndo);
        CreateBolt(leftDrivePin, "DrivePinBolt_L", 0.024f, 0.060f, recordUndo);
        CreateBolt(rightDrivePin, "DrivePinBolt_R", 0.024f, 0.060f, recordUndo);

        // ------------------------------------------------------------------
        // 3) Outer jaw pivots. The real fingers rotate about the lower outside pivots.
        // ------------------------------------------------------------------
        Transform leftJaw = Pivot(gripper, "Finger_L", new Vector3(-0.290f, 0.075f, 0f), recordUndo);
        Transform rightJaw = Pivot(gripper, "Finger_R", new Vector3(0.290f, 0.075f, 0f), recordUndo);

        // Photo proportions: lower pivot -> link joint -> long finger -> small inward hook.
        Vector3 l0 = Vector3.zero;
        Vector3 l1 = new Vector3(0.060f, 0.150f, 0f);
        Vector3 l2 = new Vector3(0.075f, 0.370f, 0f);
        Vector3 l3 = new Vector3(0.100f, 0.445f, 0f);
        Vector3 r0 = Vector3.zero;
        Vector3 r1 = new Vector3(-0.060f, 0.150f, 0f);
        Vector3 r2 = new Vector3(-0.075f, 0.370f, 0f);
        Vector3 r3 = new Vector3(-0.100f, 0.445f, 0f);

        CreateBarLocal(leftJaw, "JawLower_L", l0, l1, 0.085f, 0.070f, recordUndo);
        CreateBarLocal(leftJaw, "JawStem_L", l1, l2, 0.075f, 0.070f, recordUndo);
        CreateBarLocal(leftJaw, "JawHook_L", l2, l3, 0.080f, 0.075f, recordUndo);
        CreateBarLocal(rightJaw, "JawLower_R", r0, r1, 0.085f, 0.070f, recordUndo);
        CreateBarLocal(rightJaw, "JawStem_R", r1, r2, 0.075f, 0.070f, recordUndo);
        CreateBarLocal(rightJaw, "JawHook_R", r2, r3, 0.080f, 0.075f, recordUndo);

        // Rounded-ish contact tips. Exact rubber cap dimensions are not assumed here.
        Transform leftPad = Mesh(leftJaw, "FingerPad_L", PrimitiveType.Sphere,
             l3, new Vector3(0.085f, 0.070f, 0.080f), recordUndo);
        Transform rightPad = Mesh(rightJaw, "FingerPad_R", PrimitiveType.Sphere,
             r3, new Vector3(0.085f, 0.070f, 0.080f), recordUndo);

        Transform leftJawAnchor = Pivot(leftJaw, "JawLinkAnchor_L", l1, recordUndo);
        Transform rightJawAnchor = Pivot(rightJaw, "JawLinkAnchor_R", r1, recordUndo);

        CreateBolt(leftJaw, "BaseBolt_L", 0.027f, 0.075f, recordUndo);
        CreateBolt(rightJaw, "BaseBolt_R", 0.027f, 0.075f, recordUndo);
        CreateBolt(leftJawAnchor, "JawLinkBolt_L", 0.024f, 0.075f, recordUndo);
        CreateBolt(rightJawAnchor, "JawLinkBolt_R", 0.024f, 0.075f, recordUndo);

        // ------------------------------------------------------------------
        // 4) Dynamic top connecting rods.
        // Their endpoints are gear crank pins and the jaw middle pivots.
        // ------------------------------------------------------------------
        Transform leftBar = Mesh(gripper, "TopLink_L", PrimitiveType.Cube,
             Vector3.zero, new Vector3(0.045f, 0.20f, 0.055f), recordUndo);
        Transform rightBar = Mesh(gripper, "TopLink_R", PrimitiveType.Cube,
             Vector3.zero, new Vector3(0.045f, 0.20f, 0.055f), recordUndo);

        var visual = recordUndo ? Undo.AddComponent<G51GripperVisual>(gripper.gameObject)
                                : gripper.gameObject.AddComponent<G51GripperVisual>();
        visual.leftJawPivot = leftJaw;
        visual.rightJawPivot = rightJaw;
        visual.leftJawLinkAnchor = leftJawAnchor;
        visual.rightJawLinkAnchor = rightJawAnchor;
        visual.leftGear = leftGear;
        visual.rightGear = rightGear;
        visual.leftDrivePin = leftDrivePin;
        visual.rightDrivePin = rightDrivePin;
        visual.leftLinkBar = leftBar;
        visual.rightLinkBar = rightBar;

        // These are deliberately labeled visual placeholders. Tune after comparing with the actual hardware/video.
        visual.closedDriveDeg = -28f;
        visual.openDriveDeg = 12f;
        visual.linkThickness = 0.045f;
        visual.linkDepth = 0.055f;
        visual.CaptureRest();

        controller.gripperVisual = visual;
        controller.fingerLeft = leftJaw;   // legacy debug/fallback reference
        controller.fingerRight = rightJaw;

        // Preview the current test command immediately.
        visual.Apply(controller.testCommand.gripper_norm);

        EditorUtility.SetDirty(visual);
        EditorUtility.SetDirty(controller);
        EditorSceneManager.MarkSceneDirty(controller.gameObject.scene);
    }

    private static Transform CreateBarLocal(
        Transform parent,
        string name,
        Vector3 a,
        Vector3 b,
        float width,
        float depth,
        bool undo)
    {
        Vector3 delta = b - a;
        float length = delta.magnitude;
        Transform bar = Mesh(parent, name, PrimitiveType.Cube,
            (a + b) * 0.5f,
            new Vector3(width, Mathf.Max(0.001f, length), depth),
            undo);
        if (length > 0.0001f)
            bar.localRotation = Quaternion.FromToRotation(Vector3.up, delta.normalized);
        return bar;
    }

    private static Transform CreateBolt(
        Transform parent,
        string name,
        float diameter,
        float depth,
        bool undo)
    {
        Transform bolt = Mesh(parent, name, PrimitiveType.Cylinder,
            Vector3.zero,
            new Vector3(diameter, depth * 0.5f, diameter),
            undo);
        bolt.localRotation = Quaternion.Euler(90f, 0f, 0f);
        return bolt;
    }

    private static void CreateGearVisual(
        Transform gearPivot,
        string suffix,
        float diameter,
        int toothCount,
        bool undo)
    {
        Transform disk = Mesh(gearPivot, "GearDisc_" + suffix, PrimitiveType.Cylinder,
            Vector3.zero,
            new Vector3(diameter, 0.025f, diameter),
            undo);
        disk.localRotation = Quaternion.Euler(90f, 0f, 0f);

        float radius = diameter * 0.53f;
        for (int i = 0; i < toothCount; ++i)
        {
            float deg = 360f * i / toothCount;
            float rad = deg * Mathf.Deg2Rad;
            Transform tooth = Mesh(gearPivot, "GearTooth_" + suffix + "_" + i,
                PrimitiveType.Cube,
                new Vector3(Mathf.Cos(rad) * radius, Mathf.Sin(rad) * radius, 0f),
                new Vector3(0.030f, 0.020f, 0.060f),
                undo);
            tooth.localRotation = Quaternion.Euler(0f, 0f, deg - 90f);
        }

        CreateBolt(gearPivot, "GearCenterBolt_" + suffix, 0.028f, 0.075f, undo);
    }

    /// <summary>
    /// Controller.geometry를 현재 hierarchy에 적용한다.
    /// 회전축의 restLocalRotation/JointCommand/UDP 상태는 건드리지 않는다.
    /// </summary>
    public static void ApplyGeometry(RobotArmController controller, bool recordUndo)
    {
        if (controller == null) throw new ArgumentNullException(nameof(controller));

        Transform root = controller.transform;
        Transform baseMesh = RequiredChild(root, "BaseMesh");
        Transform yaw = RequiredChild(root, "Base_Yaw");
        Transform baseLink = RequiredChild(yaw, "BaseLink");
        Transform shoulder = RequiredChild(yaw, "Shoulder_Pitch");
        Transform upperArm = RequiredChild(shoulder, "UpperArm");
        Transform elbow = RequiredChild(shoulder, "Elbow_Pitch");
        Transform foreArm = RequiredChild(elbow, "ForeArm");
        Transform pitch = RequiredChild(elbow, "Wrist_Pitch");
        Transform wristLink = RequiredChild(pitch, "WristLink");
        Transform roll = RequiredChild(pitch, "Wrist_Roll");

        var g = controller.geometry;
        float baseHeight = Mathf.Max(0.01f, g.baseHeight);
        float shoulderOffset = Mathf.Max(0.0f, g.shoulderOffset);
        float upper = Mathf.Max(0.01f, g.upperArmLength);
        float fore = Mathf.Max(0.01f, g.forearmLength);
        float wrist = Mathf.Max(0.01f, g.wristLength);

        if (recordUndo)
        {
            Undo.RecordObjects(new UnityEngine.Object[] {
                controller, baseMesh, yaw, baseLink, shoulder, upperArm,
                elbow, foreArm, pitch, wristLink, roll
            }, "Apply Robot Geometry");
        }

        // Unity Cylinder 기본 높이는 2이므로 실제 높이 H를 만들 때 scale.y = H / 2.
        baseMesh.localPosition = new Vector3(0f, baseHeight * 0.5f, 0f);
        baseMesh.localScale = new Vector3(1f, baseHeight * 0.5f, 1f);

        yaw.localPosition = new Vector3(0f, baseHeight, 0f);

        // Base_Yaw -> Shoulder_Pitch
        baseLink.localPosition = new Vector3(0f, shoulderOffset * 0.5f, 0f);
        baseLink.localScale = new Vector3(0.5f, Mathf.Max(0.02f, shoulderOffset), 0.4f);
        shoulder.localPosition = new Vector3(0f, shoulderOffset, 0f);

        // Shoulder -> Elbow
        upperArm.localPosition = new Vector3(0f, upper * 0.5f, 0f);
        upperArm.localScale = new Vector3(0.22f, upper, 0.26f);
        elbow.localPosition = new Vector3(0f, upper, 0f);

        // Elbow -> Wrist Pitch
        foreArm.localPosition = new Vector3(0f, fore * 0.5f, 0f);
        foreArm.localScale = new Vector3(0.18f, fore, 0.22f);
        pitch.localPosition = new Vector3(0f, fore, 0f);

        // Wrist Pitch -> Wrist Roll
        wristLink.localPosition = new Vector3(0f, wrist * 0.5f, 0f);
        wristLink.localScale = new Vector3(0.2f, wrist * 0.5f, 0.2f);
        roll.localPosition = new Vector3(0f, wrist, 0f);

        EditorUtility.SetDirty(controller);
        EditorSceneManager.MarkSceneDirty(controller.gameObject.scene);
    }

    private static Transform RequiredChild(Transform parent, string name)
    {
        var child = Child(parent, name);
        if (child == null)
            throw new InvalidOperationException($"필수 RobotArm 노드가 없습니다: {parent.name}/{name}");
        return child;
    }

    private static Transform Child(Transform parent, string name)
    {
        Transform result = null;
        foreach (Transform child in parent)
        {
            if (child.name != name) continue;
            if (result != null) throw new InvalidOperationException("같은 이름의 자식이 여러 개입니다: " + name);
            result = child;
        }
        return result;
    }

    private static Transform Pivot(Transform parent, string name, Vector3 position, bool undo)
    {
        var existing = Child(parent, name);
        if (existing != null)
        {
            if (existing.GetComponent<Renderer>() != null || existing.GetComponent<MeshFilter>() != null)
                throw new InvalidOperationException(name + "은 mesh가 없는 Pivot이어야 합니다. 기존 객체는 보존합니다.");
            return existing;
        }

        var go = new GameObject(name);
        go.transform.SetParent(parent, false);
        go.transform.localPosition = position;
        if (undo) Undo.RegisterCreatedObjectUndo(go, "Create " + name);
        return go.transform;
    }

    private static Transform Mesh(Transform parent, string name, PrimitiveType type, Vector3 position, Vector3 scale, bool undo)
    {
        var existing = Child(parent, name);
        if (existing != null) return existing;

        var go = GameObject.CreatePrimitive(type);
        go.name = name;
        go.transform.SetParent(parent, false);
        go.transform.localPosition = position;
        go.transform.localScale = scale;
        UnityEngine.Object.DestroyImmediate(go.GetComponent<Collider>());
        if (undo) Undo.RegisterCreatedObjectUndo(go, "Create " + name);
        return go.transform;
    }
}

[CustomEditor(typeof(RobotArmController))]
public sealed class RobotArmControllerEditor : Editor
{
    public override void OnInspectorGUI()
    {
        EditorGUILayout.HelpBox(
            "JointCommand는 실제/Unity 로봇의 자세를 동기화합니다. " +
            "Geometry는 G51 실제 pivot-to-pivot 실측값으로 교체하세요. " +
            "사람 팔 길이를 넣는 항목이 아닙니다.",
            MessageType.Info);

        DrawDefaultInspector();

        var controller = (RobotArmController)target;

        EditorGUILayout.Space();
        if (GUILayout.Button("Apply Geometry To RobotArm"))
        {
            RobotArmBuilder.ApplyGeometry(controller, true);
            SceneView.RepaintAll();
        }

        if (GUILayout.Button("Rebuild G51 Gripper Visual"))
        {
            RobotArmBuilder.RebuildGripperVisual(controller, true);
            SceneView.RepaintAll();
        }

        // Manual mode preview만 갱신된다. UDP mode에서는 testCommand가 runtime 자세를 덮어쓰지 않는다.
        if (controller.inputMode == RobotArmController.InputMode.Manual)
        {
            Undo.RecordObjects(controller.GetComponentsInChildren<Transform>(true), "Preview JointCommand");
            controller.ApplyTestCommand();
            if (!Application.isPlaying)
                EditorSceneManager.MarkSceneDirty(controller.gameObject.scene);
            SceneView.RepaintAll();
        }
    }
}
