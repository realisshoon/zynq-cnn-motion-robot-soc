using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Opt-in only: feeds the real reducers and applies the real G51 transforms.
    // No serial transport is created and no command gateway is substituted.
    public sealed class G51SourceSwitchAcceptance : MonoBehaviour
    {
        string output; readonly List<string> log=new List<string>();
        SingleArmCommandRouter router;BoardUartSource source;ControlStudioBoardControlPanel control;ControlStudioDockLayout dock;
        readonly uint[] ticks=new uint[2];
        static readonly float[] RightQ={105,130,120,110,.2f},LeftQ={65,80,60,55,.9f};
        static readonly BindingFlags Private=BindingFlags.Instance|BindingFlags.NonPublic;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void OptionalRun(){var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--g51-source-switch-verify");if(i<0||i+1>=args.Length)return;new GameObject("Explicit G51 source switch verification").AddComponent<G51SourceSwitchAcceptance>().output=args[i+1];}
        void Check(bool ok,string text){log.Add((ok?"PASS: ":"FAIL: ")+text);if(!ok)throw new Exception(text);}
        void Save()=>File.WriteAllLines(Path.Combine(output,"g51_source_result.txt"),log);
        bool SerialFree()=>source.TxBytes(BoardSide.Right)==0&&source.TxBytes(BoardSide.Left)==0&&source.LogPath(BoardSide.Right).Length==0&&source.LogPath(BoardSide.Left).Length==0;
        void Feed(BoardSide side,string line,double? at=null){var b=Encoding.ASCII.GetBytes(line+"\r\n");source.Session(side).Feed(b,b.Length,at??BoardUartTransport.Now);}
        void State(BoardSide side,bool pwm=true,string mode="LIVE")=>Feed(side,"[REC_STATE] schema=1 side="+(side==BoardSide.Right?"r":"l")+" ui=IDLE mode="+mode+" pwm="+(pwm?1:0)+" ram_samples=0 entries=0 selected=0 playing=0 delete=0 unsaved=0");
        void Begin(BoardSide side){source.Session(side).Begin();ticks[(int)side]=0;State(side);}
        string Tk(uint tick,float[] q,uint w=1,uint err=0)=>string.Format(CultureInfo.InvariantCulture,"TK,{0},{1},100,{2},{3},{4},{5},{6},1500,1500,1500,1500,1800,0.5,{7},{8}",tick,tick*20,q[0],q[1],q[2],q[3],q[4],w,err);
        void Tick(BoardSide side,float[] q,uint w=1,uint err=0,double? at=null){uint tick=++ticks[(int)side];Feed(side,Tk(tick,q,w,err),at);}
        Transform[] Joints()=>new[]{router.robot.elbowRoll,router.robot.elbowPitch,router.robot.wristPitch,router.robot.wristRoll,router.robot.gripperVisual.leftGear,router.robot.gripperVisual.rightGear,router.robot.gripperVisual.leftJawPivot,router.robot.gripperVisual.rightJawPivot};
        Quaternion[] Pose()=>Joints().Select(t=>t.localRotation).ToArray();
        bool Same(Quaternion[] pose)=>Pose().Select((q,i)=>Quaternion.Angle(q,pose[i])<.001f).All(v=>v);
        void Command(float[] expected,string reason){Check(router.Applied.Zip(expected,(a,b)=>Math.Abs(a-b)<.001f).All(v=>v),reason+" M0~M4 exact (no additional calibration)");}
        void Select(int target){Check(control.TrySelectTarget(target),"Control selection accepted "+target);Check(source.VisualSource==(target==1?BoardSide.Left:BoardSide.Right),"canonical source "+target);Check(SerialFree(),"selection TX=0");}
        void Mapping(float[] q)
        {
            var robot=router.robot;var type=typeof(ForearmArmController);
            float Get(string name)=>(float)type.GetField(name,Private).GetValue(robot);
            Quaternion Rest(string name)=>(Quaternion)type.GetField(name,Private).GetValue(robot);
            var expected=new[]{Rest("elbowRollRest")*Quaternion.AngleAxis(Get("elbowRollVisualOffsetDeg")+Get("elbowRollVisualSign")*(q[0]-90),Vector3.up),
                Rest("elbowPitchRest")*Quaternion.AngleAxis(Get("elbowPitchVisualOffsetDeg")+Get("elbowPitchVisualSign")*(q[1]-90),Vector3.right),
                Rest("wristPitchRest")*Quaternion.AngleAxis(Get("wristPitchVisualOffsetDeg")+Get("wristPitchVisualSign")*(q[2]-90),Vector3.right),
                Rest("wristRollRest")*Quaternion.AngleAxis(-(q[3]-90),Vector3.up)};
            for(int i=0;i<4;i++)Check(Quaternion.Angle(Joints()[i].localRotation,expected[i])<.001f,"original G51 axis/offset M"+i);
        }
        IEnumerator Capture(string name){yield return null;yield return new WaitForEndOfFrame();var image=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,name+".png"),image.EncodeToPNG());Destroy(image);Save();}
        void ClickTarget(int target){var t=dock.DetailRoot.GetComponentsInChildren<Transform>(true).First(x=>x.name=="Control target "+target);var b=t.GetComponent<Button>();Check(b.interactable&&t.gameObject.activeInHierarchy,"Control button visible "+target);var data=new PointerEventData(EventSystem.current){position=RectTransformUtility.WorldToScreenPoint(null,t.TransformPoint(((RectTransform)t).rect.center)),button=PointerEventData.InputButton.Left};var hits=new List<RaycastResult>();EventSystem.current.RaycastAll(data,hits);Check(hits.Count>0&&(hits[0].gameObject==t.gameObject||hits[0].gameObject.transform.IsChildOf(t)),"Control actual pointer hit "+target);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerClickHandler);}
        void OnGUI(){if(source!=null)GUI.Label(new Rect(8,Screen.height-25,900,25),"G51 SOURCE SWITCH / SYNTHETIC TK / NO COM / PHYSICAL TX = 0 / "+source.VisualSummary);}
        IEnumerator Start(){Directory.CreateDirectory(output);Application.runInBackground=true;var run=Run();while(true){bool next;object value=null;try{next=run.MoveNext();if(next)value=run.Current;}catch(Exception e){log.Add("FAIL: "+e);Save();yield break;}if(!next)break;yield return value;}log.Add("RESULT=PASS");Save();Destroy(gameObject);}
        IEnumerator Run()
        {
            yield return new WaitForSecondsRealtime(2);router=FindFirstObjectByType<SingleArmCommandRouter>();Check(router!=null&&router.Ready,"existing G51 initialized");
            var menu=router.GetComponent<ControlStudioStartupMenu>();Check(menu.Visible,"Startup retained");menu.Cycle(1,2-menu.Choice(1));Check(menu.StartStudio()&&router.Source=="BOARD","original UART startup owns BOARD");yield return null;
            source=router.GetComponent<BoardUartSource>();control=router.GetComponent<ControlStudioBoardControlPanel>();dock=router.GetComponent<ControlStudioDockLayout>();Check(SerialFree()&&!source.Right.Connected&&!source.Left.Connected,"no COM opened at startup");
            Check(!BoardUartSource.ResolveVisualSource(-1,out _)&&!BoardUartSource.ResolveVisualSource(3,out _),"unknown selection has no fallback");
            Begin(BoardSide.Right);Begin(BoardSide.Left);
            foreach(int width in new[]{1920,1280}) {
                Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.5f);Check(Screen.width==width&&Screen.height==width*9/16,"actual resolution "+width);
                Select(0);Tick(BoardSide.Right,RightQ);Tick(BoardSide.Left,LeftQ);yield return null;yield return new WaitForEndOfFrame();Command(RightQ,"RIGHT → RIGHT TK");Mapping(RightQ);var rightPose=Pose();
                long leftCount=source.LeftApplication.UnityApplyCount;Tick(BoardSide.Left,LeftQ);yield return null;Check(Same(rightPose)&&source.LeftApplication.UnityApplyCount==leftCount,"RIGHT ignores LEFT-only RX");
                yield return Capture(width+"_RIGHT_G51");
                Select(1);Tick(BoardSide.Left,LeftQ);yield return null;yield return new WaitForEndOfFrame();Command(LeftQ,"LEFT → LEFT TK");Mapping(LeftQ);var leftPose=Pose();
                for(int i=0;i<4;i++)Check(Quaternion.Angle(rightPose[i],leftPose[i])>1,"LEFT moves actual M"+i+" Transform");
                for(int i=4;i<8;i++)Check(Quaternion.Angle(rightPose[i],leftPose[i])>.01f,"LEFT moves actual gripper gear/jaw "+i);
                long rightCount=source.RightApplication.UnityApplyCount;Tick(BoardSide.Right,RightQ);yield return null;Check(Same(leftPose)&&source.RightApplication.UnityApplyCount==rightCount,"LEFT ignores RIGHT-only RX");
                yield return Capture(width+"_LEFT_G51");
                Select(2);Tick(BoardSide.Right,RightQ);yield return null;yield return new WaitForEndOfFrame();Command(RightQ,"BOTH → RIGHT TK (no synthesis)");var bothPose=Pose();Tick(BoardSide.Left,LeftQ);yield return null;Check(Same(bothPose),"BOTH ignores LEFT-only RX");
                var hud=router.GetComponent<RecordsQuickHud>();Check(hud.StatusText.Contains("G51 RIGHT"),"BOTH HUD explains RIGHT visualization");yield return Capture(width+"_BOTH_RIGHT_G51");
                foreach(int target in new[]{1,0,1,2}){Tick(BoardSide.Right,RightQ);Tick(BoardSide.Left,LeftQ);Select(target);yield return null;yield return new WaitForEndOfFrame();Command(target==1?LeftQ:RightQ,"fresh target switch "+target);}
                dock.Select(ControlStudioDockLayout.Workspace.Control);yield return null;yield return new WaitForEndOfFrame();
                foreach(int target in new[]{0,1,2}){Tick(BoardSide.Right,RightQ);Tick(BoardSide.Left,LeftQ);ClickTarget(target);yield return null;yield return new WaitForEndOfFrame();Check(control.SelectedTarget==target&&source.VisualSource==(target==1?BoardSide.Left:BoardSide.Right),"real Control click selects visualization "+target);}
                yield return Capture(width+"_Control_BOTH");dock.CloseDetails();yield return null;
            }
            Select(1);Tick(BoardSide.Left,LeftQ);yield return null;var held=Pose();long n=source.LeftApplication.UnityApplyCount;
            Tick(BoardSide.Left,RightQ,w:0);yield return null;Check(Same(held)&&source.LeftApplication.UnityApplyCount==n&&source.LeftApplication.HoldReason=="TK w = 0","w=0 stays HOLD; approval unchanged");
            Tick(BoardSide.Left,RightQ,err:1);yield return null;Check(Same(held)&&source.LeftApplication.UnityApplyCount==n,"err=1 stays HOLD");
            State(BoardSide.Left,false);Tick(BoardSide.Left,RightQ);yield return null;Check(Same(held)&&source.LeftApplication.HoldReason=="PWM OFF","PWM OFF stays HOLD");
            State(BoardSide.Left);Tick(BoardSide.Left,LeftQ);yield return null;n=source.LeftApplication.UnityApplyCount;Feed(BoardSide.Left,Tk(ticks[1],RightQ));yield return null;Check(source.Left.OutOfOrder>0&&source.LeftApplication.UnityApplyCount==n,"duplicate tick rejected");
            Feed(BoardSide.Left,Tk(ticks[1]-1,RightQ));yield return null;Check(source.LeftApplication.UnityApplyCount==n,"reverse tick rejected");
            State(BoardSide.Left);Tick(BoardSide.Left,LeftQ);yield return null;held=Pose();n=source.LeftApplication.UnityApplyCount;
            Tick(BoardSide.Left,RightQ,at:BoardUartTransport.Now-2);yield return null;Check(Same(held)&&source.LeftApplication.UnityApplyCount==n&&source.LeftApplication.HoldReason=="STALE TK","aged RX cannot apply");
            Select(0);Tick(BoardSide.Right,RightQ);yield return null;Select(1);yield return null;Command(RightQ,"switch to stale LEFT holds RIGHT pose; no forced cached apply");
            Begin(BoardSide.Left);Tick(BoardSide.Left,LeftQ);yield return null;held=Pose();n=source.LeftApplication.UnityApplyCount;
            var invalid=(float[])RightQ.Clone();invalid[0]=float.NaN;Tick(BoardSide.Left,invalid);yield return null;Check(Same(held)&&source.LeftApplication.UnityApplyCount==n&&source.Left.ParseErrors>0,"nonfinite TK rejected by existing parser");
            Begin(BoardSide.Left);Tick(BoardSide.Left,LeftQ);yield return null;held=Pose();n=source.LeftApplication.UnityApplyCount;
            source.Left.Disconnect();Tick(BoardSide.Right,RightQ);yield return null;Check(Same(held)&&source.LeftApplication.HoldReason=="DISCONNECTED"&&source.VisualSource==BoardSide.Left,"selected disconnect HOLD without RIGHT fallback");
            source.Left.Begin();ticks[1]=0;Tick(BoardSide.Left,RightQ);yield return null;Check(Same(held)&&source.LeftApplication.UnityApplyCount==n&&source.LeftApplication.HoldReason=="REC_STATE MISSING","new generation requires role state");
            State(BoardSide.Left);Tick(BoardSide.Left,RightQ);yield return null;Check(source.LeftApplication.UnityAppliedGeneration==source.Left.Generation&&source.LeftApplication.UnityApplyCount==n+1,"new generation applies only new verified TK");
            source.Left.Begin();Feed(BoardSide.Left,"[REC_STATE] schema=1 side=r ui=IDLE mode=LIVE pwm=1 ram_samples=0 entries=0 selected=0 playing=0 delete=0 unsaved=0");held=Pose();Tick(BoardSide.Left,LeftQ);yield return null;Check(Same(held)&&!source.Left.Trusted,"wrong board role rejected");
            // Same final command must produce the same single-model transform pose
            // regardless of which board produced it. Servo calibration is not repeated.
            Begin(BoardSide.Right);Begin(BoardSide.Left);Select(0);Tick(BoardSide.Right,RightQ);yield return null;held=Pose();Select(1);Tick(BoardSide.Left,RightQ);yield return null;Check(Same(held),"identical final LEFT/RIGHT command has identical original visual mapping");
            Select(0); // Historical CSV declares the RIGHT profile; retain role isolation.
            Check(router.Csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/cnn_agent1_result.csv")),"existing Local CSV source loads");Check(router.Source=="CSV","CSV owns router");Check(router.Csv.ConfirmSingleTarget(),"explicit RIGHT consent for historical unassigned human stream");
            n=source.LeftApplication.UnityApplyCount;source.Left.Begin();State(BoardSide.Left);Tick(BoardSide.Left,LeftQ);control.TrySelectTarget(0);router.Csv.Play();yield return new WaitForSecondsRealtime(.25f);
            Check(router.Source=="CSV"&&router.Csv.ConsumedRows>0&&source.LeftApplication.UnityApplyCount==n&&source.VisualSource==null,"CSV PLAY cannot be overwritten by BOARD TK or target change");
            router.Csv.Pause();held=Pose();source.Right.Begin();State(BoardSide.Right);Tick(BoardSide.Right,RightQ);yield return null;Check(Same(held)&&router.Source=="CSV","CSV PAUSE retains virtual pose against live TK");
            Check(router.Csv.SelectManual(),"existing Manual handoff retained");Check(router.Submit("MANUAL",router.Epoch,RightQ),"existing Manual command accepted");yield return new WaitForSecondsRealtime(.15f);Check(router.Source=="MANUAL"&&source.VisualSource==null,"Manual owns G51, no BOARD override");
            Check(source.SelectSource(),"explicit BOARD handoff uses existing owner path");Begin(BoardSide.Right);Begin(BoardSide.Left);Select(2);State(BoardSide.Left,mode:"PLAYING");State(BoardSide.Right);
            double start=BoardUartTransport.Now,next=start;long rp=source.RightApplication.ParsedTkCount,lp=source.LeftApplication.ParsedTkCount;
            var r=(float[])RightQ.Clone();var l=(float[])LeftQ.Clone();
            while(BoardUartTransport.Now-start<20){double now=BoardUartTransport.Now;while(next<=now){r[0]=105+(float)Math.Sin(next-start)*5;l[0]=65+(float)Math.Cos(next-start)*5;Tick(BoardSide.Right,r);Tick(BoardSide.Left,l);next+=.02;}yield return null;}
            yield return null;Check(source.RightApplication.ParsedTkCount-rp>=990&&source.LeftApplication.ParsedTkCount-lp>=990,"20-second dual 50Hz synthetic RX processed >=990 ticks per side");
            Check(source.VisualSource==BoardSide.Right&&source.Left.State.Mode=="PLAYING"&&source.Right.State.Mode=="LIVE","RIGHT LIVE / LEFT PLAY state independent during visual RX");
            Command(source.Right.LastApplied.Command,"dual stream retains RIGHT-only BOTH visualization");Check(SerialFree(),"all tests real COM open/TX=0");Save();
        }
    }
}
