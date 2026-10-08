using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEditor.Build.Reporting;
using UnityEngine;
using HumanMotion.ControlStudio;
public static class Tool1Batch
{
    const string Key="ExplicitTool1WBatch";
    public static void Run(){if(!Application.isBatchMode)throw new Exception("Isolated batch only");Directory.CreateDirectory("Validation/Tool1W");Tool1AssetBuilder.BuildAssets();SessionState.SetBool(Key,true);SessionState.SetBool(Key+"Build",false);EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");EditorApplication.isPlaying=true;}
    [InitializeOnLoadMethod] static void Register()
    {
        EditorApplication.playModeStateChanged+=state=>{if(!SessionState.GetBool(Key,false))return;if(state==PlayModeStateChange.EnteredPlayMode)new GameObject("Explicit TOOL-1 tests").AddComponent<Tool1Acceptance>();if(state==PlayModeStateChange.EnteredEditMode&&SessionState.GetBool(Key+"Build",false))EditorApplication.delayCall+=Build;};
        EditorApplication.update+=()=>{if(!SessionState.GetBool(Key,false)||!EditorApplication.isPlaying)return;var test=UnityEngine.Object.FindFirstObjectByType<Tool1Acceptance>();if(test!=null&&test.Complete){if(!test.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);}else{SessionState.SetBool(Key+"Build",true);EditorApplication.isPlaying=false;}}};
    }
    static void Build(){SessionState.SetBool(Key,false);try{Directory.CreateDirectory("Builds/SingleArmControlToolsWelding");var result=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName="Builds/SingleArmControlToolsWelding/SingleArmControl.exe",target=BuildTarget.StandaloneWindows64});File.WriteAllText("Validation/Tool1W/build-results.txt",result.summary.result+" errors="+result.summary.totalErrors+" warnings="+result.summary.totalWarnings);EditorApplication.Exit(result.summary.result==BuildResult.Succeeded?0:3);}catch(Exception e){File.WriteAllText("Validation/Tool1W/build-results.txt",e.ToString());EditorApplication.Exit(3);}}
}
