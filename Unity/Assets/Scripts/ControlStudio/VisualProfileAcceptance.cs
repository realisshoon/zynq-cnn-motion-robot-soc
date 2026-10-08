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
    public sealed class VisualProfileAcceptance : MonoBehaviour
    {
        public bool Complete,Passed;SingleArmCommandRouter r;ManualServoSource manual;RobotVisualProfiles p;Tool1Runtime t;
        readonly List<string> log=new List<string>();string trace,manualBaseline;const string Out="Validation/VisualProfiles";
        void Check(bool b,string s){log.Add((b?"PASS ":"FAIL ")+s);if(!b){Complete=true;Save();throw new Exception(s);}}
        void Save(){Directory.CreateDirectory(Out);File.WriteAllLines(Out+"/editor-results.txt",new[]{Passed?"PASS":"FAIL"}.Concat(log));}
        void Sync(){p.Adapter.SyncVisuals();p.Sync();}
        void Settle(){for(int i=0;i<1200&&!r.IsSettled;i++)r.AdvanceVirtual(.02);Check(r.IsSettled,"settled");Sync();}
        void Home(){t.SetEnabled(false);r.Csv.SelectManual();manual.LoadAtomic(SingleArmCommandRouter.Home);r.SetPaused(false);Settle();}
        void Switch(RobotVisualProfileId id)
        {
            int epoch=r.Epoch;string state=t.Gate.State;var a=(float[])r.Approved.Clone();var applied=(float[])r.Applied.Clone();var request=(float[])r.Requested.Clone();var socket=t.Socket.localToWorldMatrix;var parent=t.Socket.parent;
            Check(p.Select(id),"select "+id);Check(epoch==r.Epoch&&a.SequenceEqual(r.Approved)&&applied.SequenceEqual(r.Applied)&&request.SequenceEqual(r.Requested)&&state==t.Gate.State,"profile changes no target/applied/epoch/gate");Check(parent==t.Socket.parent&&socket==t.Socket.localToWorldMatrix,"original ToolSocket parent and pose preserved");
        }
        void Geometry()
        {
            Sync();if(p.Current==null)return;var source=new[]{p.Adapter.m0,p.Adapter.m1,p.Adapter.m2,p.Adapter.m3};var rig=p.Current;
            for(int i=0;i<4;i++){Check(Quaternion.Angle(source[i].visual.localRotation,rig.joints[i].visual.localRotation)<.01f,"M"+i+" calibrated local rotation identical");Check(Vector3.Distance(source[i].visual.position,rig.joints[i].visual.position)<.00001f,"M"+i+" world pivot identical");}
            Check(rig.joints[2].visual.parent==rig.joints[1].visual&&rig.joints[3].visual.parent==rig.joints[2].visual,"visual M1 -> M2 -> M3 hierarchy preserved");
            Check(Vector3.Distance(rig.toolMount.position,t.Socket.position)<.00001f&&Quaternion.Angle(rig.toolMount.rotation,t.Socket.rotation)<.01f,"profile tool mount matches existing socket");
            foreach(var link in rig.linkage)Check(Vector3.Distance(link.source.position,link.visual.position)<.00001f&&Quaternion.Angle(link.source.rotation,link.visual.rotation)<.01f,"M4 linkage inherited: "+link.visual.name);
        }
        IEnumerator Trace(string name,CsvInputMode mode,RobotVisualProfileId profile)
        {
            Home();t.Select(ToolKind.Gripper);Switch(profile);var data=new StringBuilder();
            Action tick=()=>data.AppendLine("T "+string.Join(",",r.Approved)+" "+string.Join(",",r.Applied)+" "+string.Join(",",r.Velocity)+" "+r.MotionHold);
            Action<RecordedHumanRow> row=x=>data.AppendLine("R "+x.FrameId+" "+string.Join(",",r.Approved)+" "+(r.Candidate==null?"--":string.Join(",",r.Candidate)));
            r.OutputApplied+=tick;r.Csv.RowConsumed+=row;Check(r.Csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples",name+"_agent1_result.csv"),mode),"CSV load "+name+" "+mode);
            r.Csv.Play();for(int i=0;i<1000&&!r.Csv.Timeline.Finished;i++){r.AdvanceVirtual(.1);if(i%80==0){Switch((RobotVisualProfileId)(((int)profile+1)%3));Switch(profile);Sync();yield return null;}}
            Check(r.Csv.Timeline.Finished&&r.Csv.ConsumedRows==477,"all 477 rows "+profile);r.OutputApplied-=tick;r.Csv.RowConsumed-=row;trace=data.ToString();Geometry();
            using(var sha=SHA256.Create()){string hash=BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(trace))).Replace("-","");string key="trace-"+name+"-"+mode;File.WriteAllText(Out+"/"+key+"-"+profile+".sha256",hash);Check(hash==File.ReadAllText("Validation/Tool1W/"+key+".sha256"),"identical to TOOL-1W trace "+name+" "+mode+" "+profile);}
        }
        IEnumerator Start()
        {
            yield return null;r=FindFirstObjectByType<SingleArmCommandRouter>();manual=r.GetComponent<ManualServoSource>();p=r.GetComponent<RobotVisualProfiles>();t=r.GetComponent<Tool1Runtime>();r.enabled=false;t.NotifyFocus(true);
            Check(p.Error==""&&p.Selected==RobotVisualProfileId.G51,"G51 default / profile assets ready");Check(FindObjectsByType<SingleArmCommandRouter>(FindObjectsSortMode.None).Length==1,"exactly one command router");
            var originals=p.Adapter.GetComponentsInChildren<Renderer>(true).Where(x=>!x.transform.IsChildOf(t.Socket)).ToDictionary(x=>x,x=>x.enabled);
            foreach(var profile in new[]{RobotVisualProfileId.G51,RobotVisualProfileId.SingleArmHumanoid,RobotVisualProfileId.DualArmHumanoid}){
                Home();Switch(profile);var manualTrace=new StringBuilder();Action manualTick=()=>manualTrace.AppendLine(string.Join(",",r.Approved)+" / "+string.Join(",",r.Applied)+" / "+r.MotionHold);r.OutputApplied+=manualTick;Transform preview=p.Current==null?null:p.Current.leftArmPreview;var left=preview==null?Array.Empty<Matrix4x4>():preview.GetComponentsInChildren<Transform>().Select(x=>x.localToWorldMatrix).ToArray();
                for(int axis=0;axis<5;axis++){manual.LoadAtomic(SingleArmCommandRouter.Home);Settle();manual.Set(axis,new[]{110f,90f,120f,115f,.85f}[axis]);Settle();Geometry();}
                manual.LoadAtomic(new[]{90f,70f,70f,120f,.8f});Settle();Geometry();
                r.OutputApplied-=manualTick;string manualResult=manualTrace.ToString();if(manualBaseline==null)manualBaseline=manualResult;else Check(manualBaseline==manualResult,"Manual five-axis and compound Approved/Applied trace identical "+profile);
                File.WriteAllText(Out+"/manual-"+profile+".txt",manualResult);
                if(preview!=null)Check(left.SequenceEqual(preview.GetComponentsInChildren<Transform>().Select(x=>x.localToWorldMatrix)),"left preview remains static; no second-arm control");
                Home();manual.LoadAtomic(new[]{90f,120f,90f,87f,.85f});Settle();foreach(var tool in new[]{ToolKind.Spray,ToolKind.Welding}){
                    t.Select(tool);Geometry();Check(p.Current==null||!p.Current.hand.gameObject.activeSelf,"humanoid hand hidden when tool mounted");
                    t.PlaceSurface();t.SetEnabled(true);manual.Set(4,1);manual.Set(4,0);Check(t.Gate.Running,"tool gate RUNNING "+profile+" "+tool);Physics.SyncTransforms();
                    int before=t.Surface.PaintCount;t.Visual.Advance(t.Gate.Command,.02f,t.PaintColor,tool==ToolKind.Spray?t.SprayRadius:t.WeldWidth,t.EffectIntensity);
                    Check(tool==ToolKind.Spray?t.Surface.PaintCount>before:t.Visual.welding.ArcOn,"surface effect works "+profile+" "+tool);
                    Capture(profile+"-"+tool);Switch((RobotVisualProfileId)(((int)profile+1)%3));Geometry();Check(t.Gate.Running&&t.Visual.gameObject.activeInHierarchy,"running Tool survives profile switch");Switch(profile);manual.Set(4,1);Check(!t.Gate.Running,"OPEN stops unchanged gate");t.SetEnabled(false);
                }
                t.Select(ToolKind.Gripper);Geometry();Check(p.Current==null||p.Current.hand.gameObject.activeSelf,"pincer returns");Capture(profile+"-Gripper");yield return null;
            }
            Switch(RobotVisualProfileId.G51);Check(originals.All(x=>x.Key.enabled==x.Value),"G51 renderer states restored");
            foreach(var mode in new[]{CsvInputMode.RecordedHumanAngles,CsvInputMode.XyzStoredBodyAuxGripper,CsvInputMode.XyzStoredBodyHoldGripper})foreach(var name in new[]{"cnn","mediapipe"})foreach(var profile in new[]{RobotVisualProfileId.G51,RobotVisualProfileId.SingleArmHumanoid,RobotVisualProfileId.DualArmHumanoid})yield return Trace(name,mode,profile);
            Home();Switch(RobotVisualProfileId.G51);Check(r.HardwareTxCount==0,"Hardware TX=0");Passed=Complete=true;Save();Debug.Log("VISUAL PROFILES ACCEPTANCE PASS");
        }
        void Capture(string name)
        {
            var original=FindFirstObjectByType<ControlStudioOrbitCamera>().GetComponent<Camera>();var camera=new GameObject("Profile evidence camera").AddComponent<Camera>();camera.CopyFrom(original);camera.rect=new Rect(0,0,1,1);
            var renderers=p.Current==null?p.Adapter.GetComponentsInChildren<Renderer>():p.Current.GetComponentsInChildren<Renderer>();Bounds bounds=new Bounds(renderers.First(x=>x.enabled).bounds.center,Vector3.zero);foreach(var x in renderers)if(x.enabled)bounds.Encapsulate(x.bounds);
            if(p.Current!=null){camera.transform.position=bounds.center+new Vector3(.7f,.4f,1)*bounds.extents.magnitude*3;camera.transform.LookAt(bounds.center);}else camera.transform.SetPositionAndRotation(original.transform.position,original.transform.rotation);
            camera.nearClipPlane=.005f;var rt=new RenderTexture(1000,850,24);camera.targetTexture=rt;camera.Render();var old=RenderTexture.active;RenderTexture.active=rt;var tex=new Texture2D(1000,850,TextureFormat.RGB24,false);tex.ReadPixels(new Rect(0,0,1000,850),0,0);tex.Apply();File.WriteAllBytes(Out+"/"+name+".png",tex.EncodeToPNG());RenderTexture.active=old;camera.targetTexture=null;Destroy(rt);Destroy(tex);Destroy(camera.gameObject);
        }
    }
}
