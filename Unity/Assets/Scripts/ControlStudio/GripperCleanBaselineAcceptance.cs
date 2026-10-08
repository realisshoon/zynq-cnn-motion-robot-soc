using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Explicit isolated Editor test. It never starts a feeder or creates a workpiece.
    public sealed class GripperCleanBaselineAcceptance : MonoBehaviour
    {
        const string Out="Validation/GripperCleanBaseline";
        readonly List<string> results=new List<string>();
        readonly List<string> audit=new List<string>();
        SingleArmCommandRouter router;ManualServoSource manual;Tool1Runtime tool;RobotVisualProfiles profiles;DualGripperCellPresentation cell;
        public bool Complete,Passed;
        void Check(bool ok,string description)
        {results.Add((ok?"PASS ":"FAIL ")+description);if(!ok){Complete=true;Save();throw new Exception(description);}}
        void Save(){Directory.CreateDirectory(Out);File.WriteAllLines(Out+"/editor-results.txt",new[]{Passed?"PASS":"FAIL"}.Concat(results));File.WriteAllLines(Out+"/overlap-audit.txt",audit);}
        void Settle(){for(int i=0;i<1300&&!router.IsSettled;i++)router.AdvanceVirtual(.02);Check(router.IsSettled,"right output settled");profiles.Adapter.SyncVisuals();profiles.Sync();}
        static string TransformLine(Transform item)
        {return item==null?"MISSING":item.name+" | parent="+(item.parent==null?"NONE":item.parent.name)+" | local="+item.localPosition+" | world="+item.position+" | active="+item.gameObject.activeInHierarchy;}
        static Transform Named(Transform root,string name)
        {return root.GetComponentsInChildren<Transform>(true).FirstOrDefault(t=>t.name==name);}
        static float HorizontalDistance(Vector3 point,Bounds bounds)
        {float x=Mathf.Max(bounds.min.x-point.x,0,point.x-bounds.max.x),z=Mathf.Max(bounds.min.z-point.z,0,point.z-bounds.max.z);return Mathf.Sqrt(x*x+z*z);}
        void Capture(string name,Vector3 direction)
        {
            var source=FindFirstObjectByType<ControlStudioOrbitCamera>().GetComponent<Camera>();
            var camera=new GameObject("Clean baseline evidence camera").AddComponent<Camera>();camera.CopyFrom(source);
            camera.rect=new Rect(0,0,1,1);camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=new Color(.018f,.025f,.035f);
            var visible=profiles.Current.GetComponentsInChildren<Renderer>().Concat(cell.CellRoot.GetComponentsInChildren<Renderer>())
                .Where(r=>r!=null&&r.enabled&&r.gameObject.activeInHierarchy).ToArray();
            Check(visible.Length>0,"capture renderers available "+name);
            Bounds bounds=new Bounds(visible[0].bounds.center,Vector3.zero);foreach(var renderer in visible)bounds.Encapsulate(renderer.bounds);
            camera.transform.position=bounds.center+direction.normalized*Mathf.Max(bounds.extents.magnitude*2.5f,.8f);
            camera.transform.LookAt(bounds.center);camera.nearClipPlane=.005f;
            var rt=new RenderTexture(1100,850,24);camera.targetTexture=rt;camera.Render();var old=RenderTexture.active;RenderTexture.active=rt;
            var image=new Texture2D(1100,850,TextureFormat.RGB24,false);image.ReadPixels(new Rect(0,0,1100,850),0,0);image.Apply();
            File.WriteAllBytes(Out+"/"+name+".png",image.EncodeToPNG());RenderTexture.active=old;camera.targetTexture=null;
            Destroy(rt);Destroy(image);Destroy(camera.gameObject);
        }
        IEnumerator Start()
        {
            yield return null;yield return null;Directory.CreateDirectory(Out);
            router=FindFirstObjectByType<SingleArmCommandRouter>();manual=router.GetComponent<ManualServoSource>();
            tool=router.GetComponent<Tool1Runtime>();profiles=router.GetComponent<RobotVisualProfiles>();
            router.enabled=false;tool.NotifyFocus(true);tool.SetEnabled(false);router.Csv.SelectManual();router.SetPaused(false);
            Check(manual.LoadAtomic(SingleArmCommandRouter.Home),"Home command accepted");Settle();
            Check(profiles.Select(RobotVisualProfileId.MechanicalDualTable),"Dual Table selected");
            Check(profiles.SelectTool(ToolKind.Gripper),"Gripper selected");cell=profiles.GripperCell;
            Check(cell!=null&&cell.IsLayoutCheckOnly&&cell.CellRoot!=null,"layout-only cell active");
            var rig=profiles.Current;var left=cell.LeftRoot;var right=cell.RightRoot;var bench=cell.Workbench;
            Check(left!=null&&right!=null&&bench!=null,"two source robots and one independent workbench");
            Check(bench.IsChildOf(profiles.Environment.Root)&&!bench.IsChildOf(rig.transform),"workbench under EnvironmentRoot only");
            Check(cell.SupplyZone!=null&&cell.PickupZone!=null&&cell.TargetZone!=null,"three small fixed markers");
            var leftBase=Named(left,"__G51V2_Base");var rightBase=Named(right,"__G51V2_Base");
            var tableTop=bench.Find("Workbench top");Check(leftBase!=null&&rightBase!=null&&tableTop!=null,"actual source robot bases and table top located");
            var tableBounds=tableTop.GetComponent<Renderer>().bounds;
            audit.Add("LEFT ROOT: "+TransformLine(left));audit.Add("RIGHT ROOT: "+TransformLine(right));
            audit.Add("LEFT BASE: "+TransformLine(leftBase));audit.Add("RIGHT BASE: "+TransformLine(rightBase));
            audit.Add("WORKBENCH: "+TransformLine(bench)+" | bounds="+tableBounds);
            audit.Add("SUPPLY: "+TransformLine(cell.SupplyZone));audit.Add("PICKUP: "+TransformLine(cell.PickupZone));audit.Add("TARGET: "+TransformLine(cell.TargetZone));
            var legacyFrame=Named(rig.transform,"G51 source dual table / fixed frame");
            var legacy=legacyFrame==null?null:Named(legacyFrame,"Shared metal workplate");
            audit.Add("LEGACY FRAME: "+TransformLine(legacyFrame));
            audit.Add("LEGACY SHARED PLATE: "+TransformLine(legacy));
            var blocks=profiles.Environment.Root.GetComponentsInChildren<Transform>(true).Where(t=>t.name.StartsWith("Loose block ")).ToArray();
            audit.Add("LEGACY ENVIRONMENT BLOCK COUNT: "+blocks.Length);
            foreach(var item in blocks)audit.Add("LEGACY BLOCK: "+TransformLine(item));
            audit.Add("ACTIVE BOX COUNT: "+FindObjectsByType<GripperWorkpieceState>(FindObjectsInactive.Exclude,FindObjectsSortMode.None).Length);
            Check(Vector3.Distance(leftBase.position,rightBase.position)>.40f,"source robot bases separated");
            Check(HorizontalDistance(leftBase.position,tableBounds)>.07f&&HorizontalDistance(rightBase.position,tableBounds)>.07f,"workbench does not overlap either base footprint");
            Check(Vector3.Distance(cell.SupplyZone.position,cell.PickupZone.position)>.18f&&Vector3.Distance(cell.PickupZone.position,cell.TargetZone.position)>.18f,"three zones visibly separated");
            Check(cell.CellRoot.GetComponentsInChildren<Transform>(true).Count(t=>t.name=="CENTRAL WORKBENCH / FIXED")==1,"exactly one baseline workbench");
            Check(cell.CellRoot.GetComponentsInChildren<Transform>(true).Count(t=>t.name.EndsWith("/ FIXED MARKER"))==3,"exactly three baseline markers");
            Check(!profiles.Environment.Current.root.gameObject.activeInHierarchy&&legacy!=null&&!legacy.gameObject.activeInHierarchy,"legacy Gripper blocks and shared plate hidden");
            Check(cell.ActiveBox==null&&FindObjectsByType<GripperWorkpieceState>(FindObjectsInactive.Include,FindObjectsSortMode.None).Length==0,"no Box or WorkpieceState in scene");
            var leftPose=rig.leftFeederJoints.Select(j=>j.localRotation).ToArray();
            var leftRoot=left.position;var rightRoot=right.position;var workbench=bench.position;
            var supply=cell.SupplyZone.position;var pickup=cell.PickupZone.position;var target=cell.TargetZone.position;
            cell.StartAutoFeed();Check(!cell.SpawnNextBox(),"Feed and Spawn methods blocked in layout mode");
            cell.PauseFeed();cell.SetRepeatLimit(5);cell.ResetCell();
            for(int i=0;i<100;i++)cell.AdvanceVirtual(.05f);
            Check(!cell.AutoFeed&&!cell.FeedPaused&&cell.RepeatLimit==0&&cell.ActiveBox==null&&cell.CycleNumber==0,"Reset and virtual time cannot start process");
            Check(left.position==leftRoot&&right.position==rightRoot&&bench.position==workbench,"both robot roots and workbench fixed");
            Check(cell.SupplyZone.position==supply&&cell.PickupZone.position==pickup&&cell.TargetZone.position==target,"all three markers fixed");
            Check(rig.leftFeederJoints.Select(j=>j.localRotation).SequenceEqual(leftPose),"left feeder joints never rotate");
            Capture("front",new Vector3(0,.18f,1));Capture("side",new Vector3(1,.18f,0));Capture("three-quarter",new Vector3(.8f,.45f,1));
            var rightRotation=rig.joints[0].visual.localRotation;
            Check(manual.LoadAtomic(new[]{110f,70f,120f,87f,.8f}),"right Manual command accepted");Settle();
            Check(Quaternion.Angle(rightRotation,rig.joints[0].visual.localRotation)>.1f,"right source visual follows Manual command");
            Check(rig.leftFeederJoints.Select(j=>j.localRotation).SequenceEqual(leftPose)&&bench.position==workbench,"right motion leaves left and workbench fixed");
            Check(FindObjectsByType<GripperWorkpieceState>(FindObjectsInactive.Include,FindObjectsSortMode.None).Length==0,"right motion creates no Box");
            Check(profiles.Select(RobotVisualProfileId.G51),"legacy regression starts at G51");
            Check(router.HardwareTxCount==0,"Hardware TX=0");Passed=Complete=true;Save();Debug.Log("GRIPPER CLEAN BASELINE ACCEPTANCE PASS");
        }
    }
}
