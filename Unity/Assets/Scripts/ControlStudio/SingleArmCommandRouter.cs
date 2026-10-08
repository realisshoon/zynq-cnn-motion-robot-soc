using System;
using UnityEngine;
namespace HumanMotion.ControlStudio
{
    [DefaultExecutionOrder(-100)]
    public sealed class SingleArmCommandRouter : MonoBehaviour
    {
        public ForearmArmController robot;
        public const string RobotId="G51_SINGLE_01";
        public const double TickSeconds=.020;
        public static float[] Home=>new[]{90f,70f,100f,90f,.05f};
        public string Source {get;private set;}="MANUAL";
        public int Epoch {get;private set;}=1;
        public bool Ready {get;private set;}
        public bool Paused {get;private set;}
        public string PauseReason {get;private set;}="";
        public int TimingStalls {get;private set;}
        public string Status {get;private set;}="STARTING";
        public string NativeError {get;private set;}="";
        public float[] Requested {get;private set;}=Home;
        public float[] Approved {get;private set;}=Home;
        public float[] Applied {get;private set;}=Home;
        public float[] Human {get;private set;}
        public float[] Wrapped {get;private set;}
        public float[] Candidate {get;private set;}
        public int[] HumanInfo {get;private set;}
        public int MotionHold {get;private set;}
        public float[] Velocity {get;}=new float[4];
        public float[] Min {get;private set;}=new[]{20f,20f,10f,10f,0f};
        public float[] Max {get;private set;}=new[]{160f,180f,160f,170f,1f};
        public long TickCount {get;private set;}
        public double SimulatedSeconds=>TickCount*TickSeconds;
        public int HardwareTxCount=>0;
        public bool HasApplied {get;private set;}
        public bool HasApproved {get;private set;}
        public CsvRecordedHumanSource Csv {get;private set;}
        public event Action Changed;
        public event Action OutputApplied;
        ControlStudioOutputPolicy policy;
        double accumulator;bool clockStarted;
        static readonly float[] UnityMin={20,20,10,10,0},UnityMax={160,180,160,170,1};
        void Start()
        {
            Csv=GetComponent<CsvRecordedHumanSource>()??gameObject.AddComponent<CsvRecordedHumanSource>();Csv.router=this;
            try {
                if(robot==null||!robot.IsConfigured)throw new InvalidOperationException("Robot references invalid");
                policy=new ControlStudioOutputPolicy(Home);Min=policy.Min;Max=policy.Max;
                for(int i=0;i<5;i++)if(Min[i]!=UnityMin[i]||Max[i]!=UnityMax[i])throw new InvalidOperationException("C / Unity RANGE MISMATCH M"+i);
                Ready=true;HasApproved=true;Status="READY / VIRTUAL ONLY";
            }catch(Exception e){Fail(e);}Changed?.Invoke();
        }
        public bool ResetOwner(string source)
        {
            if(!Ready)return false;
            if(source!="MANUAL"&&source!="CSV"&&source!="UART"){Reject("Unknown source");return false;}
            try {
                // Seed a fresh C context at the currently displayed command: zero velocity,
                // no old observation/HOLD history, no position jump, no automatic Home.
                var seed=(float[])(HasApplied?Applied:Home).Clone();
                var next=new ControlStudioOutputPolicy(seed);policy.Dispose();policy=next;
                Source=source;Epoch++;accumulator=0;Array.Clear(Velocity,0,4);MotionHold=0;
                Requested=(float[])seed.Clone();Approved=seed;Human=Wrapped=Candidate=null;HumanInfo=null;
                Status="SOURCE "+source+" / new epoch";SetPaused(source=="CSV");Changed?.Invoke();return true;
            }catch(Exception e){Fail(e);return false;}
        }
        public bool Submit(string source,int epoch,float[] q)
        {
            if(source!="MANUAL"||Source!=source||epoch!=Epoch){Reject("SOURCE / EPOCH");return false;}
            if(!Ready){Status="NATIVE_UNAVAILABLE";return false;}
            if(q==null||q.Length!=5){Reject("Five values required");return false;}
            foreach(float v in q)if(float.IsNaN(v)||float.IsInfinity(v)){Reject("NONFINITE");return false;}
            Requested=(float[])q.Clone();
            try {int rc=policy.Submit(q);Status=ControlStudioOutputPolicy.Reason(rc);if(rc!=0){Changed?.Invoke();return false;}Approved=(float[])q.Clone();HasApproved=true;Changed?.Invoke();return true;}
            catch(Exception e){Fail(e);return false;}
        }
        public bool SubmitHuman(RecordedHumanRow row,int epoch)
        {
            if((Source!="CSV"&&Source!="UART")||epoch!=Epoch){Reject("SOURCE / EPOCH");return false;}
            if(!Ready)return false;
            try {
                Human=(float[])row.Human.Clone();Wrapped=new float[5];var candidate=new float[5];var approved=new float[5];HumanInfo=new int[4];
                int rc=policy.SubmitHuman(row,Wrapped,candidate,approved,HumanInfo);
                if(rc<0)throw new InvalidOperationException(ControlStudioOutputPolicy.Reason(rc));
                Candidate=HumanInfo[1]!=0?candidate:null;Approved=approved;
                if(Candidate!=null)Requested=(float[])Candidate.Clone();
                Status=HumanInfo[0]==0?"WAITING_VALID / previous approved target retained":HumanInfo[0]==3?"REJECTED / human validation":HumanInfo[0]==4?"REJECTED / safety flags="+HumanInfo[3]:"VALID / "+(HumanInfo[0]==2?"same target":"new target");
                if(HumanInfo[2]!=0)Status+=" / AXIS HOLD mask="+HumanInfo[2];
                if(row.Flags[4]==0)Status+=" / OBSERVATION HOLD (hand_fresh=0)";
                if(row.Flags[6]!=0)Status+=" / gripper observation held";
                Changed?.Invoke();return rc!=0;
            }catch(Exception e){Fail(e);return false;}
        }
        public bool ValidatePreset(float[] q,out string reason)
        {
            reason="MANUAL source required";if(Source!="MANUAL")return false;
            reason="NATIVE_UNAVAILABLE";if(!Ready)return false;
            try{int rc=policy.Validate(q);reason=ControlStudioOutputPolicy.Reason(rc);return rc==0;}catch(Exception e){Fail(e);return false;}
        }
        public void Reject(string reason){Status="REJECTED: "+reason;Changed?.Invoke();}
        public void SetPaused(bool pause){Paused=pause;PauseReason=pause?"USER PAUSE":"";accumulator=0;Changed?.Invoke();}
        void Update()
        {
            // Establish the render clock after loading; startup duration is not playback time.
            if(!clockStarted){clockStarted=true;return;}
            AdvanceVirtual(Time.unscaledDeltaTime);
        }
        public void AdvanceVirtual(double dt)
        {
            if(!Ready||Paused)return;
            if(dt<0||double.IsNaN(dt)||double.IsInfinity(dt)){Reject("Invalid elapsed time");return;}
            if(dt>.5){Paused=true;PauseReason="TIMING STALL / press Resume";TimingStalls++;accumulator=0;Changed?.Invoke();return;}
            if(Source=="CSV"){Csv.Advance(dt);return;}
            accumulator+=dt;
            if(accumulator>.5){Paused=true;PauseReason="TIMING STALL / press Resume";TimingStalls++;accumulator=0;return;}
            int budget=5;while(accumulator>=TickSeconds&&budget-->0&&Ready){accumulator-=TickSeconds;OutputTick();}
        }
        public bool CsvTick(int epoch)=>Source=="CSV"&&epoch==Epoch&&Ready&&OutputTick();
        public bool IsSettled
        {
            get{for(int i=0;i<5;i++)if(Math.Abs(Applied[i]-Approved[i])>.002f)return false;foreach(float v in Velocity)if(Math.Abs(v)>.002f)return false;return HasApplied;}
        }
        bool OutputTick()
        {
            try {
                var next=new float[5];int rc=policy.Tick(next,Velocity);if(rc<0)throw new InvalidOperationException(ControlStudioOutputPolicy.Reason(rc));
                var c=new ForearmJointCommandData{frame_id=(uint)(TickCount+1),valid=true,elbowRoll=next[0],elbowPitch=next[1],wristPitch=next[2],wristRoll=next[3],gripper=next[4]};
                // Sole ApplyCommand writer. CSV scheduling and UI never rotate the robot.
                if(!robot.ApplyCommand(c)){Ready=false;Status="CONTROLLER REJECTED";return false;}
                Applied=next;HasApplied=true;TickCount++;MotionHold=rc;OutputApplied?.Invoke();return true;
            }catch(Exception e){Fail(e);return false;}
        }
        void Fail(Exception e){Ready=false;NativeError=e.Message;Status="NATIVE_UNAVAILABLE / "+e.Message;Changed?.Invoke();}
        void OnDestroy(){Ready=false;Epoch++;policy?.Dispose();}
    }
}
