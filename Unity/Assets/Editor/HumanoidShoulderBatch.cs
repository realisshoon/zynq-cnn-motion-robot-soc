using System;
using System.IO;
using System.Reflection;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Explicit isolated Editor validation, never loaded into the user's working Editor session.
public static class HumanoidShoulderBatch
{
    const string Key="HandoffShoulderPlay";static HumanoidShoulderAcceptance test;
    static string Output {get{var a=Environment.GetCommandLineArgs();int i=Array.IndexOf(a,"--handoff-output");return i>=0?a[i+1]:Path.Combine(ControlStudioBatchPaths.Root,"Validation/HumanoidShoulder");}}
    public static void Run()
    {
        if(!ControlStudioBatchPaths.Allowed)throw new Exception("Use --isolated-validation");
        Directory.CreateDirectory(Output);EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        SessionState.SetBool(Key,true);SessionState.SetString(Key+"time",DateTime.UtcNow.ToString("O"));EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod]static void Hook(){EditorApplication.update+=Tick;}
    static void Tick()
    {
        if(!SessionState.GetBool(Key,false))return;
        if(DateTime.UtcNow-DateTime.Parse(SessionState.GetString(Key+"time",""))>TimeSpan.FromMinutes(5)){File.WriteAllText(Path.Combine(Output,"timeout.txt"),"FAIL timeout");SessionState.SetBool(Key,false);EditorApplication.Exit(2);return;}
        if(!EditorApplication.isPlaying||EditorApplication.isCompiling)return;
        var router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();if(router==null||router.GetComponent<RobotControlMockPanel>()==null)return;
        if(test==null){
            test=new GameObject("Explicit shoulder validation").AddComponent<HumanoidShoulderAcceptance>();test.output=Output;
            test.EditorCapture=name=>{
                ControlStudioStartupMenuBatch.ValidationOutput=Output;
                var flags=BindingFlags.Static|BindingFlags.NonPublic;
                foreach(var pair in new[]{("router",(object)router),("ui",(object)router.GetComponent<ControlStudioRuntimeUI>()),("menu",(object)router.GetComponent<ControlStudioStartupMenu>())})typeof(ControlStudioStartupMenuBatch).GetField(pair.Item1,flags).SetValue(null,pair.Item2);
                bool narrow=name.Contains("1280");typeof(ControlStudioStartupMenuBatch).GetMethod("Capture",flags).Invoke(null,new object[]{name,narrow?1280:1920,narrow?720:1080});
            };
        }
        if(!test.Complete)return;SessionState.SetBool(Key,false);EditorApplication.Exit(test.Passed?0:2);
    }
}
