using System;
using System.IO;
using UnityEditor;
using UnityEngine;

// Build verification only; no change to scene or command policy.
public static class CameraPreviewSizeBatch
{
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
