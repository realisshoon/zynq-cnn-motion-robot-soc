using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;

// Explicit disposable-clone validation only. Does not alter controller, rig or scene.
public static class HandoffReproValidation
{
    const string Scene="Assets/Scenes/Demo_07_SingleArmControl.unity";
    const string Key="HandoffXyzPlay";
    static XyzStudioAcceptance xyz;
    static double began;
    static string Output {
        get {
            var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--handoff-output");
            return i>=0&&i+1<args.Length?args[i+1]:Path.Combine(ControlStudioBatchPaths.Root,"Validation/HandoffRepro");
        }
    }
    static void Guard(){if(!ControlStudioBatchPaths.Allowed)throw new Exception("Use a disposable clone and --isolated-validation");Directory.CreateDirectory(Output);}
    public static void AssetsAndBuild()
    {
        Guard();var log=new List<string>();
        Action<bool,string> check=(ok,note)=>{log.Add((ok?"PASS ":"FAIL ")+note);if(!ok)throw new Exception(note);};
        try {
            check(Application.unityVersion=="6000.3.24f1","Unity 6000.3.24f1");
            check(!EditorUtility.scriptCompilationFailed,"C# compilation");
            var enabled=EditorBuildSettings.scenes.Where(s=>s.enabled).Select(s=>s.path).ToArray();
            check(enabled.Length>0&&enabled[0]==Scene,"Build Settings launches Demo07");
            check(GraphicsSettings.defaultRenderPipeline!=null&&GraphicsSettings.defaultRenderPipeline.GetType().Name=="UniversalRenderPipelineAsset","URP retained");
            var profile=AssetDatabase.LoadAssetAtPath<VolumeProfile>("Assets/Settings/DefaultVolumeProfile.asset");
            check(profile!=null&&profile.components.All(c=>c!=null),"default volume has no missing components");
            log.Add("Volume components="+profile.components.Count);
            var scene=EditorSceneManager.OpenScene(Scene);
            check(scene.GetRootGameObjects().Sum(g=>g.GetComponentsInChildren<Transform>(true).Sum(t=>GameObjectUtility.GetMonoBehavioursWithMissingScriptCount(t.gameObject)))==0,"Demo07 missing scripts=0");
            foreach(string path in new[]{"Assets/Resources/VisualProfiles/HumanoidRobot.prefab","Assets/Resources/VisualProfiles/DualTable.prefab"}) {
                if(!File.Exists(path))continue;
                var go=AssetDatabase.LoadAssetAtPath<GameObject>(path);
                check(go!=null&&go.GetComponentsInChildren<Transform>(true).Sum(t=>GameObjectUtility.GetMonoBehavioursWithMissingScriptCount(t.gameObject))==0,path+" scripts");
            }
            using(var output=new ControlStudioOutputPolicy(SingleArmCommandRouter.Home)) {
                var q=new float[5];var v=new float[4];check(output.Submit(SingleArmCommandRouter.Home)==0&&output.Tick(q,v)==0,"ABI 2 native create/submit/tick");
            }
            File.WriteAllLines(Path.Combine(Output,"assets_result.txt"),log);
            string build=Path.Combine(Output,"Windows/SingleArmControl.exe");Directory.CreateDirectory(Path.GetDirectoryName(build));
            var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=enabled,locationPathName=build,target=BuildTarget.StandaloneWindows64,options=BuildOptions.None});
            File.WriteAllText(Path.Combine(Output,"build_result.txt"),report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings+" bytes="+report.summary.totalSize+" output="+build+" scenes="+string.Join(",",enabled));
            EditorApplication.Exit(report.summary.result==UnityEditor.Build.Reporting.BuildResult.Succeeded?0:2);
        }catch(Exception e){log.Add("FAIL "+e);File.WriteAllLines(Path.Combine(Output,"assets_result.txt"),log);EditorApplication.Exit(2);}
    }
    public static void RunXyz()
    {
        Guard();Directory.CreateDirectory("Validation/SingleArmControlXyz");EditorSceneManager.OpenScene(Scene);
        SessionState.SetBool(Key,true);SessionState.SetString(Key+"Start",DateTime.UtcNow.ToString("O"));EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod]static void Init(){EditorApplication.update+=Tick;}
    static void Tick()
    {
        if(!SessionState.GetBool(Key,false))return;
        if(DateTime.UtcNow-DateTime.Parse(SessionState.GetString(Key+"Start",""))>TimeSpan.FromMinutes(5)){
            File.WriteAllText(Path.Combine(Output,"xyz_result.txt"),"FAIL timeout");SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;
        }
        if(!EditorApplication.isPlaying||EditorApplication.isCompiling)return;
        if(xyz==null){if(UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>()==null)return;xyz=new GameObject("Handoff explicit XYZ regression").AddComponent<XyzStudioAcceptance>();began=EditorApplication.timeSinceStartup;}
        if(!xyz.Complete)return;
        File.Copy("Validation/SingleArmControlXyz/editor-results.txt",Path.Combine(Output,"xyz_result.txt"),true);
        SessionState.SetBool(Key,false);EditorApplication.Exit(xyz.Passed?0:2);
    }
}
