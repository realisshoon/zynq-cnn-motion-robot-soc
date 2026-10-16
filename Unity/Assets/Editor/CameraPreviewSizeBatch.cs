using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;

// Build verification only; no change to scene or command policy.
public static class CameraPreviewSizeBatch
{
    [Serializable] public sealed class BuildRequest { public string project; public string output; }
    [Serializable] public sealed class BuildResultInfo
    {
        public string result,project,output,unity,scene,target,message;
        public int errors,warnings;public bool development;
    }
    static string RequestFolder=>Path.Combine(ControlStudioBatchPaths.Root,"Library/CameraPreviewBuild");
    // No request file means no action. This permits an explicitly requested build
    // in the already-open original Editor without closing it or entering Play.
    [InitializeOnLoadMethod] static void WatchExplicitBuildRequest()=>EditorApplication.update+=ConsumeRequest;
    static void ConsumeRequest()
    {
        if(Application.isBatchMode||EditorApplication.isPlayingOrWillChangePlaymode||EditorApplication.isCompiling||EditorApplication.isUpdating||BuildPipeline.isBuildingPlayer)return;
        string requestPath=Path.Combine(RequestFolder,"request.json");
        if(!File.Exists(requestPath))return;
        try {
            var request=JsonUtility.FromJson<BuildRequest>(File.ReadAllText(requestPath));
            File.Move(requestPath,Path.Combine(RequestFolder,"request_accepted_"+DateTime.UtcNow.ToString("yyyyMMdd_HHmmssfff")+".json"));
            if(request==null||!string.Equals(Path.GetFullPath(request.project),Path.GetFullPath(ControlStudioBatchPaths.Root),StringComparison.OrdinalIgnoreCase))throw new Exception("Build request project does not match this Editor");
            BuildOriginal(request.output);
        }catch(Exception e){
            Directory.CreateDirectory(RequestFolder);
            File.WriteAllText(Path.Combine(RequestFolder,"result.json"),JsonUtility.ToJson(new BuildResultInfo{result="Failed",project=ControlStudioBatchPaths.Root,message=e.ToString()},true));
            Debug.LogException(e);
        }
    }
    // CLI alternative when the original Editor is closed.
    public static void BuildLatest()
    {
        var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--camera-player-output");
        if(i<0||i+1>=args.Length)throw new Exception("Explicit --camera-player-output required");
        bool success=BuildOriginal(args[i+1]);
        if(Application.isBatchMode)EditorApplication.Exit(success?0:2);
    }
    static bool BuildOriginal(string output)
    {
        if(EditorApplication.isPlayingOrWillChangePlaymode)throw new Exception("Exit Play and release COM before building");
        if(Application.unityVersion!="6000.3.24f1")throw new Exception("Unity 6000.3.24f1 required");
        const string scene="Assets/Scenes/Demo_07_SingleArmControl.unity";
        var enabled=EditorBuildSettings.scenes.Where(s=>s.enabled).Select(s=>s.path).ToArray();
        if(enabled.Length!=1||enabled[0]!=scene)throw new Exception("Only Demo_07 must be enabled; Main must remain disabled");
        string path=Path.GetFullPath(output),folder=Path.GetDirectoryName(path);
        if(Path.GetExtension(path)!=".exe"||!Path.IsPathRooted(output))throw new Exception("An absolute EXE output path is required");
        if(Directory.Exists(folder)&&Directory.EnumerateFileSystemEntries(folder).Any())throw new Exception("Use a fresh output folder; existing builds are never overwritten");
        Directory.CreateDirectory(folder);Directory.CreateDirectory(RequestFolder);
        File.WriteAllText(Path.Combine(RequestFolder,"result.json"),JsonUtility.ToJson(new BuildResultInfo{result="Building",project=ControlStudioBatchPaths.Root,output=path},true));
        var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{scene},locationPathName=path,target=BuildTarget.StandaloneWindows64,options=BuildOptions.CleanBuildCache});
        var info=new BuildResultInfo{result=report.summary.result.ToString(),project=ControlStudioBatchPaths.Root,output=path,unity=Application.unityVersion,scene=scene,target="StandaloneWindows64",errors=report.summary.totalErrors,warnings=report.summary.totalWarnings,development=(report.summary.options&BuildOptions.Development)!=0};
        string json=JsonUtility.ToJson(info,true);
        File.WriteAllText(Path.Combine(folder,"build_result.json"),json);
        File.WriteAllText(Path.Combine(RequestFolder,"result.json"),json);
        File.WriteAllLines(Path.Combine(folder,"build_messages.txt"),report.steps.SelectMany(s=>s.messages).Where(m=>m.type==LogType.Warning||m.type==LogType.Error||m.type==LogType.Exception).Select(m=>m.type+": "+m.content));
        return report.summary.result==BuildResult.Succeeded;
    }
    public static void Build()
    {
        string main=ControlStudioBatchPaths.Root;
        if(!ControlStudioBatchPaths.Allowed)throw new Exception("Isolated validation project only");
        string path=main+"/Builds/SingleArmControlCameraSizes/SingleArmControl.exe";
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName=path,target=BuildTarget.StandaloneWindows64});
        File.WriteAllText(main+"/Validation/CameraPreviewSizes/build_result.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings+" output="+path);
        EditorApplication.Exit(report.summary.result==UnityEditor.Build.Reporting.BuildResult.Succeeded?0:2);
    }
}
