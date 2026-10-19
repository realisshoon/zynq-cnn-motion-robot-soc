using System;
using System.Globalization;
using System.IO;
using UnityEngine;

// STEP 1의 명시적 검증 실행에서만 runtime에 붙인다. Main asset에는 저장하지 않는다.
[DefaultExecutionOrder(10000)]
public sealed class JointCommandReplayProbe : MonoBehaviour
{
    [Serializable] public sealed class Status
    {
        public long utcMs, received, applied, captured, stale, malformed, invalid, missedCapture;
        public uint frame;
        public bool listening, manualRegression, playing;
        public string error;
        public JointCommandData command;
    }
    private UdpJointCommandReceiver receiver;
    private StreamWriter writer;
    private string directory;
    private long lastApplied, captured, missed;
    private float nextStatus;
    private bool manualPass, previousRunInBackground;

    public void Begin(UdpJointCommandReceiver target, bool regressionPass, string subdirectory = "")
    {
        receiver=target; manualPass=regressionPass;
        directory=Path.GetFullPath(Path.Combine(Application.dataPath,"../Validation"));
        directory=Path.Combine(directory,subdirectory);
        Directory.CreateDirectory(directory);
        writer=new StreamWriter(Path.Combine(directory,"step1_unity_applied.csv"),false);
        writer.AutoFlush=true;
        writer.WriteLine("frame_id,valid,base_deg,shoulder_deg,elbow_deg,wrist_pitch_deg,wrist_roll_deg,gripper_norm,base_local,shoulder_local,elbow_local,wrist_pitch_local,wrist_roll_local,gripper_gear_local");
        previousRunInBackground=Application.runInBackground;Application.runInBackground=true;
        WriteStatus();
    }

    private static string N(float v) => v.ToString("R",CultureInfo.InvariantCulture);
    private static float Angle(RobotArmController.JointVisual j)
    {
        Vector3 e=(Quaternion.Inverse(j.restLocalRotation)*j.pivot.localRotation).eulerAngles;
        return Mathf.DeltaAngle(0,j.axis==RobotArmController.VisualAxis.X ? e.x : j.axis==RobotArmController.VisualAxis.Y ? e.y:e.z);
    }
    private void LateUpdate()
    {
        if(receiver==null || writer==null)return;
        if(receiver.AppliedCount!=lastApplied)
        {
            missed+=Math.Max(0,receiver.AppliedCount-lastApplied-1);lastApplied=receiver.AppliedCount;captured++;
            var c=receiver.LastAppliedCommand;var robot=receiver.controller;
            writer.WriteLine(string.Join(",",c.frame_id.ToString(CultureInfo.InvariantCulture),c.valid?"1":"0",
                N(c.base_deg),N(c.shoulder_deg),N(c.elbow_deg),N(c.wrist_pitch_deg),N(c.wrist_roll_deg),N(c.gripper_norm),
                N(Angle(robot.baseYaw)),N(Angle(robot.shoulderPitch)),N(Angle(robot.elbowPitch)),N(Angle(robot.wristPitch)),N(Angle(robot.wristRoll)),
                N(Mathf.DeltaAngle(0,robot.gripperVisual.leftGear.localEulerAngles.z))));
        }
        if(Time.unscaledTime>=nextStatus){WriteStatus();nextStatus=Time.unscaledTime+0.1f;}
    }
    private void WriteStatus()
    {
        if(receiver==null)return;
        var stats=receiver.GetStatistics();
        var s=new Status{utcMs=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),received=stats.received,applied=receiver.AppliedCount,
            captured=captured,stale=stats.stale,malformed=stats.malformed,invalid=stats.invalid,missedCapture=missed,
            frame=receiver.LastAppliedFrameId,listening=receiver.IsListening,manualRegression=manualPass,playing=Application.isPlaying,
            error=receiver.LastError,command=receiver.LastAppliedCommand};
        string path=Path.Combine(directory,"step1_status.json");
        try
        {
            File.WriteAllText(path+".tmp",JsonUtility.ToJson(s,true));
            if(File.Exists(path))File.Replace(path+".tmp",path,null);else File.Move(path+".tmp",path);
        }
        catch(IOException) { } // Windows reader와의 짧은 공유 충돌은 다음 tick에서 재시도.
    }
    private void OnDestroy()
    {
        if(writer==null)return;
        WriteStatus();writer.Dispose();writer=null;Application.runInBackground=previousRunInBackground;
    }
}
