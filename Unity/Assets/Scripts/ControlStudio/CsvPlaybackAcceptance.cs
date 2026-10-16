using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Explicit Player acceptance. No serial connection or command gateway invocation.
    public sealed class CsvPlaybackAcceptance : MonoBehaviour
    {
        string output;readonly List<string> log=new List<string>();SingleArmCommandRouter router;CsvRecordedHumanSource csv;ControlStudioBoardControlPanel control;ControlStudioCsvPlaybackPanel panel;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void RunOptional(){var a=Environment.GetCommandLineArgs();int i=Array.IndexOf(a,"--csv-playback-verify");if(i>=0&&i+1<a.Length)new GameObject("Opt-in CSV acceptance").AddComponent<CsvPlaybackAcceptance>().output=Path.GetFullPath(a[i+1]);}
        void Check(bool value,string reason){log.Add((value?"PASS ":"FAIL ")+reason);File.WriteAllLines(Path.Combine(output,"csv_result.txt"),log);if(!value)throw new Exception(reason);}
        Transform[] Joints()=>new[]{router.robot.elbowRoll,router.robot.elbowPitch,router.robot.wristPitch,router.robot.wristRoll,router.robot.gripperVisual.leftGear,router.robot.gripperVisual.rightJawPivot};
        Quaternion[] Pose()=>Joints().Select(t=>t.localRotation).ToArray();
        bool Same(Quaternion[] pose)=>Pose().Select((q,i)=>Quaternion.Angle(q,pose[i])<.001f).All(v=>v);
        bool SerialFree(){var b=router.GetComponent<BoardUartSource>();return !b.Right.Connected&&!b.Left.Connected&&b.TxBytes(BoardSide.Right)==0&&b.TxBytes(BoardSide.Left)==0&&b.LogPath(BoardSide.Right).Length==0&&b.LogPath(BoardSide.Left).Length==0&&router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0;}
        Button Button(string n)=>panel.Root.GetComponentsInChildren<Button>(true).First(b=>b.name==n);
        void Click(string n){var b=Button(n);Check(b.gameObject.activeInHierarchy&&b.interactable,"visible/enabled UI "+n);b.onClick.Invoke();}
        void Finish(){int iterations=0;while(!csv.Timeline.Finished&&iterations++<3000)router.AdvanceVirtual(.02);Check(csv.Timeline.Finished&&router.Paused,"COMPLETE then paused/HOLD");}
        IEnumerator Capture(string n){yield return null;yield return new WaitForEndOfFrame();var t=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,n+".png"),t.EncodeToPNG());Destroy(t);}
        IEnumerator Start(){Directory.CreateDirectory(output);Application.runInBackground=true;var run=Tests();while(true){bool next;object value=null;try{next=run.MoveNext();if(next)value=run.Current;}catch(Exception e){log.Add("RESULT=FAIL "+e);File.WriteAllLines(Path.Combine(output,"csv_result.txt"),log);yield break;}if(!next)break;yield return value;}log.Add("RESULT=PASS");File.WriteAllLines(Path.Combine(output,"csv_result.txt"),log);}
        IEnumerator Tests(){yield return new WaitForSecondsRealtime(2);router=FindFirstObjectByType<SingleArmCommandRouter>();csv=router.Csv;control=router.GetComponent<ControlStudioBoardControlPanel>();panel=router.GetComponent<ControlStudioCsvPlaybackPanel>();
            var menu=router.GetComponent<ControlStudioStartupMenu>();Check(menu.Visible,"original Startup exists");menu.Cycle(1,1-menu.Choice(1));Check(menu.StartStudio()&&router.Source=="CSV","Startup CSV → G51 retained");router.GetComponent<ControlStudioDockLayout>().CloseDetails();yield return null;router.enabled=false;
            string header="board_role,time_sec,frame_id,angle_unit,gripper_unit,calibration_id,M0,M1,M2,M3,M4\n";
            string right="RIGHT,0,1,deg,normalized,control_studio_right_abi3,90,70,100,90,0.1\nRIGHT,1,2,deg,normalized,control_studio_right_abi3,115,125,130,120,0.9\n";
            string left="LEFT,0,1,deg,normalized,control_studio_left_abi3,90,70,100,90,0.1\nLEFT,1,2,deg,normalized,control_studio_left_abi3,65,95,70,60,0.7\n";
            string file=Path.Combine(output,"양팔 시험 파일.csv");File.WriteAllText(file,header+right+left);Check(csv.Load(file),"UTF8 Korean/spaces dual-role CSV fully loads");
            foreach(int width in new[]{1920,1280}){Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.7f);Check(Screen.width==width,"actual Player resolution "+width);
                for(int target=0;target<3;target++){control.TrySelectTarget(target);yield return null;Check(csv.SelectedRole==(target==1?BoardSide.Left:BoardSide.Right),"canonical CSV role "+target);Check(csv.Role==csv.SelectedRole,"native owner role reset "+target);yield return new WaitForSecondsRealtime(.15f);
                    var seed=Pose();Click("CSV start");router.AdvanceVirtual(.2);csv.Pause();var paused=Pose();double time=csv.Timeline.Time;router.AdvanceVirtual(.2);Check(Same(paused)&&csv.Timeline.Time==time,"PAUSE freezes pose and timeline");
                    csv.Resume();router.AdvanceVirtual(.1);Check(csv.Timeline.Time>time,"RESUME preserves position");csv.Stop();paused=Pose();router.AdvanceVirtual(.2);Check(Same(paused)&&csv.Stopped,"STOP holds last pose");
                    seed=Pose();csv.PlayFromStart();Check(csv.Timeline.Index==-1&&csv.Timeline.Time==0,"PLAY starts first row");Finish();var pose=Pose();for(int j=0;j<6;j++)Check(Quaternion.Angle(seed[j],pose[j])>.01f,"actual joint/gripper Transform "+target+" / "+j);
                    float[] q=target==1?new[]{65f,95,70,60,.7f}:new[]{115f,125,130,120,.9f};Check(router.Applied.Zip(q,(a,b)=>Math.Abs(a-b)<.002f).All(x=>x),"role final M0~M4 settled exact "+target);
                    Check(Same(pose),"complete posture retained");csv.PlayFromStart();Check(!router.Paused&&csv.ConsumedRows==0,"repeated PLAY resets epoch/rows");csv.Stop();yield return Capture(width+"_CSV_"+target);
                }
                foreach(var b in panel.Root.GetComponentsInChildren<Button>(true)){var corners=new Vector3[4];((RectTransform)b.transform).GetWorldCorners(corners);Check(corners.All(c=>c.x>=0&&c.x<=Screen.width),"horizontal bounds "+width+" / "+b.name);}
                var backdrop=Resources.FindObjectsOfTypeAll<Transform>().First(t=>t.name=="CSV lower backdrop");Check(!backdrop.gameObject.activeInHierarchy,"legacy black backdrop disabled");
                Check(!panel.Root.GetComponentsInChildren<Button>(true).Any(b=>b.name.StartsWith("CSV output"))&&router.Source=="CSV"&&SerialFree(),"CSV output toggles removed; unsupported-board target TX remains zero");
                var dock=router.GetComponent<ControlStudioDockLayout>();dock.Select(ControlStudioDockLayout.Workspace.Source);yield return Capture(width+"_CSV_source_details");dock.CloseDetails();
            }
            control.TrySelectTarget(0);yield return null;var previous=csv.Data;int epoch=router.Epoch;
            foreach(string bad in new[]{header+right.Replace(",115,",",NaN,"),header+right.Replace("RIGHT,1,2","RIGHT,0,2"),header+right.Replace("right_abi3","left_abi3"),header+right.Replace(",0.9",",1.5"),"x,y\n1,2", ""}){string path=Path.Combine(output,"bad.csv");File.WriteAllText(path,bad);Check(!csv.Load(path)&&csv.Data==previous&&router.Epoch==epoch,"invalid file rejected, previous owner/data preserved");}
            File.WriteAllText(Path.Combine(output,"right_only.csv"),header+right);Check(csv.Load(Path.Combine(output,"right_only.csv")),"single RIGHT loads");control.TrySelectTarget(1);yield return null;var hold=Pose();csv.PlayFromStart();router.AdvanceVirtual(.2);Check(!csv.RoleAvailable&&Same(hold),"single RIGHT never copied to LEFT");
            control.TrySelectTarget(0);yield return null;string sample=Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/cnn_agent1_result.csv");Check(csv.Load(sample)&&csv.NeedsConfirmation,"legacy Agent1 schema stages unassigned stream");Check(csv.ConfirmSingleTarget(),"explicit RIGHT consent before playback");
            string human=File.ReadAllText(sample);var records=RecordedHumanCsv.ReadRecords(human);string Encode(string[] row)=>string.Join(",",row.Select(s=>"\""+s.Replace("\"","\"\"")+"\""));
            string humanLeft=Path.Combine(output,"left_agent1.csv");File.WriteAllText(humanLeft,"board_role,"+Encode(records[0])+"\n"+string.Join("\n",records.Skip(1).Select(r=>"LEFT,"+Encode(r))));control.TrySelectTarget(1);yield return null;Check(csv.Load(humanLeft),"explicit LEFT human uses existing LEFT ABI3 Native");csv.PlayFromStart();Finish();Check(csv.ConsumedRows==477,"legacy LEFT scheduler consumes original 477 rows");
            control.TrySelectTarget(0);yield return null;csv.LoadAsync(file,CsvInputMode.RecordedHumanAngles);float deadline=Time.realtimeSinceStartup+10;while(csv.Loading&&Time.realtimeSinceStartup<deadline)yield return null;Check(!csv.Loading&&csv.Data.Path==file,"async load commits on Unity main thread");
            csv.LoadAsync(file,CsvInputMode.RecordedHumanAngles);csv.SelectManual();yield return new WaitForSecondsRealtime(.3f);Check(router.Source=="MANUAL","pending async result cannot steal new owner");
            var observer=router.GetComponent<ControlStudioUartOutput>();control.TrySelectTarget(0);yield return null;Check(csv.Load(file),"CSV owner restored for TX guard");
            Check(observer.Connect("",115200,true),"MOCK observer only, no COM open");observer.SetTxEnabled(true);Check(!observer.TxEnabled&&!observer.SendCurrent()&&observer.MockTxCount==0,"legacy output observer cannot arm/send CSV targets");observer.Disconnect();
            Check(SerialFree(),"no COM transports or physical TX during all CSV tests");
            Check(csv.Load(Path.Combine(output,"right_only.csv")),"RIGHT-only default fixture before LEFT startup");
            csv.SelectManual();control.TrySelectTarget(1);menu.ShowMenu();menu.Cycle(1,1-menu.Choice(1));
            Check(menu.StartStudio()&&router.Source=="CSV"&&csv.SelectedRole==BoardSide.Left,"CSV startup retains LEFT even when default RIGHT sample cannot load");
            router.GetComponent<ControlStudioDockLayout>().CloseDetails();yield return null;yield return new WaitForSecondsRealtime(.15f);
            Check(Button("CSV choose file").gameObject.activeInHierarchy,"CSV file chooser accessible without compatible default file");
            Check(!csv.Active&&!Button("CSV start").interactable,"unbound previous epoch cannot play/resume");
            Check(csv.Load(file)&&csv.Role==BoardSide.Left,"LEFT startup can load proper LEFT sequence without resetting selection");
            Check(SerialFree(),"LEFT startup fallback has no COM/TX");router.enabled=true;
        }
    }
}
