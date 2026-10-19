namespace HumanMotion.ControlStudio
{
    public enum CsvInputMode { RecordedHumanAngles, XyzStoredBodyAuxGripper, XyzStoredBodyHoldGripper }
    // Processed Agent1 snapshots, not per-point sensor measurements. No invented confidence/age.
    public sealed class Pose3DFrame
    {
        public const string Profile="CSV_RELATIVE_CAMERA_STORED_BODY_RIGHT";
        public const string Unit="relative shoulder-width unit";
        public const string PointQuality="UNKNOWN (CSV has no per-point valid/confidence/age)";
        public float[] Xyz,BodyBasis;
        public int[] SourceFlags; // eight STEP 2 flags, selected branch, ever selected branch
        public float AuxiliaryGripper;
    }
    public sealed class XyzSolveResult
    {
        public RecordedHumanRow Target;
        public int[] SolverInfo;
        public string State=>new[]{"WAITING_VALID","FRESH","WRIST_HOLD","INITIAL_HELD_SEED","MAJOR_HOLD","MAJOR_INVALID","HAND_FAILED","WRIST_UNAVAILABLE"}[SolverInfo[4]];
    }
}
