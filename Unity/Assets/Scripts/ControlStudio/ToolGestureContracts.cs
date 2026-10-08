using System;
namespace HumanMotion.ControlStudio
{
    // ID 2 is retired (legacy Drill); the new virtual Nailing tool uses ID 4.
    public enum ToolKind { Gripper=0, Spray=1, Welding=3, Nailing=4 }
    public enum ClosureState { Unknown, Open, Closed }
    public struct HandClosureObservation
    {
        public long Id; public int SourceEpoch; public double Time; public float Norm;
        public bool Valid, Fresh, MaintainedManualIntent;
        public bool HasClosureState; public ClosureState ProvidedClosure;
        public string Provenance, InvalidReason;
    }
    // A future marker adapter must validate the same session/frame/arm pair and normalize
    // it upstream. This interface accepts no fabricated point, pixel distance or color mapping.
    public interface IHandClosureSource { event Action<HandClosureObservation> Observation; }
    public sealed class HandClosureInterpreter
    {
        // robot_config.h ratio hysteresis (.20,.40) mapped through norm=(ratio-.10)/(.60-.10).
        // These are NORMALIZED thresholds; they are not pixel or raw-ratio thresholds.
        public const float CloseThreshold=.2f, OpenThreshold=.6f;
        public ClosureState State {get;private set;}
        public void Reset(){State=ClosureState.Unknown;}
        public ClosureState Observe(float norm,ClosureState? provided=null)
        {
            if(provided.HasValue){State=provided.Value;return State;}
            if(norm<CloseThreshold)State=ClosureState.Closed;
            else if(norm>OpenThreshold)State=ClosureState.Open;
            return State;
        }
    }
    public struct ToolCommand { public ToolKind Tool; public bool Run; public int ToolEpoch; }
    public sealed class ToolRunGate
    {
        public const double StaleSeconds=.35;
        readonly HandClosureInterpreter closure=new HandClosureInterpreter();
        public ClosureState Gesture=>closure.State;
        public bool Enabled {get;private set;}
        public bool Running {get;private set;}
        public ToolKind Tool {get;private set;}
        public int SourceEpoch {get;private set;} public int ToolEpoch {get;private set;}
        public string State {get;private set;}="OFF"; public string Reason {get;private set;}="DISABLED";
        public HandClosureObservation Last {get;private set;}
        public bool HasObservation {get;private set;}
        bool armed,recover;string external="";long lastId=long.MinValue;double lastTime=double.NegativeInfinity;
        public ToolCommand Command=>new ToolCommand{Tool=Tool,Run=Running,ToolEpoch=ToolEpoch};
        public double Age(double now)=>HasObservation?Math.Max(0,now-Last.Time):double.PositiveInfinity;
        public void Configure(bool enable,ToolKind tool,int epoch,int toolEpoch)
        {
            if(Enabled==enable&&Tool==tool&&SourceEpoch==epoch&&ToolEpoch==toolEpoch)return;
            if(SourceEpoch!=epoch){lastId=long.MinValue;lastTime=double.NegativeInfinity;HasObservation=false;}
            Enabled=enable;Tool=tool;SourceEpoch=epoch;ToolEpoch=toolEpoch;Rearm("CONTROL CHANGE");
        }
        public void Rearm(string reason)
        {armed=false;Running=false;recover=false;closure.Reset();State=Enabled&&Tool!=ToolKind.Gripper?"WAIT_OPEN":"OFF";Reason=reason;}
        public void SetExternalBlock(string reason)
        {
            reason=reason??"";if(external==reason)return;external=reason;Rearm(reason.Length==0?"RECOVERY: OPEN REQUIRED":reason);
            if(external.Length>0){State=Enabled&&Tool!=ToolKind.Gripper?"BLOCKED":"OFF";Reason=external;}
        }
        void Block(string why){armed=false;Running=false;closure.Reset();recover=true;State="BLOCKED";Reason=why;}
        public void Advance(double now)
        {if(Enabled&&Tool!=ToolKind.Gripper&&HasObservation&&!Last.MaintainedManualIntent&&now-Last.Time>StaleSeconds)Block("STALE");}
        public void Observe(HandClosureObservation o,double now)
        {
            Advance(now);
            if(o.SourceEpoch!=SourceEpoch){Block("SOURCE / EPOCH MISMATCH");return;}
            if(!o.Valid||float.IsNaN(o.Norm)||float.IsInfinity(o.Norm)||o.Norm<0||o.Norm>1||double.IsNaN(o.Time)||double.IsInfinity(o.Time))
            {if(Enabled&&Tool!=ToolKind.Gripper)Block(o.InvalidReason??"INVALID");return;}
            if(!o.MaintainedManualIntent&&!o.Fresh){if(Enabled&&Tool!=ToolKind.Gripper)Block("NOT FRESH / HOLD");return;}
            if(o.Id==lastId||o.Time<lastTime)return; // A repeated row is never a new observation.
            if(o.Time>now+1e-6){Block("FUTURE TIME");return;}
            lastId=o.Id;lastTime=o.Time;Last=o;HasObservation=true;
            if(!Enabled||Tool==ToolKind.Gripper){State="OFF";Running=false;return;}
            if(external.Length>0){State="BLOCKED";Reason=external;return;}
            if(!o.MaintainedManualIntent&&now-o.Time>StaleSeconds){Block("STALE");return;}
            if(recover){Rearm("RECOVERY: OPEN REQUIRED");}
            var state=closure.Observe(o.Norm,o.HasClosureState?(ClosureState?)o.ProvidedClosure:null);
            if(state==ClosureState.Open){armed=true;Running=false;State="READY";Reason="OPEN";}
            else if(state==ClosureState.Closed&&armed){Running=true;State="RUNNING";Reason="CLOSED / HOLD TO RUN";}
            else {Running=false;State="WAIT_OPEN";Reason="NEW OPEN REQUIRED";}
        }
    }
}
