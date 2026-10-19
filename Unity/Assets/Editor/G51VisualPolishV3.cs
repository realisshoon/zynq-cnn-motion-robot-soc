
#if UNITY_EDITOR
using UnityEditor;
using UnityEngine;

public static class G51VisualPolishV3
{
    private const string DecoPrefix = "__G51V3_";

    [MenuItem("Tools/Human Motion/G51 Visual v3/Polish Current Visual")]
    public static void Polish()
    {
        if (EditorApplication.isPlaying)
        {
            EditorUtility.DisplayDialog("G51 Visual v3", "Play Mode를 종료한 뒤 실행하세요.", "OK");
            return;
        }

        GameObject robot = GameObject.Find("RobotArm");
        if (robot == null)
        {
            EditorUtility.DisplayDialog("G51 Visual v3", "RobotArm을 찾지 못했습니다.", "OK");
            return;
        }

        Material black = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_BlackMetal_v3.mat",
            new Color(0.025f, 0.028f, 0.032f, 1f), 0.72f, 0.30f);

        Material darkServo = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_ServoBody_v3.mat",
            new Color(0.035f, 0.038f, 0.045f, 1f), 0.18f, 0.34f);

        Material silver = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_Silver_v3.mat",
            new Color(0.50f, 0.53f, 0.58f, 1f), 0.90f, 0.52f);

        Material rubber = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_GripPad_v3.mat",
            new Color(0.055f, 0.055f, 0.060f, 1f), 0.05f, 0.20f);

        // v2 generated robot parts
        foreach (Renderer r in robot.GetComponentsInChildren<Renderer>(true))
        {
            string n = r.gameObject.name;

            if (HasAncestorPrefix(r.transform, "__G51V2_Servo_"))
            {
                // In the converted STEP, solid_06 is the output shaft/spline.
                r.sharedMaterial = n.Contains("solid_06") ? silver : darkServo;
            }
            else if (n.StartsWith("__G51V2_BaseHub") || n.StartsWith("__G51V2_PivotCap") ||
                     n.StartsWith("__G51V2_GripperHub"))
            {
                r.sharedMaterial = silver;
            }
            else if (n.StartsWith("__G51V2_"))
            {
                r.sharedMaterial = black;
            }
        }

        // Existing moving linkage gripper: keep mechanics, just improve materials.
        Transform gripper = FindRecursive(robot.transform, "Gripper");
        if (gripper != null)
        {
            foreach (Renderer r in gripper.GetComponentsInChildren<Renderer>(true))
            {
                string n = r.gameObject.name;
                if (n == "Gear_L" || n == "Gear_R" || n == "DriveHub" || n == "ServoNeck")
                    r.sharedMaterial = silver;
                else if (n == "Finger_L" || n == "Finger_R")
                    r.sharedMaterial = rubber;
                else
                    r.sharedMaterial = black;
            }
        }

        AddJointCaps(robot.transform, silver);

        AssetDatabase.SaveAssets();
        EditorUtility.SetDirty(robot);
        EditorUtility.DisplayDialog(
            "G51 Visual v3",
            "외형 마감 완료.\n\n- MG996R 본체를 검정 서보 재질로 통일\n- 출력축/허브를 금속 재질로 구분\n- 브라켓/링크를 검정 알루미늄 느낌으로 통일\n- 그리퍼 재질 정리\n- 관절 볼트 캡 추가\n\n관절 Transform/RobotArmController/UDP는 변경하지 않았습니다.",
            "OK"
        );
    }

    [MenuItem("Tools/Human Motion/G51 Visual v3/Setup Presentation Camera")]
    public static void SetupCamera()
    {
        if (EditorApplication.isPlaying)
        {
            EditorUtility.DisplayDialog("G51 Visual v3", "Play Mode를 종료한 뒤 실행하세요.", "OK");
            return;
        }

        GameObject robot = GameObject.Find("RobotArm");
        Camera cam = Camera.main;
        if (robot == null || cam == null)
        {
            EditorUtility.DisplayDialog("G51 Visual v3", "RobotArm 또는 Main Camera를 찾지 못했습니다.", "OK");
            return;
        }

        Renderer[] rs = robot.GetComponentsInChildren<Renderer>(true);
        bool found = false;
        Bounds b = new Bounds(robot.transform.position, Vector3.one * 0.1f);

        foreach (Renderer r in rs)
        {
            if (!r.enabled) continue;
            if (!found) { b = r.bounds; found = true; }
            else b.Encapsulate(r.bounds);
        }

        Vector3 center = b.center;
        float span = Mathf.Max(b.size.x, Mathf.Max(b.size.y, b.size.z));
        float distance = Mathf.Max(0.65f, span * 2.3f);

        Vector3 viewDir = new Vector3(1.15f, 0.75f, -1.45f).normalized;

        Undo.RecordObject(cam.transform, "Setup G51 presentation camera");
        Undo.RecordObject(cam, "Setup G51 presentation camera");

        cam.transform.position = center + viewDir * distance;
        cam.transform.LookAt(center + Vector3.up * span * 0.05f);
        cam.fieldOfView = 34f;
        cam.nearClipPlane = 0.01f;
        cam.farClipPlane = 50f;

        EditorUtility.SetDirty(cam);
        EditorUtility.DisplayDialog(
            "G51 Visual v3",
            "Main Camera를 3/4 제품 렌더 구도로 배치했습니다.\nGame 탭에서 확인하세요.",
            "OK"
        );
    }

    [MenuItem("Tools/Human Motion/G51 Visual v3/Setup Presentation Lighting")]
    public static void SetupLighting()
    {
        if (EditorApplication.isPlaying)
        {
            EditorUtility.DisplayDialog("G51 Visual v3", "Play Mode를 종료한 뒤 실행하세요.", "OK");
            return;
        }

        GameObject robot = GameObject.Find("RobotArm");
        if (robot == null) return;

        RemoveExistingDecoLights();

        Renderer[] rs = robot.GetComponentsInChildren<Renderer>(true);
        Bounds b = new Bounds(robot.transform.position, Vector3.one * 0.1f);
        bool found = false;
        foreach (Renderer r in rs)
        {
            if (!r.enabled) continue;
            if (!found) { b = r.bounds; found = true; }
            else b.Encapsulate(r.bounds);
        }

        float span = Mathf.Max(0.25f, Mathf.Max(b.size.x, Mathf.Max(b.size.y, b.size.z)));
        Vector3 c = b.center;

        CreatePointLight("KeyLight", c + new Vector3(span*1.2f, span*1.4f, -span*1.2f), 4.5f, span*5f);
        CreatePointLight("FillLight", c + new Vector3(-span*1.3f, span*0.8f, -span*0.2f), 2.0f, span*5f);
        CreatePointLight("RimLight", c + new Vector3(0f, span*1.2f, span*1.4f), 2.8f, span*5f);

        EditorUtility.DisplayDialog(
            "G51 Visual v3",
            "제품 렌더용 3-point lighting을 추가했습니다.\nGame 탭에서 확인하세요.",
            "OK"
        );
    }

    private static void AddJointCaps(Transform root, Material silver)
    {
        RemoveExistingCaps(root);

        string[] names = { "Shoulder_Pitch", "Elbow_Pitch", "Wrist_Pitch", "Wrist_Roll" };
        foreach (string jointName in names)
        {
            Transform p = FindRecursive(root, jointName);
            if (p == null) continue;

            GameObject capA = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
            capA.name = DecoPrefix + "Bolt_" + jointName + "_A";
            Undo.RegisterCreatedObjectUndo(capA, "Create G51 joint bolt");
            capA.transform.SetParent(p, false);
            capA.transform.localPosition = new Vector3(0.027f, 0f, 0f);
            capA.transform.localRotation = Quaternion.Euler(0f, 0f, 90f);
            capA.transform.localScale = new Vector3(0.007f, 0.0025f, 0.007f);
            capA.GetComponent<Renderer>().sharedMaterial = silver;

            GameObject capB = Object.Instantiate(capA, p);
            capB.name = DecoPrefix + "Bolt_" + jointName + "_B";
            Undo.RegisterCreatedObjectUndo(capB, "Create G51 joint bolt");
            capB.transform.localPosition = new Vector3(-0.027f, 0f, 0f);
        }
    }

    private static void RemoveExistingCaps(Transform root)
    {
        Transform[] ts = root.GetComponentsInChildren<Transform>(true);
        for (int i = ts.Length - 1; i >= 0; --i)
        {
            if (ts[i] != root && ts[i].name.StartsWith(DecoPrefix + "Bolt_"))
                Undo.DestroyObjectImmediate(ts[i].gameObject);
        }
    }

    private static void RemoveExistingDecoLights()
    {
        GameObject[] all = Object.FindObjectsByType<GameObject>(FindObjectsSortMode.None);
        foreach (GameObject go in all)
            if (go.name.StartsWith(DecoPrefix + "Light_"))
                Undo.DestroyObjectImmediate(go);
    }

    private static void CreatePointLight(string shortName, Vector3 pos, float intensity, float range)
    {
        GameObject go = new GameObject(DecoPrefix + "Light_" + shortName);
        Undo.RegisterCreatedObjectUndo(go, "Create G51 presentation light");
        go.transform.position = pos;
        Light l = go.AddComponent<Light>();
        l.type = LightType.Point;
        l.intensity = intensity;
        l.range = range;
        l.shadows = LightShadows.Soft;
    }

    private static bool HasAncestorPrefix(Transform t, string prefix)
    {
        while (t != null)
        {
            if (t.name.StartsWith(prefix)) return true;
            t = t.parent;
        }
        return false;
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

    private static Material GetOrCreateMaterial(string path, Color color, float metallic, float smoothness)
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
