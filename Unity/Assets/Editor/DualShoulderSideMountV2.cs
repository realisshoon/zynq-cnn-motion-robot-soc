
#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

public static class DualShoulderSideMountV2
{
    private const string Prefix = "__DualShoulderSideV2_";

    [MenuItem("Tools/Human Motion/Dual Robot/Shoulder Side Mount v2")]
    public static void Apply()
    {
        if (EditorApplication.isPlaying)
        {
            EditorUtility.DisplayDialog("Shoulder Side Mount v2", "Play Mode를 종료한 뒤 실행하세요.", "OK");
            return;
        }

        GameObject left = GameObject.Find("RobotArm_L");
        GameObject right = GameObject.Find("RobotArm_R");
        GameObject frame = GameObject.Find("CenterFrame");

        if (left == null || right == null || frame == null)
        {
            EditorUtility.DisplayDialog("Shoulder Side Mount v2",
                "RobotArm_L / RobotArm_R / CenterFrame을 모두 찾을 수 있어야 합니다.", "OK");
            return;
        }

        Transform ls = FindRecursive(left.transform, "Shoulder_Pitch");
        Transform rs = FindRecursive(right.transform, "Shoulder_Pitch");
        if (ls == null || rs == null)
        {
            EditorUtility.DisplayDialog("Shoulder Side Mount v2",
                "Shoulder_Pitch를 찾지 못했습니다.", "OK");
            return;
        }

        Renderer beam = FindBeam(frame.transform);
        if (beam == null)
        {
            EditorUtility.DisplayDialog("Shoulder Side Mount v2",
                "CenterFrame의 HorizontalBeam renderer를 찾지 못했습니다.", "OK");
            return;
        }

        Undo.IncrementCurrentGroup();
        int group = Undo.GetCurrentGroup();
        Undo.SetCurrentGroupName("Dual Shoulder Side Mount v2");

        RemoveGenerated(frame.transform);

        // 1) 어깨 축 자체를 beam 좌/우 SIDE FACE 중심에 맞춘다.
        //    회전 / joint 값 / scale은 절대 건드리지 않는다.
        Bounds bb = beam.bounds;
        Vector3 leftTarget  = new Vector3(bb.min.x, bb.center.y, bb.center.z);
        Vector3 rightTarget = new Vector3(bb.max.x, bb.center.y, bb.center.z);

        Undo.RecordObject(left.transform, "Align left shoulder pivot");
        Undo.RecordObject(right.transform, "Align right shoulder pivot");
        left.transform.position  += leftTarget  - ls.position;
        right.transform.position += rightTarget - rs.position;

        // 이동 후 shoulder world position 갱신
        ls = FindRecursive(left.transform, "Shoulder_Pitch");
        rs = FindRecursive(right.transform, "Shoulder_Pitch");

        // 2) 바닥형 base visual만 숨긴다.
        //    Base_Yaw transform 자체는 컨트롤 체인 때문에 유지한다.
        HideBaseVisualOnly(left.transform);
        HideBaseVisualOnly(right.transform);

        Material dark = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/DualShoulderMount_Dark.mat",
            new Color(0.055f, 0.060f, 0.070f, 1f), 0.75f, 0.34f);

