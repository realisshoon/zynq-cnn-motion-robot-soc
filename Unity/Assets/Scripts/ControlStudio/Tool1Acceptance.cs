using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using UnityEngine;
namespace HumanMotion.ControlStudio
{
    // Explicit isolated Editor test only; not attached to any saved Scene or Player startup.
    public sealed class Tool1Acceptance : MonoBehaviour
    {
        public bool Complete,Passed;readonly List<string> log=new List<string>();
        SingleArmCommandRouter r;Tool1Runtime t;ManualServoSource manual;string trace;
        const string Out="Validation/Tool1W";
        void Check(bool value,string message){log.Add((value?"PASS ":"FAIL ")+message);if(!value){Complete=true;Save();throw new Exception(message);}}
        void Save(){Directory.CreateDirectory(Out);File.WriteAllLines(Out+"/editor-results.txt",new[]{Passed?"PASS":Complete?"FAIL":"INCOMPLETE"}.Concat(log));}
        void Home(){r.Csv.SelectManual();manual.LoadAtomic(SingleArmCommandRouter.Home);r.SetPaused(false);for(int i=0;i<1000&&!r.IsSettled;i++)r.AdvanceVirtual(.02);Check(r.IsSettled,"Home settled for fixture");}
        void OpenClose(){manual.Set(4,1);manual.Set(4,0);Check(t.Gate.Running,"new Manual OPEN/CLOSE starts effect");}
        IEnumerator Trace(string name,CsvInputMode mode,ToolKind kind)
        {
            t.SetEnabled(false);Home();t.Select(kind);t.SetEnabled(kind!=ToolKind.Gripper);var data=new StringBuilder();int requestedTicks=0;
            Action tick=()=>data.AppendLine("T "+string.Join(",",r.Approved)+" "+string.Join(",",r.Applied)+" "+string.Join(",",r.Velocity)+" "+r.MotionHold);
            Action<RecordedHumanRow> row=x=>data.AppendLine("R "+x.FrameId+" "+string.Join(",",r.Approved)+" "+(r.Candidate==null?"--":string.Join(",",r.Candidate)));
            Action count=()=>{if(t.Gate.Running)requestedTicks++;};r.OutputApplied+=count;
            r.OutputApplied+=tick;r.Csv.RowConsumed+=row;
            Check(r.Csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples",name+"_agent1_result.csv"),mode),"trace load "+name+" "+mode);
            r.Csv.Play();for(int i=0;i<1000&&!r.Csv.Timeline.Finished;i++){r.AdvanceVirtual(.1);if(i%30==0)yield return null;}
            Check(r.Csv.Timeline.Finished&&r.Csv.ConsumedRows==477,"trace EOF all rows");Check(!t.Gate.Running,"EOF effect OFF");
            r.OutputApplied-=count;r.OutputApplied-=tick;r.Csv.RowConsumed-=row;trace=data.ToString();
            if(mode==CsvInputMode.XyzStoredBodyHoldGripper)Check(requestedTicks==0,"XYZ B full trace: zero tool requests");
            File.WriteAllText(Out+"/request-ticks-"+name+"-"+mode+"-"+kind+".txt",requestedTicks.ToString());
        }
        IEnumerator Start()
        {
            yield return null;yield return null;r=FindFirstObjectByType<SingleArmCommandRouter>();manual=r.GetComponent<ManualServoSource>();t=r.GetComponent<Tool1Runtime>();r.enabled=false;t.NotifyFocus(true);
            Check(r.Ready&&t.Socket!=null&&t.Surface!=null&&t.Error=="","native / verified visual mount / generated assets ready");
            Check(Enum.GetNames(typeof(ToolKind)).SequenceEqual(new[]{"Gripper","Spray","Welding","Nailing"}),"active tool list is Gripper / Spray / Welding / Nailing");
            Check(t.Gate.Tool==ToolKind.Gripper&&!t.Gate.Enabled&&!t.Gate.Running,"default Gripper effects disabled");
            var renderers=r.robot.GetComponentsInChildren<Renderer>(true).ToDictionary(x=>x,x=>x.enabled);Home();
            int epoch=r.Epoch;var q=(float[])r.Approved.Clone();t.Select(ToolKind.Spray);t.SetEnabled(true);
            Check(r.Epoch==epoch&&q.SequenceEqual(r.Approved),"Tool switch leaves joint epoch / target unchanged");
            manual.Set(4,0);Check(!t.Gate.Running,"initial closed UI intent does not auto-run");OpenClose();
            var adapter=r.robot.GetComponentInChildren<Demo06RealisticVisualAdapter>(true);Check(t.Socket.parent==adapter.m3.visual.Find("ToolMount"),"socket under visual ToolMount, not logical/jaw/gear");
            t.PlaceSurface(); // Running: must reject.
            Check(!t.ChangeOffset(Vector3.one*.01f),"offset rejected while running");
            manual.Set(4,1);Check(!t.Gate.Running,"open stops effect");t.PlaceSurface();Physics.SyncTransforms();OpenClose();
            int n=t.Surface.PaintCount;t.Visual.Advance(t.Gate.Command,.02f,t.PaintColor,t.SprayRadius,t.EffectIntensity);
            File.WriteAllText(Out+"/paint-ray.txt","tip="+t.Visual.toolTip.position+" direction="+t.Visual.toolTip.forward+" panel="+t.Surface.transform.position+" normal="+t.Surface.GetComponent<MeshFilter>().sharedMesh.normals[0]+"\n"+string.Join("\n",Physics.RaycastAll(t.Visual.toolTip.position,t.Visual.toolTip.forward,.65f).Select(h=>h.collider.name+" "+h.distance)));
            Capture("spray-first");Check(t.Surface.PaintCount>n,"Spray paints permitted surface only while running");
            manual.Set(4,1);n=t.Surface.PaintCount;t.Visual.Advance(t.Gate.Command,.1f,t.PaintColor,t.SprayRadius,t.EffectIntensity);Check(n==t.Surface.PaintCount,"OFF particles do not paint");
            OpenClose();var obstacle=GameObject.CreatePrimitive(PrimitiveType.Cube);obstacle.name="Test non-paintable obstacle";obstacle.transform.position=t.Visual.toolTip.position+t.Visual.toolTip.forward*.1f;obstacle.transform.localScale=Vector3.one*.04f;Physics.SyncTransforms();
            n=t.Surface.PaintCount;t.Visual.Advance(t.Gate.Command,.02f,t.PaintColor,t.SprayRadius,t.EffectIntensity);Check(n==t.Surface.PaintCount,"non-paintable first hit blocks paint");Destroy(obstacle);yield return null;
            t.Surface.Clear();Check(t.Surface.PaintCount==0,"Clear Paint preserved");
            manual.Set(4,1);t.Select(ToolKind.Welding);Check(!t.Gate.Running,"Welding selection rearms");
            yield return WeldingChecks();
            r.SetPaused(true);Check(!t.Gate.Running&&!t.Visual.welding.ArcOn,"Pause request and arc OFF");r.SetPaused(false);manual.Set(4,0);Check(!t.Gate.Running,"Resume requires fresh OPEN");OpenClose();
            t.NotifyFocus(false);Check(!t.Gate.Running,"focus loss OFF");t.NotifyFocus(true);manual.Set(4,0);Check(!t.Gate.Running,"focus recovery requires OPEN");OpenClose();
            manual.Set(0,float.NaN);Check(!t.Gate.Running,"joint rejection OFF");manual.Set(0,90);Check(!t.Gate.Running,"rejection recovery closed stays OFF");
            manual.Set(4,1);manual.LoadAtomic(new[]{90f,70,110,110,1f});for(int i=0;i<800&&!r.IsSettled;i++)r.AdvanceVirtual(.02);adapter.SyncVisuals();
            var local=t.Visual.toolTip.localToWorldMatrix;Check(t.Visual.toolTip.IsChildOf(t.Socket)&&t.Socket.IsChildOf(adapter.m3.visual)&&t.Socket.IsChildOf(adapter.m2.visual),"ToolTip inherits compound M2/M3 visual chain");
            Vector3 tip=t.Visual.toolTip.position;manual.LoadAtomic(new[]{90f,70,70,70,1f});for(int i=0;i<800&&!r.IsSettled;i++)r.AdvanceVirtual(.02);adapter.SyncVisuals();Check(Vector3.Distance(tip,t.Visual.toolTip.position)>.005f,"compound M2/M3 moves ToolTip");
            t.Select(ToolKind.Gripper);Check(renderers.All(x=>x.Key.enabled==x.Value),"Gripper per-renderer states restored, references intact");
            // Same synthetic OPEN/CLOSED CSV observation flags, different XYZ M4 contracts.
            foreach(var mode in new[]{CsvInputMode.XyzStoredBodyAuxGripper,CsvInputMode.XyzStoredBodyHoldGripper}){
                Home();t.Select(ToolKind.Welding);t.SetEnabled(true);Check(r.Csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/cnn_agent1_result.csv"),mode),"XYZ trigger fixture load");
                for(int i=59;i<62;i++){var row=r.Csv.Data.Rows[i];row.Flags[0]=row.Flags[2]=1;row.Flags[6]=0;row.Pose.SourceFlags[0]=row.Pose.SourceFlags[2]=1;row.Pose.SourceFlags[6]=0;row.Pose.AuxiliaryGripper=i==60?1:0;((IDictionary<string,string>)row.Fields)["gripper_state"]=i==60?"1":"0";((IDictionary<string,string>)row.Fields)["gripper_hold_reason"]="0";}
                r.Csv.Play();while(r.Csv.Timeline.Index<61)r.AdvanceVirtual(.02);
                Check(t.Gate.Running==(mode==CsvInputMode.XyzStoredBodyAuxGripper),"XYZ A triggers / XYZ B NO_GESTURE_SOURCE");
                r.Csv.Pause();r.Csv.Step();Check(!t.Gate.Running,"Step always OFF");r.Csv.Restart();Check(!t.Gate.Running,"Restart OFF");
                r.Csv.Loop=true;r.Csv.Play();epoch=r.Epoch;for(int i=0;i<800&&r.Epoch==epoch;i++)r.AdvanceVirtual(.1);Check(r.Epoch>epoch&&!t.Gate.Running,"Loop rearm OFF");r.Csv.Loop=false;r.Csv.SelectManual();Check(!t.Gate.Running,"source transition OFF");yield return null;
            }
            foreach(var mode in new[]{CsvInputMode.RecordedHumanAngles,CsvInputMode.XyzStoredBodyAuxGripper,CsvInputMode.XyzStoredBodyHoldGripper})foreach(string name in new[]{"cnn","mediapipe"}){
                yield return Trace(name,mode,ToolKind.Gripper);string before=trace;yield return Trace(name,mode,ToolKind.Welding);Check(before==trace,"M0-M4 complete Approved/Applied trace unchanged: "+name+" "+mode);
                using(var hash=SHA256.Create())File.WriteAllText(Out+"/trace-"+name+"-"+mode+".sha256",BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(trace))).Replace("-",""));
            }
            t.SetEnabled(false);Home();t.Select(ToolKind.Spray);t.PlaceSurface();yield return null;
            Capture("spray");t.Select(ToolKind.Welding);yield return null;Capture("welding");t.Select(ToolKind.Gripper);yield return null;Capture("gripper");
            Check(r.HardwareTxCount==0,"Hardware TX=0");Passed=Complete=true;Save();Debug.Log("TOOL1 ACCEPTANCE PASS");
        }
        IEnumerator WeldingChecks()
        {
            var w=t.Visual.welding;Check(w!=null&&w.tip==t.Visual.toolTip,"torch tip and Welding effect installed");
            var wrist=r.robot.wristRoll.localRotation;var pitch=r.robot.wristPitch.localRotation;var joints=(float[])r.Approved.Clone();int epoch=r.Epoch;
            t.WeldSurface.gameObject.SetActive(false);OpenClose();w.Advance(t.Gate.Running,.02f,t.EffectIntensity,t.WeldWidth);
            Check(w.RequestOn&&!w.ArcOn&&w.SurfaceStatus=="NO_WORK_SURFACE"&&t.WeldSurface.MarkCount==0,"CLOSED without surface: request ON, arc/marks OFF");
            manual.Set(4,1);t.WeldSurface.gameObject.SetActive(true);t.PlaceSurface();OpenClose();w.Advance(t.Gate.Running,.02f,t.EffectIntensity,t.WeldWidth);
            Check(w.ArcOn&&w.SurfaceStatus=="VALID_SURFACE"&&t.WeldSurface.MarkCount==1,"valid surface: localized arc / first bead");
            int n=t.WeldSurface.MarkCount;for(int i=0;i<1000;i++)w.Advance(true,.02f,t.EffectIntensity,t.WeldWidth);
            Check(t.WeldSurface.MarkCount==n&&w.sparks.particleCount<=96,"stationary marks do not accumulate; bounded particles");
            var panel=t.WeldSurface.transform;var origin=panel.position;var realTip=w.tip;var fixture=new GameObject("Moving contact fixture").transform;fixture.SetPositionAndRotation(realTip.position,realTip.rotation);w.tip=fixture;var tipStart=fixture.position;
            for(int i=1;i<=8;i++){fixture.position=tipStart+panel.right*(i*.002f);Physics.SyncTransforms();w.Advance(true,.02f,t.EffectIntensity,t.WeldWidth);}
            Check(t.WeldSurface.MarkCount>n,"moving contact leaves surface-local weld trail");Capture("welding-active");w.tip=realTip;Destroy(fixture.gameObject);
            manual.Set(4,1);Check(!w.ArcOn&&!w.RequestOn&&w.sparks.particleCount==0,"OPEN immediately stops arc and sparks");n=t.WeldSurface.MarkCount;
            w.Advance(false,.5f,t.EffectIntensity,t.WeldWidth);Check(t.WeldSurface.MarkCount==n,"OFF creates no marks");
            int strokes=t.WeldSurface.StrokeCount;panel.position+=panel.right*.02f;Physics.SyncTransforms();OpenClose();w.Advance(true,.02f,t.EffectIntensity,t.WeldWidth);
            Check(t.WeldSurface.StrokeCount==strokes+1,"interruption starts new segment, no gap bridge");
            panel.position+=w.tip.forward*.2f;Physics.SyncTransforms();w.Advance(true,0,t.EffectIntensity,t.WeldWidth);
            Check(!w.ArcOn&&w.RequestOn,"out-of-range surface stops arc even between output ticks");
            panel.position=origin;panel.rotation=Quaternion.AngleAxis(70,w.tip.up)*Quaternion.LookRotation(w.tip.forward,w.tip.up);Physics.SyncTransforms();w.Advance(true,.02f,t.EffectIntensity,t.WeldWidth);
            Check(!w.ArcOn,"oblique surface rejected");
            manual.Set(4,1);t.PlaceSurface();OpenClose();w.Advance(true,.02f,t.EffectIntensity,t.WeldWidth);
            var second=GameObject.CreatePrimitive(PrimitiveType.Quad);second.transform.SetPositionAndRotation(panel.position,panel.rotation);second.transform.localScale=panel.localScale;
            var other=second.AddComponent<Tool1WeldableSurface>();other.Initialize(Resources.Load<Material>("Tool1/WeldBead"));t.WeldSurface.gameObject.SetActive(false);Physics.SyncTransforms();w.Advance(true,.02f,t.EffectIntensity,t.WeldWidth);
            Check(other.MarkCount==1&&other.StrokeCount==1&&other.LongestSegment==0,"different surface begins independent stroke");
            second.SetActive(false);Destroy(second);t.WeldSurface.gameObject.SetActive(true);Physics.SyncTransforms();w.Advance(true,.02f,t.EffectIntensity,t.WeldWidth);
            // Capacity uses the same mark sink; no need to allocate thousands of GameObjects.
            for(int i=0;i<600;i++)t.WeldSurface.Mark(origin,origin+panel.right*.002f,-panel.forward,t.WeldWidth,false);
            Check(t.WeldSurface.MarkCount==Tool1WeldableSurface.Capacity&&t.WeldSurface.LongestSegment<=Tool1WeldingEffect.MaxSegment,"bounded bead mesh / no long segments");
            joints=(float[])r.Approved.Clone();t.WeldSurface.Clear();Check(t.WeldSurface.MarkCount==0,"Clear Weld removes persistent marks");w.Advance(true,.02f,t.EffectIntensity,t.WeldWidth);Check(t.WeldSurface.StrokeCount==1,"Clear Weld resets active stroke anchor");
            Check(wrist==r.robot.wristRoll.localRotation&&pitch==r.robot.wristPitch.localRotation&&epoch==r.Epoch&&joints.SequenceEqual(r.Approved),"surface effects / Clear Weld do not change joints or source epoch");
            Check(!t.Select((ToolKind)2)&&t.Gate.Tool==ToolKind.Gripper&&!t.Gate.Enabled&&!t.Gate.Running,"retired Drill ID warns and selects Gripper / effects OFF");
            t.Select(ToolKind.Welding);t.SetEnabled(true);OpenClose();yield return null;
        }
        void Capture(string name)
        {
            var cam=new GameObject("Tool evidence camera").AddComponent<Camera>();cam.CopyFrom(FindFirstObjectByType<ControlStudioOrbitCamera>().GetComponent<Camera>());cam.rect=new Rect(0,0,1,1);
            Vector3 center=t.Socket.position+t.Socket.forward*.1f;cam.transform.position=center-t.Socket.forward*.32f+t.Socket.right*.32f+t.Socket.up*.17f;cam.transform.LookAt(center,t.Socket.up);cam.nearClipPlane=.005f;
            var rt=new RenderTexture(900,700,24);cam.targetTexture=rt;cam.Render();var old=RenderTexture.active;RenderTexture.active=rt;var tex=new Texture2D(900,700,TextureFormat.RGB24,false);tex.ReadPixels(new Rect(0,0,900,700),0,0);tex.Apply();File.WriteAllBytes(Out+"/"+name+".png",tex.EncodeToPNG());RenderTexture.active=old;cam.targetTexture=null;Destroy(rt);Destroy(tex);Destroy(cam.gameObject);
        }
    }
}
