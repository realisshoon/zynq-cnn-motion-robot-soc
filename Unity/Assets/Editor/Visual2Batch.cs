using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEditor.Build.Reporting;
using UnityEngine;
using HumanMotion.ControlStudio;
public static class Visual2Batch
{
    const string Key="ExplicitVisual2Batch";
    static bool commonComplete;
    public static void Run()
    {
        if(!Application.isBatchMode)throw new Exception("Isolated batch only");
        Directory.CreateDirectory("Validation/Visual2DualCell");Directory.CreateDirectory("Validation/Tool1W");
        EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        Tool1AssetBuilder.BuildAssets();RobotVisual2Builder.BuildAssets();
        commonComplete=false;SessionState.SetBool(Key,true);SessionState.SetBool(Key+"Build",false);EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod]static void Register()
    {
        EditorApplication.playModeStateChanged+=state=>{
            if(!SessionState.GetBool(Key,false))return;
            if(state==PlayModeStateChange.EnteredPlayMode)new GameObject("Explicit TOOL-1W regression").AddComponent<Tool1Acceptance>();
            if(state==PlayModeStateChange.EnteredEditMode&&SessionState.GetBool(Key+"Build",false))EditorApplication.delayCall+=Build;
        };
        EditorApplication.update+=()=>{
            if(!SessionState.GetBool(Key,false)||!EditorApplication.isPlaying)return;
            if(!commonComplete){
                var common=UnityEngine.Object.FindFirstObjectByType<Tool1Acceptance>();
                if(common==null||!common.Complete)return;
                if(!common.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}
                commonComplete=true;UnityEngine.Object.Destroy(common.gameObject);
                new GameObject("Explicit source-reuse acceptance").AddComponent<Visual2Acceptance>();
                return;
            }
            var test=UnityEngine.Object.FindFirstObjectByType<Visual2Acceptance>();
            if(test==null||!test.Complete)return;
            if(!test.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);}
            else{SessionState.SetBool(Key+"Build",true);EditorApplication.isPlaying=false;}
        };
    }
    static void Build()
    {
        SessionState.SetBool(Key,false);
        try{
            Directory.CreateDirectory("Builds/SingleArmControlVisual2DualCell");
            var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName="Builds/SingleArmControlVisual2DualCell/SingleArmControl.exe",target=BuildTarget.StandaloneWindows64});
            File.WriteAllText("Validation/Visual2DualCell/build-results.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings);
            EditorApplication.Exit(report.summary.result==BuildResult.Succeeded?0:3);
        }catch(Exception e){File.WriteAllText("Validation/Visual2DualCell/build-results.txt",e.ToString());EditorApplication.Exit(3);}
    }
}
