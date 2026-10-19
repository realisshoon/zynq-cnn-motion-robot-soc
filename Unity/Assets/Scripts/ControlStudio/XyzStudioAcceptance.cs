using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;
namespace HumanMotion.ControlStudio
{
    // Explicit batch/Menu validation only. Never attached to a saved scene or Player startup.
    public sealed class XyzStudioAcceptance : MonoBehaviour
    {
        public bool Complete {get;private set;}
        public bool Passed {get;private set;}
        readonly List<string> log=new List<string>();
        const string PathOut="Validation/SingleArmControlXyz/editor-results.txt";
        void Check(bool ok,string text){log.Add((ok?"PASS ":"FAIL ")+text);if(!ok){Complete=true;Passed=false;Save();throw new Exception(text);}}
        void Save(){File.WriteAllLines(PathOut,new[]{Passed?"PASS":Complete?"FAIL":"INCOMPLETE"}.Concat(log));}
        bool Same(float[] a,float[] b)=>a.Zip(b,(x,y)=>Math.Abs(x-y)<.002f).All(v=>v);
        IEnumerator Start()
        {
            yield return null;yield return null;
            var r=FindFirstObjectByType<SingleArmCommandRouter>();var csv=r.Csv;var manual=r.GetComponent<ManualServoSource>();var ui=r.GetComponent<ControlStudioRuntimeUI>();
            Check(r.Ready,"ABI v2 output ready");r.enabled=false;r.SetPaused(false);r.AdvanceVirtual(.02);
            var pivots=new[]{r.robot.elbowRoll,r.robot.elbowPitch,r.robot.wristPitch,r.robot.wristRoll};var positions=pivots.Select(p=>p.localPosition).ToArray();
            foreach(var mode in new[]{CsvInputMode.XyzStoredBodyAuxGripper,CsvInputMode.XyzStoredBodyHoldGripper})foreach(string name in new[]{"cnn","mediapipe"}){
                string file=System.IO.Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples",name+"_agent1_result.csv");
                float grip=r.Applied[4];Check(csv.Load(file,mode),name+" "+mode+" load");var seed=(float[])r.Applied.Clone();int epoch=r.Epoch;
                for(int i=0;i<59;i++){csv.Step();Check(r.Candidate==null&&csv.LastSolve.SolverInfo[0]==0,"initial WAITING_VALID "+i);}
                csv.Step();Check(csv.LastSolve.State=="INITIAL_HELD_SEED"&&csv.LastSolve.SolverInfo[2]==0,"non-fresh seed separately labelled");
                double time=csv.Timeline.Time;long tick=r.TickCount;r.AdvanceVirtual(.1);Check(time==csv.Timeline.Time&&tick==r.TickCount,"Pause freezes both clocks");
                Check(!manual.Set(2,110)&&!r.GetComponent<ControlStudioProfileStore>().Restore(),"Manual/preset blocked during XYZ");
                csv.Play();r.AdvanceVirtual(.8);Check(r.Paused&&csv.Timeline.Time==time,"timing stall protection");csv.Play();
                for(int i=0;i<800&&!csv.Timeline.Finished;i++){r.AdvanceVirtual(.1);if(i%20==0)yield return null;}
                Check(csv.Timeline.Finished&&csv.ConsumedRows==477,"all rows / EOF");
                if(mode==CsvInputMode.XyzStoredBodyHoldGripper)Check(Math.Abs(r.Applied[4]-grip)<.002,"M4 entry HOLD through Agent2");
                var current=(float[])r.Applied.Clone();csv.Restart();Check(r.Epoch>epoch&&csv.ConsumedRows==0&&csv.LastSolve==null&&Same(current,r.Applied),"Restart solver/epoch/pose reset");
                Check(!r.CsvTick(epoch),"stale epoch rejected");
                csv.Loop=true;csv.Play();epoch=r.Epoch;for(int i=0;i<800&&r.Epoch==epoch;i++){r.AdvanceVirtual(.1);if(i%20==0)yield return null;}
                Check(r.Epoch>epoch,"Loop solver reinitialization");csv.Loop=false;csv.Pause();
                Check(csv.Load(file),"XYZ to Recorded load");csv.Step();Check(csv.Mode==CsvInputMode.RecordedHumanAngles&&csv.LastSolve==null&&r.Candidate==null,"Recorded remains initial WAITING_VALID");
                current=(float[])r.Applied.Clone();Check(csv.SelectManual()&&Same(current,manual.Values)&&Same(current,r.Applied),"Recorded to Manual preserves pose");
            }
            string cnn=System.IO.Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples","cnn_agent1_result.csv");
            int keptEpoch=r.Epoch;string keptSource=r.Source;
            Check(!csv.Load(cnn+".missing",CsvInputMode.XyzStoredBodyAuxGripper)&&r.Epoch==keptEpoch&&r.Source==keptSource&&r.Ready,"Rejected load preserves current owner/output");
            Check(csv.Load(cnn,CsvInputMode.XyzStoredBodyAuxGripper),"XYZ fault fixture load");
            csv.Data.Rows[59].Pose.BodyBasis=new float[9];
            for(int i=0;i<60;i++)csv.Step();
            Check(csv.Message.StartsWith("XYZ REJECTED")&&r.Paused&&r.Ready,"Malformed basis blocks XYZ without disabling output DLL");
            Check(csv.SelectManual()&&r.Ready,"Manual recovery after XYZ row failure");
            foreach(var q in new[]{new[]{90f,70,110,70,.05f},new[]{90f,70,70,110,.05f},SingleArmCommandRouter.Home}){
                Check(manual.LoadAtomic(q),"Manual compound/Home accepted");r.SetPaused(false);for(int i=0;i<800&&!r.IsSettled;i++)r.AdvanceVirtual(.02);Check(Same(r.Applied,q),"Manual compound/Home settled");yield return null;
            }
            ui.Inputs[2].onEndEdit.Invoke("110");Check(r.Approved[2]==110,"Manual numeric UI callback");manual.Home();for(int i=0;i<400&&!r.IsSettled;i++)r.AdvanceVirtual(.02);
            yield return null;
            var adapter=r.robot.GetComponentInChildren<Demo06RealisticVisualAdapter>(true);var bindings=new[]{adapter.m0,adapter.m1,adapter.m2,adapter.m3};
            for(int i=0;i<4;i++){var b=bindings[i];Check(pivots[i].localPosition==positions[i],"Pivot unchanged M"+i);Check(Quaternion.Angle(b.visualRest*b.axisBasis*Quaternion.Inverse(b.logicalRest)*b.logical.localRotation*Quaternion.Inverse(b.axisBasis),b.visual.localRotation)<.06,"Adapter unchanged M"+i);}
            Check(r.HardwareTxCount==0,"TX=0");Passed=true;Complete=true;Save();Debug.Log("XYZ STUDIO ACCEPTANCE PASS");
        }
    }
}
