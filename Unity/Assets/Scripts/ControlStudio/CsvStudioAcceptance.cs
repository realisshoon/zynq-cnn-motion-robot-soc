using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;
namespace HumanMotion.ControlStudio
{
    // Explicit Editor validation only. Not attached to the saved scene or ordinary player.
    public sealed class CsvStudioAcceptance : MonoBehaviour
    {
        readonly List<string> log=new List<string>();bool failed,complete;
        string path="Validation/SingleArmControlCsv/editor-csv-results.txt";
        void Check(bool ok,string reason){log.Add((ok?"PASS ":"FAIL ")+reason);if(!ok){failed=true;Save();throw new Exception(reason);}}
        bool Same(float[] a,float[] b)=>a.Zip(b,(x,y)=>Math.Abs(x-y)<.002f).All(x=>x);
        void Save(){File.WriteAllLines(path,new[]{failed?"FAIL":complete?"PASS":"INCOMPLETE"}.Concat(log));}
        IEnumerator Start()
        {
            yield return null;yield return null;
            var router=FindFirstObjectByType<SingleArmCommandRouter>();var manual=router.GetComponent<ManualServoSource>();var csv=router.Csv;
            Check(router.Ready,"Native ABI v2 ready");
            var regression=new GameObject("Explicit Manual regression").AddComponent<ControlStudioAcceptance>();regression.EditorOutputDirectory="Validation/SingleArmControlCsv/ManualRegression";
            float deadline=Time.realtimeSinceStartup+120;
            while(!regression.Complete&&Time.realtimeSinceStartup<deadline)yield return null;
            Check(regression.Passed,"STEP 1 full Manual/compound/Home/preset regression");Destroy(regression.gameObject);
            router.enabled=false; // Drive the same router deterministically, without double wall-clock updates.
            foreach(string name in new[]{"cnn","mediapipe"})
            {
                Check(csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples",name+"_agent1_result.csv")),name+" load");
                Check(csv.Data.Rows.Count==477&&csv.Data.Columns==56,"477 rows / 56 columns");
                var seed=(float[])router.Applied.Clone();int epoch=router.Epoch;
                for(int i=0;i<59;i++){csv.Step();Check(csv.Current.Flags[0]==0&&router.Candidate==null&&Same(router.Approved,seed),name+" initial invalid "+i);}
                double time=csv.Timeline.Time;long tick=router.TickCount;router.AdvanceVirtual(.2);Check(time==csv.Timeline.Time&&tick==router.TickCount,"Pause freezes both clocks");
                csv.Step();Check(csv.Timeline.Index==59&&Math.Abs(csv.Timeline.Time-5.9)<1e-6,"Step uses next CSV time");
                var approved=(float[])router.Approved.Clone();Check(!manual.Set(2,110)&&Same(approved,router.Approved)&&router.Source=="CSV","Manual callback blocked during CSV");
                Check(!router.GetComponent<ControlStudioProfileStore>().Restore()&&Same(approved,router.Approved),"Home/preset cannot overwrite CSV");
                var previousData=csv.Data;Check(!csv.Load("missing_studio_validation.csv")&&csv.Data==previousData&&router.Epoch==epoch,"Bad load keeps prior source and epoch");
                csv.Play();time=csv.Timeline.Time;tick=router.TickCount;router.AdvanceVirtual(.8);Check(router.Paused&&router.PauseReason.Contains("TIMING STALL")&&time==csv.Timeline.Time&&tick==router.TickCount,"Timing stall discards wall time and protects both clocks");
                csv.Play();int iterations=0;
                while(!csv.Timeline.Finished&&iterations++<800){router.AdvanceVirtual(.1);yield return null;}
                Check(csv.Timeline.Finished&&csv.ConsumedRows==477&&router.Paused,"EOF consumes all rows and settles/holds without Home");
                log.Add(name+" EOF "+csv.Message+" / approved="+string.Join(",",router.Approved)+" / applied="+string.Join(",",router.Applied));
                var row=csv.Current;var current=(float[])router.Applied.Clone();csv.Restart();
                Check(router.Epoch>epoch&&csv.ConsumedRows==0&&Same(current,router.Applied)&&router.Human==null&&router.Velocity.All(v=>v==0),"Restart clears observation/velocity, keeps current pose");
                Check(!router.SubmitHuman(row,epoch)&&!router.CsvTick(epoch),"Old epoch pending row/tick rejected");
                csv.Loop=true;csv.Play();int loopEpoch=router.Epoch;iterations=0;
                while(router.Epoch==loopEpoch&&iterations++<800){router.AdvanceVirtual(.1);yield return null;}
                Check(router.Epoch>loopEpoch&&csv.ConsumedRows==0,"Loop creates fresh epoch at EOF");csv.Loop=false;csv.Pause();
                current=(float[])router.Applied.Clone();Check(csv.SelectManual()&&Same(current,router.Applied)&&Same(current,manual.Values),"CSV to Manual preserves pose and adopts displayed command");
            }
            Check(manual.Home(),"Manual Home after CSV accepted");
            for(int i=0;i<500&&!router.IsSettled;i++){router.AdvanceVirtual(.02);yield return null;}
            Check(router.IsSettled&&Same(router.Applied,SingleArmCommandRouter.Home),"Final Home via original trajectory");
            Check(router.HardwareTxCount==0,"Hardware TX=0");router.enabled=true;complete=true;Save();Debug.Log("CSV STUDIO ACCEPTANCE PASS");
        }
        void OnDestroy(){if(log.Count>0)Save();}
    }
}
