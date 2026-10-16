using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using HumanMotion.ControlStudio;

static class Program
{
    static int checks;
    static void Check(bool yes,string text){if(!yes)throw new Exception("FAIL "+text);checks++;Console.WriteLine("PASS "+text);}
    sealed class Rig : IDisposable
    {
        public BoardUartSession Session;
        public BoardCommandGateway Gateway;
        public BoardCameraOverlayController Overlay;
        public readonly List<byte[]> Wires=new();
        public double Now=10;public bool Simulation,Boundary=true,FailEnqueue;public long Receipt;
        public Rig(BoardSide side,bool verified=true){Session=new BoardUartSession(side);Session.Begin();
            Gateway=new BoardCommandGateway(Session,()=>true,()=>Boundary,()=>Simulation,(id,wire,stop)=>{
                if(FailEnqueue)return false;Wires.Add(wire);Receipt=id;Gateway.Sent(id,Now);return true;
            },()=>Now,request=>BoardCameraOverlayController.IsToggleRequest(request)||request.Wire.Length==1&&request.Wire[0]=='V');
            Overlay=new BoardCameraOverlayController(Gateway);if(verified)State();}
        public void Feed(string line,double? at=null){if(!at.HasValue)Now+=.01;var b=Encoding.ASCII.GetBytes(line+"\r\n");Session.Feed(b,b.Length,at??Now);}
        public void State(string ui="IDLE"){Feed("[REC_STATE] schema=1 side="+(Session.Side==BoardSide.Right?"r":"l")+" ui="+ui+" mode=LIVE pwm=0 ram_samples=0 entries=0 selected=0 playing=0 delete=0 unsaved=0");}
        public void Dispose(){Overlay.Dispose();Gateway.Dispose();}
    }
    static void Main()
    {
        foreach(var pair in new[]{("HDMI overlay: robot 6 points",BoardCameraOverlayMode.RobotSixPoints),("HDMI overlay: body joints (next CNN result)",BoardCameraOverlayMode.BodyJoints)})
            Check(BoardCameraOverlayController.TryParse(pair.Item1,out var m)&&m==pair.Item2,"exact existing firmware response "+pair.Item1);
        foreach(var invalid in new[]{"HDMI overlay: BODY JOINTS","prefix HDMI overlay: body joints","HDMI overlay: body joints unexpected","HDMI overlay: robot 6 points trailing","HDMI overlay: upper body","body joints",""})
            Check(!BoardCameraOverlayController.TryParse(invalid,out _),"malformed/unrelated response ignored: "+invalid);
        using(var r=new Rig(BoardSide.Right))using(var l=new Rig(BoardSide.Left)){
            Check(r.Wires.Count+l.Wires.Count==0,"construct/connect/state observation no automatic TX");
            Check(r.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown&&l.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown,"no assumed default overlay mode");
            Check(BoardCameraOverlayController.ToggleSelected(r.Overlay,l.Overlay,0,out _),"RIGHT request");
            Check(r.Wires.Single().SequenceEqual(new[]{(byte)'b'})&&l.Wires.Count==0,"RIGHT wire is one lowercase b only");
            Check(r.Gateway.State==BoardRequestState.AwaitingReply&&r.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown,"write receipt is not display confirmation");
            int n=r.Wires.Count;Check(!r.Overlay.Toggle(out _)&&r.Wires.Count==n,"double click while pending cannot toggle twice");
            r.Feed("HDMI overlay: robot 6 points",r.Now-1);Check(r.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown,"pre-write buffered response ignored");
            r.Feed("[PWM] enabled=1");Check(r.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown,"PWM response cannot confirm overlay");
            l.Feed("HDMI overlay: robot 6 points");Check(l.Overlay.ReportedMode==BoardCameraOverlayMode.RobotSixPoints&&r.Gateway.Busy,"LEFT unsolicited status cannot ACK RIGHT request");
            r.Feed("HDMI overlay: body joints (next CNN result)");Check(r.Overlay.ReportedMode==BoardCameraOverlayMode.BodyJoints&&r.Gateway.State==BoardRequestState.Accepted,"only matching RIGHT response confirms full joints");
            Check(BoardCameraOverlayController.ToggleSelected(r.Overlay,l.Overlay,1,out _),"LEFT request");l.Feed("HDMI overlay: body joints");
            Check(l.Wires.Count==1&&r.Wires.Count==1&&l.Overlay.ReportedMode==BoardCameraOverlayMode.BodyJoints,"LEFT independent one b / confirmed response");
            Check(BoardCameraOverlayController.ToggleSelected(r.Overlay,l.Overlay,2,out _),"BOTH request after independent preflight");
            Check(r.Wires.Count==2&&l.Wires.Count==2&&r.Wires.Concat(l.Wires).All(w=>w.SequenceEqual(new[]{(byte)'b'})),"BOTH two existing ports; no r/l/both prefix, newline or extra command");
            r.Feed("HDMI overlay: robot 6 points (next CNN result)");Check(r.Overlay.ReportedMode==BoardCameraOverlayMode.RobotSixPoints&&l.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown,"BOTH response states independent");
            l.Feed("HDMI overlay: body joints (next CNN result)");Check(l.Overlay.ReportedMode==BoardCameraOverlayMode.BodyJoints,"BOTH may remain different; no synthetic forced synchronization");
            r.Boundary=false;n=r.Wires.Count;Check(!r.Overlay.Toggle(out _)&&r.Wires.Count==n,"existing input boundary guard");r.Boundary=true;
            r.State("NAME");Check(!r.Overlay.Toggle(out _)&&r.Wires.Count==n,"name menu protected");r.State();
            r.Simulation=true;Check(!r.Overlay.Toggle(out _)&&r.Wires.Count==n,"RX-only fixture cannot send");r.Simulation=false;
            Check(!r.Gateway.TryRequest(new BoardRequest{Name="PWM E",Wire=new[]{(byte)'E'},Match=(_,__)=>BoardReply.Accepted},out _)&&r.Wires.Count==n,"CSV/display exception cannot send E");
            Check(!r.Gateway.TryRequest(new BoardRequest{Name="unclassified b",Wire=new[]{(byte)'b'},Match=(_,__)=>BoardReply.Accepted},out _)&&r.Wires.Count==n,"owner exception restricted to exact display request");
            l.Session.Disconnect();l.Overlay.Refresh();Check(l.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown,"disconnect invalidates display report");
            Check(!BoardCameraOverlayController.ToggleSelected(r.Overlay,l.Overlay,2,out _)&&r.Wires.Count==n,"unavailable BOTH peer blocks preflight without touching RIGHT");
            l.Session.Begin();l.Overlay.Refresh();Check(!l.Overlay.CanToggle(out _)&&l.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown,"reconnect needs new role state and new overlay response");l.State();
            Check(l.Overlay.Toggle(out _),"manual request after new state");l.Now+=4;l.Gateway.Advance(l.Now);
            Check(l.Overlay.RequestState==BoardRequestState.TimedOut&&l.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown,"timeout gives UNKNOWN, never assumed mode");
            n=l.Wires.Count;l.Now+=20;l.Gateway.Advance(l.Now);Check(l.Wires.Count==n&&!l.Overlay.Toggle(out _),"no automatic retry and existing uncertain-outcome guard");
        }
        using(var r=new Rig(BoardSide.Right))using(var l=new Rig(BoardSide.Left)){
            l.FailEnqueue=true;Check(!BoardCameraOverlayController.ToggleSelected(r.Overlay,l.Overlay,2,out _),"second enqueue failure reported");
            Check(r.Wires.Count==1&&l.Wires.Count==0,"no rollback/retry toggle on first board after second enqueue failure");
        }
        using(var r=new Rig(BoardSide.Right)){
            Check(r.Overlay.Toggle(out _),"pending request before firmware reboot");r.Feed("EV,0,BOOT,1");r.Gateway.Advance(r.Now);r.Overlay.Refresh();
            Check(r.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown&&r.Overlay.RequestState==BoardRequestState.Unknown,"BOOT generation invalidates pending and confirmed mode");
        }
        using(var r=new Rig(BoardSide.Right)){
            Check(r.Overlay.Toggle(out _),"explicit rejection test request");r.Feed("CNN 'b' rejected: menu active");
            Check(r.Overlay.RequestState==BoardRequestState.Rejected&&r.Overlay.ReportedMode==BoardCameraOverlayMode.Unknown,"board rejection shown, no guessed mode");
        }
        using(var r=new Rig(BoardSide.Right,false)){Check(!r.Overlay.CanToggle(out _)&&r.Wires.Count==0,"unverified role blocks display TX");}
        Console.WriteLine("RESULT=PASS checks="+checks+" actual_COM=0 actual_UART_TX=0 (all byte assertions use in-memory gateways)");
    }
}
