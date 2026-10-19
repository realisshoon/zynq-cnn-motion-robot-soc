using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Opt-in only. Executes production Connect/Disconnect/Update with an injected
    // in-memory transport. There is no SerialPort, no board, and no physical TX.
    public sealed class CsvPortOwnerAcceptance : MonoBehaviour
    {
        sealed class FakePort : IBoardUartTransport
        {
            public bool Opened {get;private set;}=true;public string Error {get;set;}="";
            public string PortName {get;set;}public string LogPath=>"";public long SentBytes {get;private set;}
            public BoardUartDiagnostics Diagnostics=>null;
            readonly Queue<BoardUartTransport.Chunk> rx=new Queue<BoardUartTransport.Chunk>();
            readonly Queue<BoardUartTransport.Receipt> receipts=new Queue<BoardUartTransport.Receipt>();
            public readonly List<string> Wires=new List<string>();
            public void Feed(string text){var b=Encoding.ASCII.GetBytes(text+"\r\n");rx.Enqueue(new BoardUartTransport.Chunk{Bytes=b,ReceivedAt=BoardUartTransport.Now});}
            public bool TryRead(out BoardUartTransport.Chunk c){c=rx.Count>0?rx.Dequeue():null;return c!=null;}
            public bool TryReceipt(out BoardUartTransport.Receipt r){r=receipts.Count>0?receipts.Dequeue():null;return r!=null;}
            public bool Enqueue(long id,byte[] wire,bool priority){if(!Opened||Error.Length!=0)return false;Wires.Add(Encoding.ASCII.GetString(wire));SentBytes+=wire.Length;receipts.Enqueue(new BoardUartTransport.Receipt{Id=id,WrittenAt=BoardUartTransport.Now});return true;}
            public void Dispose(){Opened=false;rx.Clear();receipts.Clear();}
        }
        string output;readonly List<string> log=new List<string>();readonly List<FakePort> ports=new List<FakePort>();
        BoardUartSource boards;SingleArmCommandRouter router;CsvRecordedHumanSource csv;ControlStudioBoardControlPanel control;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void Optional(){var a=Environment.GetCommandLineArgs();int i=Array.IndexOf(a,"--csv-port-owner-verify");if(i>=0)new GameObject("Explicit CSV port owner acceptance").AddComponent<CsvPortOwnerAcceptance>().output=a[i+1];}
        void Check(bool yes,string why){log.Add((yes?"PASS ":"FAIL ")+why);Save();if(!yes)throw new Exception(why);}
        void Save()=>File.WriteAllLines(Path.Combine(output,"port_owner_result.txt"),log);
        FakePort Last=>ports.Last();
        string State(BoardSide side)=>"[REC_STATE] schema=1 side="+(side==BoardSide.Left?"l":"r")+" ui=IDLE mode=LIVE pwm=1 ram_samples=0 entries=0 selected=0 playing=0 delete=0 unsaved=0";
        string Tk(uint n)=>"TK,"+n+","+(n*20)+",100,105,130,120,110,0.2,1500,1500,1500,1500,1800,0.5,1,0";
        IEnumerator Capture(string name){yield return null;yield return new WaitForEndOfFrame();var t=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,name+".png"),t.EncodeToPNG());Destroy(t);}
        IEnumerator Start(){Directory.CreateDirectory(output);Application.runInBackground=true;var run=Tests();while(true){bool next;object value=null;try{next=run.MoveNext();if(next)value=run.Current;}catch(Exception e){log.Add("RESULT=FAIL "+e);Save();yield break;}if(!next)break;yield return value;}log.Add("RESULT=PASS");Save();}
        IEnumerator Tests(){yield return new WaitForSecondsRealtime(2);router=FindFirstObjectByType<SingleArmCommandRouter>();csv=router.Csv;boards=router.GetComponent<BoardUartSource>();control=router.GetComponent<ControlStudioBoardControlPanel>();
            var menu=router.GetComponent<ControlStudioStartupMenu>();menu.Cycle(1,1-menu.Choice(1));Check(menu.StartStudio()&&router.Source=="CSV","original CSV Startup retained");router.GetComponent<ControlStudioDockLayout>().CloseDetails();router.enabled=false;
            var originalFactory=boards.TransportFactory;boards.TransportFactory=(port,baud,path,d)=>{var p=new FakePort{PortName=port};ports.Add(p);return p;};
            string sample=Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/cnn_agent1_result.csv");
            foreach(int width in new[]{1920,1280}){Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.6f);Check(Screen.width==width,"actual Player resolution "+width);
                foreach(int target in new[]{1,0}){control.TrySelectTarget(target);yield return null;Check(csv.Load(sample)&&csv.ConfirmSingleTarget(),"explicit single human role confirmation "+target);csv.PlayFromStart();router.AdvanceVirtual(.4);csv.Pause();
                    var side=target==1?BoardSide.Left:BoardSide.Right;int epoch=router.Epoch;double time=csv.Timeline.Time;var data=csv.Data;var applied=(float[])router.Applied.Clone();
                    Check(boards.Connect(side,"FAKE_"+side,921600),"production Connect with in-memory transport "+side);var port=Last;yield return null;Check(router.Source=="CSV"&&router.Epoch==epoch&&csv.Data==data&&csv.Timeline.Time==time,"Connect preserves CSV owner/epoch/clip/clock");
                    Check(port.Wires.Count==0,"Connect auto TX zero");port.Feed(State(side));port.Feed(Tk(1));yield return null;Check(boards.Session(side).Latest?.Tick==1&&boards.Session(side).Trusted,"real RX reducer parses fake port TK and role state");Check(router.Applied.SequenceEqual(applied)&&router.Source=="CSV","PWM1/w1 TK cannot overwrite paused CSV transforms");
                    Check(CsvBoardDeliveryStatus.Describe(boards,target).Contains("CSV 입력 미지원"),"connected unsupported firmware never reported as accepted/applied");
                    Check(!boards.Control(side,'E')&&!boards.Control(side,'A')&&!boards.Control(side,'O')&&!boards.Control(side,'H')&&port.Wires.Count==0,"existing hardware command owner gate preserved in CSV");
                    boards.ConfirmCommandBoundary(side,true);Check(boards.Query(side,'V'),"explicit status query available without owner switch");yield return null;port.Feed(State(side));yield return null;Check(boards.Commands(side).State==BoardRequestState.Accepted&&port.Wires.SequenceEqual(new[]{"V"}),"status response correlation / V byte unchanged / no automatic query");
                    csv.Resume();router.AdvanceVirtual(.2);port.Feed(Tk(2));yield return null;Check(csv.Timeline.Time>time&&router.Source=="CSV"&&boards.ApplicationState(side).UnityApplyCount==0,"CSV playback continues while TK RX stays telemetry only");csv.Stop();applied=(float[])router.Applied.Clone();time=csv.Timeline.Time;port.Feed(Tk(3));yield return null;Check(csv.Stopped&&router.Applied.SequenceEqual(applied)&&csv.Timeline.Time==time,"STOP holds CSV despite ongoing UART RX");
                    yield return new WaitForSecondsRealtime(.15f);yield return Capture(width+"_CSV_"+side+"_connected_unsupported");
                    int generation=boards.Session(side).Generation;boards.Disconnect(side);yield return null;Check(router.Source=="CSV"&&router.Epoch==epoch&&csv.Data==data,"Disconnect retains CSV input and loaded clip");Check(boards.Connect(side,"FAKE_NEW_"+side,921600),"manual reconnect via production API");port=Last;yield return null;Check(router.Source=="CSV"&&router.Epoch==epoch&&boards.Session(side).Generation>generation&&!boards.Session(side).Trusted,"reconnect keeps CSV / new port generation needs fresh identity");
                    csv.PlayFromStart();router.AdvanceVirtual(.2);Check(port.Wires.Count==0,"PLAY never sends invented CSV command, E/A, or queries");
                    port.Error="FAKE READ FAILURE";yield return null;Check(!boards.Session(side).Connected&&router.Source=="CSV","port error cannot select UART owner");Check(port.Wires.Count==0,"read failure has no retry/TX");boards.Disconnect(side);csv.Stop();
                }
                Check(!router.GetComponent<ControlStudioCsvPlaybackPanel>().Root.GetComponentsInChildren<Button>(true).Any(b=>b.name.StartsWith("CSV output")),"CSV single PLAY UI has no Virtual/Real/Both buttons");
            }
            Check(csv.SelectManual(),"explicit Manual handoff");int manualEpoch=router.Epoch;Check(boards.Connect(BoardSide.Right,"FAKE_MANUAL",921600),"Manual port connection");yield return null;Check(router.Source=="MANUAL"&&router.Epoch==manualEpoch,"Manual owner also independent of port");Last.Feed(State(BoardSide.Right));Last.Feed(Tk(1));yield return null;Check(router.Source=="MANUAL"&&boards.Right.Latest?.Tick==1,"Manual RX is parsed without claiming visual owner");
            Check(boards.SelectSource()&&router.Source=="BOARD","explicit UART input handoff retained");Last.Feed(Tk(2));yield return null;Check(router.HasApplied&&router.Applied[0]==105,"UART mode still applies verified TK to existing G51");
            Check(boards.Connect(BoardSide.Left,"FAKE_LEFT",921600),"independent LEFT connection");yield return null;var left=Last;var right=ports[ports.Count-2];Check(!boards.Connect(BoardSide.Left,"FAKE_MANUAL",921600)&&left.Opened&&right.Opened,"duplicate port rejected without disconnecting either board");
            boards.Disconnect(BoardSide.Left);Check(right.Opened,"LEFT disconnect preserves RIGHT transport");boards.Disconnect(BoardSide.Right);boards.TransportFactory=originalFactory;
            Check(ports.Sum(p=>p.Wires.Count)==4&&ports.SelectMany(p=>p.Wires).All(w=>w=="V"),"four explicitly requested MOCK V bytes only; no targets/E/A/O/H/S/X");
            Check(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"physical UART TX zero; all connections injected, no COM opened");router.enabled=true;
        }
    }
}
