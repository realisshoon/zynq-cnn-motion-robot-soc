using System;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEditor.SceneManagement;
using HumanMotion.MediaPipeDebug;

// Demo_06에 한정된 조사/검증 진입점. 요청 파일이 없으면 아무 작업도 하지 않는다.
[InitializeOnLoad]
public static class Demo06Workbench
{
    public const string ScenePath = "Assets/Scenes/Demo_06_MediaPipeRobotLive.unity";
    public const string Folder = "Validation/Demo06MediaPipeRobot";
    static Demo06Workbench() { EditorApplication.update += Poll; }
    private static void Poll()
    {
        var path = Folder + "/command.txt";
        if (EditorApplication.isCompiling || EditorApplication.isUpdating || !File.Exists(path)) return;
        string command = File.ReadAllText(path).Trim();
        File.Delete(path);
        try
        {
            if (SceneManager.GetActiveScene().path != ScenePath) throw new Exception("Demo_06을 먼저 열어야 합니다.");
            if (command == "inspect") Inspect();
            else if (command == "build") Build();
            else if (command == "validate") Demo06Validation.Begin();
            else if (command == "live-check") Demo06LiveCheck.Begin();
            else throw new Exception("Unknown Demo06 command: " + command);
        }
        catch (Exception e) { File.WriteAllText(Folder + "/error.txt", e.ToString()); Debug.LogException(e); }
    }
    [MenuItem("Tools/Human Motion/Demo 06/Configure Live Comparison")]
    public static void Build()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode || SceneManager.GetActiveScene().path != ScenePath)
            throw new Exception("Demo_06 Edit Mode only");
        var source = UnityEngine.Object.FindFirstObjectByType<UdpMediaPipePoseSource>();
        var human = UnityEngine.Object.FindFirstObjectByType<HumanArmDebugController>();
        var robot = UnityEngine.Object.FindFirstObjectByType<ForearmArmController>();
        if (source == null || human == null || robot == null || !robot.IsConfigured) throw new Exception("Missing configured source/human/robot");
        var old = source.GetComponent<MediaPipeLiveView>();
        if (old != null) UnityEngine.Object.DestroyImmediate(old);
        var adapter = source.GetComponent<MediaPipeRobotRetargetController>();
        if (adapter == null) adapter = source.gameObject.AddComponent<MediaPipeRobotRetargetController>();
        adapter.source = source; adapter.humanArm = human; adapter.robot = robot;
        var view = source.GetComponent<MediaPipeRobotLiveView>();
        if (view == null) view = source.gameObject.AddComponent<MediaPipeRobotLiveView>();
        view.source = source; view.retarget = adapter;
        // 내부 pivot/rest/reference를 보존하고 설치 회전만 Human Body +Z 정면에 맞춘다.
        var install = robot.transform.parent;
        if (install.name != "RobotInstallMount") throw new Exception("Unexpected install mount");
        install.localRotation = Quaternion.Euler(0, 180, 0);
        var green = Material("Demo06ServoGreen", new Color(.12f, .48f, .24f), .45f);
        var black = Material("Demo06BracketBlack", new Color(.09f, .10f, .12f), .4f);
        var silver = Material("Demo06ShaftSilver", new Color(.72f, .76f, .80f), .7f);
        var brass = Material("Demo06SpacerBrass", new Color(.55f, .38f, .14f), .65f);
        var mount = robot.transform.Find("ElbowVisualMount");
        if (mount == null) throw new Exception("Existing ElbowVisualMount required");
        mount.Find("BaseServo_Visual").GetComponent<Renderer>().sharedMaterial = green;
        mount.Find("ElbowBracket_L_Visual").GetComponent<Renderer>().sharedMaterial = black;
        mount.Find("ElbowBracket_R_Visual").GetComponent<Renderer>().sharedMaterial = black;
        Primitive(mount, "Demo06ServoCap", PrimitiveType.Cube, new Vector3(0, .014f, 0), new Vector3(.067f, .008f, .077f), black);
        // 기존 판의 mesh를 원판으로 교체하고 지지대만 보완한다.
        var plate = robot.transform.Find("RobotBasePlate_Visual");
        var temporary = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        plate.GetComponent<MeshFilter>().sharedMesh = temporary.GetComponent<MeshFilter>().sharedMesh;
        UnityEngine.Object.DestroyImmediate(temporary);
        plate.localScale = new Vector3(.20f, .006f, .20f);
        plate.GetComponent<Renderer>().sharedMaterial = black;
        Primitive(mount, "Demo06LowerRoundPlate", PrimitiveType.Cylinder, new Vector3(0, -.078f, 0), new Vector3(.20f, .006f, .20f), black);
        for (int i = 0; i < 4; i++)
        {
            float a = (45 + i * 90) * Mathf.Deg2Rad;
            Primitive(mount, "Demo06Spacer" + i, PrimitiveType.Cylinder,
                new Vector3(Mathf.Cos(a) * .075f, -.060f, Mathf.Sin(a) * .075f), new Vector3(.009f, .012f, .009f), brass);
        }
        var shaft = robot.elbowPitch.Find("ForearmShaft_Visual");
        shaft.localScale = new Vector3(.018f, robot.wristRoll.localPosition.y * .5f, .018f);
        shaft.GetComponent<Renderer>().sharedMaterial = silver;
        var servo = robot.elbowRoll.Find("__G51V2_Servo_Elbow_Pitch");
        if (servo != null) servo.localRotation = Quaternion.identity; // 기존 builder의 X-axis servo mesh를 pitch X축에 맞춘다.
        foreach (Transform child in robot.elbowPitch)
        {
            if (child.name == "__G51V2_Bracket_Elbow_Pitch") child.localRotation = Quaternion.identity;
            if (child.name.StartsWith("__G51V2_LinkA_Elbow_Pitch") || child.name.StartsWith("__G51V2_LinkB_Elbow_Pitch")) child.gameObject.SetActive(false);
        }
        foreach (var t in robot.GetComponentsInChildren<Transform>(true)) t.gameObject.layer = 29;
        var lightObject = GameObject.Find("Demo06RobotLight");
        if (lightObject == null) lightObject = new GameObject("Demo06RobotLight");
        var light = lightObject.GetComponent<Light>();
        if (light == null) light = lightObject.AddComponent<Light>();
        light.type = LightType.Directional; light.intensity = 2; light.cullingMask = 1 << 29;
        lightObject.transform.rotation = Quaternion.Euler(35, -35, 0);
        EditorSceneManager.MarkSceneDirty(SceneManager.GetActiveScene());
        EditorSceneManager.SaveScene(SceneManager.GetActiveScene()); AssetDatabase.SaveAssets();
        Inspect();
        File.WriteAllText(Folder + "/build-result.txt", "PASS: Demo06 configured. IsConfigured=" + robot.IsConfigured);
    }
    private static Material Material(string name, Color color, float metallic)
    {
        string path = "Assets/Materials/" + name + ".mat";
        var material = AssetDatabase.LoadAssetAtPath<Material>(path);
        if (material != null) return material;
        material = new Material(Shader.Find("Universal Render Pipeline/Lit"));
        material.SetColor("_BaseColor", color); material.SetFloat("_Metallic", metallic); material.SetFloat("_Smoothness", .4f);
        AssetDatabase.CreateAsset(material, path); return material;
    }
    private static void Primitive(Transform parent, string name, PrimitiveType type, Vector3 position, Vector3 scale, Material material)
    {
        Transform t = parent.Find(name);
        if (t == null) { var go = GameObject.CreatePrimitive(type); go.name = name; t = go.transform; t.SetParent(parent, false); }
        var collider = t.GetComponent<Collider>(); if (collider != null) UnityEngine.Object.DestroyImmediate(collider);
        t.localPosition = position; t.localRotation = Quaternion.identity; t.localScale = scale;
        t.GetComponent<Renderer>().sharedMaterial = material;
    }
    [MenuItem("Tools/Human Motion/Demo 06/Inspect Current Scene")]
    public static void Inspect()
    {
        var scene = SceneManager.GetActiveScene();
        if (scene.path != ScenePath) throw new Exception("Demo_06 only");
        Directory.CreateDirectory(Folder);
        var text = new StringBuilder();
        text.AppendLine("Scene=" + scene.path + " Playing=" + EditorApplication.isPlaying + " Dirty=" + scene.isDirty);
        foreach (var root in scene.GetRootGameObjects()) Dump(root.transform, text, "");
        File.WriteAllText(Folder + "/scene-inspection.txt", text.ToString());
    }
    private static void Dump(Transform t, StringBuilder text, string parent)
    {
        string path = parent + "/" + t.name;
        text.AppendLine(path + " active=" + t.gameObject.activeSelf + " pos=" + t.localPosition.ToString("F4") +
            " rot=" + t.localEulerAngles.ToString("F2") + " scale=" + t.localScale.ToString("F4"));
        foreach (var b in t.GetComponents<MonoBehaviour>())
            text.AppendLine("  SCRIPT " + (b == null ? "MISSING" : b.GetType().FullName + " enabled=" + b.enabled));
        var r = t.GetComponent<Renderer>();
        if (r != null) text.AppendLine("  RENDER enabled=" + r.enabled + " bounds=" + r.bounds);
        foreach (Transform child in t) Dump(child, text, path);
    }
}
