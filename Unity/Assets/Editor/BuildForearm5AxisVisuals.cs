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
    private const string SilverPath = "Assets/Materials/MAT_MetalSilver.mat";
    private const float PlateThickness = 0.015f;

    private struct ArmParts
    {
        public Transform root;
        public Transform elbowRoll;
        public Transform elbowPitch;
        public Transform wristRoll;
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
        if (tableRenderer == null ||
            !TryGetArm(system, "RobotArm_L", out left) ||
            !TryGetArm(system, "RobotArm_R", out right) ||
            !ReceiverMatches(system, left.root, right.root))
        {
            Warn("Demo 02의 Table 또는 검증된 5축 control hierarchy가 일치하지 않습니다. 변경하지 않았습니다.");
            return;
        }

        Material black = GetOrCreateMaterial(BlackPath, new Color(0.035f, 0.038f, 0.042f), 0.12f, 0.3f);
        Material silver = GetOrCreateMaterial(SilverPath, new Color(0.72f, 0.75f, 0.78f), 0.8f, 0.65f);
        if (black == null || silver == null)
        {
            Warn("Robot visual Material을 준비할 수 없습니다. 변경하지 않았습니다.");
            return;
        }

        float tableTopY = tableRenderer.bounds.max.y;
        int undoGroup = Undo.GetCurrentGroup();
        Undo.SetCurrentGroupName("Build 5-Axis Robot Visuals");
        BuildArm(left, tableTopY, black, silver);
        BuildArm(right, tableTopY, black, silver);
        Undo.CollapseUndoOperations(undoGroup);
        EditorSceneManager.MarkSceneDirty(scene);
        Debug.Log("Demo 02 5-axis visual 생성/갱신 완료: RobotArm_L, RobotArm_R. Scene은 저장하지 않았습니다.");
    }

    private static Transform FindRoot(Scene scene, string name)
    {
        foreach (GameObject root in scene.GetRootGameObjects())
            if (root.name == name) return root.transform;
        return null;
    }

    private static bool TryGetArm(Transform system, string name, out ArmParts arm)
    {
        arm = new ArmParts();
        if (system == null) return false;
        arm.root = system.Find(name);
        if (arm.root == null) return false;
        arm.elbowRoll = arm.root.Find("ElbowRoll");
        arm.elbowPitch = arm.elbowRoll != null ? arm.elbowRoll.Find("ElbowPitch") : null;
        arm.wristRoll = arm.elbowPitch != null ? arm.elbowPitch.Find("WristRoll") : null;
        Transform wristPitch = arm.wristRoll != null ? arm.wristRoll.Find("WristPitch") : null;
        Transform toolMount = wristPitch != null ? wristPitch.Find("ToolMount") : null;
        Transform gripper = toolMount != null ? toolMount.Find("Gripper") : null;
        ForearmArmController control = arm.root.GetComponent<ForearmArmController>();
        if (control == null || !control.enabled ||
            control.elbowRoll != arm.elbowRoll ||
            control.elbowPitch != arm.elbowPitch ||
            control.wristRoll != arm.wristRoll ||
            control.wristPitch != wristPitch ||
            gripper == null || control.gripperVisual != gripper.GetComponent<G51GripperVisual>())
            return false;

        Vector3 wrist = arm.elbowPitch.InverseTransformPoint(arm.wristRoll.position);
        return wrist.y > 0.01f &&
               Mathf.Abs(wrist.x) < 0.01f && Mathf.Abs(wrist.z) < 0.01f;
    }

    private static bool ReceiverMatches(Transform system, Transform left, Transform right)
    {
        DualUdpJointCommandReceiver[] receivers =
            system.GetComponentsInChildren<DualUdpJointCommandReceiver>(true);
        return receivers.Length == 1 && receivers[0].enabled &&
               receivers[0].port == 5005 &&
               receivers[0].leftArm == left.GetComponent<ForearmArmController>() &&
               receivers[0].rightArm == right.GetComponent<ForearmArmController>();
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

    private static void BuildArm(ArmParts arm, float tableTopY, Material black, Material silver)
    {
        Vector3 elbow = arm.elbowRoll.position;
        Vector3 plateCenter = new Vector3(elbow.x, tableTopY + PlateThickness * 0.5f, elbow.z);
        Transform plate = EnsurePrimitive(arm.root, "RobotBasePlate_Visual", PrimitiveType.Cube);
        PlaceWorld(plate, plateCenter, new Vector3(0.24f, PlateThickness, 0.20f));
        SetMaterial(plate, black);

        Transform mount = EnsureEmpty(arm.root, "ElbowVisualMount");
        Undo.RecordObject(mount, "Place elbow visual mount");
        mount.position = elbow;
        mount.rotation = Quaternion.identity;
        mount.localScale = Vector3.one;

        float plateTop = tableTopY + PlateThickness;
        float bracketHeight = Mathf.Max(0.045f, elbow.y - plateTop + 0.015f);
        float bracketCenterY = plateTop + bracketHeight * 0.5f - elbow.y;
        Transform servo = EnsurePrimitive(mount, "BaseServo_Visual", PrimitiveType.Cube);
        PlaceLocal(servo, new Vector3(0f, bracketCenterY, 0f),
            new Vector3(0.065f, Mathf.Max(0.03f, bracketHeight - 0.008f), 0.075f));
        SetMaterial(servo, black);
        Transform bracketL = EnsurePrimitive(mount, "ElbowBracket_L_Visual", PrimitiveType.Cube);
        PlaceLocal(bracketL, new Vector3(-0.047f, bracketCenterY, 0f),
            new Vector3(0.012f, bracketHeight, 0.085f));
        SetMaterial(bracketL, black);
        Transform bracketR = EnsurePrimitive(mount, "ElbowBracket_R_Visual", PrimitiveType.Cube);
        PlaceLocal(bracketR, new Vector3(0.047f, bracketCenterY, 0f),
            new Vector3(0.012f, bracketHeight, 0.085f));
        SetMaterial(bracketR, black);

        Vector3 wristLocal = arm.elbowPitch.InverseTransformPoint(arm.wristRoll.position);
        Transform shaft = EnsurePrimitive(arm.elbowPitch, "ForearmShaft_Visual", PrimitiveType.Cylinder);
        Undo.RecordObject(shaft, "Place forearm shaft");
        shaft.localPosition = wristLocal * 0.5f;
        shaft.localRotation = Quaternion.FromToRotation(Vector3.up, wristLocal.normalized);
        shaft.localScale = new Vector3(0.018f, wristLocal.magnitude * 0.5f, 0.018f);
        SetMaterial(shaft, silver);

        // 새 은색 shaft가 기존 검은 링크에 가려지지 않게 시각 자식만 끈다.
        Transform oldForearm = arm.elbowPitch.Find("ForeArm");
        if (oldForearm != null && oldForearm.childCount == 0 && oldForearm.gameObject.activeSelf)
        {
            Undo.RecordObject(oldForearm.gameObject, "Hide legacy forearm visual");
            oldForearm.gameObject.SetActive(false);
        }
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
        Undo.RecordObject(visual, "Place " + visual.name);
        visual.localPosition = position;
        visual.localRotation = Quaternion.identity;
        visual.localScale = size;
    }

    private static void SetMaterial(Transform visual, Material material)
    {
        Renderer renderer = visual.GetComponent<Renderer>();
        if (renderer == null) return;
        Undo.RecordObject(renderer, "Set " + visual.name + " material");
        renderer.sharedMaterial = material;
    }

    private static void Warn(string message)
    {
        Debug.LogWarning(message);
        EditorUtility.DisplayDialog("5-Axis Robot Visuals", message, "OK");
    }
}
#endif
