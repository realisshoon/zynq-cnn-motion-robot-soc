using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEditor.Build.Reporting;
using UnityEngine;
using HumanMotion.ControlStudio;
public static class Visual1Batch
{
    const string Key="ExplicitVisual1Batch";
    public static void Run()
    {
        if(!Application.isBatchMode)throw new Exception("Isolated batch only");
        Directory.CreateDirectory("Validation/Visual1");Directory.CreateDirectory("Validation/Tool1W");
        EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        RobotVisualProfileBuilder.BuildAssets();SessionState.SetBool(Key,true);SessionState.SetBool(Key+"Build",false);SessionState.SetBool(Key+"VisualOnly",false);EditorApplication.isPlaying=true;
    }
    public static void RunVisualOnly()
    {
        if(!Application.isBatchMode)throw new Exception("Isolated batch only");
        Directory.CreateDirectory("Validation/Visual1");
        EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        RobotVisualProfileBuilder.BuildAssets();SessionState.SetBool(Key,true);SessionState.SetBool(Key+"Build",false);
        SessionState.SetBool(Key+"VisualOnly",true);EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod]static void Register()
    {
        EditorApplication.playModeStateChanged+=state=>{
            if(!SessionState.GetBool(Key,false))return;
            if(state==PlayModeStateChange.EnteredPlayMode){
                if(SessionState.GetBool(Key+"VisualOnly",false))new GameObject("Explicit Visual1 tests").AddComponent<Visual1Acceptance>();
                else new GameObject("Existing Tool regression").AddComponent<Tool1Acceptance>();
            }
            if(state==PlayModeStateChange.EnteredEditMode&&SessionState.GetBool(Key+"Build",false))EditorApplication.delayCall+=Build;
        };
        EditorApplication.update+=()=>{
            if(!SessionState.GetBool(Key,false)||!EditorApplication.isPlaying)return;
            var old=UnityEngine.Object.FindFirstObjectByType<Tool1Acceptance>();var test=UnityEngine.Object.FindFirstObjectByType<Visual1Acceptance>();
            if(old!=null&&old.Complete){
                if(!old.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}
                if(test==null)new GameObject("Explicit Visual1 tests").AddComponent<Visual1Acceptance>();
            }
            if(test!=null&&test.Complete){
                if(!test.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);}
                else{SessionState.SetBool(Key+"Build",true);EditorApplication.isPlaying=false;}
            }
        };
    }
    static void Build()
    {
        SessionState.SetBool(Key,false);SessionState.SetBool(Key+"VisualOnly",false);
        try{
            Directory.CreateDirectory("Builds/SingleArmControlVisual1");
            var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName="Builds/SingleArmControlVisual1/SingleArmControl.exe",target=BuildTarget.StandaloneWindows64});
            File.WriteAllText("Validation/Visual1/build-results.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings);
            EditorApplication.Exit(report.summary.result==BuildResult.Succeeded?0:3);
        }catch(Exception e){File.WriteAllText("Validation/Visual1/build-results.txt",e.ToString());EditorApplication.Exit(3);}
    }
}
