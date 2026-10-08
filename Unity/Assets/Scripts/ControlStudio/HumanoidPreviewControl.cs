using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Visual-local state only. No additional servo channel, transport or calibration.
    public sealed class HumanoidPreviewControl
    {
        public const float PitchMin=0,PitchMax=10,RollMin=-8,RollMax=8;
        // Small demonstration envelope. Pitch/arm extents reuse FinalPresentationDirector;
        // roll is a deliberately bounded preview sweep, NOT a measured hardware limit.
        public static readonly float[] LeftMin={84,90,74,74,0};
        public static readonly float[] LeftMax={96,118,90,106,1};
        readonly float[,] requested=new float[2,2],applied=new float[2,2];
        readonly float[] leftRequested={90,90,90,90,1},leftApplied={90,90,90,90,1};
        public bool LeftEdited {get;private set;}
        public float Shoulder(int side,int axis)=>requested[side,axis];
        public float LeftValue(int axis)=>leftRequested[axis];
        public bool SetShoulder(int selection,int axis,float value)
        {
            if(selection<0||selection>2||axis<0||axis>1||!Finite(value))return false;
            value=Mathf.Clamp(value,axis==0?PitchMin:RollMin,axis==0?PitchMax:RollMax);
            for(int side=0;side<2;side++)if(selection==2||selection==side)requested[side,axis]=value;
            return true;
        }
        public bool SetLeft(int axis,float value)
        {
            if(axis<0||axis>4||!Finite(value)||value<LeftMin[axis]||value>LeftMax[axis])return false;
            leftRequested[axis]=value;LeftEdited=true;return true;
        }
        static bool Finite(float value)=>!float.IsNaN(value)&&!float.IsInfinity(value);
        public void Advance(float dt)
        {
            dt=Mathf.Clamp(dt,0,.05f);
            for(int side=0;side<2;side++)for(int axis=0;axis<2;axis++)applied[side,axis]=Mathf.MoveTowards(applied[side,axis],requested[side,axis],30*dt);
            for(int axis=0;axis<5;axis++)leftApplied[axis]=Mathf.MoveTowards(leftApplied[axis],leftRequested[axis],(axis==4?2:30)*dt);
        }
        public HumanoidArmJointTarget RightTarget(float[] approvedApplied)
        {
            var t=HumanoidJointAdapter.FromSingleArmRight(approvedApplied);
            t.shoulderPitch=applied[0,0];t.shoulderRoll=applied[0,1];return t;
        }
        public HumanoidArmJointTarget LeftTarget()
        {
            var t=HumanoidJointAdapter.FromSingleArmRight(leftApplied);
            t.shoulderPitch=applied[1,0];t.shoulderRoll=applied[1,1];return t;
        }
    }
}
