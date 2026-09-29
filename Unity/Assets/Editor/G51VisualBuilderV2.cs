
#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

public static class G51VisualBuilderV2
{
    private const string GeneratedPrefix = "__G51V2_";
    private const string ServoAssetName = "MG996R_Clean_Shoulder_XAxis";

    private static readonly string[] PlaceholderNames =
    {
        "BaseMesh",
        "BaseLink",
        "UpperArm",
        "ForeArm",
        "WristLink",
        "Hand"
    };

    [MenuItem("Tools/Human Motion/G51 Visual v2/Build or Rebuild")]
    public static void BuildOrRebuild()
    {
        if (EditorApplication.isPlaying)
        {
            EditorUtility.DisplayDialog("G51 Visual v2", "Play Mode를 종료한 뒤 실행하세요.", "OK");
            return;
        }

        GameObject robotArm = GameObject.Find("RobotArm");
        if (robotArm == null)
        {
            EditorUtility.DisplayDialog("G51 Visual v2", "Hierarchy에서 'RobotArm'을 찾지 못했습니다.", "OK");
            return;
        }

        Transform root = robotArm.transform;
        Transform baseYaw = FindRecursive(root, "Base_Yaw");
        Transform shoulder = FindRecursive(root, "Shoulder_Pitch");
        Transform elbow = FindRecursive(root, "Elbow_Pitch");
        Transform wristPitch = FindRecursive(root, "Wrist_Pitch");
        Transform wristRoll = FindRecursive(root, "Wrist_Roll");
        Transform gripper = FindRecursive(root, "Gripper");

        if (baseYaw == null || shoulder == null || elbow == null ||
            wristPitch == null || wristRoll == null || gripper == null)
        {
            EditorUtility.DisplayDialog(
                "G51 Visual v2",
                "필수 Pivot을 찾지 못했습니다.\nBase_Yaw / Shoulder_Pitch / Elbow_Pitch / Wrist_Pitch / Wrist_Roll / Gripper",
                "OK"
            );
            return;
        }

        GameObject servoPrefab = FindServoAsset();
        if (servoPrefab == null)
        {
            EditorUtility.DisplayDialog(
                "G51 Visual v2",
                "MG996R_Clean_Shoulder_XAxis.obj를 찾지 못했습니다.",
                "OK"
            );
            return;
        }

        Undo.IncrementCurrentGroup();
        int undoGroup = Undo.GetCurrentGroup();
        Undo.SetCurrentGroupName("Build G51 Visual v2");

        // Remove v1/v2 generated visual objects only.
        RemoveGenerated(root, "__G51V1_");
        RemoveGenerated(root, "__G51V2_");

        HidePlaceholders(root);

        // Critical v2 fix:
        // the old photo-proportion gripper was authored at ~0.6 m scale.
        // Keep its working linkage logic, but uniformly scale it to a realistic servo/gripper size.
        Undo.RecordObject(gripper, "Scale and place G51 gripper");
        gripper.localPosition = new Vector3(0f, 0.050f, 0f);
        gripper.localScale = Vector3.one * 0.16f;

        Material black = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_BlackMetal.mat",
            new Color(0.035f, 0.040f, 0.045f, 1f), 0.72f, 0.28f);

