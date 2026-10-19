using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Explicit test entry only; absent from the saved scene and normal player startup.
    public sealed class ControlStudioAcceptance : MonoBehaviour
    {
        SingleArmCommandRouter router;ManualServoSource manual;ControlStudioRuntimeUI ui;ControlStudioProfileStore profiles;
        Demo06RealisticVisualAdapter adapter;
        Transform[] pivots;Vector3[] positions;Quaternion[] homeRotations;
        readonly List<string> log=new List<string>();
        string directory;bool failed;
        public string EditorOutputDirectory="Validation/SingleArmControl";
        public bool Complete {get;private set;}
        public bool Passed=>Complete&&!failed;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void OptionalRun()
        {
            if(Environment.GetCommandLineArgs().Contains("--studio-verify") && UnityEngine.SceneManagement.SceneManager.GetActiveScene().name=="Demo_07_SingleArmControl")
                new GameObject("Explicit Control Studio Acceptance").AddComponent<ControlStudioAcceptance>();
        }
        void Check(bool ok,string message){if(!ok){failed=true;throw new InvalidOperationException(message);}log.Add("PASS "+message);}
        static bool Same(float[] a,float[] b)=>a.Length==b.Length&&a.Zip(b,(x,y)=>Mathf.Abs(x-y)<.002f).All(v=>v);
        IEnumerator Start()
        {
            directory=Application.isEditor?Path.GetFullPath(EditorOutputDirectory):Path.Combine(Application.persistentDataPath,"ControlStudio","Validation");Directory.CreateDirectory(directory);
            yield return null;yield return new WaitForEndOfFrame();
            router=FindFirstObjectByType<SingleArmCommandRouter>();manual=router.GetComponent<ManualServoSource>();ui=router.GetComponent<ControlStudioRuntimeUI>();profiles=router.GetComponent<ControlStudioProfileStore>();
            adapter=router.robot.GetComponentInChildren<Demo06RealisticVisualAdapter>(true);
            if(!router.Ready){log.Add("NATIVE_UNAVAILABLE "+router.Status);failed=true;Save();yield break;}
            yield return new WaitForSecondsRealtime(.3f);router.SetPaused(false);
            while(!router.HasApplied)yield return null;
            yield return new WaitForEndOfFrame();
            var robot=router.robot;pivots=new[]{robot.elbowRoll,robot.elbowPitch,robot.wristPitch,robot.wristRoll};
            positions=pivots.Select(p=>p.localPosition).ToArray();homeRotations=pivots.Select(p=>p.localRotation).ToArray();
            var allowed=new[]{typeof(ForearmArmController),typeof(Demo06RealisticVisualAdapter),typeof(G51GripperVisual)};
            Check(robot.transform.root.GetComponentsInChildren<MonoBehaviour>(true).All(b=>b!=null&&allowed.Contains(b.GetType())),"Only preserved controller/adapter/gripper under robot");
            float[][] points={new[]{70f,90,110,90},new[]{50f,70,90,70},new[]{70f,90,110,100},new[]{70f,90,110,90},new[]{0f,.5f,1,.05f}};
            for(int a=0;a<5;a++)foreach(float v in points[a])
            {
                var old=(float[])manual.Values.Clone();
                ui.Inputs[a].onEndEdit.Invoke(v.ToString(CultureInfo.InvariantCulture));
                Check(Mathf.Abs(manual.Values[a]-v)<.001f,"Numeric callback M"+a+"="+v);
                for(int j=0;j<5;j++)if(j!=a)Check(manual.Values[j]==old[j],"Other request M"+j+" preserved");
                yield return Settle("M"+a+"="+v);
            }
            foreach(var q in new[]{new[]{90f,70,110,70,.05f},new[]{90f,70,70,70,.05f},new[]{90f,70,110,110,.05f},SingleArmCommandRouter.Home})
            {
                Check(manual.LoadAtomic(q),"Combined wrist target");ui.Sync();yield return Settle("M2+M3");
                yield return new WaitForEndOfFrame();
                ScreenCapture.CaptureScreenshot(Path.Combine(directory,"wrist-"+q[2]+"-"+q[3]+".png"));
            }
            var before=(float[])router.Approved.Clone();
            foreach(var s in new[]{"","NaN","Infinity","999"})Check(!manual.SetText(2,s)&&Same(router.Approved,before),"Reject numeric "+s);
            Check(!router.Submit("UART",router.Epoch,before),"Reject non-owner source");Check(!router.Submit("MANUAL",router.Epoch+1,before),"Reject stale epoch");
            router.SetPaused(true);long tick=router.TickCount;Check(manual.Set(2,110),"Paused target accepted");yield return new WaitForSecondsRealtime(.15f);Check(router.TickCount==tick,"Paused output ticks frozen");router.SetPaused(false);yield return Settle("Resume pending target");
            Check(manual.Home(),"Home same output path");yield return Settle("Home");
            byte[] backup=File.Exists(profiles.FilePath)?File.ReadAllBytes(profiles.FilePath):null;
            try
            {
                Check(profiles.Save(),"Preset save");Check(manual.Set(2,110),"Preset changed target");Check(profiles.Load()&&Same(manual.Values,SingleArmCommandRouter.Home),"Preset atomic load");
                var keep=(float[])manual.Values.Clone();
                foreach(string bad in new[]{"{}","{\"version\":2,\"robotId\":\"G51_SINGLE_01\",\"kind\":\"MANUAL_SERVO_PRESET\",\"values\":[90,70,100,90,0.05]}","{\"version\":1,\"robotId\":\"G51_SINGLE_01\",\"kind\":\"MANUAL_SERVO_PRESET\",\"values\":[90,70,999,90,0.05]}"})
                    Check(!profiles.LoadJson(bad)&&Same(keep,manual.Values),"Invalid preset rollback");
                Check(profiles.Restore(),"Default restore");
            }
            finally{if(backup!=null)File.WriteAllBytes(profiles.FilePath,backup);else if(File.Exists(profiles.FilePath))File.Delete(profiles.FilePath);}
            ui.Sync();yield return Settle("Final Home");
            Check(router.HardwareTxCount==0,"Hardware TX=0");
            Complete=true;Save();Debug.Log("CONTROL STUDIO ACCEPTANCE PASS "+directory);
        }
        IEnumerator Settle(string label)
        {
            float deadline=Time.realtimeSinceStartup+5;
            while(!Same(router.Applied,router.Approved)&&Time.realtimeSinceStartup<deadline){Check(router.Ready,"Native ready");yield return null;}
            yield return new WaitForEndOfFrame();
            Check(Same(router.Applied,router.Approved),label+" applied reaches approved");
            var bindings=new[]{adapter.m0,adapter.m1,adapter.m2,adapter.m3};
            for(int i=0;i<4;i++)
            {
                Check(Vector3.Distance(positions[i],pivots[i].localPosition)<1e-7f,"Local pivot M"+i+" unchanged");
                var b=bindings[i];var expected=b.visualRest*b.axisBasis*Quaternion.Inverse(b.logicalRest)*b.logical.localRotation*Quaternion.Inverse(b.axisBasis);
                Check(Quaternion.Angle(expected,b.visual.localRotation)<.06f,"Visual M"+i+" current-frame adapter match");
                float delta=router.Applied[i]-SingleArmCommandRouter.Home[i];if(i==0||i==3)delta=-delta;
                var logicalExpected=homeRotations[i]*Quaternion.AngleAxis(delta,(i==0||i==3)?Vector3.up:Vector3.right);
                Check(Quaternion.Angle(logicalExpected,pivots[i].localRotation)<.06f,"Logical M"+i+" axis/sign");
            }
            log.Add(label+" | requested="+string.Join(",",router.Requested)+" | approved="+string.Join(",",router.Approved)+" | applied="+string.Join(",",router.Applied)+" | tick="+router.TickCount);
        }
        void Save(){File.WriteAllLines(Path.Combine(directory,Application.isEditor?"editor-play-results.txt":"player-results.txt"),new[]{failed?"FAIL":"PASS"}.Concat(log));}
        void OnDestroy(){if(directory!=null&&log.Count>0)Save();}
    }
}
