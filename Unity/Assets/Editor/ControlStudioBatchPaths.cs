using System;
using System.IO;
using UnityEngine;

// No developer workstation path. Opt in only in a disposable validation project.
public static class ControlStudioBatchPaths
{
    public static string Root => Path.GetDirectoryName(Application.dataPath).Replace('\\', '/');
    public static bool Allowed => Application.isBatchMode && Array.IndexOf(Environment.GetCommandLineArgs(), "--isolated-validation") >= 0;
}
