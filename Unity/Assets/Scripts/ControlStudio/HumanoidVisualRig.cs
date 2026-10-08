using System;
using UnityEngine;
#if UNITY_EDITOR
using UnityEditor;
#endif

namespace HumanMotion.ControlStudio
{
    // Standalone visual rig; it never writes a robot command or a logical G51 joint.
    public sealed class HumanoidVisualRig : MonoBehaviour
    {
        [Serializable]
        public struct HumanoidRestPose
        {
            public Quaternion shoulderYaw,shoulderPitch,shoulderRoll;
            public Quaternion elbowPitch,elbowRoll,wristPitch,wristRoll;
            public float gripperOpen;
        }

        [Serializable]
        public sealed class Arm
        {
            public Transform shoulderRoot,shoulderYaw,shoulderPitch,shoulderRoll;
            public Transform upperArm,elbowPitch,elbowRoll,forearm;
            public Transform wristPitch,wristRoll,toolMount,gripper;
            public G51GripperVisual gripperVisual;
            public HumanoidRestPose rest;

            public bool Configured=>shoulderRoot!=null&&shoulderYaw!=null&&shoulderPitch!=null&&shoulderRoll!=null&&
                upperArm!=null&&elbowPitch!=null&&elbowRoll!=null&&forearm!=null&&wristPitch!=null&&
                wristRoll!=null&&toolMount!=null&&gripper!=null&&gripperVisual!=null&&
                shoulderYaw.IsChildOf(shoulderRoot)&&shoulderPitch.IsChildOf(shoulderYaw)&&
                shoulderRoll.IsChildOf(shoulderPitch)&&upperArm.IsChildOf(shoulderRoll)&&
                elbowPitch.IsChildOf(upperArm)&&elbowRoll.IsChildOf(elbowPitch)&&
                forearm.IsChildOf(elbowRoll)&&wristPitch.IsChildOf(forearm)&&
                wristRoll.IsChildOf(wristPitch)&&toolMount.IsChildOf(wristRoll)&&gripper.IsChildOf(toolMount);
        }

        public Transform bodyRoot,torso,head;
        public Arm left=new Arm(),right=new Arm();
        public bool showJointAxes;
        [NonSerialized]public bool presentationMode;
        public bool IsConfigured=>bodyRoot!=null&&torso!=null&&head!=null&&left.Configured&&right.Configured;
        public Transform RightToolMount=>right.toolMount;

        public void CaptureRestPose()
        {Capture(left);Capture(right);}
        static void Capture(Arm a)
        {
            a.rest=new HumanoidRestPose{
                shoulderYaw=a.shoulderYaw.localRotation,shoulderPitch=a.shoulderPitch.localRotation,
                shoulderRoll=a.shoulderRoll.localRotation,elbowPitch=a.elbowPitch.localRotation,
                elbowRoll=a.elbowRoll.localRotation,wristPitch=a.wristPitch.localRotation,
                wristRoll=a.wristRoll.localRotation,gripperOpen=1f};
        }

        public void ResetToRest()
        {Rest(left);Rest(right);}
        static void Rest(Arm a)
        {
            if(!a.Configured)return;
            a.shoulderYaw.localRotation=a.rest.shoulderYaw;
            a.shoulderPitch.localRotation=a.rest.shoulderPitch;
            a.shoulderRoll.localRotation=a.rest.shoulderRoll;
            a.elbowPitch.localRotation=a.rest.elbowPitch;
            a.elbowRoll.localRotation=a.rest.elbowRoll;
            a.wristPitch.localRotation=a.rest.wristPitch;
            a.wristRoll.localRotation=a.rest.wristRoll;
            a.gripperVisual.Apply(a.rest.gripperOpen);
        }

        static bool Finite(float value)=>!float.IsNaN(value)&&!float.IsInfinity(value);
        static bool Valid(HumanoidArmJointTarget t)=>t.valid&&t.fresh&&
            Finite(t.shoulderYaw)&&Finite(t.shoulderPitch)&&Finite(t.shoulderRoll)&&
            Finite(t.elbowPitch)&&Finite(t.elbowRoll)&&Finite(t.wristPitch)&&
            Finite(t.wristRoll)&&Finite(t.gripper);

        public bool ApplyArmTarget(HumanoidArmSide side,HumanoidArmJointTarget target)
        {
            Arm a=side==HumanoidArmSide.Left?left:right;
            if(!a.Configured||!Valid(target))return false;
            a.shoulderYaw.localRotation=a.rest.shoulderYaw*Quaternion.AngleAxis(target.shoulderYaw,Vector3.up);
            a.shoulderPitch.localRotation=a.rest.shoulderPitch*Quaternion.AngleAxis(target.shoulderPitch,Vector3.right);
            a.shoulderRoll.localRotation=a.rest.shoulderRoll*Quaternion.AngleAxis(target.shoulderRoll,Vector3.forward);
            a.elbowPitch.localRotation=a.rest.elbowPitch*Quaternion.AngleAxis(target.elbowPitch,Vector3.right);
            a.elbowRoll.localRotation=a.rest.elbowRoll*Quaternion.AngleAxis(target.elbowRoll,Vector3.up);
            a.wristPitch.localRotation=a.rest.wristPitch*Quaternion.AngleAxis(target.wristPitch,Vector3.right);
            a.wristRoll.localRotation=a.rest.wristRoll*Quaternion.AngleAxis(target.wristRoll,Vector3.up);
            a.gripperVisual.Apply(Mathf.Clamp01(target.gripper));
            return true;
        }

        public bool ApplyArmTarget(HumanoidArmSide side,DualHumanoidTarget target)
            =>ApplyArmTarget(side,side==HumanoidArmSide.Left?target.left:target.right);

        public void SetTool(ToolKind tool)
        {
            if(right.gripper!=null)right.gripper.gameObject.SetActive(tool==ToolKind.Gripper);
            if(left.gripper!=null)left.gripper.gameObject.SetActive(true);
        }

        void OnDrawGizmos()
        {
            if(!showJointAxes||presentationMode)return;
            DrawArm(left,new Color(.35f,.8f,1f));DrawArm(right,new Color(1f,.76f,.24f));
        }
        static void DrawArm(Arm a,Color color)
        {
            if(a==null)return;
            Joint(a.shoulderYaw,Vector3.up,color);Joint(a.shoulderPitch,Vector3.right,color);
            Joint(a.shoulderRoll,Vector3.forward,color);Joint(a.elbowPitch,Vector3.right,color);
            Joint(a.elbowRoll,Vector3.up,color);Joint(a.wristPitch,Vector3.right,color);
            Joint(a.wristRoll,Vector3.up,color);
        }
        static void Joint(Transform pivot,Vector3 axis,Color color)
        {
            if(pivot==null)return;
            Gizmos.color=color;Gizmos.DrawSphere(pivot.position,.012f);
            Gizmos.DrawRay(pivot.position,pivot.TransformDirection(axis)*.10f);
#if UNITY_EDITOR
            Handles.color=color;Handles.Label(pivot.position+Vector3.up*.025f,pivot.name);
#endif
        }
    }
}
