using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Explicit virtual presentation driver. It only submits through the existing Manual source.
    public sealed class PresentationSprayDemo : MonoBehaviour
    {
        ControlStudioRuntimeUI studio;ControlStudioPresentationUI presentation;
        SingleArmCommandRouter router;ManualServoSource manual;Tool1Runtime tool;RobotVisualProfiles profiles;
        int epoch,phase;float elapsed,nextSubmit;
        public bool Running {get;private set;}
        public bool AutomaticClock {get;set;}=true;
        public string Error {get;private set;}="";
        public bool ReadyObserved {get;private set;}
        public bool SprayObserved {get;private set;}
        public bool StoppedObserved {get;private set;}
        public int PeakParticles {get;private set;}
        public int InitialPaintCount {get;private set;}
        public float Elapsed=>elapsed;
        static readonly float[] OpenPose={90f,70f,100f,90f,1f};
        void InitializeReferences(){router=studio.router;manual=studio.manual;tool=GetComponent<Tool1Runtime>();profiles=GetComponent<RobotVisualProfiles>();}
        public void Initialize(ControlStudioRuntimeUI owner,ControlStudioPresentationUI ui)
        {studio=owner;presentation=ui;InitializeReferences();}
        public bool StartDemo()
        {
            Error="";if(Running)StopDemo();
            if(router==null||manual==null||tool==null||profiles==null||!router.Ready){Error="CONTROL STUDIO unavailable";return false;}
            if(router.Source!="MANUAL"&&!router.Csv.SelectManual()){Error="Manual source unavailable";return false;}
            router.SetPaused(false);tool.SetEnabled(false);
            if(!profiles.Select(RobotVisualProfileId.G51)||!profiles.SelectTool(ToolKind.Spray)){Error="G51 / Spray unavailable";return false;}
            if(!manual.LoadAtomic(OpenPose)){Error="Open home command rejected";return false;}
            tool.PaintColor=new Color(.08f,.42f,.94f);tool.SprayRadius=.06f;
            tool.Surface.Clear();InitialPaintCount=tool.Surface.PaintCount;
            epoch=router.Epoch;phase=0;elapsed=nextSubmit=0;
            ReadyObserved=SprayObserved=StoppedObserved=false;PeakParticles=0;Running=true;
            presentation.SetPresentationMode(true);
            return true;
        }
        public void StopDemo()
        {
            if(!Running)return;
            Running=false;tool?.SetEnabled(false);
        }
        void Update(){if(AutomaticClock&&Running)AdvanceVirtual(Time.unscaledDeltaTime);}
        bool Submit(float pitch,float roll,float grip)
        {
            var command=new[]{roll,70f,pitch,90f,grip};
            if(manual.LoadAtomic(command))return true;
            Error="Manual presentation command rejected: "+router.Status;StopDemo();return false;
        }
        public void AdvanceVirtual(float seconds)
        {
            if(!Running||seconds<=0)return;
            if(router.Source!="MANUAL"||router.Epoch!=epoch||router.Paused||!router.Ready||tool.Gate.Tool!=ToolKind.Spray||!presentation.PresentationMode){Error="Presentation ownership or output lost";StopDemo();return;}
            elapsed+=Mathf.Min(seconds,.1f);
            if(tool.Gate.State=="READY")ReadyObserved=true;
            if(tool.Gate.Running)SprayObserved=true;
            if(tool.Visual?.spray!=null)PeakParticles=Mathf.Max(PeakParticles,tool.Visual.spray.particleCount);
            if(phase==0&&elapsed>=1f){
                if(!tool.PlaceSurface()){Error="Paint panel placement blocked";StopDemo();return;}
                studio.orbit.SetPresentationCamera();
                tool.SetEnabled(true);
                if(!Submit(88f,90f,1f))return;
                phase=1;
            }
            if(phase==1&&elapsed>=2f){if(!Submit(88f,90f,0f))return;phase=2;nextSubmit=elapsed;}
            if(phase==2&&elapsed<7f&&elapsed>=nextSubmit){
                float t=Mathf.Clamp01((elapsed-2f)/5f);
                float sweep=t<.5f?t*2f:2f-t*2f;
                float pitch=88f+24f*sweep;
                float roll=90f-6f*Mathf.SmoothStep(0f,1f,Mathf.Clamp01(t/.15f))
                    +14f*Mathf.SmoothStep(0f,1f,Mathf.Clamp01((t-.43f)/.14f));
                if(!Submit(pitch,roll,0f))return;
                nextSubmit=elapsed+.08f;
            }
            if(phase==2&&elapsed>=7f){if(!Submit(100f,94f,1f))return;phase=3;}
            if(phase==3&&elapsed>=8f){StoppedObserved=!tool.Gate.Running;tool.SetEnabled(false);phase=4;}
            if(phase==4&&elapsed>=9.5f)Running=false;
        }
        void OnDisable(){StopDemo();}
    }
}
