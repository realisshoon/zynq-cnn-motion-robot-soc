using System;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Offline presentation only. No sensor, router, UART or hardware ownership.
    public sealed class FinalPresentationDirector : MonoBehaviour
    {
        public const float Duration=11f;
        public ForearmArmController g51;
        public Demo06RealisticVisualAdapter adapter;
        public HumanoidVisualRig humanoid;
        public Camera presentationCamera;
        public bool automaticPlayback=true;
        RobotVisualQualityBinding quality;
        SimulationEnvironment environment;
        bool initialized;float elapsed;
        public Transform RobotRoot=>humanoid!=null?humanoid.transform:g51.transform.root;
        public bool IsHumanoid=>humanoid!=null;
        public void Initialize()
        {
            if(initialized)return;initialized=true;
            quality=new RobotVisualQualityBinding();
            if(IsHumanoid){if(!humanoid.IsConfigured)throw new Exception("Humanoid hierarchy invalid");humanoid.presentationMode=true;humanoid.SetTool(ToolKind.Gripper);quality.Apply(humanoid.transform,true);}
            else{if(!g51.IsConfigured||!adapter.IsConfigured)throw new Exception("G51 hierarchy invalid");var detail=adapter.gameObject.AddComponent<G51IndustrialVisual>();detail.Initialize(adapter);detail.SetVisible(false);quality.Apply(RobotRoot);}
            Evaluate(0);
            environment=new SimulationEnvironment(presentationCamera);
            environment.PlaceUnder(RobotRoot);environment.ApplyBackground(true);
        }
        void Start(){Initialize();}
        void Update(){if(automaticPlayback){elapsed+=Time.deltaTime;Evaluate(elapsed%Duration);}}
        static float Blend(float t,float a,float b){float x=Mathf.Clamp01((t-a)/(b-a));return x*x*x*(x*(x*6-15)+10);}
        static float Pulse(float t,float a,float b,float c,float d)=>Blend(t,a,b)*(1-Blend(t,c,d));
        public static ForearmJointCommandData G51At(float t)=>new ForearmJointCommandData{
            valid=true,frame_id=(uint)Mathf.RoundToInt(t*60),
            elbowRoll=45+90*Pulse(t,.6f,2.4f,8.9f,10.5f),
            elbowPitch=82+28*Pulse(t,3.2f,5f,7.3f,8.6f),
            wristPitch=95+23*Pulse(t,3.2f,5f,7.3f,8.6f),
            wristRoll=90,gripper=1-Pulse(t,5f,5.8f,6.6f,7.3f)};
        public static HumanoidArmJointTarget HumanAt(float t,bool left)
        {
            float arm=Pulse(t,.7f,2.8f,8.5f,10.5f),wrist=Pulse(t,3.3f,4.7f,7.5f,8.5f);
            return new HumanoidArmJointTarget{valid=true,fresh=true,shoulderPitch=10*arm,
                shoulderYaw=(left?4:-4)*arm,shoulderRoll=0,elbowPitch=28*arm,
                elbowRoll=(left?-6:6)*arm,wristPitch=-16*wrist,wristRoll=(left?-16:16)*wrist,
                gripper=1-Pulse(t,5.2f,6f,6.8f,7.5f)};
        }
        public void Evaluate(float time)
        {
            if(IsHumanoid){if(!humanoid.ApplyArmTarget(HumanoidArmSide.Left,HumanAt(time,true))||!humanoid.ApplyArmTarget(HumanoidArmSide.Right,HumanAt(time,false)))throw new Exception("Scripted humanoid target rejected");}
            else{var command=G51At(time);if(!g51.ApplyCommand(command))throw new Exception("Presentation command rejected");adapter.SyncVisuals();g51.gripperVisual.Apply(command.gripper);}
        }
        public void UpdateStudio(){environment?.ApplyBackground(true);}
        void OnDestroy(){quality?.Destroy();environment?.Destroy();}
    }
}
