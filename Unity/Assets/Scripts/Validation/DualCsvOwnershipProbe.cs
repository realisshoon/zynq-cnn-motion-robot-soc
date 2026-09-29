using System;
using System.IO;
using System.Linq;
using UnityEngine;

// 검증 실행에서만 추가한다. 매 LateUpdate에서 양팔의 실제 소유권을 감시한다.
[DefaultExecutionOrder(11000)]
public sealed class DualCsvOwnershipProbe : MonoBehaviour
{
    [Serializable] public sealed class Result
    {
        public long utcMs, checkedFrames, checkedRightFrames, leftChangedFrames, overwriteFrames, listenerErrors;
        public int armCount, receiverCount, listenerCount;
        public bool framePresent, startResetGuard;
    }
    private DualArmDemoController demo;
    private Transform[] left;
    private Vector3[] positions, scales;
    private Quaternion[] rotations;
    private readonly Result result = new Result();
    private float nextWrite;
    private string path;

    public void Begin(DualArmDemoController target, bool guardPass)
    {
        demo=target; result.startResetGuard=guardPass;
        left=demo.leftArm.GetComponentsInChildren<Transform>(true);
        positions=left.Select(t=>t.localPosition).ToArray();
        scales=left.Select(t=>t.localScale).ToArray();
        rotations=left.Select(t=>t.localRotation).ToArray();
        path=Path.GetFullPath(Path.Combine(Application.dataPath,"../Validation/DualStep1/ownership.json"));
    }
    private static bool Same(Quaternion a, Quaternion b) =>
        Mathf.Min((new Vector4(a.x-b.x,a.y-b.y,a.z-b.z,a.w-b.w)).sqrMagnitude,
                  (new Vector4(a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w)).sqrMagnitude)<1e-9f;
    private static bool Matches(RobotArmController.JointVisual joint, float value)
    {
        Vector3 axis=joint.axis==RobotArmController.VisualAxis.X?Vector3.right:
                     joint.axis==RobotArmController.VisualAxis.Y?Vector3.up:Vector3.forward;
        return Same(joint.pivot.localRotation,joint.restLocalRotation*Quaternion.AngleAxis((value-joint.neutral_deg)*(int)joint.direction,axis));
    }
    private void LateUpdate()
    {
        if(demo==null)return;
        result.checkedFrames++;
        bool changed=false;
        for(int i=0;i<left.Length;i++)
            changed |= left[i]==null || (left[i].localPosition-positions[i]).sqrMagnitude>1e-10f ||
                (left[i].localScale-scales[i]).sqrMagnitude>1e-10f || !Same(left[i].localRotation,rotations[i]);
        if(changed)result.leftChangedFrames++;
        // START가 CSV command를 덮거나 왼팔 scripted 동작을 시작하지 않는지 실제로 자극한다.
        demo.StartDemo();
        demo.Advance(0.02f);
        var receiver=demo.rightReceiver; var robot=demo.rightArm;
        if(receiver.AppliedCount>0)
        {
            result.checkedRightFrames++;
            var c=receiver.LastAppliedCommand;
            bool correct=robot.inputMode==RobotArmController.InputMode.UDP && !robot.ApplyTestCommand() &&
                Matches(robot.baseYaw,c.base_deg) && Matches(robot.shoulderPitch,c.shoulder_deg) &&
                Matches(robot.elbowPitch,c.elbow_deg) && Matches(robot.wristPitch,c.wrist_pitch_deg) && Matches(robot.wristRoll,c.wrist_roll_deg) &&
                Mathf.Abs(Mathf.DeltaAngle(robot.gripperVisual.leftGear.localEulerAngles.z,
                    Mathf.Lerp(robot.gripperVisual.closedDriveDeg,robot.gripperVisual.openDriveDeg,c.gripper_norm)))<0.001f;
            if(!correct)result.overwriteFrames++;
        }
        var receivers=FindObjectsByType<UdpJointCommandReceiver>(FindObjectsInactive.Include,FindObjectsSortMode.None);
        result.receiverCount=receivers.Length; result.listenerCount=receivers.Count(r=>r.IsListening);
        result.armCount=FindObjectsByType<RobotArmController>(FindObjectsInactive.Include,FindObjectsSortMode.None).Length;
        result.framePresent=GameObject.Find("CenterFrame")!=null && GameObject.Find("HorizontalBeam")!=null && GameObject.Find("VerticalColumn")!=null;
        if(result.receiverCount!=1 || result.listenerCount!=1 || receiver.controller!=robot || result.armCount!=2 || !result.framePresent)result.listenerErrors++;
        if(Time.unscaledTime<nextWrite)return;
        nextWrite=Time.unscaledTime+0.1f;result.utcMs=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        try {File.WriteAllText(path+".tmp",JsonUtility.ToJson(result,true));if(File.Exists(path))File.Replace(path+".tmp",path,null);else File.Move(path+".tmp",path);}
        catch(IOException) { }
    }
}
