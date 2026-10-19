using System;
using System.Globalization;
using System.Text;

namespace HumanMotion.ControlStudio
{
    // Provisional wire contract for processed RIGHT-arm relative XYZ, not raw camera or stereo XYZ.
    // POSE3D_V1,RIGHT,frame_id,target_frame_id,time_sec,18 XYZ,9 BodyBasis,10 source flags,gripper_norm\n
    public static class UartPose3DProtocol
    {
        public const string Name = "POSE3D_V1 / PROVISIONAL";
        public const int FieldCount = 43;
        static readonly CultureInfo CI = CultureInfo.InvariantCulture;
        public static string Serialize(RecordedHumanRow row)
        {
            if (row?.Pose?.Xyz?.Length != 18 || row.Pose.BodyBasis?.Length != 9 || row.Pose.SourceFlags?.Length != 10)
                throw new ArgumentException("Complete processed Pose3DFrame required");
            var b = new StringBuilder(512).Append("POSE3D_V1,RIGHT,").Append(row.FrameId).Append(',')
                .Append(row.TargetFrameId).Append(',').Append(row.Time.ToString("R", CI));
            foreach (float v in row.Pose.Xyz) b.Append(',').Append(v.ToString("R", CI));
            foreach (float v in row.Pose.BodyBasis) b.Append(',').Append(v.ToString("R", CI));
            foreach (int v in row.Pose.SourceFlags) b.Append(',').Append(v);
            return b.Append(',').Append(row.Pose.AuxiliaryGripper.ToString("R", CI)).Append('\n').ToString();
        }
        public static bool TryParse(string line, out RecordedHumanRow row, out string error)
        {
            row = null; error = "";
            if (line == null || line.Length > 2048) { error = "FRAME LENGTH"; return false; }
            var f = line.TrimEnd('\r').Split(',');
            if (f.Length != FieldCount || f[0] != "POSE3D_V1" || f[1] != "RIGHT")
            { error = f.Length < 32 ? "BODY FRAME: UNAVAILABLE / incomplete packet" : "PROTOCOL / FIELD COUNT / ARM SIDE"; return false; }
            if (!uint.TryParse(f[2], NumberStyles.None, CI, out uint frame) ||
                !uint.TryParse(f[3], NumberStyles.None, CI, out uint target) ||
                !double.TryParse(f[4], NumberStyles.Float, CI, out double time) ||
                double.IsNaN(time) || double.IsInfinity(time) || time < 0)
            { error = "FRAME / TIME"; return false; }
            var xyz = new float[18]; var basis = new float[9]; var flags = new int[10];
            for (int i = 0; i < 18; i++) if (!TryFloat(f[5 + i], out xyz[i])) { error = "NONFINITE / INVALID XYZ field " + i; return false; }
            for (int i = 0; i < 9; i++) if (!TryFloat(f[23 + i], out basis[i])) { error = "NONFINITE / INVALID BODY field " + i; return false; }
            for (int i = 0; i < 10; i++)
                if (!int.TryParse(f[32 + i], NumberStyles.None, CI, out flags[i]) || flags[i] < 0 || flags[i] > 1)
                { error = "SOURCE FLAG field " + i; return false; }
            if (!TryFloat(f[42], out float grip) || grip < 0 || grip > 1) { error = "GRIPPER NORM"; return false; }
            // A missing/invalid BodyFrame is never replaced with a guessed basis.
            if (flags[7] == 0) { error = "BODY FRAME: UNAVAILABLE"; return false; }
            var outputFlags = new int[8]; Array.Copy(flags, outputFlags, 8);
            row = new RecordedHumanRow { FrameId = frame, TargetFrameId = target, Time = time,
                Flags = outputFlags, Pose = new Pose3DFrame { Xyz = xyz, BodyBasis = basis,
                    SourceFlags = flags, AuxiliaryGripper = grip } };
            return true;
        }
        static bool TryFloat(string text, out float value) =>
            float.TryParse(text, NumberStyles.Float, CI, out value) && !float.IsNaN(value) && !float.IsInfinity(value);
    }

    // Byte framing is independent of serial chunk boundaries and never zero-fills a partial pose.
    public sealed class UartPoseLineFramer
    {
        readonly StringBuilder pending = new StringBuilder();
        bool discard;
        public int FramingErrors { get; private set; }
        public void Reset() { pending.Clear(); discard = false; FramingErrors = 0; }
        public int TakeFramingErrors() { int count = FramingErrors; FramingErrors = 0; return count; }
        public void Feed(byte[] bytes, int count, Action<string> complete)
        {
            for (int i = 0; i < count; i++)
            {
                byte c = bytes[i];
                if (c == 10)
                {
                    if (!discard && pending.Length > 0) complete(pending.ToString());
                    pending.Clear(); discard = false; continue;
                }
                if (discard) continue;
                if (c < 32 && c != 13 || c > 126 || pending.Length >= 2048)
                { pending.Clear(); discard = true; FramingErrors++; continue; }
                if (c != 13) pending.Append((char)c);
            }
        }
    }
}
