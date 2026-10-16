using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Explicit offline opt-in only. Production source/gateway/UI with in-memory
    // transports. Does not instantiate SerialPort or send to a physical board.
    public sealed class CameraOverlayAcceptance : MonoBehaviour
    {
        sealed class FakePort : IBoardUartTransport
        {
            public bool Opened {get;private set;}=true;public string Error=>"";public string PortName {get;set;}public string LogPath=>"";
            public long SentBytes {get;private set;}public BoardUartDiagnostics Diagnostics=>null;
            readonly Queue<BoardUartTransport.Chunk> rx=new Queue<BoardUartTransport.Chunk>();
            readonly Queue<BoardUartTransport.Receipt> receipts=new Queue<BoardUartTransport.Receipt>();
            public readonly List<string> Wires=new List<string>();
            public void Feed(string line){var b=Encoding.ASCII.GetBytes(line+"\r\n");rx.Enqueue(new BoardUartTransport.Chunk{Bytes=b,ReceivedAt=BoardUartTransport.Now});}
            public bool TryRead(out BoardUartTransport.Chunk c){c=rx.Count>0?rx.Dequeue():null;return c!=null;}
            public bool TryReceipt(out BoardUartTransport.Receipt r){r=receipts.Count>0?receipts.Dequeue():null;return r!=null;}
            public bool Enqueue(long id,byte[] wire,bool priority){if(!Opened)return false;Wires.Add(Encoding.ASCII.GetString(wire));SentBytes+=wire.Length;receipts.Enqueue(new BoardUartTransport.Receipt{Id=id,WrittenAt=BoardUartTransport.Now});return true;}
            public void Dispose(){Opened=false;rx.Clear();receipts.Clear();}
        }
        string output;readonly List<string> log=new List<string>();readonly List<FakePort> ports=new List<FakePort>();
        BoardUartSource boards;ControlStudioBoardControlPanel panel;ControlStudioDockLayout dock;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void Optional(){var a=Environment.GetCommandLineArgs();int i=Array.IndexOf(a,"--camera-overlay-verify");if(i>=0)new GameObject("Explicit camera overlay fake UART acceptance").AddComponent<CameraOverlayAcceptance>().output=a[i+1];}
        void Check(bool yes,string why){log.Add((yes?"PASS ":"FAIL ")+why);Save();if(!yes)throw new Exception(why);}
        void Save()=>File.WriteAllLines(Path.Combine(output,"overlay_result.txt"),log);
        Transform Find(string name)=>dock.DetailRoot.GetComponentsInChildren<Transform>(true).First(t=>t.name==name);
        void Scroll(Transform t){Canvas.ForceUpdateCanvases();var s=dock.DetailScroll;float max=Mathf.Max(0,s.content.rect.height-s.viewport.rect.height);float y=-s.content.InverseTransformPoint(t.TransformPoint(((RectTransform)t).rect.center)).y;s.verticalNormalizedPosition=max>0?1-Mathf.Clamp(y-s.viewport.rect.height/2,0,max)/max:1;Canvas.ForceUpdateCanvases();}
        void Click(Transform t){var b=t.GetComponent<Button>();Check(b.interactable&&t.gameObject.activeInHierarchy,"visible enabled "+t.name);var d=new PointerEventData(EventSystem.current){position=RectTransformUtility.WorldToScreenPoint(null,t.TransformPoint(((RectTransform)t).rect.center)),button=PointerEventData.InputButton.Left};var hits=new List<RaycastResult>();EventSystem.current.RaycastAll(d,hits);Check(hits.Count>0&&(hits[0].gameObject==t.gameObject||hits[0].gameObject.transform.IsChildOf(t)),"actual pointer hit "+t.name);ExecuteEvents.ExecuteHierarchy(t.gameObject,d,ExecuteEvents.pointerClickHandler);}
        string State(BoardSide side)=>"[REC_STATE] schema=1 side="+(side==BoardSide.Right?"r":"l")+" ui=IDLE mode=LIVE pwm=0 ram_samples=0 entries=0 selected=0 playing=0 delete=0 unsaved=0";
        IEnumerator Capture(string name){yield return null;yield return new WaitForEndOfFrame();var t=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,name+".png"),t.EncodeToPNG());Destroy(t);}
        IEnumerator Start(){Directory.CreateDirectory(output);Application.runInBackground=true;var run=Tests();while(true){bool next;object v=null;try{next=run.MoveNext();if(next)v=run.Current;}catch(Exception e){log.Add("RESULT=FAIL "+e);Save();yield break;}if(!next)break;yield return v;}log.Add("RESULT=PASS");Save();}
        void OnGUI(){if(boards!=null)GUI.Label(new Rect(8,Screen.height-25,950,25),"OFFLINE CAMERA OVERLAY / FAKE UART b ONLY / ACTUAL COM & PHYSICAL TX = 0");}
        IEnumerator Tests()
        {
            yield return new WaitForSecondsRealtime(2);var router=FindFirstObjectByType<SingleArmCommandRouter>();boards=router.GetComponent<BoardUartSource>();panel=router.GetComponent<ControlStudioBoardControlPanel>();dock=router.GetComponent<ControlStudioDockLayout>();
            var menu=router.GetComponent<ControlStudioStartupMenu>();Check(menu.Visible,"Startup retained");menu.Cycle(1,2-menu.Choice(1));Check(menu.StartStudio(),"existing UART Startup without automatic connect");dock.Select(ControlStudioDockLayout.Workspace.Control);yield return null;yield return new WaitForEndOfFrame();
            var button=Find("Camera overlay toggle").GetComponent<Button>();Check(!button.interactable&&!boards.Right.Connected&&!boards.Left.Connected,"disconnected overlay disabled, no automatic COM");button.onClick.Invoke();Check(boards.TxBytes(BoardSide.Right)+boards.TxBytes(BoardSide.Left)==0,"disabled listener cannot transmit");
            var factory=boards.TransportFactory;boards.TransportFactory=(port,baud,path,diagnostics)=>{Check(port.StartsWith("FAKE_OVERLAY_"),"only fake transports created");var p=new FakePort{PortName=port};ports.Add(p);return p;};
            foreach(int width in new[]{1920,1280}){
                boards.Disconnect(BoardSide.Right);boards.Disconnect(BoardSide.Left);Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.6f);Check(Screen.width==width,"actual Player resolution "+width);
                Check(router.Csv.PrepareInput()&&router.Source=="CSV","explicit CSV owner for display-only path");
                Check(boards.Connect(BoardSide.Right,"FAKE_OVERLAY_"+width+"_R",921600),"production RIGHT connect injected");var right=ports.Last();Check(boards.Connect(BoardSide.Left,"FAKE_OVERLAY_"+width+"_L",921600),"production LEFT connect injected");var left=ports.Last();
                right.Feed(State(BoardSide.Right));left.Feed(State(BoardSide.Left));yield return null;boards.ConfirmCommandBoundary(BoardSide.Right,true);boards.ConfirmCommandBoundary(BoardSide.Left,true);
                Check(right.Wires.Count+left.Wires.Count==0&&router.Source=="CSV","connect/status RX produces zero automatic TX and preserves CSV");
                Check(panel.Overlay(BoardSide.Right).ReportedMode==BoardCameraOverlayMode.Unknown&&panel.Overlay(BoardSide.Left).ReportedMode==BoardCameraOverlayMode.Unknown,"connection is not overlay state confirmation");
                dock.Select(ControlStudioDockLayout.Workspace.Control);panel.TrySelectTarget(0);yield return null;yield return new WaitForEndOfFrame();Scroll(button.transform);yield return null;yield return new WaitForEndOfFrame();
                Click(button.transform);yield return new WaitForSecondsRealtime(.025f);Check(right.Wires.SequenceEqual(new[]{"b"})&&left.Wires.Count==0,"RIGHT click sends one b to RIGHT fake port only");
                Check(panel.Overlay(BoardSide.Right).ReportedMode==BoardCameraOverlayMode.Unknown&&!button.interactable,"write/pending never displayed as mode success");button.onClick.Invoke();Check(right.Wires.Count==1,"duplicate pending listener blocked");
                right.Feed("HDMI overlay: body joints (next CNN result)");yield return null;yield return new WaitForEndOfFrame();Check(panel.Overlay(BoardSide.Right).ReportedMode==BoardCameraOverlayMode.BodyJoints,"RIGHT response reports full joints");
                panel.TrySelectTarget(1);yield return null;yield return new WaitForEndOfFrame();Click(button.transform);yield return new WaitForSecondsRealtime(.025f);left.Feed("HDMI overlay: robot 6 points (next CNN result)");yield return null;yield return new WaitForEndOfFrame();
                Check(left.Wires.SequenceEqual(new[]{"b"})&&right.Wires.Count==1&&panel.Overlay(BoardSide.Left).ReportedMode==BoardCameraOverlayMode.RobotSixPoints,"LEFT independent b / six-point response");
                panel.TrySelectTarget(2);yield return null;yield return new WaitForEndOfFrame();Click(button.transform);yield return new WaitForSecondsRealtime(.025f);right.Feed("HDMI overlay: robot 6 points (next CNN result)");left.Feed("HDMI overlay: body joints (next CNN result)");yield return null;yield return new WaitForEndOfFrame();
                Check(right.Wires.SequenceEqual(new[]{"b","b"})&&left.Wires.SequenceEqual(new[]{"b","b"}),"BOTH exact b bytes, no prefixes or queries");Check(panel.Overlay(BoardSide.Right).ReportLabel=="6점"&&panel.Overlay(BoardSide.Left).ReportLabel=="전체 관절점","BOTH different reported states remain independent");
                Check(router.Source=="CSV"&&!boards.Control(BoardSide.Right,'E')&&!boards.Control(BoardSide.Left,'A')&&right.Wires.Count==2&&left.Wires.Count==2,"CSV owner unchanged / motion TX guards preserved");
                Color color=button.GetComponent<Image>().color;yield return new WaitForSecondsRealtime(2);Check(button.interactable&&button.transition==Selectable.Transition.None&&button.GetComponent<Image>().color==color,"enabled color stable without blink");
                var label=button.GetComponentInChildren<Text>();Check(label.preferredWidth<=label.rectTransform.rect.width,"Korean button label fits detail width");foreach(char c in label.text.Where(c=>c>=0xAC00&&c<=0xD7A3).Distinct()){label.font.RequestCharactersInTexture(c.ToString(),label.fontSize,label.fontStyle);Check(label.font.GetCharacterInfo(c,out _,label.fontSize,label.fontStyle),"Korean glyph "+c);}
                var corners=new Vector3[4];((RectTransform)button.transform).GetWorldCorners(corners);Check(corners.All(c=>c.x>=0&&c.x<=Screen.width&&c.y>=0&&c.y<=Screen.height),"actual detail button unclipped "+width);yield return Capture(width+"_DETAIL_BOTH_reported");
                foreach(char key in "EASXOH")Check(Find("Board control "+key).gameObject.activeInHierarchy,"existing command UI retained "+key);
                Check(boards.SelectSource()&&router.Source=="BOARD","explicit UART owner still supported");dock.CloseDetails();yield return null;yield return new WaitForEndOfFrame();var compact=router.GetComponentsInChildren<Button>(true).First(b=>b.name=="Camera overlay toggle");
                Check(compact.gameObject.activeInHierarchy,"compact Dock overlay visible");((RectTransform)compact.transform).GetWorldCorners(corners);Check(corners.All(c=>c.x>=0&&c.x<=Screen.width&&c.y>=0&&c.y<=Screen.height),"compact button unclipped "+width);Check(compact.GetComponentInChildren<Text>().preferredWidth<=compact.GetComponentInChildren<Text>().rectTransform.rect.width,"compact caption fits "+width);yield return Capture(width+"_COMPACT_BOTH_reported");
                boards.Disconnect(BoardSide.Left);yield return null;yield return new WaitForEndOfFrame();int before=right.Wires.Count;Check(panel.Overlay(BoardSide.Left).ReportLabel=="UNKNOWN"&&!compact.interactable,"disconnect invalidates LEFT / BOTH disabled");compact.onClick.Invoke();Check(right.Wires.Count==before&&right.Opened,"BOTH missing peer cannot toggle/close RIGHT");
                panel.TrySelectTarget(0);yield return null;Check(compact.interactable,"RIGHT still independent after LEFT disconnect");yield return Capture(width+"_LEFT_disconnected_RIGHT_available");
                dock.Select(ControlStudioDockLayout.Workspace.Control);yield return null;yield return new WaitForEndOfFrame();
            }
            boards.Disconnect(BoardSide.Right);boards.Disconnect(BoardSide.Left);boards.TransportFactory=factory;
            Check(ports.Sum(p=>p.Wires.Count)==8&&ports.SelectMany(p=>p.Wires).All(w=>w=="b"),"8 explicitly clicked MOCK b bytes only; no E/A/S/X/O/H/V/T/t");
            Check(!boards.Right.Connected&&!boards.Left.Connected&&router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"actual COM never created / physical TX=0");
        }
    }
}
