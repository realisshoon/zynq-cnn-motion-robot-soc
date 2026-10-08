using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Isolated Editor check. No feeder or hardware output is started.
    public sealed class GripperSingleBoxAcceptance : MonoBehaviour
    {
        const string Out="Validation/GripperSingleBox";
        readonly List<string> results=new List<string>();
        readonly List<string> audit=new List<string>();
        SingleArmCommandRouter router;ManualServoSource manual;Tool1Runtime tool;RobotVisualProfiles profiles;DualGripperCellPresentation cell;
        public bool Complete,Passed;
        void Check(bool ok,string description)
        {results.Add((ok?"PASS ":"FAIL ")+description);if(!ok){Complete=true;Save();throw new Exception(description);}}
        void Save(){Directory.CreateDirectory(Out);File.WriteAllLines(Out+"/editor-results.txt",new[]{Passed?"PASS":"FAIL"}.Concat(results));File.WriteAllLines(Out+"/placement-audit.txt",audit);}
        static Transform Named(Transform root,string name)=>root.GetComponentsInChildren<Transform>(true).FirstOrDefault(t=>t.name==name);
        static string Position(Transform item)=>item==null?"MISSING":item.name+" | parent="+item.parent.name+" | local="+item.localPosition+" | world="+item.position;
        void Settle(){for(int i=0;i<1300&&!router.IsSettled;i++)router.AdvanceVirtual(.02);Check(router.IsSettled,"right output settled");profiles.Adapter.SyncVisuals();profiles.Sync();}
        void Capture(string name,Vector3 direction,bool detail=false)
        {
            var source=FindFirstObjectByType<ControlStudioOrbitCamera>().GetComponent<Camera>();
            var camera=new GameObject("Single box evidence camera").AddComponent<Camera>();camera.CopyFrom(source);
            camera.rect=new Rect(0,0,1,1);camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=new Color(.018f,.025f,.035f);
            var visible=profiles.Current.GetComponentsInChildren<Renderer>().Concat(cell.CellRoot.GetComponentsInChildren<Renderer>())
                .Where(r=>r!=null&&r.enabled&&r.gameObject.activeInHierarchy).ToArray();
            Check(visible.Length>0,"capture renderers available "+name);
            Bounds bounds;
            if(detail){
                bounds=new Bounds(cell.ActiveBox.Collider.bounds.center,Vector3.zero);
                bounds.Encapsulate(cell.SupplyZone.position);bounds.Encapsulate(cell.PickupZone.position);
                bounds.Encapsulate(cell.Workbench.Find("Workbench top").GetComponent<Renderer>().bounds);
                bounds.Encapsulate(profiles.Current.leftHandlerGrip.position);
            }else{
                bounds=new Bounds(visible[0].bounds.center,Vector3.zero);
                foreach(var renderer in visible)bounds.Encapsulate(renderer.bounds);
            }
            camera.transform.position=bounds.center+direction.normalized*Mathf.Max(bounds.extents.magnitude*(detail?1.9f:2.5f),detail?.25f:.8f);
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
            yield return null;
            Check(cell!=null&&cell.IsLayoutCheckOnly&&cell.CellRoot!=null,"fixed cell active, feeder disabled");
            var rig=profiles.Current;var left=cell.LeftRoot;var right=cell.RightRoot;var bench=cell.Workbench;var supply=cell.SupplyZone;
            var leftBase=Named(left,"__G51V2_Base");var rightBase=Named(right,"__G51V2_Base");
            Check(leftBase!=null&&rightBase!=null&&bench!=null&&supply!=null&&cell.SupplyPlatform!=null,"fixed layout and supply support present");
            var box=cell.ActiveBox;
            Check(box!=null&&box.name=="WorkpieceBox_001"&&box.Id==1,"one named supply box");
            Check(FindObjectsByType<GripperWorkpieceState>(FindObjectsInactive.Exclude,FindObjectsSortMode.None).Length==1,"exactly one active WorkpieceState");
            Check(FindObjectsByType<GripperWorkpieceState>(FindObjectsInactive.Include,FindObjectsSortMode.None).Length==1,"no inactive duplicate after startup");
            var cellUi=FindObjectsByType<RectTransform>(FindObjectsInactive.Include,FindObjectsSortMode.None)
                .FirstOrDefault(t=>t.name=="Gripper Pick Place UI");
            var blocked=cellUi==null?Array.Empty<Button>():cellUi.GetComponentsInChildren<Button>(true)
                .Where(b=>new[]{"Start Auto Feed","Spawn Next Box","Reset Cell","Pause Feed"}.Contains(b.name)).ToArray();
            Check(blocked.Length==4&&blocked.All(b=>!b.interactable),"feeder and spawn buttons disabled");
            Check(box.GetComponent<MeshRenderer>()!=null&&box.Collider!=null&&box.Body!=null,"MeshRenderer BoxCollider Rigidbody State present");
            Check(box.Stage==WorkpieceStage.AvailableAtSupply&&box.Owner==WorkpieceOwner.Environment,"initial stage and Environment ownership");
            Check(box.Body.isKinematic&&!box.Body.useGravity,"static placement Rigidbody policy");
            var platformTop=cell.SupplyPlatform.Find("Supply platform top").GetComponent<BoxCollider>();
            var tableTop=bench.Find("Workbench top").GetComponent<BoxCollider>();
            Check(Mathf.Abs(box.Collider.bounds.min.y-platformTop.bounds.max.y)<.001f,"box bottom contacts supply platform top");
            float supplyOffset=Vector2.Distance(new Vector2(box.transform.position.x,box.transform.position.z),new Vector2(supply.position.x,supply.position.z));
            Check(supplyOffset>.035f&&supplyOffset<.041f&&Vector3.Distance(box.transform.position,cell.PickupZone.position)<Vector3.Distance(supply.position,cell.PickupZone.position),"box within Supply station, clear of anchor marker");
            Check(box.transform.localScale.x<=.035f&&box.transform.localScale.z<=.04f,"box small enough for gripper approach");
            Check(!box.Collider.bounds.Intersects(tableTop.bounds),"box does not penetrate workbench");
            Check(Vector3.Distance(leftBase.position,box.transform.position)>.06f&&Vector3.Distance(rightBase.position,box.transform.position)>.25f,"box clear of robot bases");
            audit.Add("LEFT ROOT: "+Position(left));audit.Add("RIGHT ROOT: "+Position(right));audit.Add("LEFT BASE: "+Position(leftBase));audit.Add("RIGHT BASE: "+Position(rightBase));
            audit.Add("WORKBENCH: "+Position(bench));audit.Add("SUPPLY: "+Position(supply));audit.Add("PICKUP: "+Position(cell.PickupZone));audit.Add("TARGET: "+Position(cell.TargetZone));
            audit.Add("SUPPLY PLATFORM: "+Position(cell.SupplyPlatform)+" | surfaceY="+platformTop.bounds.max.y);
            audit.Add("BOX: "+Position(box.transform)+" | size="+box.transform.localScale+" | bottomY="+box.Collider.bounds.min.y+" | owner="+box.Owner+" | stage="+box.Stage);
            var visualOverlaps=rig.GetComponentsInChildren<Renderer>(true).Where(r=>r.enabled&&r.gameObject.activeInHierarchy&&r.bounds.Intersects(box.Collider.bounds)).Select(r=>r.name).ToArray();
            audit.Add("BOX/ROBOT RENDERER BOUNDS INTERSECTIONS: "+(visualOverlaps.Length==0?"NONE":string.Join(", ",visualOverlaps)));
            Check(visualOverlaps.Length==0,"box does not overlap robot renderer bounds");
            var leftPos=left.position;var rightPos=right.position;var benchPos=bench.position;var supplyPos=supply.position;
            var pickupPos=cell.PickupZone.position;var targetPos=cell.TargetZone.position;var leftPose=rig.leftFeederJoints.Select(j=>j.localRotation).ToArray();var boxPos=box.transform.position;
            cell.StartAutoFeed();Check(!cell.SpawnNextBox(),"Feed and Spawn remain blocked");cell.ResetCell();
            for(int i=0;i<100;i++)cell.AdvanceVirtual(.05f);
            Check(!cell.AutoFeed&&cell.CycleNumber==0&&cell.ActiveBox==box&&box.transform.position==boxPos,"no feeder motion, reset spawn or box drift");
            Check(rig.leftFeederJoints.Select(j=>j.localRotation).SequenceEqual(leftPose),"left feeder joints fixed");
            Check(left.position==leftPos&&right.position==rightPos&&bench.position==benchPos,"robot roots and workbench fixed");
            Check(supply.position==supplyPos&&cell.PickupZone.position==pickupPos&&cell.TargetZone.position==targetPos,"three anchors fixed");
            Capture("front",new Vector3(1,.18f,0));Capture("side",new Vector3(0,.18f,1));Capture("three-quarter",new Vector3(.8f,.45f,1));Capture("supply-detail",new Vector3(.8f,.42f,.65f),true);
            var rightRotation=rig.joints[0].visual.localRotation;
            Check(manual.LoadAtomic(new[]{110f,70f,120f,87f,.8f}),"right Manual command accepted");Settle();
            Check(Quaternion.Angle(rightRotation,rig.joints[0].visual.localRotation)>.1f,"right source visual follows Manual command");
            Check(rig.leftFeederJoints.Select(j=>j.localRotation).SequenceEqual(leftPose)&&bench.position==benchPos&&box.transform.position==boxPos,"right motion leaves left, workbench and box fixed");
            Check(FindObjectsByType<GripperWorkpieceState>(FindObjectsInactive.Exclude,FindObjectsSortMode.None).Length==1,"right motion creates no box");
            Check(profiles.Select(RobotVisualProfileId.G51),"legacy regression starts at G51");
            Check(router.HardwareTxCount==0,"Hardware TX=0");Passed=Complete=true;Save();Debug.Log("GRIPPER SINGLE BOX ACCEPTANCE PASS");
        }
    }
}
