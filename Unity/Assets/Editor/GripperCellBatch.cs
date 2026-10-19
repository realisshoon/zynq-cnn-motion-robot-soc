using System;
using System.IO;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;

// Explicit isolated Editor validation and Windows build. Never launches the Player.
public static class GripperCellBatch
{
    const string Key="ExplicitGripperCellBatch";
    static int stage;
    public static void Run()
    {
        if(!Application.isBatchMode)throw new Exception("Isolated batch only");
        Directory.CreateDirectory("Validation/GripperCell");Directory.CreateDirectory("Validation/Tool1W");Directory.CreateDirectory("Validation/Visual2DualCell");
        EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        Tool1AssetBuilder.BuildAssets();RobotVisual2Builder.BuildAssets();
        stage=0;SessionState.SetBool(Key,true);SessionState.SetBool(Key+"Build",false);EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod]static void Register()
    {
        EditorApplication.playModeStateChanged+=state=>{
            if(!SessionState.GetBool(Key,false))return;
            if(state==PlayModeStateChange.EnteredPlayMode)new GameObject("Existing Tool gate regression").AddComponent<Tool1Acceptance>();
            if(state==PlayModeStateChange.EnteredEditMode&&SessionState.GetBool(Key+"Build",false))EditorApplication.delayCall+=Build;
        };
        EditorApplication.update+=()=>{
            if(!SessionState.GetBool(Key,false)||!EditorApplication.isPlaying)return;
            if(stage==0){
                var test=UnityEngine.Object.FindFirstObjectByType<Tool1Acceptance>();if(test==null||!test.Complete)return;
                if(!test.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}
                stage=1;UnityEngine.Object.Destroy(test.gameObject);new GameObject("Gripper Cell acceptance").AddComponent<GripperCellAcceptance>();return;
            }
            if(stage==1){
                var test=UnityEngine.Object.FindFirstObjectByType<GripperCellAcceptance>();if(test==null||!test.Complete)return;
                if(!test.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}
                stage=2;UnityEngine.Object.Destroy(test.gameObject);new GameObject("Existing Visual2 command regression").AddComponent<Visual2Acceptance>();return;
            }
            var old=UnityEngine.Object.FindFirstObjectByType<Visual2Acceptance>();if(old==null||!old.Complete)return;
            if(!old.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}
            SessionState.SetBool(Key+"Build",true);EditorApplication.isPlaying=false;
        };
    }
    static void Build()
    {
        SessionState.SetBool(Key,false);
        try{
            Directory.CreateDirectory("Builds/SingleArmControlGripperCell");
            var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName="Builds/SingleArmControlGripperCell/SingleArmControl.exe",target=BuildTarget.StandaloneWindows64});
            File.WriteAllText("Validation/GripperCell/build-results.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings);
            EditorApplication.Exit(report.summary.result==BuildResult.Succeeded?0:3);
        }catch(Exception e){File.WriteAllText("Validation/GripperCell/build-results.txt",e.ToString());EditorApplication.Exit(3);}
    }
}
