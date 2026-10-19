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
    // Explicit isolated Editor acceptance; never attached to the saved scene or Player.
    public sealed class Visual1Acceptance : MonoBehaviour
    {
        public bool Complete, Passed;
        const string Out="Validation/Visual1";
        readonly List<string> log=new List<string>();
        SingleArmCommandRouter r; ManualServoSource manual; Tool1Runtime tool; RobotVisualProfiles profiles;
        void Check(bool ok,string name){log.Add((ok?"PASS ":"FAIL ")+name);if(!ok){Complete=true;Save();throw new Exception(name);}}
        void Save(){Directory.CreateDirectory(Out);File.WriteAllLines(Out+"/editor-results.txt",new[]{Passed?"PASS":"FAIL"}.Concat(log));}
        void Home(){tool.SetEnabled(false);r.Csv.SelectManual();manual.LoadAtomic(SingleArmCommandRouter.Home);r.SetPaused(false);Settle();}
        void Settle(){for(int n=0;n<1200&&!r.IsSettled;n++)r.AdvanceVirtual(.02);Check(r.IsSettled,"motion settled");profiles.Adapter.SyncVisuals();profiles.Sync();}
        void Preserved(Action action,string name)
        {
            var request=(float[])r.Requested.Clone();var approved=(float[])r.Approved.Clone();var applied=(float[])r.Applied.Clone();int epoch=r.Epoch;var socket=tool.Socket.localToWorldMatrix;var parent=tool.Socket.parent;
            action();Check(request.SequenceEqual(r.Requested)&&approved.SequenceEqual(r.Approved)&&applied.SequenceEqual(r.Applied)&&epoch==r.Epoch,name+" leaves command and epoch unchanged");
            Check(parent==tool.Socket.parent&&socket==tool.Socket.localToWorldMatrix,name+" preserves ToolSocket");
        }
        void Geometry()
        {
            profiles.Adapter.SyncVisuals();profiles.Sync();var rig=profiles.Current;if(rig==null)return;
            var src=new[]{profiles.Adapter.m0,profiles.Adapter.m1,profiles.Adapter.m2,profiles.Adapter.m3};
            for(int i=0;i<4;i++){
                Check(Quaternion.Angle(src[i].visual.localRotation,rig.joints[i].visual.localRotation)<.01f,"M"+i+" calibrated rotation");
                Check(Vector3.Distance(src[i].visual.position,rig.joints[i].visual.position)<.00001f,"M"+i+" world pivot");
            }
            Check(Vector3.Distance(rig.toolMount.position,tool.Socket.position)<.00001f&&Quaternion.Angle(rig.toolMount.rotation,tool.Socket.rotation)<.01f,"ToolMount follows verified ToolSocket");
            if(profiles.Selected==RobotVisualProfileId.Humanoid)Check(rig.leftArmPreview!=null&&rig.leftArmPreview.gameObject.activeInHierarchy,"left arm static preview exists");
        }
        void WorkEffect(ToolKind kind)
        {
            if(kind==ToolKind.Gripper)return;
            tool.SetEnabled(true);manual.Set(4,0);Check(!tool.Gate.Running,"initial CLOSED cannot arm "+kind);
            manual.Set(4,1);manual.Set(4,0);Check(tool.Gate.Running,"fresh OPEN then CLOSED requests "+kind);
            Physics.SyncTransforms();int paint=tool.Surface.PaintCount;int weld=tool.WeldSurface.MarkCount;
            tool.Visual.Advance(tool.Gate.Command,.02f,tool.PaintColor,kind==ToolKind.Spray?tool.SprayRadius:tool.WeldWidth,tool.EffectIntensity);
            if(kind==ToolKind.Spray)Check(tool.Surface.PaintCount>paint,"Spray effect on active panel");
            else Check(tool.Visual.welding.ArcOn&&tool.WeldSurface.MarkCount>=weld,"Welding arc on active jig");
            var family=profiles.Selected;var other=family==RobotVisualProfileId.Industrial?RobotVisualProfileId.Humanoid:RobotVisualProfileId.Industrial;
            Preserved(()=>Check(profiles.Select(other),"switch family while running"),"running family switch");
            Check(tool.Gate.Tool==kind&&!tool.Gate.Running&&tool.Gate.State=="WAIT_OPEN","family switch retains Tool and rearms OPEN");
            if(kind==ToolKind.Welding)Check(!tool.Visual.welding.ArcOn,"family switch stops arc immediately");
            Preserved(()=>Check(profiles.Select(family),"restore family"),"family restore");
            manual.Set(4,0);Check(!tool.Gate.Running,"retained CLOSED cannot restart after family switch");
            manual.Set(4,1);manual.Set(4,0);Check(tool.Gate.Running,"new OPEN/CLOSED rearms effect after family switch");
            var disabled=profiles.Environment.Current.root;disabled.gameObject.SetActive(false);Physics.SyncTransforms();
            if(kind==ToolKind.Spray){paint=tool.Surface.PaintCount;tool.Visual.Advance(tool.Gate.Command,.02f,tool.PaintColor,tool.SprayRadius,tool.EffectIntensity);Check(tool.Surface.PaintCount==paint,"inactive Spray surface receives no paint");}
            else{tool.Visual.Advance(tool.Gate.Command,.02f,tool.PaintColor,tool.WeldWidth,tool.EffectIntensity);Check(!tool.Visual.welding.ArcOn,"inactive Weld surface has no arc");}
            disabled.gameObject.SetActive(true);Physics.SyncTransforms();manual.Set(4,1);Check(!tool.Gate.Running,"OPEN stops effect");tool.SetEnabled(false);
        }
        IEnumerator Trace(string name,CsvInputMode mode,RobotVisualProfileId family)
        {
            Home();Preserved(()=>profiles.Select(family),"trace family "+family);Preserved(()=>profiles.SelectTool(ToolKind.Gripper),"trace Gripper");
            var data=new StringBuilder();Action tick=()=>data.AppendLine("T "+string.Join(",",r.Approved)+" "+string.Join(",",r.Applied)+" "+string.Join(",",r.Velocity)+" "+r.MotionHold);
            Action<RecordedHumanRow> row=x=>data.AppendLine("R "+x.FrameId+" "+string.Join(",",r.Approved)+" "+(r.Candidate==null?"--":string.Join(",",r.Candidate)));
            r.OutputApplied+=tick;r.Csv.RowConsumed+=row;
            Check(r.Csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples",name+"_agent1_result.csv"),mode),"CSV load "+name+" "+mode);
            r.Csv.Play();var sequence=new[]{ToolKind.Spray,ToolKind.Welding,ToolKind.Gripper};int switchIndex=0;
            for(int i=0;i<1000&&!r.Csv.Timeline.Finished;i++){
                r.AdvanceVirtual(.1);
                if(i>0&&i%80==0){Preserved(()=>profiles.SelectTool(sequence[switchIndex++%3]),"CSV tool switch");Check(!tool.Gate.Running,"CSV switch effect OFF");}
                if(i%30==0)yield return null;
            }
            r.OutputApplied-=tick;r.Csv.RowConsumed-=row;
            Check(r.Csv.Timeline.Finished&&r.Csv.ConsumedRows==477,"all 477 rows / EOF");
            string key="trace-"+name+"-"+mode;
            using(var sha=SHA256.Create()){
                string hash=BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(data.ToString()))).Replace("-","");
                File.WriteAllText(Out+"/"+key+"-"+family+".sha256",hash);
                Check(hash==File.ReadAllText("Validation/Tool1W/"+key+".sha256"),"Approved/Applied/output trace equals TOOL-1W baseline "+key+" "+family);
            }
            Check(!tool.Gate.Running,"EOF effect OFF");Geometry();
        }
        IEnumerator Start()
        {
            yield return null;yield return null;
            r=FindFirstObjectByType<SingleArmCommandRouter>();manual=r.GetComponent<ManualServoSource>();tool=r.GetComponent<Tool1Runtime>();profiles=r.GetComponent<RobotVisualProfiles>();
            r.enabled=false;tool.NotifyFocus(true);
            Check(r.Ready&&profiles!=null&&profiles.Error==""&&tool.Error=="","runtime and all virtual assets ready");
            Check(profiles.Selected==RobotVisualProfileId.G51&&profiles.Environment.Current.tool==ToolKind.Gripper,"default G51 / Gripper / bench");
            Check(FindObjectsByType<SingleArmCommandRouter>(FindObjectsSortMode.None).Length==1,"one command router");
            var original=profiles.Adapter.GetComponentsInChildren<Renderer>(true).ToDictionary(x=>x,x=>x.enabled);
            string manualBaseline=null;
            foreach(var family in new[]{RobotVisualProfileId.Industrial,RobotVisualProfileId.Humanoid}){
                Home();Preserved(()=>Check(profiles.Select(family),"select "+family),"family switch");
                var manualTrace=new StringBuilder();Action manualTick=()=>manualTrace.AppendLine(string.Join(",",r.Approved)+" / "+string.Join(",",r.Applied)+" / "+r.MotionHold);r.OutputApplied+=manualTick;
                var left=profiles.Current.leftArmPreview;var leftMatrices=left==null?Array.Empty<Matrix4x4>():left.GetComponentsInChildren<Transform>().Select(t=>t.localToWorldMatrix).ToArray();
                for(int axis=0;axis<5;axis++){
                    manual.LoadAtomic(SingleArmCommandRouter.Home);Settle();manual.Set(axis,new[]{110f,90f,120f,115f,.85f}[axis]);Settle();Geometry();
                }
                manual.LoadAtomic(new[]{90f,70f,70f,120f,.8f});Settle();Geometry();r.OutputApplied-=manualTick;
                if(manualBaseline==null)manualBaseline=manualTrace.ToString();else Check(manualBaseline==manualTrace.ToString(),"Manual five-axis + M2/M3 command trace invariant");
                if(left!=null)Check(leftMatrices.SequenceEqual(left.GetComponentsInChildren<Transform>().Select(t=>t.localToWorldMatrix)),"left preview never driven");
                foreach(var kind in new[]{ToolKind.Gripper,ToolKind.Spray,ToolKind.Welding}){
                    Home();Preserved(()=>Check(profiles.SelectTool(kind),"select "+kind),"tool switch");
                    Check(profiles.Selected==family&&tool.Gate.Tool==kind&&profiles.Environment.Current.tool==kind,"Robot x Tool x AUTO Environment "+family+" "+kind);
                    Check(profiles.Current.Supports(kind)&&profiles.Current.ShellName(kind)!="UNAVAILABLE","matching shell available");
                    Check(!tool.Gate.Running,"switch disarms effect");Geometry();
                    Check(profiles.Current.hand.gameObject.activeSelf==(kind==ToolKind.Gripper),"gripper visual matches Tool");
                    if(kind!=ToolKind.Gripper)WorkEffect(kind);
                    Capture(family+"-"+kind);yield return null;
                }
            }
            Home();Preserved(()=>Check(profiles.SelectTool(ToolKind.Gripper),"return Gripper"),"Gripper return");
            Preserved(()=>Check(profiles.Select(RobotVisualProfileId.G51),"return G51"),"G51 return");profiles.Sync();
            Check(original.All(x=>x.Key.enabled==x.Value),"original G51 renderers restored");
            foreach(var mode in new[]{CsvInputMode.RecordedHumanAngles,CsvInputMode.XyzStoredBodyAuxGripper,CsvInputMode.XyzStoredBodyHoldGripper})
                foreach(var name in new[]{"cnn","mediapipe"})
                    foreach(var family in new[]{RobotVisualProfileId.Industrial,RobotVisualProfileId.Humanoid})yield return Trace(name,mode,family);
            Home();Check(r.HardwareTxCount==0,"Hardware TX=0");Passed=Complete=true;Save();Debug.Log("VISUAL-1 ACCEPTANCE PASS");
        }
        void Capture(string name)
        {
            var original=FindFirstObjectByType<ControlStudioOrbitCamera>().GetComponent<Camera>();var camera=new GameObject("Visual1 evidence camera").AddComponent<Camera>();camera.CopyFrom(original);camera.rect=new Rect(0,0,1,1);
            var renderers=profiles.Current.GetComponentsInChildren<Renderer>().Where(x=>x.enabled).ToArray();
            var bounds=new Bounds(renderers[0].bounds.center,Vector3.zero);foreach(var renderer in renderers)bounds.Encapsulate(renderer.bounds);
            camera.transform.position=bounds.center+new Vector3(.7f,.4f,1f)*Mathf.Max(bounds.extents.magnitude*3,.45f);camera.transform.LookAt(bounds.center);camera.nearClipPlane=.005f;
            var rt=new RenderTexture(1000,850,24);camera.targetTexture=rt;camera.Render();var previous=RenderTexture.active;RenderTexture.active=rt;
            var tex=new Texture2D(1000,850,TextureFormat.RGB24,false);tex.ReadPixels(new Rect(0,0,1000,850),0,0);tex.Apply();
            File.WriteAllBytes(Out+"/"+name+".png",tex.EncodeToPNG());RenderTexture.active=previous;camera.targetTexture=null;Destroy(rt);Destroy(tex);Destroy(camera.gameObject);
        }
    }
}
