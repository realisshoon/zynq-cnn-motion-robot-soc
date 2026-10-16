using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Explicit serial-free Player fixture. Replaces only the Records panel's
    // controller references with Fake UART, never BoardUartSource/Transport or
    // global output protection. Not reachable during ordinary program execution.
    public sealed class RecordsKoUxAcceptance : MonoBehaviour
    {
        string output;readonly List<string> log=new List<string>();
        BoardUartSource boards;ControlStudioDockLayout dock;ControlStudioRecordingPanel panel;
        Rig right,left;bool fixtureVisible;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void OptionalRun(){var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--records-ko-ux-verify");if(i<0||i+1>=args.Length)return;new GameObject("Explicit KO Records Fake UART verification").AddComponent<RecordsKoUxAcceptance>().output=args[i+1];}
        void Check(bool ok,string text){log.Add((ok?"PASS: ":"FAIL: ")+text);if(!ok)throw new Exception(text);}
        void Save()=>File.WriteAllLines(Path.Combine(output,"records_ko_ux_result.txt"),log);
        bool SerialFree()=>boards.LogPath(BoardSide.Right).Length==0&&boards.LogPath(BoardSide.Left).Length==0&&boards.TxBytes(BoardSide.Right)==0&&boards.TxBytes(BoardSide.Left)==0;
        sealed class Rig:IDisposable
        {
            internal readonly BoardUartSession Session;internal readonly BoardCommandGateway Gateway;internal readonly BoardRecordingController Controller;internal readonly BoardRecordsUxFlow Flow;internal readonly List<string> Wires=new List<string>();readonly Func<bool> guard;
            internal Rig(BoardUartSession s,Func<bool> guard){this.guard=guard;Session=s;Session.Begin();Gateway=new BoardCommandGateway(s,guard,()=>true,()=>false,Enqueue,()=>BoardUartTransport.Now);Controller=new BoardRecordingController(Gateway);s.LineReceived+=Controller.ObserveLine;Flow=new BoardRecordsUxFlow(Controller,guard,()=>BoardUartTransport.Now);}
            bool Enqueue(long id,byte[] b,bool priority){if(!guard())return false;Wires.Add(Encoding.ASCII.GetString(b));Gateway.Sent(id,BoardUartTransport.Now);return true;}
            internal void Feed(string line){var b=Encoding.ASCII.GetBytes(line+"\r\n");Session.Feed(b,b.Length,BoardUartTransport.Now);}
            internal void State(string ui="IDLE",string mode="LIVE",bool pwm=true,bool unsaved=false,uint selected=0,uint playing=0)=>Feed("[REC_STATE] schema=1 side="+(Session.Side==BoardSide.Right?"r":"l")+" ui="+ui+" mode="+mode+" pwm="+(pwm?1:0)+" ram_samples=8 entries=1 selected="+selected+" playing="+playing+" delete=0 unsaved="+(unsaved?1:0));
            internal void Follow()=>Feed("[ST] async_test=1 pwm=1 result="+(Session.Side==BoardSide.Right?"enabled_UNSYNCHRONIZED_test":"enabled_remote_UNSYNCHRONIZED_test")+" mode=LIVE");
            internal void List(){string r=Session.Side==BoardSide.Right?"r":"l";Feed("[REC] SD records; select: "+r+" record select <number>");Feed("[REC] 3 take_3 samples=8 duration_ms=160");Feed("[REC] count=1; delete: "+r+" record delete <number>");}
            public void Dispose(){Flow.Dispose();Session.LineReceived-=Controller.ObserveLine;Gateway.Dispose();}
        }
        void OnGUI(){if(fixtureVisible)GUI.Label(new Rect(8,Screen.height-25,700,25),"OFFLINE FIXTURE / FAKE UART / NO COM / PHYSICAL TX = 0");}
        Transform Find(string name)=>dock.DetailRoot.GetComponentsInChildren<Transform>(true).First(t=>t.name==name);
        static Rect Bounds(RectTransform t){var p=new Vector3[4];t.GetWorldCorners(p);return Rect.MinMaxRect(p[0].x,p[0].y,p[2].x,p[2].y);}
        void Scroll(Transform t){Canvas.ForceUpdateCanvases();var s=dock.DetailScroll;float max=Mathf.Max(0,s.content.rect.height-s.viewport.rect.height);float y=-s.content.InverseTransformPoint(t.TransformPoint(((RectTransform)t).rect.center)).y;s.verticalNormalizedPosition=max>0?1-Mathf.Clamp(y-s.viewport.rect.height/2,0,max)/max:1;Canvas.ForceUpdateCanvases();}
        void Click(string name){var t=Find(name);Scroll(t);var b=t.GetComponent<Button>();Check(b!=null&&b.interactable&&t.gameObject.activeInHierarchy,"clickable "+name);var data=new PointerEventData(EventSystem.current){position=RectTransformUtility.WorldToScreenPoint(null,t.TransformPoint(((RectTransform)t).rect.center)),button=PointerEventData.InputButton.Left};var hits=new List<RaycastResult>();EventSystem.current.RaycastAll(data,hits);Check(hits.Count>0&&(hits[0].gameObject==t.gameObject||hits[0].gameObject.transform.IsChildOf(t)),"actual pointer hit "+name);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerDownHandler);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerUpHandler);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerClickHandler);}
        IEnumerator Capture(string name){yield return new WaitForSecondsRealtime(.15f);yield return new WaitForEndOfFrame();var image=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,name+".png"),image.EncodeToPNG());Destroy(image);Save();}
        IEnumerator Start(){Directory.CreateDirectory(output);Application.runInBackground=true;var run=Run();while(true){bool next;object value=null;try{next=run.MoveNext();if(next)value=run.Current;}catch(Exception e){log.Add("FAIL: "+e);Save();yield break;}if(!next)break;yield return value;}log.Add("RESULT=PASS");Save();Destroy(gameObject);}
        IEnumerator Run()
        {
            yield return new WaitForSecondsRealtime(2);var router=FindFirstObjectByType<SingleArmCommandRouter>();Check(router!=null&&router.Ready,"original KO controller initialized");
            var menu=router.GetComponent<ControlStudioStartupMenu>();Check(menu.Visible,"original Startup retained");menu.Cycle(1,2);Check(menu.StartStudio(),"Startup UART selection / no automatic COM");
            boards=router.GetComponent<BoardUartSource>();dock=router.GetComponent<ControlStudioDockLayout>();panel=router.GetComponent<ControlStudioRecordingPanel>();dock.Select(ControlStudioDockLayout.Workspace.Records);yield return null;
            Check(SerialFree()&&!boards.Right.Connected&&!boards.Left.Connected,"before fixture: actual ports disconnected TX=0");
            foreach(string name in new[]{"Start recording","Play SD record","Return to follow"}){var b=Find(name).GetComponent<Button>();Check(!b.interactable,"real-disconnected button locked "+name);b.onClick.Invoke();Check(SerialFree()&&!boards.Right.Connected&&!boards.Left.Connected,"disabled listener cannot open COM "+name);}
            // Private fixture references are swapped only after the serial-free
            // opt-in check. The actual Source gateway and all TX policies remain.
            var flags=BindingFlags.Instance|BindingFlags.NonPublic;
            var controllers=(BoardRecordingController[])typeof(ControlStudioRecordingPanel).GetField("controllers",flags).GetValue(panel);
            var flows=(BoardRecordsUxFlow[])typeof(ControlStudioRecordingPanel).GetField("flows",flags).GetValue(panel);
            var observers=(Action<string,double>[])typeof(ControlStudioRecordingPanel).GetField("observers",flags).GetValue(panel);
            right=new Rig(boards.Right,SerialFree);left=new Rig(boards.Left,SerialFree);
            for(int i=0;i<2;i++){var s=boards.Session(i==0?BoardSide.Right:BoardSide.Left);s.LineReceived-=observers[i];flows[i].Dispose();var rig=i==0?right:left;controllers[i]=rig.Controller;flows[i]=rig.Flow;observers[i]=rig.Controller.ObserveLine;}
            fixtureVisible=true;
            foreach(int width in new[]{1920,1280}){
                Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.6f);Check(Screen.width==width&&Screen.height==width*9/16,"actual resolution "+width);
                right.State();right.Feed("[ST] async_test=0 pwm=1 mode=LIVE");right.Feed("GM,0,0,0,0,0,0,0,0,0");left.State();yield return null;
                Click("Refresh SD list");yield return new WaitForSecondsRealtime(.01f);right.List();yield return null;right.Wires.Clear();
                Check(Find("Play SD record").GetComponent<Button>().interactable&&right.Session.FollowEnabled==false,"PLAY button enabled with Follow OFF");
                Click("Play SD record");yield return null;Check(right.Wires.SequenceEqual(new[]{"P"}),"pointer PLAY sends fake P once");right.State("STOP_MENU");yield return null;Check(right.Wires.Count==1,"wait until MENU/HOLDING");right.State("MENU","HOLDING");yield return null;Check(right.Wires.SequenceEqual(new[]{"P","!REC,SELECT,3\r"}),"actual panel sends correct SELECT once");
                right.Feed("[REC] PLAY id=3 name=take_3 samples=8 repeat=1");right.State(mode:"ALIGNING",selected:3,playing:3);yield return null;right.State(mode:"PLAYING",selected:3,playing:3);yield return null;Check(!right.Flow.Busy,"PLAYING complete");
                Check(left.Wires.Count==0,"RIGHT requests never reach LEFT");Click("Stop playback");yield return null;right.Feed("[REC] playback stop=1 mode=LIVE");right.State(selected:3);yield return null;
                right.State("NAME","HOLDING",unsaved:true);Find("SD record name").GetComponent<InputField>().text="New_4";yield return null;right.Wires.Clear();Click("Save SD recording");yield return null;right.Feed("[REC] SAVED id=4 name=New_4 samples=8; persistent on SD");yield return null;Check(!right.Flow.SaveConfirmed,"SAVED alone not success");right.State("MENU","HOLDING",selected:4);yield return null;
                Check(right.Flow.SaveConfirmed&&Find("Return to follow").GetComponent<Button>().interactable,"saved MENU enables explicit return");Check(right.Wires.SequenceEqual(new[]{"!REC,NAME,New_4\r"}),"save never automatically returns");
                Scroll(Find("Start recording"));yield return Capture(width+"_KO_saved_MENU");
                right.Wires.Clear();Click("Return to follow");yield return null;Check(right.Wires.SequenceEqual(new[]{"V"}),"return click V only");right.State("MENU","HOLDING",selected:4);yield return null;Check(right.Wires.Last()=="!REC,CANCEL\r","verified save then CANCEL");right.State("IDLE","HOLDING",selected:4);yield return null;Check(right.Wires.Count==2,"IDLE not LIVE does not issue A");right.State(selected:4);yield return null;yield return null;Check(right.Wires.Last()=="A","confirmed LIVE then A");right.Follow();yield return null;Check(right.Wires.Last()=="T","A approved then T");right.Follow();yield return null;Check(!right.Flow.Busy&&right.Flow.Message.Contains("LIVE"),"return approval completed");
                Scroll(Find("Recording feedback"));yield return Capture(width+"_KO_LIVE_return");
                foreach(string name in new[]{"Start recording","Cancel preparation","Finish recording","Enter recording menu","Stop playback","Refresh SD list","Save SD recording","SD catalog IDs","Play SD record","Request SD deletion","Confirm SD deletion","Cancel recording workflow","Return to follow"}){
                    var t=Find(name);Scroll(t);yield return null;var r=Bounds((RectTransform)t);var v=Bounds(dock.DetailScroll.viewport);Check(r.xMin>=v.xMin-.1f&&r.xMax<=v.xMax+.1f&&r.yMin>=v.yMin-.1f&&r.yMax<=v.yMax+.1f,width+" scrollable unclipped "+name);
                    var text=t.GetComponentsInChildren<Text>().FirstOrDefault();if(text!=null){Check(text.preferredWidth<=text.rectTransform.rect.width+1,"original Korean caption fits "+name);text.font.RequestCharactersInTexture(text.text,text.fontSize,text.fontStyle);foreach(char c in text.text.Where(c=>c>=0xAC00&&c<=0xD7A3).Distinct())Check(text.font.GetCharacterInfo(c,out _,text.fontSize,text.fontStyle),"glyph "+c);}
                }
                Check(SerialFree(),"physical UART TX=0; all above wires are fake");
            }
        }
        void OnDestroy(){right?.Dispose();left?.Dispose();}
    }
}
