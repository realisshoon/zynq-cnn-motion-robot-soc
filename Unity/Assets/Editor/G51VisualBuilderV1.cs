
#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

public static class G51VisualBuilderV1
{
    private const string GeneratedPrefix = "__G51V1_";
    private const string ServoAssetName = "MG996R_Clean_Shoulder_XAxis";

    private static readonly string[] PlaceholderNames =
    {
        "BaseMesh",
        "BaseLink",
        "UpperArm",
        "ForeArm",
        "WristLink"
    };

    private sealed class JointInfo
    {
        public Transform pivot;
        public bool axisIsY;
        public JointInfo(Transform p, bool y) { pivot = p; axisIsY = y; }
    }

    [MenuItem("Tools/Human Motion/G51 Visual v1/Build or Rebuild")]
    public static void BuildOrRebuild()
    {
        if (EditorApplication.isPlaying)
        {
            EditorUtility.DisplayDialog("G51 Visual v1", "Play Mode를 종료한 뒤 실행하세요.", "OK");
            return;
        }

        GameObject robotArm = GameObject.Find("RobotArm");
        if (robotArm == null)
        {
            EditorUtility.DisplayDialog("G51 Visual v1", "Hierarchy에서 이름이 정확히 'RobotArm'인 오브젝트를 찾지 못했습니다.", "OK");
            return;
        }

        Transform root = robotArm.transform;
        Transform baseYaw = FindRecursive(root, "Base_Yaw");
        Transform shoulder = FindRecursive(root, "Shoulder_Pitch");
        Transform elbow = FindRecursive(root, "Elbow_Pitch");
        Transform wristPitch = FindRecursive(root, "Wrist_Pitch");
        Transform wristRoll = FindRecursive(root, "Wrist_Roll");

        if (baseYaw == null || shoulder == null || elbow == null || wristPitch == null || wristRoll == null)
        {
            EditorUtility.DisplayDialog(
                "G51 Visual v1",
                "필수 Pivot 이름을 찾지 못했습니다.\n\n필요:\nBase_Yaw\nShoulder_Pitch\nElbow_Pitch\nWrist_Pitch\nWrist_Roll",
                "OK"
            );
            return;
        }

        GameObject servoPrefab = FindServoAsset();
        if (servoPrefab == null)
        {
            EditorUtility.DisplayDialog(
                "G51 Visual v1",
                "MG996R_Clean_Shoulder_XAxis.obj를 찾지 못했습니다.\nAssets/Models/G51/MG996R/ 아래에 넣은 뒤 다시 실행하세요.",
                "OK"
            );
            return;
        }

        Undo.IncrementCurrentGroup();
        int undoGroup = Undo.GetCurrentGroup();
        Undo.SetCurrentGroupName("Build G51 Visual v1");

        RemoveGenerated(root);
        HidePlaceholders(root);

        Material black = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_BlackMetal.mat",
            new Color(0.035f, 0.04f, 0.045f, 1f),
            0.72f, 0.28f
        );

