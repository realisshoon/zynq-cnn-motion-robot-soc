using System;
using System.IO;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;

// Explicit, isolated Editor validation and Windows build. The Player is not launched here.
public static class PresentationBatch
{
    const string Key="ExplicitPresentationBatch";
    static int stage;
    public static void Run()
    {
        if(!Application.isBatchMode)throw new Exception("Isolated batch only");
        Directory.CreateDirectory("Validation/PresentationCapture");
        Directory.CreateDirectory("Validation/Visual2DualCell");
        Directory.CreateDirectory("Validation/Tool1W");
        EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        Tool1AssetBuilder.BuildAssets();RobotVisual2Builder.BuildAssets();
        stage=0;SessionState.SetBool(Key,true);SessionState.SetBool(Key+"Build",false);EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod]static void Register()
    {
        EditorApplication.playModeStateChanged+=state=>{
            if(!SessionState.GetBool(Key,false))return;
            if(state==PlayModeStateChange.EnteredPlayMode)new GameObject("Presentation acceptance").AddComponent<PresentationAcceptance>();
            if(state==PlayModeStateChange.EnteredEditMode&&SessionState.GetBool(Key+"Build",false))EditorApplication.delayCall+=Build;
        };
        EditorApplication.update+=()=>{
            if(!SessionState.GetBool(Key,false)||!EditorApplication.isPlaying)return;
            if(stage==0){
                var test=UnityEngine.Object.FindFirstObjectByType<PresentationAcceptance>();if(test==null||!test.Complete)return;
                if(!test.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}
                stage=1;UnityEngine.Object.Destroy(test.gameObject);
                new GameObject("Existing command regression").AddComponent<Visual2Acceptance>();return;
            }
            var regression=UnityEngine.Object.FindFirstObjectByType<Visual2Acceptance>();
            if(regression==null||!regression.Complete)return;
            if(!regression.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}
            SessionState.SetBool(Key+"Build",true);EditorApplication.isPlaying=false;
        };
    }
    static void Build()
    {
        SessionState.SetBool(Key,false);
        try{
            Directory.CreateDirectory("Builds/SingleArmControlPresentation");
            var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{
                scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},
                locationPathName="Builds/SingleArmControlPresentation/SingleArmControl.exe",
                target=BuildTarget.StandaloneWindows64});
            File.WriteAllText("Validation/PresentationCapture/build-results.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings);
            EditorApplication.Exit(report.summary.result==BuildResult.Succeeded?0:3);
        }catch(Exception e){File.WriteAllText("Validation/PresentationCapture/build-results.txt",e.ToString());EditorApplication.Exit(3);}
    }
}
