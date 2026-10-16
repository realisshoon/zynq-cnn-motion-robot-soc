using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEngine;

// Explicit no-device check of the actual managed serial backend, not a fake port.
// Enumeration does not open a port; the only Open target is an invalid non-COM name.
public static class ControlStudioSerialBatch
{
    const string InvalidPort = "ControlStudioValidation_InvalidPort_512706757b414bf8a86ee09a2c568852";
    static int checks;
    static readonly StringBuilder report = new StringBuilder();

    static void Need(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
        checks++;
        report.AppendLine("SERIAL PASS: " + message);
        Debug.Log("SERIAL PASS: " + message);
    }
    static bool ContainsPlatformFailure(Exception error)
    {
        for (var current = error; current != null; current = current.InnerException)
            if (current is PlatformNotSupportedException || current is DllNotFoundException || current is EntryPointNotFoundException)
                return true;
        return false;
    }
    static string Hash(string path)
    {
        using (var file = File.OpenRead(path))
        using (var sha = SHA256.Create())
            return BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "").ToLowerInvariant();
    }
    // Editor compiler references can include both framework System and the managed
    // plugin. Resolve the runtime assembly's own reference so this exercises the
    // backend actually selected for BoardUartTransport rather than another copy.
    static Type SerialType()
    {
        var references = typeof(BoardUartTransport).Assembly.GetReferencedAssemblies();
        var reference = references.FirstOrDefault(name => name.Name == "System.IO.Ports")
            ?? references.FirstOrDefault(name => name.Name == "System");
        if (reference == null) throw new Exception("Board transport serial assembly reference missing");
        return Assembly.Load(reference).GetType("System.IO.Ports.SerialPort", true);
    }
    static object Get(Type type, object instance, string name) => type.GetProperty(name).GetValue(instance);
    static void Set(Type type, object instance, string name, object value) => type.GetProperty(name).SetValue(instance, value);
    static object EnumValue(Assembly assembly, string name, string value) => Enum.Parse(assembly.GetType("System.IO.Ports." + name, true), value);
    public static void Run()
    {
        string root = ControlStudioBatchPaths.Root;
        bool disposable = root.IndexOf("/Tools/IntegrationValidation/build/", StringComparison.OrdinalIgnoreCase) >= 0
            || root.StartsWith("D:/ControlStudioValidation/Unity_", StringComparison.OrdinalIgnoreCase);
        if (!ControlStudioBatchPaths.Allowed || !disposable)
            throw new InvalidOperationException("--isolated-validation and disposable project required");
        string output = Path.Combine(root, "Validation/SerialBackend/result.txt");
        checks = 0;
        report.Clear();
        int exitCode = 0;
        try
        {
            Need(Application.platform == RuntimePlatform.WindowsEditor, "Windows Editor runtime");
            var serialType = SerialType();
            var assembly = serialType.Assembly;
            report.AppendLine("assembly=" + assembly.FullName);
            report.AppendLine("location=" + assembly.Location);
            report.AppendLine("sha256=" + Hash(assembly.Location));
            var names = (string[])serialType.GetMethod("GetPortNames", BindingFlags.Public | BindingFlags.Static).Invoke(null, null);
            Need(names != null, "real SerialPort.GetPortNames executes; enumerated=" + names.Length + "; opened=0");
            using (var port = (IDisposable)Activator.CreateInstance(serialType, new[]
            {
                InvalidPort, (object)921600, EnumValue(assembly, "Parity", "None"), 8, EnumValue(assembly, "StopBits", "One")
            }))
            {
                Set(serialType, port, "Handshake", EnumValue(assembly, "Handshake", "None"));
                Set(serialType, port, "DtrEnable", false); Set(serialType, port, "RtsEnable", false);
                Set(serialType, port, "ReadTimeout", 30); Set(serialType, port, "WriteTimeout", 250); Set(serialType, port, "ReadBufferSize", 65536);
                Need((string)Get(serialType, port, "PortName") == InvalidPort && (int)Get(serialType, port, "BaudRate") == 921600
                    && (int)Get(serialType, port, "DataBits") == 8 && Get(serialType, port, "Parity").ToString() == "None"
                    && Get(serialType, port, "StopBits").ToString() == "One" && Get(serialType, port, "Handshake").ToString() == "None",
                    "real SerialPort constructor and board 921600 / 8N1 options execute");
                Need(!(bool)Get(serialType, port, "DtrEnable") && !(bool)Get(serialType, port, "RtsEnable") && !(bool)Get(serialType, port, "IsOpen"),
                    "DTR and RTS stay false before open");
                Exception rejected = null;
                try { serialType.GetMethod("Open").Invoke(port, null); }
                catch (TargetInvocationException error) when (error.InnerException is ArgumentException || error.InnerException is IOException)
                { rejected = error.InnerException; }
                Need(rejected != null && !ContainsPlatformFailure(rejected) && !(bool)Get(serialType, port, "IsOpen"),
                    "invalid non-COM name is rejected normally without platform or native-loader failure");
                report.AppendLine("invalid_open=" + rejected.GetType().FullName + ": " + rejected.Message);
            }
            report.AppendLine("RESULT PASS checks=" + checks + " unity=" + Application.unityVersion + " hardware=none real_ports_opened=0");
        }
        catch (Exception error)
        {
            exitCode = 1;
            report.AppendLine("RESULT FAIL " + error);
            Debug.LogException(error);
        }
        Directory.CreateDirectory(Path.GetDirectoryName(output));
        File.WriteAllText(output, report.ToString());
        Debug.Log(report.ToString());
        EditorApplication.Exit(exitCode);
    }
}