        Material silver = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/DualShoulderMount_Silver.mat",
            new Color(0.53f, 0.56f, 0.61f, 1f), 0.90f, 0.48f);

        // 3) beam 끝에 붙는 shoulder housing을 별도 visual로 생성.
        CreateSideHousing(frame.transform, "Left", ls.position, dark, silver);
        CreateSideHousing(frame.transform, "Right", rs.position, dark, silver);

        EditorUtility.SetDirty(left);
        EditorUtility.SetDirty(right);
        EditorUtility.SetDirty(frame);
        Undo.CollapseUndoOperations(group);

        Selection.activeGameObject = frame;

        EditorUtility.DisplayDialog(
            "Shoulder Side Mount v2",
            "완료.\n\n" +
            "이번 버전은:\n" +
            "• Shoulder_Pitch 축 중심을 beam 좌/우 SIDE FACE에 맞춤\n" +
            "• 위에 떠 있던 BaseYaw/Base visual만 숨김\n" +
            "• Base_Yaw transform과 제어 로직은 유지\n" +
            "• RobotArm root Rotation은 변경하지 않음\n" +
            "• JointCommand 값도 변경하지 않음\n" +
            "• beam 끝에 shoulder housing visual 생성\n\n" +
            "이상하면 Ctrl+Z 한 번으로 전체 적용을 되돌릴 수 있습니다.",
            "OK");
    }

    private static void HideBaseVisualOnly(Transform armRoot)
    {
        foreach (Renderer r in armRoot.GetComponentsInChildren<Renderer>(true))
        {
            string n = r.gameObject.name;

            bool hide =
                n == "BaseMesh" ||
                n == "BaseLink" ||
                n.StartsWith("__G51V1_Base", StringComparison.Ordinal) ||
                n.StartsWith("__G51V2_Base", StringComparison.Ordinal) ||
                n.StartsWith("__G51V1_Servo_Base_Yaw", StringComparison.Ordinal) ||
                n.StartsWith("__G51V2_Servo_Base_Yaw", StringComparison.Ordinal);

            if (hide)
            {
                Undo.RecordObject(r, "Hide floor-base visual");
                r.enabled = false;
            }
        }
    }

    private static void CreateSideHousing(
        Transform parent, string side, Vector3 worldPos, Material dark, Material silver)
    {
        GameObject root = new GameObject(Prefix + side + "_ShoulderMount");
        Undo.RegisterCreatedObjectUndo(root, "Create shoulder side mount");
        root.transform.SetParent(parent, true);
        root.transform.position = worldPos;
        root.transform.rotation = Quaternion.identity;

        // Main shoulder housing. Cylinder default axis Y -> rotate to world X.
        GameObject body = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        body.name = Prefix + side + "_Housing";
        Undo.RegisterCreatedObjectUndo(body, "Create shoulder housing");
        body.transform.SetParent(root.transform, false);
        body.transform.localRotation = Quaternion.Euler(0f, 0f, 90f);
        body.transform.localScale = new Vector3(0.042f, 0.027f, 0.042f);
        body.GetComponent<Renderer>().sharedMaterial = dark;

        // Silver axis/hub through the shoulder.
        GameObject hub = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        hub.name = Prefix + side + "_AxisHub";
        Undo.RegisterCreatedObjectUndo(hub, "Create shoulder axis hub");
        hub.transform.SetParent(root.transform, false);
        hub.transform.localRotation = Quaternion.Euler(0f, 0f, 90f);
        hub.transform.localScale = new Vector3(0.024f, 0.032f, 0.024f);
        hub.GetComponent<Renderer>().sharedMaterial = silver;

        // Small clamp plate joining the housing visually to the beam side.
        GameObject clamp = GameObject.CreatePrimitive(PrimitiveType.Cube);
        clamp.name = Prefix + side + "_Clamp";
        Undo.RegisterCreatedObjectUndo(clamp, "Create shoulder clamp");
        clamp.transform.SetParent(root.transform, false);
        clamp.transform.localPosition = Vector3.zero;
        clamp.transform.localScale = new Vector3(0.018f, 0.095f, 0.085f);
        clamp.GetComponent<Renderer>().sharedMaterial = dark;

        // Put cylinders slightly outside clamp so they remain readable.
        float sign = side == "Left" ? -1f : 1f;
        body.transform.localPosition = new Vector3(sign * 0.025f, 0f, 0f);
        hub.transform.localPosition = new Vector3(sign * 0.028f, 0f, 0f);
        clamp.transform.localPosition = new Vector3(sign * 0.005f, 0f, 0f);
    }

    private static Renderer FindBeam(Transform frame)
    {
        Transform named = FindRecursive(frame, "HorizontalBeam");
        if (named != null)
        {
            Renderer r = named.GetComponent<Renderer>();
            if (r != null) return r;
            r = named.GetComponentInChildren<Renderer>(true);
            if (r != null) return r;
        }

        Renderer best = null;
        float bestScore = -1f;
        foreach (Renderer r in frame.GetComponentsInChildren<Renderer>(true))
        {
            Bounds b = r.bounds;
            float score = b.size.x / Mathf.Max(0.001f, b.size.y + b.size.z);
            if (score > bestScore)
            {
                bestScore = score;
                best = r;
            }
        }
        return best;
    }

    private static void RemoveGenerated(Transform root)
    {
        List<GameObject> kill = new List<GameObject>();
        foreach (Transform t in root.GetComponentsInChildren<Transform>(true))
            if (t != root && t.name.StartsWith(Prefix, StringComparison.Ordinal))
                kill.Add(t.gameObject);

        for (int i = kill.Count - 1; i >= 0; --i)
            if (kill[i] != null)
                Undo.DestroyObjectImmediate(kill[i]);
    }

    private static Transform FindRecursive(Transform p, string name)
    {
        if (p.name == name) return p;
        foreach (Transform c in p)
        {
            Transform f = FindRecursive(c, name);
            if (f != null) return f;
        }
        return null;
    }

    private static Material GetOrCreateMaterial(
        string path, Color color, float metallic, float smoothness)
    {
        EnsureFolder("Assets/Materials");
        EnsureFolder("Assets/Materials/G51Generated");

        Material m = AssetDatabase.LoadAssetAtPath<Material>(path);
        if (m == null)
        {
            Shader s = Shader.Find("Universal Render Pipeline/Lit");
            if (s == null) s = Shader.Find("Standard");
            m = new Material(s);
            AssetDatabase.CreateAsset(m, path);
        }

        if (m.HasProperty("_BaseColor")) m.SetColor("_BaseColor", color);
        if (m.HasProperty("_Color")) m.SetColor("_Color", color);
        if (m.HasProperty("_Metallic")) m.SetFloat("_Metallic", metallic);
        if (m.HasProperty("_Smoothness")) m.SetFloat("_Smoothness", smoothness);
        if (m.HasProperty("_Glossiness")) m.SetFloat("_Glossiness", smoothness);
        EditorUtility.SetDirty(m);
        AssetDatabase.SaveAssets();
        return m;
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
