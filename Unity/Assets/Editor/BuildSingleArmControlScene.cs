using System;
using System.IO;
using System.Linq;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEditor.Build.Reporting;

public static class BuildSingleArmControlScene
{
    public const string Destination="Assets/Scenes/Demo_07_SingleArmControl.unity";
    const string Source="Assets/Scenes/Demo_06_MediaPipeRobotLive.unity";
    [MenuItem("Tools/Control Studio/1. Create Demo 07 (new scene only)")]
    public static void Create()
    {
        if(EditorApplication.isPlayingOrWillChangePlaymode)throw new InvalidOperationException("Stop Play before creating Studio");
        if(File.Exists(Destination))throw new InvalidOperationException("Demo_07 already exists; refusing overwrite");
        for(int i=0;i<SceneManager.sceneCount;i++)if(SceneManager.GetSceneAt(i).isDirty)throw new InvalidOperationException("Unsaved scene: user must decide before creation");
        var src=EditorSceneManager.OpenPreviewScene(Source);
        GameObject copy=null;
        try
        {
            var original=src.GetRootGameObjects().Single(g=>g.name=="RobotArm_Physical");
            var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
            copy=UnityEngine.Object.Instantiate(original);copy.name=original.name;
            SceneManager.MoveGameObjectToScene(copy,scene);
            foreach(var b in copy.GetComponentsInChildren<MonoBehaviour>(true))
                if(b!=null && b.GetType().Name=="UdpJointCommandReceiver")UnityEngine.Object.DestroyImmediate(b);
            foreach(var b in copy.GetComponentsInChildren<MonoBehaviour>(true))
                if(b!=null && !(b is ForearmArmController) && !(b is Demo06RealisticVisualAdapter) && !(b is G51GripperVisual))UnityEngine.Object.DestroyImmediate(b);
            RestoreTransforms(original.transform,copy.transform);
            var controller=copy.GetComponentsInChildren<ForearmArmController>(true).Single();
            controller.enabled=true;
            var adapter=copy.GetComponentsInChildren<Demo06RealisticVisualAdapter>(true).Single();
            if(!controller.IsConfigured||!adapter.IsConfigured)throw new InvalidOperationException("Cloned references invalid");
            foreach(var b in copy.GetComponentsInChildren<MonoBehaviour>(true))
            {
                var so=new SerializedObject(b);var it=so.GetIterator();
                while(it.Next(true))if(it.propertyType==SerializedPropertyType.ObjectReference)
                {
                    var obj=it.objectReferenceValue;
                    if(obj is Component comp && comp.gameObject.scene!=scene)throw new InvalidOperationException("External component reference: "+it.propertyPath);
                    if(obj is GameObject go && go.scene.IsValid() && go.scene!=scene)throw new InvalidOperationException("External object reference: "+it.propertyPath);
                }
            }
            var host=new GameObject("SingleArmControlStudio");var router=host.AddComponent<SingleArmCommandRouter>();router.robot=controller;
            var manual=host.AddComponent<ManualServoSource>();manual.router=router;
            var profiles=host.AddComponent<ControlStudioProfileStore>();profiles.source=manual;
            var camGO=new GameObject("Studio Camera",typeof(Camera),typeof(AudioListener));camGO.tag="MainCamera";
            var cam=camGO.GetComponent<Camera>();cam.clearFlags=CameraClearFlags.SolidColor;cam.backgroundColor=new Color(.045f,.07f,.095f);cam.fieldOfView=38;cam.nearClipPlane=.01f;cam.farClipPlane=100;
            cam.rect=new Rect(0,.1f,.60f,.79f);
            var orbit=camGO.AddComponent<ControlStudioOrbitCamera>();orbit.router=router;
            orbit.axisShader=Shader.Find("Universal Render Pipeline/Unlit");
            var ui=host.AddComponent<ControlStudioRuntimeUI>();ui.router=router;ui.manual=manual;ui.profiles=profiles;ui.orbit=orbit;
            var light=new GameObject("Studio Key Light").AddComponent<Light>();light.type=LightType.Directional;light.intensity=2.3f;light.transform.rotation=Quaternion.Euler(35,-35,0);
            var fill=new GameObject("Studio Fill Light").AddComponent<Light>();fill.type=LightType.Directional;fill.intensity=1.2f;fill.transform.rotation=Quaternion.Euler(30,130,0);
            RenderSettings.ambientMode=UnityEngine.Rendering.AmbientMode.Flat;RenderSettings.ambientLight=new Color(.6f,.65f,.7f);
            EditorSceneManager.SaveScene(scene,Destination);
            Directory.CreateDirectory("Validation/SingleArmControl");
            File.WriteAllText("Validation/SingleArmControl/scene-build.txt","PASS: one robot; all cloned component references belong to Demo_07; preserved hierarchy; no external input writer.\n");
        }
        finally{EditorSceneManager.ClosePreviewScene(src);}
    }
    static void RestoreTransforms(Transform original,Transform copy)
    {
        if(original.name!=copy.name||original.childCount!=copy.childCount)throw new InvalidOperationException("Clone hierarchy differs");
        copy.localPosition=original.localPosition;copy.localRotation=original.localRotation;copy.localScale=original.localScale;
        for(int i=0;i<original.childCount;i++)RestoreTransforms(original.GetChild(i),copy.GetChild(i));
    }
    [MenuItem("Tools/Control Studio/2. Run Editor Play acceptance")]
    public static void PlayAcceptance()
    {
        if(SceneManager.GetActiveScene().path!=Destination||SceneManager.GetActiveScene().isDirty||EditorApplication.isPlayingOrWillChangePlaymode)throw new InvalidOperationException("Requires clean Demo_07 edit mode");
        SessionState.SetBool("ControlStudioAcceptance",true);EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod]
    static void RegisterAcceptance()
    {
        EditorApplication.playModeStateChanged+=s=>
        {
            if(s==PlayModeStateChange.EnteredPlayMode&&SessionState.GetBool("ControlStudioAcceptance",false))
            {SessionState.SetBool("ControlStudioAcceptance",false);new GameObject("Explicit Control Studio Acceptance").AddComponent<ControlStudioAcceptance>();}
            if(s==PlayModeStateChange.EnteredPlayMode&&SessionState.GetBool("CsvStudioAcceptance",false))
            {SessionState.SetBool("CsvStudioAcceptance",false);new GameObject("Explicit CSV Studio Acceptance").AddComponent<CsvStudioAcceptance>();}
        };
    }
    [MenuItem("Tools/Control Studio/4. Run CSV and Manual regression")]
    public static void CsvAcceptance()
    {
        if(SceneManager.GetActiveScene().path!=Destination||SceneManager.GetActiveScene().isDirty||EditorApplication.isPlayingOrWillChangePlaymode)throw new InvalidOperationException("Requires clean Demo_07 edit mode");
        SessionState.SetBool("CsvStudioAcceptance",true);EditorApplication.isPlaying=true;
    }
    [MenuItem("Tools/Control Studio/3. Build Windows Player")]
    public static void BuildPlayer()
    {
        var current=SceneManager.GetActiveScene();
        if(EditorApplication.isPlayingOrWillChangePlaymode||current.path!=Destination||current.isDirty)throw new InvalidOperationException("Build requires clean Demo_07 edit mode");
        // ExecuteAlways linkage previews can update connecting-rod rotations in edit mode.
        // Save the cloned source snapshot, not incidental preview output.
        var preview=EditorSceneManager.OpenPreviewScene(Source);
        try{RestoreTransforms(preview.GetRootGameObjects().Single(g=>g.name=="RobotArm_Physical").transform,current.GetRootGameObjects().Single(g=>g.name=="RobotArm_Physical").transform);EditorSceneManager.SaveScene(current);}
        finally{EditorSceneManager.ClosePreviewScene(preview);}
        Directory.CreateDirectory("Builds/SingleArmControl");
        var preserve=new[]{"Assets/Settings/DefaultVolumeProfile.asset","Assets/Settings/PC_RPAsset.asset","Assets/Settings/UniversalRenderPipelineGlobalSettings.asset","ProjectSettings/ProjectSettings.asset","ProjectSettings/UnityConnectSettings.asset"}.ToDictionary(p=>p,File.ReadAllBytes);
        try
        {
            var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{Destination},locationPathName="Builds/SingleArmControl/SingleArmControl.exe",target=BuildTarget.StandaloneWindows64,options=BuildOptions.None});
            string reportDirectory=Directory.Exists("Validation/SingleArmControlCsv")?"Validation/SingleArmControlCsv":"Validation/SingleArmControl";
            File.WriteAllText(reportDirectory+"/player-build.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings);
            if(report.summary.result!=BuildResult.Succeeded)throw new Exception("Player build failed");
        }
        finally{foreach(var p in preserve)if(!File.ReadAllBytes(p.Key).SequenceEqual(p.Value))File.WriteAllBytes(p.Key,p.Value);AssetDatabase.Refresh();}
    }
    [MenuItem("Tools/Control Studio/Bind Demo 07 display shader")]
    public static void BindDisplayShader()
    {
        var scene=SceneManager.GetActiveScene();
        if(EditorApplication.isPlayingOrWillChangePlaymode||scene.path!=Destination||scene.isDirty)throw new InvalidOperationException("Requires clean Demo_07 edit mode");
        var camera=UnityEngine.Object.FindFirstObjectByType<ControlStudioOrbitCamera>();camera.axisShader=Shader.Find("Universal Render Pipeline/Unlit");
        if(camera.axisShader==null)throw new InvalidOperationException("Axis shader unavailable");
        EditorSceneManager.SaveScene(scene);
    }
}