        Material dark = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_DarkServo.mat",
            new Color(0.055f, 0.055f, 0.065f, 1f), 0.18f, 0.30f);

        Material silver = GetOrCreateMaterial(
            "Assets/Materials/G51Generated/G51_Silver.mat",
            new Color(0.55f, 0.57f, 0.60f, 1f), 0.85f, 0.45f);

        CreateBaseVisual(baseYaw, black, silver);

        PlaceServoAtJoint(servoPrefab, baseYaw, true, dark);
        PlaceServoAtJoint(servoPrefab, shoulder, false, dark);
        PlaceServoAtJoint(servoPrefab, elbow, false, dark);
        PlaceServoAtJoint(servoPrefab, wristPitch, false, dark);
        PlaceServoAtJoint(servoPrefab, wristRoll, true, dark);

        CreateUBracket(shoulder, black, silver, 0.060f, 0.050f, 0.030f);
        CreateUBracket(elbow, black, silver, 0.058f, 0.048f, 0.028f);
        CreateUBracket(wristPitch, black, silver, 0.052f, 0.042f, 0.025f);
        CreateUBracket(wristRoll, black, silver, 0.048f, 0.038f, 0.023f);

        CreateTwinLink(shoulder, elbow, black, 0.020f, 0.004f, 0.020f);
        CreateTwinLink(elbow, wristPitch, black, 0.018f, 0.004f, 0.018f);
        CreateTwinLink(wristPitch, wristRoll, black, 0.015f, 0.0035f, 0.016f);

        // Small wrist-to-gripper adapter so the scaled linkage gripper looks mechanically attached.
        CreateAdapter(wristRoll, gripper, black, silver);

        Selection.activeGameObject = robotArm;
        EditorUtility.SetDirty(robotArm);
        Undo.CollapseUndoOperations(undoGroup);
        SceneView.lastActiveSceneView?.FrameSelected();

        EditorUtility.DisplayDialog(
            "G51 Visual v2",
            "완료.\n\nv2 수정:\n- 거대한 기존 Hand 숨김\n- 기존 G51 linkage gripper를 0.16배로 축소\n- Gripper를 Wrist에 다시 붙임\n- MG996R / U-bracket / twin plate 자동 생성\n\n초록 원/축은 Scene Gizmo라 Game 화면에는 나오지 않습니다.",
            "OK"
        );
    }

    [MenuItem("Tools/Human Motion/G51 Visual v2/Restore Placeholder Visuals")]
    public static void Restore()
    {
        if (EditorApplication.isPlaying)
        {
            EditorUtility.DisplayDialog("G51 Visual v2", "Play Mode를 종료한 뒤 실행하세요.", "OK");
            return;
        }

        GameObject robotArm = GameObject.Find("RobotArm");
        if (robotArm == null) return;

        Transform root = robotArm.transform;
        Transform gripper = FindRecursive(root, "Gripper");

        Undo.IncrementCurrentGroup();
        int undoGroup = Undo.GetCurrentGroup();

        RemoveGenerated(root, "__G51V1_");
        RemoveGenerated(root, "__G51V2_");

        foreach (Renderer r in root.GetComponentsInChildren<Renderer>(true))
        {
            foreach (string n in PlaceholderNames)
            {
                if (r.gameObject.name == n)
                {
                    Undo.RecordObject(r, "Enable placeholder");
                    r.enabled = true;
                    break;
                }
            }
        }

        if (gripper != null)
        {
            Undo.RecordObject(gripper, "Restore gripper transform");
            gripper.localPosition = new Vector3(0f, 0.24f, 0f);
            gripper.localScale = Vector3.one;
        }

        Undo.CollapseUndoOperations(undoGroup);
        EditorUtility.DisplayDialog("G51 Visual v2", "v1/v2 생성 Visual 제거 + 기존 placeholder 복구 완료.", "OK");
    }

    private static GameObject FindServoAsset()
    {
        string[] guids = AssetDatabase.FindAssets(ServoAssetName + " t:Model");
        if (guids == null || guids.Length == 0)
            guids = AssetDatabase.FindAssets("MG996R t:Model");
        if (guids == null || guids.Length == 0) return null;
        string path = AssetDatabase.GUIDToAssetPath(guids[0]);
        return AssetDatabase.LoadAssetAtPath<GameObject>(path);
    }

    private static void HidePlaceholders(Transform root)
    {
        foreach (Renderer r in root.GetComponentsInChildren<Renderer>(true))
        {
            foreach (string n in PlaceholderNames)
            {
                if (r.gameObject.name == n)
                {
                    Undo.RecordObject(r, "Hide placeholder renderer");
                    r.enabled = false;
                    break;
                }
            }
        }
    }

    private static void RemoveGenerated(Transform root, string prefix)
    {
        List<GameObject> doomed = new List<GameObject>();
        foreach (Transform t in root.GetComponentsInChildren<Transform>(true))
            if (t != root && t.name.StartsWith(prefix, StringComparison.Ordinal))
                doomed.Add(t.gameObject);

        doomed.Sort((a,b) => GetDepth(a.transform).CompareTo(GetDepth(b.transform)));
        HashSet<Transform> removedRoots = new HashSet<Transform>();

        foreach (GameObject go in doomed)
        {
            if (go == null) continue;
            bool covered = false;
            Transform p = go.transform.parent;
            while (p != null)
            {
                if (removedRoots.Contains(p)) { covered = true; break; }
                p = p.parent;
            }
            if (covered) continue;
            removedRoots.Add(go.transform);
            Undo.DestroyObjectImmediate(go);
        }
    }

    private static int GetDepth(Transform t)
    {
        int d=0;
        while (t.parent != null) { d++; t=t.parent; }
        return d;
    }

    private static Transform FindRecursive(Transform parent, string name)
    {
        if (parent.name == name) return parent;
        foreach (Transform child in parent)
        {
            Transform f = FindRecursive(child, name);
            if (f != null) return f;
        }
        return null;
    }

    private static void PlaceServoAtJoint(GameObject servoPrefab, Transform jointPivot, bool axisY, Material fallback)
    {
        Transform fixedParent = jointPivot.parent != null ? jointPivot.parent : jointPivot;
        GameObject instance = (GameObject)PrefabUtility.InstantiatePrefab(servoPrefab, fixedParent);
        if (instance == null) instance = UnityEngine.Object.Instantiate(servoPrefab, fixedParent);

        instance.name = GeneratedPrefix + "Servo_" + jointPivot.name;
        Undo.RegisterCreatedObjectUndo(instance, "Create MG996R visual");

        instance.transform.position = jointPivot.position;
        instance.transform.rotation = jointPivot.rotation *
            (axisY ? Quaternion.Euler(0f,0f,90f) : Quaternion.identity);
        instance.transform.localScale = Vector3.one;

        foreach (Renderer r in instance.GetComponentsInChildren<Renderer>(true))
        {
            if (r.sharedMaterial == null) r.sharedMaterial = fallback;
            r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.On;
            r.receiveShadows = true;
        }
    }

    private static void CreateBaseVisual(Transform baseYaw, Material black, Material silver)
    {
        Transform parent = baseYaw.parent != null ? baseYaw.parent : baseYaw;

        GameObject b = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        b.name = GeneratedPrefix + "Base";
        Undo.RegisterCreatedObjectUndo(b, "Create base");
        b.transform.SetParent(parent, false);
        b.transform.position = baseYaw.position + Vector3.down * 0.018f;
        b.transform.localScale = new Vector3(0.075f,0.018f,0.075f);
        SetMaterial(b, black);

        GameObject hub = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        hub.name = GeneratedPrefix + "BaseHub";
        Undo.RegisterCreatedObjectUndo(hub, "Create base hub");
        hub.transform.SetParent(parent, false);
        hub.transform.position = baseYaw.position;
        hub.transform.localScale = new Vector3(0.038f,0.006f,0.038f);
        SetMaterial(hub, silver);
    }

    private static void CreateUBracket(Transform pivot, Material black, Material silver,
        float outerWidth, float height, float depth)
    {
        GameObject root = new GameObject(GeneratedPrefix + "Bracket_" + pivot.name);
        Undo.RegisterCreatedObjectUndo(root, "Create bracket");
        root.transform.SetParent(pivot, false);

        float t = 0.0035f;
        float sx = outerWidth * 0.5f - t * 0.5f;

        CreateBox(root.transform,"Side_L",new Vector3(t,height,depth),
            new Vector3(-sx,height*0.25f,0f),black);
        CreateBox(root.transform,"Side_R",new Vector3(t,height,depth),
            new Vector3(sx,height*0.25f,0f),black);
        CreateBox(root.transform,"Bridge",new Vector3(outerWidth,t,depth),
            new Vector3(0f,height*0.5f,0f),black);

        CreatePivotCap(root.transform,new Vector3(-outerWidth*0.5f,0f,0f),silver);
        CreatePivotCap(root.transform,new Vector3( outerWidth*0.5f,0f,0f),silver);
    }

    private static void CreateTwinLink(Transform from, Transform to, Material material,
        float sideOffset, float thickness, float plateWidth)
    {
        Vector3 a = from.position;
        Vector3 b = to.position;
        if ((b-a).magnitude < 0.001f) return;

        Vector3 side = from.TransformDirection(Vector3.forward).normalized;
        CreateWorldBar(from, GeneratedPrefix+"LinkA_"+from.name,
            a+side*sideOffset,b+side*sideOffset,plateWidth,thickness,material);
        CreateWorldBar(from, GeneratedPrefix+"LinkB_"+from.name,
            a-side*sideOffset,b-side*sideOffset,plateWidth,thickness,material);
    }

    private static void CreateAdapter(Transform wristRoll, Transform gripper, Material black, Material silver)
    {
        GameObject root = new GameObject(GeneratedPrefix + "GripperAdapter");
        Undo.RegisterCreatedObjectUndo(root, "Create gripper adapter");
        root.transform.SetParent(wristRoll, false);
        root.transform.localPosition = new Vector3(0f,0.024f,0f);

        CreateBox(root.transform,"Neck",
            new Vector3(0.030f,0.030f,0.022f),
            Vector3.zero,black);

        GameObject hub = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        hub.name = GeneratedPrefix + "GripperHub";
        Undo.RegisterCreatedObjectUndo(hub, "Create gripper hub");
        hub.transform.SetParent(root.transform,false);
        hub.transform.localPosition = new Vector3(0f,0.020f,0f);
        hub.transform.localScale = new Vector3(0.018f,0.005f,0.018f);
        SetMaterial(hub,silver);
    }

    private static void CreateWorldBar(Transform parent, string name,
        Vector3 a, Vector3 b, float width, float thickness, Material material)
    {
        Vector3 d=b-a;
        float len=d.magnitude;
        if (len < 0.001f) return;

        GameObject bar=GameObject.CreatePrimitive(PrimitiveType.Cube);
        bar.name=name;
        Undo.RegisterCreatedObjectUndo(bar,"Create arm plate");
        bar.transform.SetParent(parent,true);
        bar.transform.position=(a+b)*0.5f;
        bar.transform.rotation=Quaternion.FromToRotation(Vector3.up,d.normalized);
        bar.transform.localScale=new Vector3(width,len,thickness);
        SetMaterial(bar,material);
    }

    private static void CreatePivotCap(Transform parent, Vector3 pos, Material mat)
    {
        GameObject cap=GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        cap.name=GeneratedPrefix+"PivotCap";
        Undo.RegisterCreatedObjectUndo(cap,"Create pivot cap");
        cap.transform.SetParent(parent,false);
        cap.transform.localPosition=pos;
        cap.transform.localRotation=Quaternion.Euler(0f,0f,90f);
        cap.transform.localScale=new Vector3(0.007f,0.002f,0.007f);
        SetMaterial(cap,mat);
    }

    private static GameObject CreateBox(Transform parent,string name,Vector3 size,Vector3 pos,Material mat)
    {
        GameObject box=GameObject.CreatePrimitive(PrimitiveType.Cube);
        box.name=GeneratedPrefix+name;
        Undo.RegisterCreatedObjectUndo(box,"Create G51 box");
        box.transform.SetParent(parent,false);
        box.transform.localPosition=pos;
        box.transform.localScale=size;
        SetMaterial(box,mat);
        return box;
    }

    private static void SetMaterial(GameObject go, Material mat)
    {
        Renderer r=go.GetComponent<Renderer>();
        if (r!=null)
        {
            r.sharedMaterial=mat;
            r.shadowCastingMode=UnityEngine.Rendering.ShadowCastingMode.On;
            r.receiveShadows=true;
        }
    }

    private static Material GetOrCreateMaterial(string path, Color color, float metallic, float smoothness)
    {
        EnsureFolder("Assets/Materials");
        EnsureFolder("Assets/Materials/G51Generated");

        Material m=AssetDatabase.LoadAssetAtPath<Material>(path);
        if (m==null)
        {
            Shader s=Shader.Find("Universal Render Pipeline/Lit");
            if (s==null) s=Shader.Find("Standard");
            m=new Material(s);
            AssetDatabase.CreateAsset(m,path);
        }

        if (m.HasProperty("_BaseColor")) m.SetColor("_BaseColor",color);
        if (m.HasProperty("_Color")) m.SetColor("_Color",color);
        if (m.HasProperty("_Metallic")) m.SetFloat("_Metallic",metallic);
        if (m.HasProperty("_Smoothness")) m.SetFloat("_Smoothness",smoothness);
        if (m.HasProperty("_Glossiness")) m.SetFloat("_Glossiness",smoothness);
        EditorUtility.SetDirty(m);
        AssetDatabase.SaveAssets();
        return m;
    }

    private static void EnsureFolder(string path)
    {
        if (AssetDatabase.IsValidFolder(path)) return;
        string parent=System.IO.Path.GetDirectoryName(path)?.Replace("\\","/");
        string name=System.IO.Path.GetFileName(path);
        if (!string.IsNullOrEmpty(parent) && !AssetDatabase.IsValidFolder(parent))
            EnsureFolder(parent);
        if (!string.IsNullOrEmpty(parent))
            AssetDatabase.CreateFolder(parent,name);
    }
}
#endif
