using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEditor.Build.Reporting;
using UnityEngine;
using HumanMotion.ControlStudio;
public static class VisualProfileBatch
{
    const string Key="ExplicitVisualProfileBatch";
    public static void Run(){if(!Application.isBatchMode)throw new Exception("Isolated batch only");Directory.CreateDirectory("Validation/VisualProfiles");Directory.CreateDirectory("Validation/Tool1W");EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");RobotVisualProfileBuilder.BuildAssets();SessionState.SetBool(Key,true);SessionState.SetBool(Key+"Build",false);EditorApplication.isPlaying=true;}
    [InitializeOnLoadMethod]static void Register()
    {
        EditorApplication.playModeStateChanged+=s=>{if(!SessionState.GetBool(Key,false))return;if(s==PlayModeStateChange.EnteredPlayMode)new GameObject("Existing Tool regression").AddComponent<Tool1Acceptance>();if(s==PlayModeStateChange.EnteredEditMode&&SessionState.GetBool(Key+"Build",false))EditorApplication.delayCall+=Build;};
        EditorApplication.update+=()=>{if(!SessionState.GetBool(Key,false)||!EditorApplication.isPlaying)return;var old=UnityEngine.Object.FindFirstObjectByType<Tool1Acceptance>();var test=UnityEngine.Object.FindFirstObjectByType<VisualProfileAcceptance>();if(old!=null&&old.Complete){if(!old.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}if(test==null)new GameObject("Explicit visual profile tests").AddComponent<VisualProfileAcceptance>();}if(test!=null&&test.Complete){if(!test.Passed){SessionState.SetBool(Key,false);EditorApplication.Exit(2);}else{SessionState.SetBool(Key+"Build",true);EditorApplication.isPlaying=false;}}};
    }
    static void Build(){SessionState.SetBool(Key,false);try{Directory.CreateDirectory("Builds/SingleArmControlVisualProfiles");var result=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName="Builds/SingleArmControlVisualProfiles/SingleArmControl.exe",target=BuildTarget.StandaloneWindows64});File.WriteAllText("Validation/VisualProfiles/build-results.txt",result.summary.result+" errors="+result.summary.totalErrors+" warnings="+result.summary.totalWarnings);EditorApplication.Exit(result.summary.result==BuildResult.Succeeded?0:3);}catch(Exception e){File.WriteAllText("Validation/VisualProfiles/build-results.txt",e.ToString());EditorApplication.Exit(3);}}
}
