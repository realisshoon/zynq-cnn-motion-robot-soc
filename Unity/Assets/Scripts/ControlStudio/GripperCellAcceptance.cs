using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Runs only in the isolated batch project. Right-arm motion uses ManualServoSource.
    public sealed class GripperCellAcceptance : MonoBehaviour
    {
        const string Out="Validation/GripperCell";
        public bool Complete,Passed;
        readonly List<string> log=new List<string>();
        SingleArmCommandRouter router;ManualServoSource manual;Tool1Runtime tool;RobotVisualProfiles profiles;DualGripperCellPresentation cell;
        Vector3 leftBase,rightBase,bench,pickup,target,supply,environment;
        Quaternion leftBaseRotation,rightBaseRotation;Vector3[] leftJointPositions;
        void Check(bool ok,string name){log.Add((ok?"PASS ":"FAIL ")+name);if(!ok){Complete=true;Save();throw new Exception(name);}}
        void Save(){Directory.CreateDirectory(Out);File.WriteAllLines(Out+"/editor-results.txt",new[]{Passed?"PASS":"FAIL"}.Concat(log));}
        void Settle()
        {for(int i=0;i<1300&&!router.IsSettled;i++)router.AdvanceVirtual(.02);Check(router.IsSettled,"right output settled");profiles.Adapter.SyncVisuals();profiles.Sync();}
        void Fixed()
        {
            Check(cell.LeftRoot.position==leftBase&&cell.LeftRoot.rotation==leftBaseRotation,"left root/base fixed");
            Check(cell.RightRoot.position==rightBase&&cell.RightRoot.rotation==rightBaseRotation,"right root/base fixed");
            Check(cell.Workbench.position==bench&&cell.PickupZone.position==pickup&&cell.TargetZone.position==target&&cell.SupplyZone.position==supply&&profiles.Environment.Root.position==environment,"workbench/zones/EnvironmentRoot fixed");
            Check(cell.LeftRoot!=null&&cell.LeftRoot.IsChildOf(profiles.Current.transform)&&!profiles.Environment.Root.IsChildOf(profiles.Current.transform),"robot/environment independent roots");
            for(int i=0;i<4;i++)Check(profiles.Current.leftFeederJoints[i].localPosition==leftJointPositions[i],"left joint local position fixed M"+i);
        }
        void FeedUntilReady(int cycle,bool capture)
        {
            bool picked=false,carried=false;
            for(int i=0;i<240&&cell.State!=DualGripperCellPresentation.CycleState.READY_FOR_PICK;i++){
                cell.AdvanceVirtual(.05f);
                if(!picked&&cell.ActiveBox!=null&&cell.ActiveBox.Owner==WorkpieceOwner.LeftFeeder){picked=true;if(capture)Capture("02-left-box-pick");}
                if(!carried&&cell.Feeder==DualGripperCellPresentation.FeederState.FEEDER_MOVE_TO_PICKUP){carried=true;if(capture)Capture("03-left-carry-to-pickup");}
                if(i%25==0)Fixed();
            }
            Check(picked&&carried&&cell.State==DualGripperCellPresentation.CycleState.READY_FOR_PICK,"left fixed-joint feed reached READY cycle "+cycle);
            Check(cell.ActiveBox.Owner==WorkpieceOwner.None&&Vector3.Distance(cell.ActiveBox.transform.position,cell.PickupZone.position)<.015f,"box released at fixed Pickup Zone cycle "+cycle);
            Check(profiles.Current.leftFeederJoints.Zip(leftJointPositions,(j,p)=>j.localPosition==p).All(x=>x),"feeder used rotation only");
            if(capture)Capture("04-pickup-box-ready");
        }
        float[] SearchPose(Vector3 destination,bool measureBox,string label)
        {
            float best=float.PositiveInfinity;float[] command=null;Vector3 bestPoint=Vector3.zero;
            foreach(float m0 in new[]{30f,50f,70f,90f,110f,130f,150f})
            foreach(float m1 in new[]{30f,50f,70f,90f,110f,130f,150f})
            foreach(float m2 in new[]{70f,90f,110f}){
                var q=new[]{m0,m1,m2,90f,measureBox?0f:1f};
                if(!manual.LoadAtomic(q))continue;
                Settle();
                var sample=measureBox?cell.ActiveBox.transform.position:cell.RightCarryAnchor.position;
                float distance=Vector3.Distance(sample,destination);
                if(distance<best){best=distance;command=q;bestPoint=sample;}
            }
            File.AppendAllText(Out+"/reachability.txt",label+" best="+best.ToString("F5")+" destination="+destination+" point="+bestPoint+" leftRoot="+cell.LeftRoot.position+" rightRoot="+cell.RightRoot.position+" command="+(command==null?"NONE":string.Join(",",command))+Environment.NewLine);
            Check(command!=null,label+" reachable command found");return command;
        }
        void Move(float[] q){Check(manual.LoadAtomic(q),"Manual right-arm command approved");Settle();}
        void Capture(string name)
        {
            var source=FindFirstObjectByType<ControlStudioOrbitCamera>().GetComponent<Camera>();var camera=new GameObject("Gripper cell evidence camera").AddComponent<Camera>();camera.CopyFrom(source);camera.rect=new Rect(0,0,1,1);camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=new Color(.018f,.025f,.035f);
            var list=new List<Renderer>();list.AddRange(profiles.Current.GetComponentsInChildren<Renderer>());list.AddRange(profiles.Environment.Current.root.GetComponentsInChildren<Renderer>());if(cell.CellRoot!=null)list.AddRange(cell.CellRoot.GetComponentsInChildren<Renderer>());
            var visible=list.Where(x=>x!=null&&x.enabled&&x.gameObject.activeInHierarchy).ToArray();Bounds bounds=new Bounds(visible[0].bounds.center,Vector3.zero);foreach(var renderer in visible)bounds.Encapsulate(renderer.bounds);
            camera.transform.position=bounds.center+new Vector3(.9f,.55f,1.1f).normalized*Mathf.Max(bounds.extents.magnitude*2.25f,.7f);camera.transform.LookAt(bounds.center);camera.nearClipPlane=.005f;
            var rt=new RenderTexture(1100,850,24);camera.targetTexture=rt;camera.Render();var previous=RenderTexture.active;RenderTexture.active=rt;var image=new Texture2D(1100,850,TextureFormat.RGB24,false);image.ReadPixels(new Rect(0,0,1100,850),0,0);image.Apply();File.WriteAllBytes(Out+"/"+name+".png",image.EncodeToPNG());RenderTexture.active=previous;camera.targetTexture=null;Destroy(rt);Destroy(image);Destroy(camera.gameObject);
        }
        IEnumerator Start()
        {
            yield return null;yield return null;
            Directory.CreateDirectory(Out);File.WriteAllText(Out+"/reachability.txt","");
            router=FindFirstObjectByType<SingleArmCommandRouter>();manual=router.GetComponent<ManualServoSource>();tool=router.GetComponent<Tool1Runtime>();profiles=router.GetComponent<RobotVisualProfiles>();cell=profiles.GripperCell;
            router.enabled=false;tool.NotifyFocus(true);tool.SetEnabled(false);router.Csv.SelectManual();router.SetPaused(false);manual.LoadAtomic(SingleArmCommandRouter.Home);Settle();
            Check(profiles.Select(RobotVisualProfileId.MechanicalDualTable),"Dual Table selected");Check(profiles.SelectTool(ToolKind.Gripper),"Gripper selected");
            Check(cell!=null&&cell.SupplyZone!=null&&cell.PickupZone!=null&&cell.TargetZone!=null,"fixed three-zone cell initialized");
            var leftVisual=profiles.Current.leftHandlerGripperVisual;var rightVisual=profiles.Current.sourceGripperVisual;
            Check(leftVisual!=null&&rightVisual!=null&&leftVisual.leftGear!=null&&rightVisual.leftGear!=null,"both source G51 gripper linkage visuals retained");
            var leftOpen=leftVisual.leftGear.localRotation;leftVisual.Apply(0f);
            Check(Quaternion.Angle(leftOpen,leftVisual.leftGear.localRotation)>.1f,"left feeder jaw gear moves on CLOSE");leftVisual.Apply(1f);
            Check(FindObjectsByType<SingleArmCommandRouter>(FindObjectsSortMode.None).Length==1,"right arm single command writer");
            leftBase=cell.LeftRoot.position;rightBase=cell.RightRoot.position;leftBaseRotation=cell.LeftRoot.rotation;rightBaseRotation=cell.RightRoot.rotation;
            bench=cell.Workbench.position;pickup=cell.PickupZone.position;target=cell.TargetZone.position;supply=cell.SupplyZone.position;environment=profiles.Environment.Root.position;
            leftJointPositions=profiles.Current.leftFeederJoints.Select(x=>x.localPosition).ToArray();Fixed();Capture("01-industrial-cell-initial");
            Check(cell.SpawnNextBox()&&cell.ActiveBox!=null,"manual spawn creates a box");
            int singleBoxId=cell.ActiveBox.Id;Check(!cell.SpawnNextBox()&&cell.ActiveBox.Id==singleBoxId,"manual spawn never duplicates active box");cell.ResetCell();
            cell.SetRepeatLimit(5);Check(cell.RepeatLimit==5,"five-cycle limit selectable");cell.SetRepeatLimit(10);Check(cell.RepeatLimit==10,"ten-cycle limit selectable");cell.SetRepeatLimit(0);Check(cell.RepeatLimit==0,"infinite repeat selectable");
            var approved=(float[])router.Approved.Clone();var applied=(float[])router.Applied.Clone();int epoch=router.Epoch;
            cell.StartAutoFeed();Check(cell.AutoFeed&&cell.ActiveBox!=null&&cell.ActiveBox.Owner==WorkpieceOwner.None,"auto feed spawns one persistent box");
            var heldFeeder=cell.Feeder;var heldRotation=profiles.Current.leftFeederJoints[0].localRotation;
            cell.PauseFeed();cell.AdvanceVirtual(1f);Check(cell.FeedPaused&&cell.Feeder==heldFeeder&&profiles.Current.leftFeederJoints[0].localRotation==heldRotation,"Pause Feed freezes left feeder");cell.PauseFeed();
            FeedUntilReady(1,true);Check(approved.SequenceEqual(router.Approved)&&applied.SequenceEqual(router.Applied)&&epoch==router.Epoch,"left scripted feed does not write right command");
            var pickupCommand=SearchPose(cell.PickupZone.position,false,"PICKUP");Move(pickupCommand);
            float pickupDistance=Vector3.Distance(cell.RightCarryAnchor.position,cell.ActiveBox.transform.position);
            File.AppendAllText(Out+"/reachability.txt","PICKUP box distance="+pickupDistance.ToString("F5")+Environment.NewLine);
            Check(pickupDistance<=cell.PickRadius,"right grip reaches Pickup box");
            float[] targetCommand=null;
            for(int cycle=1;cycle<=3;cycle++){
                if(cycle>1){Move(pickupCommand);FeedUntilReady(cycle,cycle==2);}
                Check(manual.Set(4,1f),"fresh right OPEN accepted");Settle();
                Check(manual.Set(4,0f),"fresh right CLOSE accepted");
                Check(cell.ActiveBox.Owner==WorkpieceOwner.RightProcess&&cell.State==DualGripperCellPresentation.CycleState.RIGHT_HOLDING,"user Manual CLOSE picks box cycle "+cycle);
                if(cycle==1)Capture("05-right-box-pick");
                if(targetCommand==null)targetCommand=SearchPose(cell.TargetZone.position,true,"TARGET");
                Move(targetCommand);cell.AdvanceVirtual(.02f);Fixed();
                Check(cell.ActiveBox.Owner==WorkpieceOwner.RightProcess&&cell.ActiveBox.transform.IsChildOf(cell.RightCarryAnchor),"right CarryAnchor owns box cycle "+cycle);
                if(cycle==1)Capture("06-right-box-carry");
                Check(cell.TargetZone.GetComponent<BoxCollider>().bounds.Contains(cell.ActiveBox.transform.position),"right arm brings box inside Target Zone cycle "+cycle);
                Check(manual.Set(4,1f),"fresh right OPEN releases box");Check(cell.ActiveBox.Owner==WorkpieceOwner.None,"right ownership released cycle "+cycle);
                if(cycle==1)Capture("07-target-zone-place");
                for(int i=0;i<8&&cell.State!=DualGripperCellPresentation.CycleState.COMPLETE;i++)cell.AdvanceVirtual(.05f);
                Check(cell.State==DualGripperCellPresentation.CycleState.COMPLETE&&cell.CompletedCount==cycle,"target settle completes cycle "+cycle);
                if(cycle==1)Capture("08-cycle-complete");
                for(int i=0;i<55&&(cell.ActiveBox==null||cell.ActiveBox.Owner==WorkpieceOwner.Completed);i++)cell.AdvanceVirtual(.05f);
                Check(cell.ActiveBox!=null&&cell.ActiveBox.Id==cycle+1,"automatic next box after completion "+cycle);
                if(cycle==1)Capture("09-next-box-supply");
                Fixed();yield return null;
            }
            cell.ResetCell();Check(cell.ActiveBox==null&&cell.CompletedCount==0&&cell.State==DualGripperCellPresentation.CycleState.IDLE,"Reset clears box/count/cell state");Fixed();
            Check(profiles.Select(RobotVisualProfileId.G51)&&!tool.Gate.Enabled,"existing Visual2 regression starts from default G51 / effects OFF");
            Check(router.HardwareTxCount==0,"Hardware TX=0");Passed=Complete=true;Save();Debug.Log("GRIPPER CELL ACCEPTANCE PASS");
        }
    }
}
