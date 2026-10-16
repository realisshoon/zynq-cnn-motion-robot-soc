using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Explicit offline Player test. Loads the user's original CSV; never opens a COM.
    public sealed class CsvSingleRoleAcceptance : MonoBehaviour
    {
        string output,path; readonly List<string> log=new List<string>();
        SingleArmCommandRouter router;CsvRecordedHumanSource csv;ControlStudioBoardControlPanel control;ControlStudioCsvPlaybackPanel panel;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void Optional(){var a=Environment.GetCommandLineArgs();int i=Array.IndexOf(a,"--csv-single-role-verify"),f=Array.IndexOf(a,"--csv-single-file");if(i<0||f<0)return;var t=new GameObject("Explicit single CSV role verification").AddComponent<CsvSingleRoleAcceptance>();t.output=a[i+1];t.path=a[f+1];}
        void Check(bool yes,string text){log.Add((yes?"PASS ":"FAIL ")+text);Save();if(!yes)throw new Exception(text);}
        void Save()=>File.WriteAllLines(Path.Combine(output,"single_role_result.txt"),log);
        Transform[] Joints()=>new[]{router.robot.elbowRoll,router.robot.elbowPitch,router.robot.wristPitch,router.robot.wristRoll,router.robot.gripperVisual.leftGear,router.robot.gripperVisual.rightJawPivot};
        Quaternion[] Pose()=>Joints().Select(t=>t.localRotation).ToArray();
        bool Same(Quaternion[] q)=>Pose().Select((v,i)=>Quaternion.Angle(v,q[i])<.001f).All(x=>x);
        Button Button(string name)=>panel.Root.GetComponentsInChildren<Button>(true).First(b=>b.name==name);
        void Click(string name){var b=Button(name);Check(b.interactable&&b.gameObject.activeInHierarchy,"UI visible/enabled "+name);b.onClick.Invoke();}
        bool SerialFree(){var b=router.GetComponent<BoardUartSource>();return !b.Right.Connected&&!b.Left.Connected&&b.TxBytes(BoardSide.Right)==0&&b.TxBytes(BoardSide.Left)==0&&router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0;}
        IEnumerator Capture(string name){yield return null;yield return new WaitForEndOfFrame();var t=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,name+".png"),t.EncodeToPNG());Destroy(t);}
        IEnumerator Start(){Directory.CreateDirectory(output);Application.runInBackground=true;var run=Tests();while(true){bool next;object value=null;try{next=run.MoveNext();if(next)value=run.Current;}catch(Exception e){log.Add("RESULT=FAIL "+e);Save();yield break;}if(!next)break;yield return value;}log.Add("RESULT=PASS");Save();}
        IEnumerator Tests(){yield return new WaitForSecondsRealtime(2);router=FindFirstObjectByType<SingleArmCommandRouter>();csv=router.Csv;control=router.GetComponent<ControlStudioBoardControlPanel>();panel=router.GetComponent<ControlStudioCsvPlaybackPanel>();
            var menu=router.GetComponent<ControlStudioStartupMenu>();menu.Cycle(1,1-menu.Choice(1));Check(menu.StartStudio(),"unchanged Startup → CSV");router.GetComponent<ControlStudioDockLayout>().CloseDetails();router.enabled=false;
            Check(CsvRoleFile.Load(path,CsvInputMode.RecordedHumanAngles).Streams.Count==0,"untagged CSV never silently classified RIGHT");
            foreach(int width in new[]{1920,1280}){
                Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.6f);Check(Screen.width==width,"actual Windows Player resolution "+width);
                foreach(int role in new[]{1,0}){
                    control.TrySelectTarget(role);yield return null;var seed=Pose();Check(csv.Load(path)&&csv.NeedsConfirmation,"same file staged for explicit role "+role);
                    Check(csv.PendingFile.Unassigned.Rows.Count==1356&&Math.Abs(csv.PendingFile.Unassigned.Duration-27.1)<1e-6,"1356 original rows / 27.100 seconds");
                    Check(csv.PendingFile.Sha256=="1b6917f4014d8d08e4c08fa0f43eac3123fee22a0d143c710d3e8d1c66fc99f6","original input SHA256");
                    csv.PlayFromStart();csv.Resume();csv.Step();router.AdvanceVirtual(.1);Check(Same(seed)&&!csv.RoleAvailable,"all playback paths blocked before role consent");
                    yield return new WaitForSecondsRealtime(.15f);yield return Capture(width+"_"+role+"_confirm");Click("CSV confirm single target");
                    Check(csv.Role==(role==1?BoardSide.Left:BoardSide.Right)&&csv.RoleAvailable&&!csv.NeedsConfirmation,"explicit selected role bound once "+role);
                    double error=0;foreach(var row in csv.Data.Rows)for(int i=0;i<5;i++)error=Math.Max(error,Math.Abs(row.Servo[i]-float.Parse(row.Fields["source_servo_M"+i],CultureInfo.InvariantCulture)));
                    Check(error<=.00002,"all 1356 RIGHT inverse decoder targets match original source_servo within 0.00002; no second human calibration");log.Add("METRIC role="+role+" target_max_error="+error.ToString("R",CultureInfo.InvariantCulture));
                    yield return new WaitForSecondsRealtime(.15f);Click("CSV start");int[] moves=new int[6];var previous=Pose();int steps=0;long start=router.TickCount;int captures=0;
                    using(var trace=new StreamWriter(Path.Combine(output,width+"_"+role+"_trace.csv"))){trace.WriteLine("time,row,applied_M0,applied_M1,applied_M2,applied_M3,applied_M4,hold");
                        while(!csv.Timeline.Finished&&steps++<4000){router.AdvanceVirtual(.02);var now=Pose();for(int j=0;j<6;j++)if(Quaternion.Angle(previous[j],now[j])>.0001)moves[j]++;previous=now;
                            trace.WriteLine(csv.Timeline.Time.ToString("F3",CultureInfo.InvariantCulture)+","+csv.Timeline.Index+","+string.Join(",",router.Applied.Select(v=>v.ToString("R",CultureInfo.InvariantCulture)))+","+router.MotionHold);
                            if(csv.Timeline.Time>=captures*9&&captures<4){yield return Capture(width+"_"+role+"_pose_"+captures);captures++;}
                            if(steps%100==0)yield return null;
                        }
                    }
                    Check(csv.ConsumedRows==1356&&csv.Timeline.Finished,"all 1356 samples reach actual native trajectory and settle "+role);
                    for(int j=0;j<6;j++)Check(moves[j]>2,"actual G51 Transform moved "+role+" joint/gripper "+j+" frames="+moves[j]);
                    Check(router.Human==null&&router.Applied.Zip(csv.Data.Rows.Last().Servo,(a,b)=>Math.Abs(a-b)<.002f).All(x=>x),"final command matches / human decoder not applied twice");
                    log.Add("METRIC width="+width+" role="+role+" rows="+csv.ConsumedRows+" ticks="+(router.TickCount-start)+" nativeHold="+router.MotionHold+" settledTime="+csv.Timeline.Time);Save();
                    var held=Pose();router.AdvanceVirtual(.2);Check(Same(held),"completion retains final posture");yield return Capture(width+"_"+role+"_complete");
                }
                foreach(var b in panel.Root.GetComponentsInChildren<Button>(false)){var c=new Vector3[4];((RectTransform)b.transform).GetWorldCorners(c);Check(c.All(p=>p.x>=0&&p.x<=Screen.width),"button horizontal bounds "+width+" "+b.name);}
            }
            control.TrySelectTarget(2);yield return null;var hold=Pose();Check(csv.NeedsConfirmation&&!csv.ConfirmSingleTarget(),"BOTH untagged stream cannot confirm or clone");csv.PlayFromStart();router.AdvanceVirtual(.2);Check(Same(hold),"BOTH single stream HOLD");csv.CancelBinding();control.TrySelectTarget(0);yield return null;Check(csv.NeedsConfirmation&&!csv.RoleAvailable,"BOTH cancel → RIGHT still requires fresh consent");Check(!csv.ConfirmSingleTarget(BoardSide.Left,csv.PendingFile),"stale LEFT button label cannot authorize RIGHT");
            control.TrySelectTarget(1);yield return null;Check(csv.ConfirmSingleTarget(),"LEFT explicit rebind after BOTH");control.TrySelectTarget(0);yield return null;Check(csv.NeedsConfirmation&&!csv.RoleAvailable,"role change requires new consent");csv.CancelBinding();Check(!csv.RoleAvailable,"cancel cannot activate wrong role");
            Check(csv.Load(path)&&csv.NeedsConfirmation,"stage consent for epoch test");csv.SelectManual();Check(!csv.ConfirmSingleTarget()&&router.Source=="MANUAL","stale pending consent cannot steal Manual owner");yield return null;
            var original=csv.File;string bad=Path.Combine(output,"bad_inverse.csv");File.WriteAllText(bad,File.ReadAllText(path).Replace("83.2006073","84.2006073"));Check(!csv.Load(bad)&&csv.File==original,"mismatched inverse/source columns rejected transactionally");
            string sample=Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/cnn_agent1_result.csv");foreach(int role in new[]{1,0}){control.TrySelectTarget(role);yield return null;Check(csv.Load(sample)&&csv.NeedsConfirmation&&csv.ConfirmSingleTarget(),"ordinary untagged human schema explicit binding "+role);csv.PlayFromStart();int i=0;while(!csv.Timeline.Finished&&i++<3000)router.AdvanceVirtual(.02);Check(csv.ConsumedRows==477,"ordinary human native role pipeline retained "+role);}
            Check(csv.Load(path)&&csv.ConfirmSingleTarget(),"restore same CSV for real-time LEFT demonstration");control.TrySelectTarget(1);yield return null;if(csv.NeedsConfirmation)Check(csv.ConfirmSingleTarget(),"LEFT real-time run consent");csv.PlayFromStart();router.enabled=true;float deadline=Time.realtimeSinceStartup+40;while(!csv.Timeline.Finished&&Time.realtimeSinceStartup<deadline)yield return null;Check(csv.Timeline.Finished&&csv.ConsumedRows==1356,"actual real-time Player scheduler completes same 1356-row LEFT file");yield return Capture("LEFT_real_time_complete");
            Check(!panel.Root.GetComponentsInChildren<Button>(true).Any(b=>b.name.StartsWith("CSV output"))&&SerialFree()&&router.Source=="CSV","single CSV UI, unsupported-board target TX zero / no physical COM");
        }
    }
}
