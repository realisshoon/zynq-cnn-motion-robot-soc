using System;

namespace HumanMotion.ControlStudio
{
    public enum HumanoidArmSide { Left, Right }

    // Angles are rig-local degrees. This is a target contract, not a UART packet or an XYZ solver.
    [Serializable]
    public struct HumanoidArmJointTarget
    {
        public float shoulderYaw,shoulderPitch,shoulderRoll;
        public float elbowPitch,elbowRoll;
        public float wristPitch,wristRoll;
        public float gripper;
        public bool valid,fresh;
    }

    [Serializable]
    public struct DualHumanoidTarget
    {
        public HumanoidArmJointTarget left,right;
        public long frameId;
        public double timestamp;
    }

    public static class HumanoidJointAdapter
    {
        // Temporary visual compatibility only: already-approved G51 servo commands, not human XYZ.
        public static HumanoidArmJointTarget FromSingleArmRight(float[] applied)
        {
            if(applied==null||applied.Length<5)return default;
            return new HumanoidArmJointTarget{
                shoulderYaw=0,shoulderPitch=0,shoulderRoll=0,
                elbowRoll=applied[0]-90f,elbowPitch=applied[1]-90f,
                wristPitch=applied[2]-90f,wristRoll=-(applied[3]-90f),
                gripper=applied[4],valid=true,fresh=true
            };
        }
    }
}
