using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEditor.Build.Reporting;
using UnityEngine;
using HumanMotion.ControlStudio;
public static class ControlStudioXyzBatch
{
    const string Key="StudioXyzExplicitBatch";
    public static void Run()
    {
        if(!Application.isBatchMode)throw new InvalidOperationException("Use isolated batch invocation only");
        SessionState.SetBool(Key,true);SessionState.SetBool(Key+"Build",false);
        EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod] static void Register()
    {
        EditorApplication.playModeStateChanged+=state=>{
            if(!SessionState.GetBool(Key,false))return;
            if(state==PlayModeStateChange.EnteredPlayMode)new GameObject("Explicit XYZ acceptance").AddComponent<XyzStudioAcceptance>();
            if(state==PlayModeStateChange.EnteredEditMode&&SessionState.GetBool(Key+"Build",false))EditorApplication.delayCall+=Build;
        };
        EditorApplication.update+=()=>{
            if(!SessionState.GetBool(Key,false)||!EditorApplication.isPlaying)return;
            var test=UnityEngine.Object.FindFirstObjectByType<XyzStudioAcceptance>();
            if(test!=null&&test.Complete){if(!test.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}SessionState.SetBool(Key+"Build",true);EditorApplication.isPlaying=false;}
        };
    }
    static void Build()
    {
        SessionState.SetBool(Key,false);
        try{
            // Build only the saved Demo_07; no Create/Builder, no scene save or pivot mutation.
            Directory.CreateDirectory("Builds/SingleArmControl");
            var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName="Builds/SingleArmControl/SingleArmControl.exe",target=BuildTarget.StandaloneWindows64,options=BuildOptions.None});
            File.WriteAllText("Validation/SingleArmControlXyz/build-results.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings);
            EditorApplication.Exit(report.summary.result==BuildResult.Succeeded?0:3);
        }catch(Exception e){File.WriteAllText("Validation/SingleArmControlXyz/build-results.txt",e.ToString());EditorApplication.Exit(3);}
    }
}
