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
    // Explicit opt-in UI fixture. Never creates a SerialPort or changes TX guards.
    public sealed class CnnSettingsAcceptance : MonoBehaviour
    {
        string output; readonly List<string> log=new List<string>();
        BoardUartSource source; ControlStudioDockLayout dock; Rig right,left;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void OptionalRun(){var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--cnn-settings-verify");if(i<0||i+1>=args.Length)return;new GameObject("Explicit CNN Settings Fake UART verification").AddComponent<CnnSettingsAcceptance>().output=args[i+1];}
        void Check(bool ok,string text){log.Add((ok?"PASS: ":"FAIL: ")+text);if(!ok)throw new Exception(text);}
        void Save()=>File.WriteAllLines(Path.Combine(output,"cnn_settings_result.txt"),log);
        bool SerialFree()=>source.LogPath(BoardSide.Right).Length==0&&source.LogPath(BoardSide.Left).Length==0&&source.TxBytes(BoardSide.Right)==0&&source.TxBytes(BoardSide.Left)==0;
        sealed class Rig:IDisposable
        {
            internal readonly BoardUartSession Session;internal readonly BoardCommandGateway Gateway;internal readonly BoardConfigurationController Config;
            internal readonly List<string> Wires=new List<string>();
            internal Rig(BoardUartSession s,Func<bool> guard){Session=s;Session.Begin();Gateway=new BoardCommandGateway(s,guard,()=>true,()=>false,(id,b,p)=>{if(!guard())return false;Wires.Add(Encoding.ASCII.GetString(b));Gateway.Sent(id,BoardUartTransport.Now);return true;},()=>BoardUartTransport.Now);Config=new BoardConfigurationController(Gateway,()=>BoardUartTransport.Now);Session.LineReceived+=Config.ObserveLine;State();}
            internal void Feed(string line){var b=Encoding.ASCII.GetBytes(line+"\r\n");Session.Feed(b,b.Length,BoardUartTransport.Now);}
            internal void State()=>Feed("[REC_STATE] schema=1 side="+(Session.Side==BoardSide.Right?"r":"l")+" ui=IDLE mode=LIVE pwm=0 ram_samples=0 entries=0 selected=0 playing=0 delete=0 unsaved=0");
            internal void Cnn(bool on,bool running=false){Feed("[CFG] supported=1 role="+(Session.Side==BoardSide.Right?"2":"1")+" protocol=@CFG schema=1; show/save/load");Feed(" CNN status : "+(running?"00000002":"00000000"));Feed(" CNN error : 00000000");Feed("Frame capture: folder=0:/CALIB, pending=0");Feed("CNN app state: running="+(running?1:0)+" auto="+(on?1:0)+" single=0 stopping=0 fault=0");}
            public void Dispose(){Session.LineReceived-=Config.ObserveLine;Gateway.Dispose();}
        }
        Transform Find(string name)=>dock.DetailRoot.GetComponentsInChildren<Transform>(true).First(t=>t.name==name);
        static Rect Bounds(RectTransform t){var a=new Vector3[4];t.GetWorldCorners(a);return Rect.MinMaxRect(a[0].x,a[0].y,a[2].x,a[2].y);}
        void Scroll(Transform t){Canvas.ForceUpdateCanvases();var s=dock.DetailScroll;float max=Mathf.Max(0,s.content.rect.height-s.viewport.rect.height);float y=-s.content.InverseTransformPoint(t.TransformPoint(((RectTransform)t).rect.center)).y;s.verticalNormalizedPosition=max>0?1-Mathf.Clamp(y-s.viewport.rect.height/2,0,max)/max:1;Canvas.ForceUpdateCanvases();}
        void Click(string name){var t=Find(name);var b=t.GetComponent<Button>();Check(b.interactable&&t.gameObject.activeInHierarchy,"enabled "+name);var data=new PointerEventData(EventSystem.current){position=RectTransformUtility.WorldToScreenPoint(null,t.TransformPoint(((RectTransform)t).rect.center)),button=PointerEventData.InputButton.Left};var hits=new List<RaycastResult>();EventSystem.current.RaycastAll(data,hits);Check(hits.Count>0&&(hits[0].gameObject==t.gameObject||hits[0].gameObject.transform.IsChildOf(t)),"actual pointer hit "+name);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerClickHandler);}
        IEnumerator Capture(string name){yield return null; yield return new WaitForEndOfFrame();var image=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,name+".png"),image.EncodeToPNG());Destroy(image);Save();}
        IEnumerator Start(){Directory.CreateDirectory(output);Application.runInBackground=true;var run=Run();while(true){bool next;object value=null;try{next=run.MoveNext();if(next)value=run.Current;}catch(Exception e){log.Add("FAIL: "+e);Save();yield break;}if(!next)break;yield return value;}log.Add("RESULT=PASS");Save();Destroy(gameObject);}
        void OnGUI(){if(source!=null)GUI.Label(new Rect(8,Screen.height-25,800,25),"OFFLINE CNN SETTINGS FIXTURE / FAKE UART / NO COM / PHYSICAL TX = 0");}
        IEnumerator Run()
        {
            yield return new WaitForSecondsRealtime(2);
            var router=FindFirstObjectByType<SingleArmCommandRouter>();var menu=router.GetComponent<ControlStudioStartupMenu>();Check(menu.Visible,"Startup unchanged");menu.Cycle(1,2);Check(menu.StartStudio(),"UART startup without COM connect");
            source=router.GetComponent<BoardUartSource>();dock=router.GetComponent<ControlStudioDockLayout>();var panel=router.GetComponent<ControlStudioConfigurationPanel>();dock.Select(ControlStudioDockLayout.Workspace.Settings);yield return null; yield return new WaitForEndOfFrame();
            Check(SerialFree()&&!source.Right.Connected&&!source.Left.Connected,"Settings entry sends no command");Click("RGBY13 tab");yield return null; yield return new WaitForEndOfFrame();
            foreach(string name in new[]{"CNN stop inference","CNN start inference"}){var b=Find(name).GetComponent<Button>();Check(!b.interactable,"disconnected CNN button locked");b.onClick.Invoke();Check(SerialFree(),"disabled listener cannot create serial TX");}
            var flags=BindingFlags.Instance|BindingFlags.NonPublic;var controllers=(BoardConfigurationController[])typeof(ControlStudioConfigurationPanel).GetField("controllers",flags).GetValue(panel);
            right=new Rig(source.Right,SerialFree);left=new Rig(source.Left,SerialFree);controllers[0]=right.Config;controllers[1]=left.Config;
            foreach(int width in new[]{1920,1280}){
                Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.5f);Check(Screen.width==width,"actual Player resolution "+width);
                Scroll(Find("RGBY13 read t"));yield return null; yield return new WaitForEndOfFrame();Click("Configuration RIGHT");yield return null; yield return new WaitForEndOfFrame();
                Click("RGBY13 read t");yield return new WaitForSecondsRealtime(.01f);right.Cnn(true,true);yield return null; yield return new WaitForEndOfFrame();
                Check(Find("CNN stop inference").GetComponent<Button>().interactable&&!Find("CNN start inference").GetComponent<Button>().interactable,"ON enables STOP only");
                yield return Capture(width+"_CNN_ON");int before=right.Wires.Count;Click("CNN stop inference");yield return new WaitForSecondsRealtime(.01f);Check(right.Wires.Count==before+1&&right.Wires.Last()=="a","STOP sends lowercase a once only to RIGHT fake port");
                right.Feed("CNN continuous mode: OFF after 12 frame(s); active frame will finish");yield return null; yield return new WaitForEndOfFrame();Check(!Find("RGBY13 begin").GetComponent<Button>().interactable,"OFF ACK does not falsely enable RGB before idle readback");
                Click("RGBY13 read t");yield return new WaitForSecondsRealtime(.01f);right.Cnn(false);yield return null; yield return new WaitForEndOfFrame();
                Check(Find("CNN start inference").GetComponent<Button>().interactable&&Find("RGBY13 begin").GetComponent<Button>().interactable,"idle t enables START and RGB menu");
                var button=Find("CNN start inference").GetComponent<Button>();Color color=button.targetGraphic.canvasRenderer.GetColor();yield return new WaitForSecondsRealtime(2);Check(button.interactable&&button.targetGraphic.canvasRenderer.GetColor()==color,"enabled color stable over 2 seconds");
                yield return Capture(width+"_CNN_OFF_IDLE");
                foreach(string name in new[]{"RGBY13 read t","RGBY13 begin","CNN stop inference","CNN start inference","RGBY13 next value","RGBY13 keep"}){
                    var t=Find(name);Scroll(t);yield return null; yield return new WaitForEndOfFrame();var a=Bounds((RectTransform)t);var v=Bounds(dock.DetailScroll.viewport);Check(a.xMin>=v.xMin&&a.xMax<=v.xMax&&a.yMin>=v.yMin&&a.yMax<=v.yMax,width+" unclipped "+name);
                    var label=t.GetComponentInChildren<Text>();Check(label.preferredWidth<=label.rectTransform.rect.width+1,"caption fits "+name);foreach(char c in label.text.Where(c=>c>=0xAC00&&c<=0xD7A3).Distinct()){label.font.RequestCharactersInTexture(c.ToString(),label.fontSize,label.fontStyle);Check(label.font.GetCharacterInfo(c,out _,label.fontSize,label.fontStyle),"Korean glyph "+c);}
                }
                Scroll(Find("RGBY13 read t"));yield return null; yield return new WaitForEndOfFrame();Click("RGBY13 read t");yield return new WaitForSecondsRealtime(.01f);right.Cnn(false);yield return null; yield return new WaitForEndOfFrame();
                before=right.Wires.Count;Click("CNN start inference");yield return new WaitForSecondsRealtime(.01f);right.Feed("CNN continuous mode: ON; press 'a' to stop after the current frame");yield return null; yield return new WaitForEndOfFrame();Check(right.Wires.Count==before+1&&right.Wires.Last()=="a"&&!right.Config.CnnVerifiedIdle,"START explicit and ACK not a readback");
                Click("Configuration LEFT");yield return null; yield return new WaitForEndOfFrame();Click("RGBY13 read t");yield return new WaitForSecondsRealtime(.01f);left.Cnn(true);yield return null; yield return new WaitForEndOfFrame();before=right.Wires.Count;Click("CNN stop inference");yield return new WaitForSecondsRealtime(.01f);left.Feed("CNN continuous mode: OFF after 1 frame(s)");yield return null; yield return new WaitForEndOfFrame();Check(right.Wires.Count==before&&left.Wires.Last()=="a","LEFT target cannot toggle RIGHT");
                Check(SerialFree()&&right.Wires.Concat(left.Wires).All(w=>w=="a"||w=="t"),"physical TX=0; no PWM/follow/gripper commands");
            }
        }
        void OnDestroy(){right?.Dispose();left?.Dispose();}
    }
}
