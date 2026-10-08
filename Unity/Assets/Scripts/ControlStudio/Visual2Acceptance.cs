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
    // Explicit isolated Editor acceptance only; not attached to the saved Scene or Player startup.
    public sealed class Visual2Acceptance : MonoBehaviour
    {
        public bool Complete,Passed;
        const string Out="Validation/Visual2DualCell";
        readonly List<string> log=new List<string>();
        readonly RobotVisualProfileId[] families={RobotVisualProfileId.G51,RobotVisualProfileId.MechanicalDualTable,RobotVisualProfileId.MechanicalHumanoid};
        readonly ToolKind[] kinds={ToolKind.Gripper,ToolKind.Spray,ToolKind.Welding,ToolKind.Nailing};
        SingleArmCommandRouter router;ManualServoSource manual;Tool1Runtime tool;RobotVisualProfiles profiles;
        void Check(bool value,string name){log.Add((value?"PASS ":"FAIL ")+name);if(!value){Complete=true;Save();throw new Exception(name);}}
        void Save(){Directory.CreateDirectory(Out);File.WriteAllLines(Out+"/editor-results.txt",new[]{Passed?"PASS":"FAIL"}.Concat(log));}
        void Settle(){for(int i=0;i<1200&&!router.IsSettled;i++)router.AdvanceVirtual(.02);Check(router.IsSettled,"motion settled");profiles.Adapter.SyncVisuals();profiles.Sync();}
        void Home(){tool.SetEnabled(false);router.Csv.SelectManual();manual.LoadAtomic(SingleArmCommandRouter.Home);router.SetPaused(false);Settle();}
        void Preserved(Action change,string reason)
        {
            var request=(float[])router.Requested.Clone();var approved=(float[])router.Approved.Clone();var applied=(float[])router.Applied.Clone();
            int epoch=router.Epoch;var parent=tool.Socket.parent;var socket=tool.Socket.localToWorldMatrix;
            double time=router.Csv.Timeline==null?0:router.Csv.Timeline.Time;
            change();
            Check(request.SequenceEqual(router.Requested)&&approved.SequenceEqual(router.Approved)&&applied.SequenceEqual(router.Applied),reason+" command invariant");
            Check(epoch==router.Epoch&&(router.Csv.Timeline==null||time==router.Csv.Timeline.Time),reason+" source epoch/time invariant");
            Check(parent==tool.Socket.parent&&socket==tool.Socket.localToWorldMatrix,reason+" verified ToolSocket invariant");
        }
        void Select(RobotVisualProfileId family,ToolKind kind)
        {
            if(profiles.Selected!=family)Preserved(()=>Check(profiles.Select(family),"select family "+family),"family selection");
            if(tool.Gate.Tool!=kind)Preserved(()=>Check(profiles.SelectTool(kind),"select Tool "+kind),"Tool selection");
            Check(profiles.Selected==family&&tool.Gate.Tool==kind&&profiles.Environment.Current.tool==kind,"Robot x Tool x AUTO Environment "+family+" "+kind);
            Check(!tool.Gate.Running,"selection effect OFF / OPEN rearm");
            if(family!=RobotVisualProfileId.G51){
                var rig=profiles.Current;
                Check(rig!=null&&rig.Supports(kind)&&rig.leftArmPreview!=null,"source profile / static left preview / shell "+family+" "+kind);
                Check(!profiles.Environment.Root.IsChildOf(rig.transform)&&!rig.transform.IsChildOf(profiles.Environment.Root),"Robot presentation and EnvironmentRoot remain separate "+family);
                int expected=family==RobotVisualProfileId.MechanicalDualTable?414:120;
                Check(rig.sourceRightRendererCount==expected&&rig.sourceLeftRendererCount==expected,"original scene arm Renderer inventory "+family);
                var definition=Resources.Load<RobotVisualProfileDefinition>("VisualProfiles/"+family);
                Check(definition!=null&&definition.sourceAssetPath.Contains(family==RobotVisualProfileId.MechanicalDualTable?"Demo_07":"Demo_01"),"original source asset recorded "+family);
                Check(rig.toolShells.All(s=>s.roots.All(root=>root.gameObject.activeSelf==(s.tool==kind))),"only selected Tool shell active "+kind);
                if(family==RobotVisualProfileId.MechanicalDualTable){
                    Check(rig.leftHandlerGrip!=null&&rig.leftHandlerHand!=null&&rig.leftHandlerHand.GetComponentsInChildren<Renderer>(true).Length>10&&rig.leftHandlerHand.gameObject.activeSelf,"left source G51 Handler Gripper retained "+kind);
                    Check(rig.leftArmPreview.GetComponentsInChildren<Transform>(true).All(x=>!x.name.Contains("_LeftSourceToolVisual_STATIC")),"Dual left has no mirrored Process Tool head "+kind);
                    if(kind==ToolKind.Gripper)Check(profiles.GripperCell!=null&&profiles.GripperCell.SupplyZone!=null&&profiles.GripperCell.PickupZone!=null&&profiles.GripperCell.TargetZone!=null,"Gripper fixed Supply/Pickup/Target cell ready");
                    else Check(profiles.DualCell!=null&&profiles.DualCell.Workpiece!=null&&!tool.PresentationEffectsAllowed,"Dual workpiece starts Infeed / effects blocked");
                }
                else{
                    Check(rig.GetComponentsInChildren<Transform>(true).All(x=>!x.name.Contains("Added neck")&&!x.name.Contains("Added angular head")&&!x.name.Contains("Added head visor")),"Humanoid source torso has no generated head/neck/face");
                    if(kind==ToolKind.Gripper)Check(rig.hand.gameObject.activeSelf&&rig.hand.GetComponentsInChildren<Renderer>(true).Length>10&&rig.toolShells.Single(s=>s.tool==kind).roots.Last().GetComponentsInChildren<Renderer>(true).Length>10,"both original source grippers visible");
                    else{
                    var leftHead=rig.toolShells.Single(s=>s.tool==kind).roots.Last();
                    var rightHead=tool.Visual.transform.Find(kind==ToolKind.Nailing?"Mechanical fastening head":"ToolVisual");
                    var leftMesh=leftHead.GetComponentsInChildren<MeshFilter>(true).Select(x=>x.sharedMesh).ToArray();
                    var rightMesh=rightHead.GetComponentsInChildren<MeshFilter>(true).Select(x=>x.sharedMesh).ToArray();
                    Check(leftMesh.Length>0&&leftMesh.SequenceEqual(rightMesh),"left/right ToolVisual geometry from same prefab "+kind);
                    var leftMaterials=leftHead.GetComponentsInChildren<Renderer>(true).SelectMany(x=>x.sharedMaterials).ToArray();
                    var rightMaterials=rightHead.GetComponentsInChildren<Renderer>(true).SelectMany(x=>x.sharedMaterials).ToArray();
                    Check(leftMaterials.Length>0&&leftMaterials.SequenceEqual(rightMaterials),"left/right ToolVisual materials from same prefab "+kind);
                        Check(!rig.hand.gameObject.activeSelf&&rig.toolShells.Single(s=>s.tool==ToolKind.Gripper).roots.Last().gameObject.activeSelf==false,"source grippers hidden for non-Gripper Tool");
                    }
                }
            }
        }
        void Geometry()
        {
            profiles.Adapter.SyncVisuals();profiles.Sync();var rig=profiles.Current;if(rig==null)return;
            var source=new[]{profiles.Adapter.m0,profiles.Adapter.m1,profiles.Adapter.m2,profiles.Adapter.m3};
            for(int i=0;i<4;i++){
                var joint=rig.joints[i];var delta=Quaternion.Inverse(joint.logicalRest)*source[i].logical.localRotation;
                var expected=joint.restRotation*joint.axisBasis*delta*Quaternion.Inverse(joint.axisBasis);
                Check(Quaternion.Angle(expected,joint.visual.localRotation)<.01f,"M"+i+" source-arm visual follows existing logical delta");
                if(profiles.Selected==RobotVisualProfileId.MechanicalDualTable)
                    Check(Vector3.Distance(source[i].visual.position,joint.visual.position)<.00001f,"M"+i+" Demo_07 source pivot preserved");
            }
            if(tool.Visual!=null)Check(Vector3.Distance(tool.Visual.transform.position,rig.toolMount.position+tool.Socket.rotation*tool.Offset)<.00001f&&Quaternion.Angle(tool.Visual.transform.rotation,tool.Socket.rotation)<.01f,"right Tool head follows source-arm wrist position and verified Socket direction");
        }
        void TickEffect(ToolKind kind,int count=10)
        {for(int i=0;i<count;i++)tool.Visual.Advance(tool.Gate.Command,.02f,tool.PaintColor,kind==ToolKind.Welding?tool.WeldWidth:kind==ToolKind.Nailing?tool.FastenerSize:tool.SprayRadius,tool.EffectIntensity);}
        void Effect(ToolKind kind)
        {
            if(kind==ToolKind.Gripper)return;
            if(profiles.Selected==RobotVisualProfileId.MechanicalDualTable){
                var cell=profiles.DualCell;cell.RunCellDemo();
                for(int i=0;i<25&&cell.State!=DualTableCellPresentation.CellState.HANDLER_PLACE;i++)cell.AdvanceVirtual(.05f);
                Check(cell.State==DualTableCellPresentation.CellState.HANDLER_PLACE&&!tool.PresentationEffectsAllowed,"Handler carries workpiece with effects blocked "+kind);
                tool.SetEnabled(true);manual.Set(4,1);manual.Set(4,0);
                int blockedPaint=tool.Surface.PaintCount,blockedWeld=tool.WeldSurface.MarkCount,blockedNail=tool.FastenSurface.MarkCount;
                tool.SendMessage("LateUpdate");
                Check(tool.Gate.Running&&!tool.PresentationEffectsAllowed&&tool.Surface.PaintCount==blockedPaint&&tool.WeldSurface.MarkCount==blockedWeld&&tool.FastenSurface.MarkCount==blockedNail,"CLOSED request cannot emit effect while Handler carries "+kind);
                if(kind==ToolKind.Welding)Check(!tool.Visual.welding.ArcOn,"Welding arc OFF while Handler carries");
                if(kind==ToolKind.Nailing)Check(!tool.Visual.nailing.ImpactOn,"Nailing impact OFF while Handler carries");
                tool.SetEnabled(false);
                if(kind==ToolKind.Spray||kind==ToolKind.Welding)Capture("Dual-"+kind+"-handler-place");
                for(int i=0;i<200&&cell.State!=DualTableCellPresentation.CellState.PROCESS_READY;i++)cell.AdvanceVirtual(.05f);
                Check(cell.State==DualTableCellPresentation.CellState.PROCESS_READY&&tool.PresentationEffectsAllowed&&Vector3.Distance(profiles.Current.leftHandlerGrip.position,cell.ProcessPosition)>.30f,"Handler clears before Process effect "+kind);
                Check(Vector3.Distance(cell.Workpiece.position,cell.ProcessPosition)<.002f,"workpiece physically placed at Process "+kind);
            }
            if(kind==ToolKind.Spray)tool.Surface.Clear();
            else if(kind==ToolKind.Welding)tool.WeldSurface.Clear();
            else tool.FastenSurface.Clear();
            tool.SetEnabled(true);manual.Set(4,0);Check(!tool.Gate.Running,"initial CLOSED effect OFF "+kind);
            manual.Set(4,1);manual.Set(4,0);Check(tool.Gate.Running,"OPEN/CLOSED gate RUNNING "+kind);
            Physics.SyncTransforms();int paint=tool.Surface.PaintCount,weld=tool.WeldSurface.MarkCount,nail=tool.FastenSurface.MarkCount;
            TickEffect(kind);
            Check(kind==ToolKind.Spray?tool.Surface.PaintCount>paint:kind==ToolKind.Welding?tool.Visual.welding.ArcOn&&tool.WeldSurface.MarkCount>weld:tool.FastenSurface.MarkCount>nail,"active surface effect "+kind);
            if(profiles.Selected==RobotVisualProfileId.MechanicalDualTable)Capture("Dual-"+kind+"-process");
            var env=profiles.Environment.Current.root;env.gameObject.SetActive(false);Physics.SyncTransforms();
            paint=tool.Surface.PaintCount;weld=tool.WeldSurface.MarkCount;nail=tool.FastenSurface.MarkCount;TickEffect(kind);
            Check(tool.Surface.PaintCount==paint&&tool.WeldSurface.MarkCount==weld&&tool.FastenSurface.MarkCount==nail,"inactive environment cannot receive marks "+kind);
            if(kind==ToolKind.Welding)Check(!tool.Visual.welding.ArcOn,"inactive weld arc OFF");
            if(kind==ToolKind.Nailing)Check(!tool.Visual.nailing.ImpactOn,"inactive fastening impact OFF");
            env.gameObject.SetActive(true);Physics.SyncTransforms();
            var family=profiles.Selected;var other=family==RobotVisualProfileId.G51?RobotVisualProfileId.MechanicalDualTable:RobotVisualProfileId.G51;
            Preserved(()=>Check(profiles.Select(other),"running family switch"),"running family switch");
            Check(tool.Gate.Tool==kind&&!tool.Gate.Running,"family switch keeps Tool and stops effect");
            Preserved(()=>Check(profiles.Select(family),"family return"),"family return");
            manual.Set(4,0);Check(!tool.Gate.Running,"retained CLOSED cannot restart after family switch");
            manual.Set(4,1);manual.Set(4,0);Check(tool.Gate.Running,"new OPEN/CLOSED rearms after family switch");
            if(kind==ToolKind.Nailing){
                router.SetPaused(true);Check(!tool.Gate.Running&&!tool.Visual.nailing.ImpactOn,"Nailing Pause stops request and impact");
                router.SetPaused(false);manual.Set(4,0);Check(!tool.Gate.Running,"Nailing Resume retained CLOSED stays OFF");
                manual.Set(4,1);manual.Set(4,0);Check(tool.Gate.Running,"Nailing Resume requires new OPEN/CLOSED");
                Preserved(()=>Check(profiles.SelectTool(ToolKind.Gripper),"running Tool switch"),"running Tool switch");
                Check(!tool.Gate.Running&&profiles.Environment.Current.tool==ToolKind.Gripper,"Tool switch stops fastening and changes environment");
                Preserved(()=>Check(profiles.SelectTool(ToolKind.Nailing),"restore Nailing"),"Nailing restore");
                manual.Set(4,0);Check(!tool.Gate.Running,"retained CLOSED cannot restart after Tool switch");
            }
            tool.SetEnabled(false);Check(!tool.Gate.Running,"effect disable OFF");
        }
        IEnumerator Trace(string name,CsvInputMode mode,RobotVisualProfileId family,ToolKind kind)
        {
            Home();Select(family,kind);var output=new StringBuilder();int requested=0;
            Action tick=()=>{output.AppendLine("T "+string.Join(",",router.Approved)+" "+string.Join(",",router.Applied)+" "+string.Join(",",router.Velocity)+" "+router.MotionHold);if(tool.Gate.Running)requested++;};
            Action<RecordedHumanRow> row=x=>output.AppendLine("R "+x.FrameId+" "+string.Join(",",router.Approved)+" "+(router.Candidate==null?"--":string.Join(",",router.Candidate)));
            router.OutputApplied+=tick;router.Csv.RowConsumed+=row;
            Check(router.Csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples",name+"_agent1_result.csv"),mode),"load "+name+" "+mode);
            router.Csv.Play();
            for(int i=0;i<1000&&!router.Csv.Timeline.Finished;i++){
                router.AdvanceVirtual(.1);
                if(i==20)tool.SetEnabled(true);
                if(i==80)tool.SetEnabled(false);
                if(i==100){var other=family==RobotVisualProfileId.G51?RobotVisualProfileId.MechanicalDualTable:RobotVisualProfileId.G51;Preserved(()=>Check(profiles.Select(other),"CSV family switch"),"CSV family switch");Preserved(()=>Check(profiles.Select(family),"CSV family restore"),"CSV family restore");}
                if(i%40==0)yield return null;
            }
            router.OutputApplied-=tick;router.Csv.RowConsumed-=row;
            Check(router.Csv.Timeline.Finished&&router.Csv.ConsumedRows==477,"all 477 rows / EOF "+name+" "+mode);
            using(var hash=SHA256.Create()){
                string actual=BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(output.ToString()))).Replace("-","");
                string key="trace-"+name+"-"+mode;
                File.WriteAllText(Out+"/"+key+"-"+family+"-"+kind+".sha256",actual);
                Check(actual==File.ReadAllText("Validation/Tool1W/"+key+".sha256"),"baseline Approved/Applied trace "+name+" "+mode+" "+family+" "+kind);
            }
            if(mode==CsvInputMode.XyzStoredBodyHoldGripper)Check(requested==0,"XYZ B has zero Tool requests");
            Check(!tool.Gate.Running,"EOF effect OFF");Geometry();
        }
        void CellFlow(ToolKind kind)
        {
            Home();Select(RobotVisualProfileId.MechanicalDualTable,kind);
            var cell=profiles.DualCell;var requested=(float[])router.Requested.Clone();var approved=(float[])router.Approved.Clone();var applied=(float[])router.Applied.Clone();int epoch=router.Epoch;
            cell.RunCellDemo();for(int i=0;i<220&&cell.State!=DualTableCellPresentation.CellState.PROCESS_READY;i++)cell.AdvanceVirtual(.05f);
            Check(cell.State==DualTableCellPresentation.CellState.PROCESS_READY&&cell.HandlerClear&&tool.PresentationEffectsAllowed&&Vector3.Distance(profiles.Current.leftHandlerGrip.position,cell.ProcessPosition)>.30f,"Cell Infeed to Process and Handler clear "+kind);
            Check(requested.SequenceEqual(router.Requested)&&approved.SequenceEqual(router.Approved)&&applied.SequenceEqual(router.Applied)&&epoch==router.Epoch,"Scripted Handler never writes right-arm command "+kind);
            if(kind!=ToolKind.Gripper){
                tool.SetEnabled(true);manual.Set(4,1);manual.Set(4,0);cell.AdvanceVirtual(.05f);
                Check(cell.State==DualTableCellPresentation.CellState.PROCESS_RUNNING&&tool.Gate.Running,"Process request only after Handler clear "+kind);
                manual.Set(4,1);cell.AdvanceVirtual(.05f);Check(cell.State==DualTableCellPresentation.CellState.PROCESS_COMPLETE&&!tool.PresentationEffectsAllowed,"Process stops before Handler removal "+kind);
            }
            else cell.NextPart(); // Gripper has no virtual Tool effect; explicit UI advances the scripted cell.
            for(int i=0;i<220&&!(cell.State==DualTableCellPresentation.CellState.WAITING_FOR_PART&&Vector3.Distance(cell.Workpiece.position,cell.OutfeedPosition)<.002f);i++)cell.AdvanceVirtual(.05f);
            Check(cell.State==DualTableCellPresentation.CellState.WAITING_FOR_PART&&Vector3.Distance(cell.Workpiece.position,cell.OutfeedPosition)<.002f,"Cell Process to Outfeed continuous flow "+kind);
            Check(!tool.PresentationEffectsAllowed,"Outfeed effects OFF "+kind);tool.SetEnabled(false);
        }
        IEnumerator Start()
        {
            yield return null;yield return null;
            router=FindFirstObjectByType<SingleArmCommandRouter>();manual=router.GetComponent<ManualServoSource>();tool=router.GetComponent<Tool1Runtime>();profiles=router.GetComponent<RobotVisualProfiles>();
            router.enabled=false;tool.NotifyFocus(true);
            Check(router.Ready&&tool.Error==""&&profiles.Error==""&&tool.FastenSurface!=null,"native path / Tool / four environments ready");
            Check(Enum.GetNames(typeof(ToolKind)).SequenceEqual(new[]{"Gripper","Spray","Welding","Nailing"}),"four active Tool names / legacy ID 2 reserved");
            Check(profiles.Selected==RobotVisualProfileId.G51&&tool.Gate.Tool==ToolKind.Gripper&&!tool.Gate.Enabled,"default G51 / Gripper / effects OFF");
            Check(FindObjectsByType<SingleArmCommandRouter>(FindObjectsSortMode.None).Length==1,"single command writer");
            var original=profiles.Adapter.GetComponentsInChildren<Renderer>(true).ToDictionary(x=>x,x=>x.enabled);
            string manualBaseline=null;
            foreach(var family in families)foreach(var kind in kinds){
                Home();Select(family,kind);var data=new StringBuilder();Action tick=()=>data.AppendLine(string.Join(",",router.Approved)+" / "+string.Join(",",router.Applied)+" / "+router.MotionHold);router.OutputApplied+=tick;
                var left=profiles.Current==null?null:profiles.Current.leftArmPreview;
                var matrices=left==null?Array.Empty<Matrix4x4>():left.GetComponentsInChildren<Transform>().Select(t=>t.localToWorldMatrix).ToArray();
                for(int axis=0;axis<5;axis++){
                    manual.LoadAtomic(SingleArmCommandRouter.Home);Settle();manual.Set(axis,new[]{110f,90f,120f,115f,.85f}[axis]);Settle();Geometry();
                }
                manual.LoadAtomic(new[]{90f,70f,70f,120f,.8f});Settle();Geometry();router.OutputApplied-=tick;
                if(manualBaseline==null)manualBaseline=data.ToString();else Check(manualBaseline==data.ToString(),"Manual five-axis / compound trace unchanged "+family+" "+kind);
                if(left!=null)Check(matrices.SequenceEqual(left.GetComponentsInChildren<Transform>().Select(t=>t.localToWorldMatrix)),"left unchanged until scripted demo / no second writer");
                Home();Select(family,kind);Effect(kind);Capture(family+"-"+kind);yield return null;
                if(family==RobotVisualProfileId.MechanicalHumanoid&&kind==ToolKind.Gripper){Capture("Humanoid-front",new Vector3(0,.25f,1));Capture("Humanoid-three-quarter",new Vector3(.8f,.45f,1));Capture("Humanoid-workbench",new Vector3(.6f,.3f,1));}
            }
            Home();Select(RobotVisualProfileId.G51,ToolKind.Gripper);profiles.Sync();
            Check(original.All(x=>x.Key.enabled==x.Value),"original G51 renderers and linkage restored");
            Check(!profiles.SelectTool((ToolKind)2)&&tool.Gate.Tool==ToolKind.Gripper&&!tool.Gate.Enabled&&!tool.Gate.Running,"legacy Drill ID 2 remains inactive / Gripper fallback");
            foreach(var kind in kinds)if(kind!=ToolKind.Gripper)CellFlow(kind);
            foreach(var mode in new[]{CsvInputMode.RecordedHumanAngles,CsvInputMode.XyzStoredBodyAuxGripper,CsvInputMode.XyzStoredBodyHoldGripper})
                foreach(var name in new[]{"cnn","mediapipe"})foreach(var family in families)foreach(var kind in kinds)
                    yield return Trace(name,mode,family,kind);
            Home();Select(RobotVisualProfileId.G51,ToolKind.Gripper);Check(router.HardwareTxCount==0,"Hardware TX=0");
            Passed=Complete=true;Save();Debug.Log("VISUAL-2 ACCEPTANCE PASS");
        }
        void Capture(string name,Vector3? direction=null)
        {
            var source=FindFirstObjectByType<ControlStudioOrbitCamera>().GetComponent<Camera>();var camera=new GameObject("Visual2 evidence camera").AddComponent<Camera>();camera.CopyFrom(source);camera.rect=new Rect(0,0,1,1);
            camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=new Color(.025f,.04f,.06f);
            var list=new List<Renderer>();
            if(profiles.Current==null)list.AddRange(profiles.Adapter.GetComponentsInChildren<Renderer>());
            else list.AddRange(profiles.Current.GetComponentsInChildren<Renderer>());
            list.AddRange(profiles.Environment.Current.root.GetComponentsInChildren<Renderer>());
            if(tool.Visual!=null)list.AddRange(tool.Visual.GetComponentsInChildren<Renderer>());
            var renderers=list.Where(x=>x!=null&&x.enabled&&x.gameObject.activeInHierarchy).ToArray();
            Bounds bounds=new Bounds(renderers[0].bounds.center,Vector3.zero);foreach(var renderer in renderers)bounds.Encapsulate(renderer.bounds);
            camera.transform.position=bounds.center+(direction??new Vector3(.8f,.45f,1f)).normalized*Mathf.Max(bounds.extents.magnitude*2.5f,.65f);camera.transform.LookAt(bounds.center);camera.nearClipPlane=.005f;
            var rt=new RenderTexture(1100,850,24);camera.targetTexture=rt;camera.Render();var old=RenderTexture.active;RenderTexture.active=rt;
            var tex=new Texture2D(1100,850,TextureFormat.RGB24,false);tex.ReadPixels(new Rect(0,0,1100,850),0,0);tex.Apply();
            File.WriteAllBytes(Out+"/"+name+".png",tex.EncodeToPNG());RenderTexture.active=old;camera.targetTexture=null;Destroy(rt);Destroy(tex);Destroy(camera.gameObject);
        }
    }
}
