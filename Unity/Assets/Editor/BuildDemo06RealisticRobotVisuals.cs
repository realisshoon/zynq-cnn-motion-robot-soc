using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

// Demo_02 donor geometry is a prefab, never a replacement runtime scene.
public static class BuildDemo06RealisticRobotVisuals
{
    public const string ScenePath = "Assets/Scenes/Demo_06_MediaPipeRobotLive.unity";
    public const string PrefabPath = "Assets/Validation/Demo06RealisticVisuals/Demo06RealisticVisual.prefab";
    const string ReportDirectory = "Validation/YahboomIntegration";

    [MenuItem("Tools/Demo06 Realistic Visuals/1. Build Visuals (current Demo06 only)")]
    public static void Build()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode || SceneManager.GetActiveScene().path != ScenePath)
            throw new InvalidOperationException("Open Demo_06 in Edit Mode first.");
        var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(PrefabPath);
        if (prefab == null) throw new InvalidOperationException("Missing isolated visual reference prefab.");
        var controllers = SceneManager.GetActiveScene().GetRootGameObjects()
            .SelectMany(root => root.GetComponentsInChildren<ForearmArmController>(true))
            .Where(c => c.enabled && c.IsConfigured).ToArray();
        if (controllers.Length == 0) throw new InvalidOperationException("No configured Demo06 robot.");
        foreach (var controller in controllers)
        {
            var existing = controller.GetComponentInChildren<Demo06RealisticVisualAdapter>(true);
            if (existing != null)
            {
                if (!existing.IsConfigured) throw new InvalidOperationException("Existing visual adapter is incomplete.");
                continue; // Do not accumulate geometry or recapture a posed linkage.
            }
            // Preserve old objects, pivots and references. Only their meshes are superseded.
            foreach (var renderer in controller.GetComponentsInChildren<Renderer>(true))
            {
                Undo.RecordObject(renderer, "Hide superseded visual");
                renderer.enabled = false;
            }
            var visual = (GameObject)PrefabUtility.InstantiatePrefab(prefab, controller.transform);
            Undo.RegisterCreatedObjectUndo(visual, "Create Demo06 realistic visual");
            PrefabUtility.UnpackPrefabInstance(visual, PrefabUnpackMode.Completely, InteractionMode.AutomatedAction);
            visual.name = "Demo06RealisticVisualRoot";
            visual.transform.localPosition = Vector3.zero;
            visual.transform.localRotation = Quaternion.identity;
            visual.transform.localScale = Vector3.one;
            foreach (var t in visual.GetComponentsInChildren<Transform>(true)) t.gameObject.layer = 29;

            var er = Required(visual.transform, "ElbowRoll");
            var ep = Required(er, "ElbowPitch");
            var wp = Required(ep, "WristPitch");
            var wr = Required(wp, "WristRoll");
            er.localPosition = controller.elbowRoll.localPosition;
            // Donor +Z pitch becomes the original logical +X pitch in the forearm frame.
            // +Y (forearm/azimuth) is unchanged. This is a visual basis, not servo calibration.
            er.localRotation = controller.elbowRoll.localRotation * Quaternion.Euler(0, 90, 0);
            var adapter = Undo.AddComponent<Demo06RealisticVisualAdapter>(visual);
            adapter.controller = controller;
            adapter.m0 = Bind(controller.elbowRoll, er, Quaternion.identity);
            adapter.m1 = Bind(controller.elbowPitch, ep, Quaternion.Euler(0, -90, 0));
            adapter.m2 = Bind(controller.wristPitch, wp, Quaternion.Euler(0, -90, 0));
            adapter.m3 = Bind(controller.wristRoll, wr, Quaternion.identity);

            var template = Required(wr, "ToolMount/Gripper").GetComponent<G51GripperVisual>();
            if (template == null || template.visualRoot == null)
                throw new InvalidOperationException("Donor gripper linkage/solver frame missing.");
            // Keep the existing controller's G51 component identity and logical parent.
            // Copy only visual linkage fields, including the donor's captured solver rest.
            Undo.RecordObject(controller.gripperVisual, "Connect realistic gripper linkage");
            var from = new SerializedObject(template);
            var to = new SerializedObject(controller.gripperVisual);
            var property = from.GetIterator();
            while (property.NextVisible(true))
                if (property.depth == 0 && !property.name.StartsWith("m_"))
                    to.CopyFromSerializedProperty(property);
            to.ApplyModifiedPropertiesWithoutUndo();
            foreach (var duplicate in visual.GetComponentsInChildren<G51GripperVisual>(true))
                UnityEngine.Object.DestroyImmediate(duplicate);
            if (!adapter.IsConfigured) throw new InvalidOperationException("Adapter configuration failed.");
            EditorUtility.SetDirty(adapter);
        }
        EditorSceneManager.MarkSceneDirty(SceneManager.GetActiveScene());
    }

    static Transform Required(Transform root, string path) => root.Find(path) ??
        throw new InvalidOperationException("Missing visual transform: " + path);

    static Demo06RealisticVisualAdapter.RotationBinding Bind(Transform logical, Transform visual, Quaternion basis) =>
        new Demo06RealisticVisualAdapter.RotationBinding
        { logical = logical, visual = visual, logicalRest = logical.localRotation,
          visualRest = visual.localRotation, axisBasis = basis };

    // Batch entry point: the only scene this tool saves is Demo_06.
    public static void BuildAndValidate()
    {
        EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Single);
        Build();
        EditorSceneManager.SaveScene(SceneManager.GetActiveScene());
        Validate();
    }

    [MenuItem("Tools/Demo06 Realistic Visuals/2. Validate Saved Scene")]
    public static void Validate()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode || SceneManager.GetActiveScene().path != ScenePath ||
            SceneManager.GetActiveScene().isDirty)
            throw new InvalidOperationException("Save Demo_06 in Edit Mode before validating.");
        Directory.CreateDirectory(ReportDirectory);
        string report = "Unity 엔진 Edit Mode 검증 (실제 Webcam/Hardware 검증 아님)\n";
        try
        {
            foreach (var a in UnityEngine.Object.FindObjectsByType<Demo06RealisticVisualAdapter>(FindObjectsSortMode.None))
            {
                Require(a.IsConfigured, "adapter references");
                var c = a.controller;
                typeof(ForearmArmController).GetMethod("Awake",
                    System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(c, null);
                var joints = new[] { c.elbowRoll, c.elbowPitch, c.wristRoll, c.wristPitch, c.gripperVisual.transform };
                var parents = joints.Select(t => t.parent).ToArray();
                var positions = joints.Select(t => t.localPosition).ToArray();
                Quaternion previousRoll = Quaternion.identity;
                foreach (float pitch in new[] { 70f, 90f, 110f, 90f })
                {
                    Require(c.ApplyCommand(new ForearmJointCommandData { valid = true, frame_id = 1,
                        elbowRoll = 90, elbowPitch = 90, wristPitch = pitch, wristRoll = 87, gripper = 1 }), "ApplyCommand");
                    var before = joints.Select(t => t.localRotation).ToArray();
                    a.SyncVisuals();
                    Require(Quaternion.Angle(c.wristPitch.localRotation,
                        a.m2.logicalRest * Quaternion.AngleAxis(pitch - 90, Vector3.right)) < .05f, "logical M2 contract");
                    Require(Quaternion.Angle(a.m2.visual.localRotation,
                        a.m2.visualRest * Quaternion.AngleAxis(pitch - 90, Vector3.forward)) < .05f, "visual M2 axis");
                    Require(Vector3.Angle(c.wristRoll.position - c.elbowPitch.position,
                        a.m2.visual.position - a.m1.visual.position) < .05f, "forearm direction preserved");
                    for (int i = 0; i < joints.Length; i++)
                        Require(joints[i].parent == parents[i] && joints[i].localPosition == positions[i] &&
                            Quaternion.Angle(joints[i].localRotation, before[i]) < .05f, "logical pivots unchanged by adapter");
                    if (pitch != 70) Require(Quaternion.Angle(previousRoll, c.wristRoll.localRotation) < .05f, "M3 unchanged");
                    previousRoll = c.wristRoll.localRotation;
                    report += $"PASS M2={pitch}: logical Rx={pitch - 90}, visual Rz={pitch - 90}; pivots/M3 preserved\n";
                }
                foreach (float value in new[] { 40f, 90f, 140f })
                {
                    c.ApplyCommand(new ForearmJointCommandData { valid = true, elbowRoll = value, elbowPitch = value,
                        wristPitch = 90, wristRoll = value, gripper = (value - 40) / 100f });
                    a.SyncVisuals();
                    Require(Vector3.Angle(c.wristRoll.position - c.elbowPitch.position,
                        a.m2.visual.position - a.m1.visual.position) < .05f, "M0/M1 forearm direction");
                    Require(Quaternion.Angle(a.m3.visual.localRotation, a.m3.visualRest *
                        Quaternion.AngleAxis(-(value - 90), Vector3.up)) < .05f, "M3 delta preserved");
                    foreach (var t in a.GetComponentsInChildren<Transform>(true))
                        Require(float.IsFinite(t.position.x) && float.IsFinite(t.position.y) &&
                            float.IsFinite(t.position.z) && float.IsFinite(t.localRotation.w), "finite visual geometry");
                }
                var rotation = c.wristPitch.localRotation;
                Require(!c.ApplyCommand(new ForearmJointCommandData { valid = false }), "invalid rejected");
                Require(Quaternion.Angle(rotation, c.wristPitch.localRotation) < .05f, "invalid does not move joint");
                Require(a.GetComponentsInChildren<ForearmArmController>(true).Length == 0, "no visual command controller");
                Require(c.gripperVisual.visualRoot.IsChildOf(a.transform), "M4 uses same original component");
                report += "PASS M0/M1 direction, M3 rotation delta, finite M4 linkage, invalid rejection, original controller identity\n";
            }
            File.WriteAllText(ReportDirectory + "/engine-validation.txt", report);
            Debug.Log(report);
        }
        finally
        {
            // The tests never save command poses or alter the saved rest configuration.
            EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Single);
        }
    }

    static void Require(bool ok, string message)
    { if (!ok) throw new InvalidOperationException("Demo06 realistic visual validation failed: " + message); }
}
