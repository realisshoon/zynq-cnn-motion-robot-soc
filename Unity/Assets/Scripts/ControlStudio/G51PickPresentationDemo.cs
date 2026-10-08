using System.Collections.Generic;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Virtual presentation input. Manual owns the existing router; this component never rotates a joint.
    [DefaultExecutionOrder(4000)]
    public sealed class G51PickPresentationDemo : MonoBehaviour
    {
        static readonly float[] StartPose={40f,72f,92f,90f,1f};
        static readonly float[] EndPose={145f,108f,124f,90f,0f};
        const float DurationSeconds=6.5f;
        readonly Dictionary<Renderer,bool> hidden=new Dictionary<Renderer,bool>();
        ControlStudioRuntimeUI studio;ControlStudioPresentationUI presentation;
        SingleArmCommandRouter router;ManualServoSource manual;RobotVisualProfiles profiles;Tool1Runtime tool;
        int epoch;float elapsed,nextSubmit;bool sceneActive;
        public bool Running {get;private set;}
        public bool SequenceComplete {get;private set;}
        public bool AutomaticClock {get;set;}=true;
        public string Error {get;private set;}="";
        public float Elapsed=>elapsed;
        public Vector3 GripperPosition=>router?.robot?.gripperVisual==null?Vector3.zero:router.robot.gripperVisual.transform.position;
        public Vector3 RobotBasePosition=>router?.robot==null?Vector3.zero:router.robot.transform.position;
        public void Initialize(ControlStudioRuntimeUI owner,ControlStudioPresentationUI ui)
        {
            studio=owner;presentation=ui;router=owner.router;manual=owner.manual;
            profiles=GetComponent<RobotVisualProfiles>();tool=GetComponent<Tool1Runtime>();
        }
        static float Smooth(float t){t=Mathf.Clamp01(t);return t*t*(3f-2f*t);}
        static float Between(float a,float b,float start,float end,float time)
            =>Mathf.Lerp(a,b,Smooth((time-start)/(end-start)));
        public static float[] RequestedAt(float time)
        {
            return new[]{
                Between(StartPose[0],EndPose[0],1f,2.5f,time),
                Between(StartPose[1],EndPose[1],2.5f,4f,time),
                Between(StartPose[2],EndPose[2],4f,5f,time),
                90f,
                Between(1f,0f,5f,5.7f,time)
            };
        }
        public bool StartDemo()
        {
            Error="";StopDemo();
            if(router==null||manual==null||profiles==null||presentation==null||tool==null||!router.Ready)
            {Error="Control Studio unavailable";return false;}
            GetComponent<PresentationSprayDemo>()?.StopDemo();
            if(router.Source!="MANUAL"&&!router.Csv.SelectManual()){Error="Manual source unavailable";return false;}
            router.SetPaused(false);tool.SetEnabled(false);
            if(!profiles.Select(RobotVisualProfileId.G51)||!profiles.SelectTool(ToolKind.Gripper))
            {Error="G51 Gripper unavailable";return false;}
            if(!manual.LoadAtomic(StartPose)){Error="Presentation start command rejected: "+router.Status;return false;}
            epoch=router.Epoch;elapsed=nextSubmit=0;SequenceComplete=false;
            sceneActive=true;Running=true;
            presentation.SetPresentationMode(true);
            studio.orbit.SetG51PickPresentationCamera(router.robot.transform,null);
            HideUnrelated();return true;
        }
        bool Keep(Renderer renderer)
        {
            if(renderer==null)return false;
            return router!=null&&router.robot!=null&&renderer.transform.IsChildOf(router.robot.transform.root);
        }
        void HideUnrelated()
        {
            if(!sceneActive)return;
            foreach(var renderer in FindObjectsByType<Renderer>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                if(Keep(renderer))continue;
                if(!hidden.ContainsKey(renderer))hidden.Add(renderer,renderer.enabled);
                renderer.enabled=false;
            }
        }
        void LateUpdate(){if(sceneActive)HideUnrelated();}
        void Update(){if(AutomaticClock&&Running)AdvanceVirtual(Time.unscaledDeltaTime);}
        public void AdvanceVirtual(float seconds)
        {
            if(!Running||seconds<=0)return;
            if(router.Source!="MANUAL"||router.Epoch!=epoch||router.Paused||!router.Ready||
                profiles.Selected!=RobotVisualProfileId.G51||tool.Gate.Tool!=ToolKind.Gripper||!presentation.PresentationMode)
            {Error="Presentation input ownership or output lost";StopDemo();return;}
            elapsed+=Mathf.Min(seconds,.1f);
            if(elapsed>=nextSubmit)
            {
                if(!manual.LoadAtomic(RequestedAt(elapsed)))
                {Error="Presentation command rejected: "+router.Status;StopDemo();return;}
                nextSubmit=elapsed+.05f;
            }
            if(elapsed>=DurationSeconds){Running=false;SequenceComplete=true;}
        }
        public void StopDemo()
        {
            if(!sceneActive&&!Running)return;
            Running=false;sceneActive=false;
            foreach(var entry in hidden)if(entry.Key!=null)entry.Key.enabled=entry.Value;
            hidden.Clear();
        }
        void OnDisable(){StopDemo();}
    }
}
