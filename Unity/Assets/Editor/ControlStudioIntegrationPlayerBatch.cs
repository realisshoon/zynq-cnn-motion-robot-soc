using System;
using System.IO;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

// Windows Player validation build. Invoke only against the disposable validation copy.
public static class ControlStudioIntegrationPlayerBatch
{
    const string Scene = "Assets/Scenes/Demo_07_SingleArmControl.unity";
    [Serializable] sealed class Result
    {
        public string result, unity, project, executable, backend, hardware;
        public int errors, warnings;
        public ulong bytes;
    }
    static string Argument(string name)
    {
        var args = Environment.GetCommandLineArgs(); int index = Array.IndexOf(args, name);
        if (index < 0) return null;
        if (index + 1 == args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal)) throw new ArgumentException("Missing " + name + " value");
        return args[index + 1];
    }
    public static void Build()
    {
        string project = ControlStudioBatchPaths.Root;
        bool disposable = project.IndexOf("/Tools/IntegrationValidation/build/", StringComparison.OrdinalIgnoreCase) >= 0
            || project.StartsWith("D:/ControlStudioValidation/Unity_", StringComparison.OrdinalIgnoreCase);
        if (!ControlStudioBatchPaths.Allowed || !disposable)
            throw new InvalidOperationException("--isolated-validation and disposable validation project required");
        if (!BuildPipeline.IsBuildTargetSupported(BuildTargetGroup.Standalone, BuildTarget.StandaloneWindows64))
            throw new InvalidOperationException("Windows x64 build support module required");
        if (!File.Exists(Scene)) throw new FileNotFoundException("Demo_07 scene required", Scene);
        string output = Path.GetFullPath(Argument("--player-output") ?? Path.Combine(project, "Validation/WindowsPlayer"));
        Directory.CreateDirectory(output); string executable = Path.Combine(output, "ControlStudioIntegration.exe");
        var target = NamedBuildTarget.Standalone;
        var previousBackend = PlayerSettings.GetScriptingBackend(target);
        BuildReport report;
        try {
            // This validation covers the deployed Mono P/Invoke path, separately from Editor.
            PlayerSettings.SetScriptingBackend(target, ScriptingImplementation.Mono2x);
            report = BuildPipeline.BuildPlayer(new BuildPlayerOptions {
                scenes = new[] { Scene }, locationPathName = executable,
                target = BuildTarget.StandaloneWindows64, targetGroup = BuildTargetGroup.Standalone,
                options = BuildOptions.Development
            });
        } finally { PlayerSettings.SetScriptingBackend(target, previousBackend); }
        var result = new Result {
            result = report.summary.result.ToString(), unity = Application.unityVersion, project = project,
            executable = executable, backend = "Mono / Windows x64", hardware = "none",
            errors = report.summary.totalErrors, warnings = report.summary.totalWarnings, bytes = report.summary.totalSize
        };
        File.WriteAllText(Path.Combine(output, "build-result.json"), JsonUtility.ToJson(result, true));
        if (report.summary.result != BuildResult.Succeeded || !File.Exists(executable))
            throw new Exception("Windows Player build failed: " + report.summary.result + ", errors=" + report.summary.totalErrors);
        // Ensure the newest libraries actually ship in the built Player.
        string plugins = Path.Combine(output, "ControlStudioIntegration_Data/Plugins/x86_64");
        foreach (string name in new[] { "control_studio_right", "control_studio_left", "control_studio_xyz_integration" })
            if (!File.Exists(Path.Combine(plugins, name + ".dll"))) throw new FileNotFoundException("Required Player plugin missing", Path.Combine(plugins, name + ".dll"));
        Debug.Log("PLAYER BUILD PASS unity=" + Application.unityVersion + " executable=" + executable + " hardware=none");
        EditorApplication.Exit(0);
    }
}