        Material dark = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_DarkServo.mat",
            new Color(0.055f, 0.055f, 0.065f, 1f),
            0.18f, 0.30f
        );

        Material silver = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_Silver.mat",
            new Color(0.55f, 0.57f, 0.60f, 1f),
            0.85f, 0.45f
        );

        // Base: visual only. The generated base is intentionally modest in size
        // so it does not swallow the real-size MG996R visual.
        CreateBaseVisual(baseYaw, black, silver);

        // Servo bodies are placed on the fixed side of each joint.
        PlaceServoAtJoint(servoPrefab, shoulder, false, dark);
        PlaceServoAtJoint(servoPrefab, elbow, false, dark);
        PlaceServoAtJoint(servoPrefab, wristPitch, false, dark);
        PlaceServoAtJoint(servoPrefab, wristRoll, true, dark);
        PlaceServoAtJoint(servoPrefab, baseYaw, true, dark);

        // Black U-style brackets around each pitch/roll joint.
        CreateUBracket(shoulder, black, silver, 0.060f, 0.050f, 0.030f);
        CreateUBracket(elbow, black, silver, 0.058f, 0.048f, 0.028f);
        CreateUBracket(wristPitch, black, silver, 0.054f, 0.044f, 0.026f);
        CreateUBracket(wristRoll, black, silver, 0.050f, 0.040f, 0.024f);

        // Twin side plates make the arm look like the real aluminium kit rather than
        // a single placeholder cube. They remain children of the moving pivot.
        CreateTwinLink(shoulder, elbow, black, 0.020f, 0.004f, 0.020f);
        CreateTwinLink(elbow, wristPitch, black, 0.018f, 0.004f, 0.018f);
        CreateTwinLink(wristPitch, wristRoll, black, 0.016f, 0.0035f, 0.016f);

        Selection.activeGameObject = robotArm;
        EditorUtility.SetDirty(robotArm);
        Undo.CollapseUndoOperations(undoGroup);

        SceneView.lastActiveSceneView?.FrameSelected();

        EditorUtility.DisplayDialog(
            "G51 Visual v1",
            "완료.\n\n- 기존 placeholder Mesh Renderer 자동 숨김\n- MG996R 5개 자동 배치\n- 검은 U-bracket 자동 생성\n- Twin arm plate 자동 생성\n- 기존 Pivot/Controller/Gripper는 유지\n\n원복은 Tools > Human Motion > G51 Visual v1 > Restore Placeholder Visuals",
            "OK"
        );
    }

    [MenuItem("Tools/Human Motion/G51 Visual v1/Restore Placeholder Visuals")]
    public static void Restore()
    {
        if (EditorApplication.isPlaying)
        {
            EditorUtility.DisplayDialog("G51 Visual v1", "Play Mode를 종료한 뒤 실행하세요.", "OK");
            return;
        }

        GameObject robotArm = GameObject.Find("RobotArm");
        if (robotArm == null)
        {
            EditorUtility.DisplayDialog("G51 Visual v1", "RobotArm을 찾지 못했습니다.", "OK");
            return;
        }

        Undo.IncrementCurrentGroup();
        int undoGroup = Undo.GetCurrentGroup();
        Undo.SetCurrentGroupName("Restore G51 Placeholder Visuals");

        RemoveGenerated(robotArm.transform);

        foreach (Renderer r in robotArm.GetComponentsInChildren<Renderer>(true))
        {
            for (int i = 0; i < PlaceholderNames.Length; i++)
            {
                if (r.gameObject.name == PlaceholderNames[i])
                {
                    Undo.RecordObject(r, "Enable placeholder renderer");
                    r.enabled = true;
                    break;
                }
            }
        }

        Undo.CollapseUndoOperations(undoGroup);
        EditorUtility.DisplayDialog("G51 Visual v1", "생성 Visual을 제거하고 기존 placeholder Renderer를 다시 켰습니다.", "OK");
    }

    private static GameObject FindServoAsset()
    {
        string[] guids = AssetDatabase.FindAssets(ServoAssetName + " t:Model");
        if (guids == null || guids.Length == 0)
            guids = AssetDatabase.FindAssets("MG996R t:Model");

        if (guids == null || guids.Length == 0)
            return null;

        string path = AssetDatabase.GUIDToAssetPath(guids[0]);
        return AssetDatabase.LoadAssetAtPath<GameObject>(path);
    }

    private static void HidePlaceholders(Transform root)
    {
        foreach (Renderer r in root.GetComponentsInChildren<Renderer>(true))
        {
            for (int i = 0; i < PlaceholderNames.Length; i++)
            {
                if (r.gameObject.name == PlaceholderNames[i])
                {
                    Undo.RecordObject(r, "Hide placeholder renderer");
                    r.enabled = false;
                    break;
                }
            }
        }
    }

    private static void RemoveGenerated(Transform root)
    {
        List<GameObject> doomed = new List<GameObject>();
        foreach (Transform t in root.GetComponentsInChildren<Transform>(true))
        {
            if (t != root && t.name.StartsWith(GeneratedPrefix, StringComparison.Ordinal))
                doomed.Add(t.gameObject);
        }

        // Destroy highest roots first; children will disappear with their parents.
        doomed.Sort((a, b) => GetDepth(a.transform).CompareTo(GetDepth(b.transform)));
        HashSet<Transform> deletedRoots = new HashSet<Transform>();

        foreach (GameObject go in doomed)
        {
            if (go == null) continue;
            bool ancestorAlreadyDeleted = false;
            Transform p = go.transform.parent;
            while (p != null)
            {
                if (deletedRoots.Contains(p))
                {
                    ancestorAlreadyDeleted = true;
                    break;
                }
                p = p.parent;
            }
            if (ancestorAlreadyDeleted) continue;

            deletedRoots.Add(go.transform);
            Undo.DestroyObjectImmediate(go);
        }
    }

    private static int GetDepth(Transform t)
    {
        int d = 0;
        while (t.parent != null) { d++; t = t.parent; }
        return d;
    }

    private static Transform FindRecursive(Transform parent, string name)
    {
        if (parent.name == name) return parent;
        foreach (Transform child in parent)
        {
            Transform found = FindRecursive(child, name);
            if (found != null) return found;
        }
        return null;
    }

    private static void PlaceServoAtJoint(GameObject servoPrefab, Transform jointPivot, bool axisY, Material fallbackMaterial)
    {
        Transform fixedParent = jointPivot.parent != null ? jointPivot.parent : jointPivot;

        GameObject instance = (GameObject)PrefabUtility.InstantiatePrefab(servoPrefab, fixedParent);
        if (instance == null)
            instance = UnityEngine.Object.Instantiate(servoPrefab, fixedParent);

        instance.name = GeneratedPrefix + "Servo_" + jointPivot.name;
        Undo.RegisterCreatedObjectUndo(instance, "Create MG996R visual");

        // The OBJ was authored with its shaft center at local origin and shaft axis +X.
        // Put that origin directly on the joint's world-space pivot.
        instance.transform.position = jointPivot.position;
        instance.transform.rotation = jointPivot.rotation * (axisY
            ? Quaternion.Euler(0f, 0f, 90f)   // X-axis model -> Y-axis joint
            : Quaternion.identity);
        instance.transform.localScale = Vector3.one;

        // The OBJ already carries materials. Fallback only if import produced none.
        foreach (Renderer r in instance.GetComponentsInChildren<Renderer>(true))
        {
            if (r.sharedMaterial == null)
                r.sharedMaterial = fallbackMaterial;
            r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.On;
            r.receiveShadows = true;
        }
    }

    private static void CreateBaseVisual(Transform baseYaw, Material black, Material silver)
    {
        Transform parent = baseYaw.parent != null ? baseYaw.parent : baseYaw;

        GameObject baseCylinder = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        baseCylinder.name = GeneratedPrefix + "Base";
        Undo.RegisterCreatedObjectUndo(baseCylinder, "Create G51 base");
        baseCylinder.transform.SetParent(parent, false);
        baseCylinder.transform.position = baseYaw.position + Vector3.down * 0.018f;
        baseCylinder.transform.localScale = new Vector3(0.075f, 0.018f, 0.075f);
        SetMaterial(baseCylinder, black);

        GameObject topDisc = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        topDisc.name = GeneratedPrefix + "BaseHub";
        Undo.RegisterCreatedObjectUndo(topDisc, "Create G51 base hub");
        topDisc.transform.SetParent(parent, false);
        topDisc.transform.position = baseYaw.position;
        topDisc.transform.localScale = new Vector3(0.038f, 0.006f, 0.038f);
        SetMaterial(topDisc, silver);
    }

    private static void CreateUBracket(
        Transform pivot,
        Material black,
        Material silver,
        float outerWidth,
        float height,
        float depth)
    {
        GameObject root = new GameObject(GeneratedPrefix + "Bracket_" + pivot.name);
        Undo.RegisterCreatedObjectUndo(root, "Create G51 bracket");
        root.transform.SetParent(pivot, false);
        root.transform.localPosition = Vector3.zero;
        root.transform.localRotation = Quaternion.identity;

        float t = 0.0035f;
        float sideX = outerWidth * 0.5f - t * 0.5f;

        GameObject left = CreateBox(root.transform, "Side_L",
            new Vector3(t, height, depth),
            new Vector3(-sideX, height * 0.25f, 0f),
            black);

        GameObject right = CreateBox(root.transform, "Side_R",
            new Vector3(t, height, depth),
            new Vector3(sideX, height * 0.25f, 0f),
            black);

        GameObject bridge = CreateBox(root.transform, "Bridge",
            new Vector3(outerWidth, t, depth),
            new Vector3(0f, height * 0.5f, 0f),
            black);

        // Small silver caps make the pivot visually readable.
        CreatePivotCap(root.transform, new Vector3(-outerWidth * 0.5f, 0f, 0f), silver);
        CreatePivotCap(root.transform, new Vector3( outerWidth * 0.5f, 0f, 0f), silver);
    }

    private static void CreateTwinLink(
        Transform fromPivot,
        Transform toPivot,
        Material material,
        float sideOffset,
        float thickness,
        float plateWidth)
    {
        Vector3 start = fromPivot.position;
        Vector3 end = toPivot.position;
        Vector3 dir = end - start;
        float len = dir.magnitude;
        if (len < 0.001f) return;

        Vector3 worldSide = fromPivot.TransformDirection(Vector3.forward).normalized;

        CreateWorldBar(
            fromPivot,
            GeneratedPrefix + "LinkA_" + fromPivot.name,
            start + worldSide * sideOffset,
            end + worldSide * sideOffset,
            plateWidth,
            thickness,
            material);

        CreateWorldBar(
            fromPivot,
            GeneratedPrefix + "LinkB_" + fromPivot.name,
            start - worldSide * sideOffset,
            end - worldSide * sideOffset,
            plateWidth,
            thickness,
            material);
    }

    private static void CreateWorldBar(
        Transform parent,
        string name,
        Vector3 a,
        Vector3 b,
        float width,
        float thickness,
        Material material)
    {
        Vector3 delta = b - a;
        float length = delta.magnitude;
        if (length < 0.001f) return;

        GameObject bar = GameObject.CreatePrimitive(PrimitiveType.Cube);
        bar.name = name;
        Undo.RegisterCreatedObjectUndo(bar, "Create G51 arm plate");
        bar.transform.SetParent(parent, true);

        bar.transform.position = (a + b) * 0.5f;
        bar.transform.rotation = Quaternion.FromToRotation(Vector3.up, delta.normalized);
        bar.transform.localScale = new Vector3(width, length, thickness);
        SetMaterial(bar, material);
    }

    private static void CreatePivotCap(Transform parent, Vector3 localPosition, Material material)
    {
        GameObject cap = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        cap.name = GeneratedPrefix + "PivotCap";
        Undo.RegisterCreatedObjectUndo(cap, "Create pivot cap");
        cap.transform.SetParent(parent, false);
        cap.transform.localPosition = localPosition;
        cap.transform.localRotation = Quaternion.Euler(0f, 0f, 90f);
        cap.transform.localScale = new Vector3(0.007f, 0.002f, 0.007f);
        SetMaterial(cap, material);
    }

    private static GameObject CreateBox(
        Transform parent,
        string name,
        Vector3 size,
        Vector3 localPosition,
        Material material)
    {
        GameObject box = GameObject.CreatePrimitive(PrimitiveType.Cube);
        box.name = GeneratedPrefix + name;
        Undo.RegisterCreatedObjectUndo(box, "Create G51 box");
        box.transform.SetParent(parent, false);
        box.transform.localPosition = localPosition;
        box.transform.localRotation = Quaternion.identity;
        box.transform.localScale = size;
        SetMaterial(box, material);
        return box;
    }

    private static void SetMaterial(GameObject go, Material mat)
    {
        Renderer r = go.GetComponent<Renderer>();
        if (r != null)
        {
            r.sharedMaterial = mat;
            r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.On;
            r.receiveShadows = true;
        }
    }

    private static Material GetOrCreateMaterial(
        string assetPath,
        Color baseColor,
        float metallic,
        float smoothness)
    {
        EnsureFolder("Assets/Materials");
        EnsureFolder("Assets/Materials/G51Generated");

        Material mat = AssetDatabase.LoadAssetAtPath<Material>(assetPath);
        if (mat == null)
        {
            Shader shader = Shader.Find("Universal Render Pipeline/Lit");
            if (shader == null) shader = Shader.Find("Standard");

            mat = new Material(shader);
            AssetDatabase.CreateAsset(mat, assetPath);
        }

        if (mat.HasProperty("_BaseColor")) mat.SetColor("_BaseColor", baseColor);
        if (mat.HasProperty("_Color")) mat.SetColor("_Color", baseColor);
        if (mat.HasProperty("_Metallic")) mat.SetFloat("_Metallic", metallic);
        if (mat.HasProperty("_Smoothness")) mat.SetFloat("_Smoothness", smoothness);
        if (mat.HasProperty("_Glossiness")) mat.SetFloat("_Glossiness", smoothness);

        EditorUtility.SetDirty(mat);
        AssetDatabase.SaveAssets();
        return mat;
    }

    private static void EnsureFolder(string path)
    {
        if (AssetDatabase.IsValidFolder(path)) return;

        string parent = System.IO.Path.GetDirectoryName(path)?.Replace("\\", "/");
        string name = System.IO.Path.GetFileName(path);

        if (!string.IsNullOrEmpty(parent) && !AssetDatabase.IsValidFolder(parent))
            EnsureFolder(parent);

        if (!string.IsNullOrEmpty(parent))
            AssetDatabase.CreateFolder(parent, name);
    }
}
#endif
