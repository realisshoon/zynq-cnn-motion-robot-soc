using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Fixed-base, virtual pick/place presentation. The right arm has no command writer here.
    [DefaultExecutionOrder(1450)]
    public sealed class DualGripperCellPresentation : MonoBehaviour
    {
        // Keep the feeder implementation for a later approved phase. Only one fixed supply box is enabled.
        static readonly bool LayoutCheckOnly=true;
        public bool IsLayoutCheckOnly=>LayoutCheckOnly;
        public enum FeederState { FEEDER_HOME, FEEDER_APPROACH_SUPPLY, FEEDER_PICK, FEEDER_LIFT, FEEDER_MOVE_TO_PICKUP, FEEDER_PLACE, FEEDER_RETURN_HOME, WAITING_FOR_RIGHT_ARM }
        public enum CycleState { IDLE, FEEDING, READY_FOR_PICK, RIGHT_HOLDING, WAIT_TARGET, COMPLETE }
        public CycleState State {get;private set;}=CycleState.IDLE;
        public FeederState Feeder {get;private set;}=FeederState.FEEDER_HOME;
        public GripperWorkpieceState ActiveBox {get;private set;}
        public int CompletedCount {get;private set;}
        public int CycleNumber {get;private set;}
        public bool AutoFeed {get;private set;}
        public bool FeedPaused {get;private set;}
        public Transform SupplyZone {get;private set;}
        public Transform PickupZone {get;private set;}
        public Transform TargetZone {get;private set;}
        public Transform Workbench {get;private set;}
        public Transform SupplyPlatform {get;private set;}
        public Transform LeftCarryAnchor {get;private set;}
        public Transform RightCarryAnchor {get;private set;}
        public Transform LeftRoot=>rig==null?null:rig.leftArmPreview;
        public Transform RightRoot=>rig==null?null:rig.rightArm;
        public Transform CellRoot=>cellRoot;
        public float PickRadius=>pickRadius;
        public int RepeatLimit=>repeatLimit;
        [SerializeField,Range(.04f,.18f)]float pickRadius=.12f;
        RobotVisualProfiles profiles;SingleArmCommandRouter router;Tool1Runtime tool;RobotVisualProfileRig rig;
        Transform cellRoot,completedRack;Transform[] leftJoints;Quaternion[] rest,from,to;Quaternion[] basis;
        readonly List<GripperWorkpieceState> completed=new List<GripperWorkpieceState>();
        readonly HandClosureInterpreter closure=new HandClosureInterpreter();
        Text status,repeatLabel,pauseLabel;GameObject ui;int repeatLimit,nextId=1,observedEpoch;long lastObservationId=long.MinValue;
        bool active,rightArmed,targetSettle;float phase,duration,completeTimer,settleTimer,rackTimer;Vector3 rackFrom,rackTo;
        static readonly float[] Home={0,0,0,0},Supply={-35,-20,0,0},Lift={-35,4,0,0},Pickup={35,-20,0,0};
        const float FeedSeconds=.85f,SettleSeconds=.25f,SuccessDisplaySeconds=.75f,RackMoveSeconds=.65f;

        public void Initialize(RobotVisualProfiles presentation,SingleArmCommandRouter command,Tool1Runtime runtime,Transform canvas)
        {profiles=presentation;router=command;tool=runtime;BuildUI(canvas);tool.GripperObservation+=OnGripperObservation;RefreshSelection();}

        public void RefreshSelection()
        {
            if(leftJoints!=null&&rest!=null)for(int i=0;i<leftJoints.Length;i++)if(leftJoints[i]!=null)leftJoints[i].localRotation=rest[i];
            if(ActiveBox!=null)Destroy(ActiveBox.gameObject);
            foreach(var box in completed)if(box!=null)Destroy(box.gameObject);
            if(LeftCarryAnchor!=null)Destroy(LeftCarryAnchor.gameObject);
            if(RightCarryAnchor!=null)Destroy(RightCarryAnchor.gameObject);
            if(cellRoot!=null){cellRoot.gameObject.SetActive(false);Destroy(cellRoot.gameObject);}
            cellRoot=null;rig=null;leftJoints=null;rest=null;ActiveBox=null;SupplyPlatform=null;completed.Clear();
            active=profiles.Selected==RobotVisualProfileId.MechanicalDualTable&&tool.Gate.Tool==ToolKind.Gripper;
            if(ui!=null)ui.SetActive(active);
            if(!active){State=CycleState.IDLE;return;}
            rig=profiles.Current;
            if(rig==null||rig.leftFeederJoints==null||rig.leftFeederJoints.Length!=4||rig.leftHandlerGrip==null||rig.toolMount==null){active=false;ui.SetActive(false);return;}
            if(LayoutCheckOnly){BuildCleanBaseline();return;}
            leftJoints=rig.leftFeederJoints;rest=new Quaternion[4];basis=new[]{profiles.Adapter.m0.axisBasis,profiles.Adapter.m1.axisBasis,profiles.Adapter.m2.axisBasis,profiles.Adapter.m3.axisBasis};
            for(int i=0;i<4;i++)rest[i]=leftJoints[i].localRotation;
            cellRoot=new GameObject("Fixed Gripper Pick Place Cell / EnvironmentRoot").transform;cellRoot.SetParent(profiles.Environment.Root,false);
            LeftCarryAnchor=new GameObject("Left CarryAnchor / visual only").transform;LeftCarryAnchor.SetParent(rig.leftHandlerGrip,false);LeftCarryAnchor.localPosition=Vector3.forward*.025f;
            RightCarryAnchor=new GameObject("Right CarryAnchor / visual only").transform;RightCarryAnchor.SetParent(rig.toolMount,false);RightCarryAnchor.localPosition=Vector3.forward*.025f;
            ApplyPose(Supply);Vector3 supply=LeftCarryAnchor.position;
            ApplyPose(Pickup);Vector3 pickup=LeftCarryAnchor.position;
            ApplyPose(Home);
            Vector3 target=pickup-rig.transform.right*.23f;
            SupplyZone=Anchor("BOX SUPPLY",supply,new Color(.14f,.42f,.80f));
            PickupZone=Anchor("PICKUP ZONE",pickup,new Color(.93f,.64f,.12f));
            TargetZone=Anchor("TARGET ZONE",target,new Color(.18f,.74f,.40f));
            var metal=Resources.Load<Material>("VisualProfiles/Titanium");var dark=Resources.Load<Material>("VisualProfiles/JointGraphite");
            Vector3 center=(supply+pickup+target)/3f;float width=Mathf.Max(.66f,Vector3.Distance(supply,target)+.30f);
            Workbench=Solid("FIXED industrial workbench",cellRoot,center-Vector3.up*.23f,new Vector3(width,.024f,.38f),metal);
            for(int side=-1;side<=1;side+=2)for(int end=-1;end<=1;end+=2)
                Solid("Workbench metal leg",cellRoot,center+rig.transform.right*(side*width*.42f)+rig.transform.forward*(end*.15f)-Vector3.up*.365f,new Vector3(.025f,.25f,.025f),dark);
            foreach(var zone in new[]{SupplyZone,PickupZone,TargetZone})
                Solid(zone.name+" fixture post",cellRoot,zone.position-Vector3.up*.12f,new Vector3(.028f,.19f,.028f),dark);
            Vector3 floorCenter=(rig.leftArmPreview.position+rig.rightArm.position+center)/3f;
            floorCenter.y=center.y-.50f;
            Solid("Dark workshop floor",cellRoot,floorCenter,new Vector3(1.25f,.02f,.85f),dark);
            completedRack=new GameObject("Completed rack / fixed").transform;completedRack.SetParent(cellRoot,false);completedRack.position=target+rig.transform.forward*.42f;
            Solid("Completed rack shelf",cellRoot,completedRack.position-Vector3.up*.04f,new Vector3(.42f,.025f,.18f),metal);
            Solid("Metal rear rack rail",cellRoot,completedRack.position+Vector3.up*.035f-rig.transform.forward*.08f,new Vector3(.42f,.025f,.018f),dark);
            for(int side=-1;side<=1;side+=2)
                Solid("Completed rack support",cellRoot,completedRack.position+rig.transform.right*(side*.17f)-Vector3.up*.255f,new Vector3(.022f,.42f,.022f),dark);
            State=CycleState.IDLE;Feeder=FeederState.FEEDER_HOME;AutoFeed=false;FeedPaused=false;CompletedCount=CycleNumber=0;nextId=1;observedEpoch=router.Epoch;lastObservationId=long.MinValue;closure.Reset();rightArmed=false;targetSettle=false;
            rig.leftHandlerGripperVisual?.Apply(1f);
            tool.SetPresentationEffectsAllowed(true);
            Physics.SyncTransforms();
        }

        void BuildCleanBaseline()
        {
            cellRoot=new GameObject("Gripper Cell CLEAN BASELINE / EnvironmentRoot").transform;
            cellRoot.SetParent(profiles.Environment.Root,false);
            Vector3 left=rig.leftArmPreview.position,right=rig.rightArm.position;
            Vector3 lateral=rig.transform.right,forward=rig.transform.forward;
            float markerY=right.y-.68f,tableY=right.y-.90f;
            Vector3 supply=left-lateral*.10f+forward*.04f;
            Vector3 pickup=(left+right)*.5f-lateral*.08f+forward*.04f;
            Vector3 target=right-lateral*.05f+forward*.04f;
            supply.y=pickup.y=target.y=markerY;
            SupplyZone=SmallAnchor("SUPPLY",supply,new Color(.12f,.48f,1f));
            PickupZone=SmallAnchor("PICKUP",pickup,new Color(1f,.65f,.10f));
            TargetZone=SmallAnchor("TARGET",target,new Color(.18f,.9f,.4f));
            var metal=Resources.Load<Material>("VisualProfiles/Titanium");
            var dark=Resources.Load<Material>("VisualProfiles/JointGraphite");
            Workbench=new GameObject("CENTRAL WORKBENCH / FIXED").transform;
            Workbench.SetParent(cellRoot,false);
            Workbench.position=(left+right)*.5f+forward*.04f;
            Workbench.position=new Vector3(Workbench.position.x,tableY,Workbench.position.z);
            Solid("Workbench top",Workbench,Workbench.position,new Vector3(.28f,.02f,.18f),metal);
            for(int side=-1;side<=1;side+=2)for(int end=-1;end<=1;end+=2)
                Solid("Workbench leg",Workbench,Workbench.position+new Vector3(side*.11f,-.13f,end*.065f),new Vector3(.018f,.24f,.018f),dark);
            CreateSingleSupplyBox(metal,dark);
            State=CycleState.IDLE;Feeder=FeederState.FEEDER_HOME;AutoFeed=false;FeedPaused=false;
            CompletedCount=CycleNumber=0;nextId=1;rightArmed=targetSettle=false;closure.Reset();
            Physics.SyncTransforms();
        }

        void CreateSingleSupplyBox(Material metal,Material dark)
        {
            const float platformThickness=.008f;
            Vector3 boxSize=new Vector3(.030f,.025f,.035f);
            Vector3 station=SupplyZone.position+(PickupZone.position-SupplyZone.position).normalized*.04f;
            float workSurface=Workbench.Find("Workbench top").GetComponent<Renderer>().bounds.max.y;
            float supplySurface=SupplyZone.position.y-boxSize.y;
            SupplyPlatform=new GameObject("SUPPLY PLATFORM / FIXED").transform;
            SupplyPlatform.SetParent(cellRoot,false);
            SupplyPlatform.position=new Vector3(station.x,supplySurface-platformThickness*.5f,station.z);
            Solid("Supply platform top",SupplyPlatform,SupplyPlatform.position,new Vector3(.065f,platformThickness,.065f),metal);
            float supportHeight=supplySurface-platformThickness-workSurface;
            if(supportHeight>0)
                Solid("Supply platform support",SupplyPlatform,new Vector3(station.x,workSurface+supportHeight*.5f,station.z),new Vector3(.018f,supportHeight,.018f),dark);
            var go=GameObject.CreatePrimitive(PrimitiveType.Cube);
            go.transform.SetParent(cellRoot,true);
            go.transform.localScale=boxSize;
            go.transform.position=new Vector3(station.x,supplySurface+boxSize.y*.5f,station.z);
            var renderer=go.GetComponent<Renderer>();renderer.sharedMaterial=Resources.Load<Material>("VisualProfiles/Polymer");
            var tint=new MaterialPropertyBlock();tint.SetColor("_BaseColor",new Color(.62f,.59f,.52f));tint.SetColor("_Color",new Color(.62f,.59f,.52f));renderer.SetPropertyBlock(tint);
            go.AddComponent<Rigidbody>();
            ActiveBox=go.AddComponent<GripperWorkpieceState>();
            ActiveBox.InitializeAtSupply(1);
        }

        Transform SmallAnchor(string title,Vector3 center,Color color)
        {
            var anchor=new GameObject(title+" / FIXED MARKER").transform;anchor.SetParent(cellRoot,false);anchor.position=center;
            var sphere=GameObject.CreatePrimitive(PrimitiveType.Sphere);sphere.name=title+" marker";sphere.transform.SetParent(anchor,false);sphere.transform.localScale=Vector3.one*.035f;
            Destroy(sphere.GetComponent<Collider>());
            var tint=new MaterialPropertyBlock();tint.SetColor("_BaseColor",color);tint.SetColor("_Color",color);sphere.GetComponent<Renderer>().SetPropertyBlock(tint);
            var label=new GameObject(title+" label",typeof(TextMesh));label.transform.SetParent(anchor,false);label.transform.localPosition=Vector3.up*.035f;label.transform.rotation=Quaternion.Euler(90,0,0);
            var text=label.GetComponent<TextMesh>();text.text=title;text.fontSize=30;text.characterSize=.006f;text.anchor=TextAnchor.MiddleCenter;text.color=color;
            return anchor;
        }

        Transform Anchor(string title,Vector3 center,Color color)
        {
            var anchor=new GameObject(title+" / FIXED",typeof(BoxCollider)).transform;anchor.SetParent(cellRoot,false);anchor.position=center;
            var trigger=anchor.GetComponent<BoxCollider>();trigger.isTrigger=true;trigger.size=new Vector3(.13f,.10f,.13f);
            var pad=Solid(title+" marker",anchor,center-Vector3.up*.037f,new Vector3(.15f,.008f,.15f),Resources.Load<Material>("VisualProfiles/SignalTeal"));
            var renderer=pad.GetComponent<Renderer>();var tint=new MaterialPropertyBlock();tint.SetColor("_BaseColor",color);tint.SetColor("_Color",color);renderer.SetPropertyBlock(tint);
            var text=new GameObject(title+" label",typeof(TextMesh));text.transform.SetParent(anchor,false);text.transform.position=center-Vector3.up*.028f;text.transform.rotation=Quaternion.Euler(90,0,0);
            var label=text.GetComponent<TextMesh>();label.text=title;label.fontSize=30;label.characterSize=.006f;label.anchor=TextAnchor.MiddleCenter;label.color=Color.white;
            return anchor;
        }
        static Transform Solid(string title,Transform parent,Vector3 world,Vector3 size,Material material)
        {var part=GameObject.CreatePrimitive(PrimitiveType.Cube).transform;part.name=title;part.SetParent(parent,true);part.position=world;part.localScale=size;part.GetComponent<Renderer>().sharedMaterial=material;return part;}
        Quaternion PoseRotation(int index,float degrees)
        {var axis=index==0||index==3?Vector3.up:Vector3.right;return rest[index]*basis[index]*Quaternion.AngleAxis(degrees,axis)*Quaternion.Inverse(basis[index]);}
        void ApplyPose(float[] angles){for(int i=0;i<4;i++)leftJoints[i].localRotation=PoseRotation(i,angles[i]);}
        void Transition(FeederState state,float[] pose,float seconds)
        {Feeder=state;phase=0;duration=seconds;from=new Quaternion[4];to=new Quaternion[4];for(int i=0;i<4;i++){from[i]=leftJoints[i].localRotation;to[i]=PoseRotation(i,pose[i]);}}
        bool StepPose(float seconds)
        {phase=Mathf.Min(1,phase+seconds/duration);float t=Mathf.SmoothStep(0,1,phase);for(int i=0;i<4;i++)leftJoints[i].localRotation=Quaternion.Slerp(from[i],to[i],t);return phase>=1;}

        public void StartAutoFeed(){if(LayoutCheckOnly||!active)return;AutoFeed=true;FeedPaused=false;if(ActiveBox==null)SpawnNextBox();}
        public void PauseFeed(){if(LayoutCheckOnly)return;if(active)FeedPaused=!FeedPaused;}
        public void SetRepeatLimit(int limit){if(LayoutCheckOnly)return;repeatLimit=limit==5||limit==10?limit:0;}
        public bool SpawnNextBox()
        {
            if(LayoutCheckOnly||!active||ActiveBox!=null||(repeatLimit>0&&CompletedCount>=repeatLimit))return false;
            var material=Resources.Load<Material>("VisualProfiles/Polymer");var go=GameObject.CreatePrimitive(PrimitiveType.Cube);
            go.name="BOX_"+nextId.ToString("D3");
            go.transform.SetParent(cellRoot,true);go.transform.position=SupplyZone.position;go.transform.localScale=new Vector3(.052f,.045f,.052f);
            go.GetComponent<Renderer>().sharedMaterial=material;go.AddComponent<Rigidbody>();ActiveBox=go.AddComponent<GripperWorkpieceState>();ActiveBox.Initialize(nextId++);
            CycleNumber++;State=CycleState.FEEDING;Feeder=FeederState.FEEDER_HOME;phase=0;rightArmed=false;targetSettle=false;closure.Reset();rig.leftHandlerGripperVisual?.Apply(1f);
            return true;
        }
        public void ResetCell()
        {
            if(LayoutCheckOnly){AutoFeed=FeedPaused=false;State=CycleState.IDLE;return;}
            if(!active)return;
            if(ActiveBox!=null)Destroy(ActiveBox.gameObject);ActiveBox=null;
            foreach(var box in completed)if(box!=null)Destroy(box.gameObject);completed.Clear();
            for(int i=0;i<4;i++)leftJoints[i].localRotation=rest[i];rig.leftHandlerGripperVisual?.Apply(1f);
            CompletedCount=CycleNumber=0;nextId=1;AutoFeed=FeedPaused=false;State=CycleState.IDLE;Feeder=FeederState.FEEDER_HOME;closure.Reset();rightArmed=false;targetSettle=false;lastObservationId=long.MinValue;
        }

        void OnGripperObservation(HandClosureObservation observation)
        {
            if(LayoutCheckOnly)return;
            if(!active||ActiveBox==null||tool.Gate.Tool!=ToolKind.Gripper||!router.Ready||router.Paused||router.MotionHold!=0||observation.SourceEpoch!=router.Epoch||!observation.Valid||(!observation.Fresh&&!observation.MaintainedManualIntent)||observation.Id==lastObservationId||observation.Time>tool.Now+1e-6||(!observation.MaintainedManualIntent&&tool.Now-observation.Time>ToolRunGate.StaleSeconds))return;
            if(float.IsNaN(observation.Norm)||float.IsInfinity(observation.Norm)||observation.Norm<0||observation.Norm>1)return;
            if(router.Source=="CSV"&&router.Csv.Timeline!=null&&router.Csv.Timeline.EndOfInput)return;
            lastObservationId=observation.Id;
            var previous=closure.State;var current=closure.Observe(observation.Norm,observation.HasClosureState?(ClosureState?)observation.ProvidedClosure:null);
            if(current==ClosureState.Open){rightArmed=true;if(ActiveBox.Owner==WorkpieceOwner.RightProcess){ActiveBox.Release(WorkpieceOwner.RightProcess,cellRoot);State=CycleState.WAIT_TARGET;settleTimer=0;targetSettle=true;}return;}
            if(current!=ClosureState.Closed||previous==ClosureState.Closed||!rightArmed||Feeder!=FeederState.WAITING_FOR_RIGHT_ARM||ActiveBox.Owner!=WorkpieceOwner.None)return;
            if(Vector3.Distance(RightCarryAnchor.position,ActiveBox.transform.position)>pickRadius)return;
            if(ActiveBox.Attach(WorkpieceOwner.RightProcess,RightCarryAnchor)){State=CycleState.RIGHT_HOLDING;rightArmed=false;targetSettle=false;}
        }

        public void AdvanceVirtual(float seconds)
        {
            if(LayoutCheckOnly)return;
            if(!active||seconds<=0||router.Paused)return;
            if(observedEpoch!=router.Epoch){observedEpoch=router.Epoch;closure.Reset();rightArmed=false;lastObservationId=long.MinValue;}
            if(State==CycleState.FEEDING&&!FeedPaused)AdvanceFeeder(seconds);
            if(ActiveBox!=null&&ActiveBox.Owner==WorkpieceOwner.RightProcess&&State==CycleState.RIGHT_HOLDING&&Vector3.Distance(ActiveBox.transform.position,PickupZone.position)>.12f)State=CycleState.WAIT_TARGET;
            if(ActiveBox!=null&&ActiveBox.Owner==WorkpieceOwner.None&&targetSettle){
                if(TargetZone.GetComponent<BoxCollider>().bounds.Contains(ActiveBox.transform.position)){settleTimer+=seconds;if(settleTimer>=SettleSeconds)CompleteCycle();}
                else settleTimer=0;
            }
            if(State==CycleState.COMPLETE&&!FeedPaused)AdvanceCompleted(seconds);
            Physics.SyncTransforms();
        }
        void AdvanceFeeder(float seconds)
        {
            if(ActiveBox==null)return;
            switch(Feeder)
            {
                case FeederState.FEEDER_HOME:
                    phase+=seconds;if(phase>=.2f)Transition(FeederState.FEEDER_APPROACH_SUPPLY,Supply,FeedSeconds);
                    break;
                case FeederState.FEEDER_APPROACH_SUPPLY:
                    if(StepPose(seconds)){Feeder=FeederState.FEEDER_PICK;phase=0;}
                    break;
                case FeederState.FEEDER_PICK:
                    phase+=seconds;if(phase>=.18f){
                        if(Vector3.Distance(LeftCarryAnchor.position,ActiveBox.transform.position)>pickRadius||!ActiveBox.Attach(WorkpieceOwner.LeftFeeder,LeftCarryAnchor)){State=CycleState.IDLE;return;}
                        rig.leftHandlerGripperVisual?.Apply(0f);Transition(FeederState.FEEDER_LIFT,Lift,.6f);
                    }
                    break;
                case FeederState.FEEDER_LIFT:
                    if(StepPose(seconds))Transition(FeederState.FEEDER_MOVE_TO_PICKUP,Pickup,1.1f);
                    break;
                case FeederState.FEEDER_MOVE_TO_PICKUP:
                    if(StepPose(seconds)){Feeder=FeederState.FEEDER_PLACE;phase=0;}
                    break;
                case FeederState.FEEDER_PLACE:
                    phase+=seconds;if(phase>=.18f){
                        if(!ActiveBox.Release(WorkpieceOwner.LeftFeeder,cellRoot)){State=CycleState.IDLE;return;}
                        rig.leftHandlerGripperVisual?.Apply(1f);Transition(FeederState.FEEDER_RETURN_HOME,Home,.85f);
                    }
                    break;
                case FeederState.FEEDER_RETURN_HOME:
                    if(StepPose(seconds)){Feeder=FeederState.WAITING_FOR_RIGHT_ARM;State=CycleState.READY_FOR_PICK;}
                    break;
            }
        }
        void CompleteCycle()
        {
            if(ActiveBox==null||ActiveBox.Owner!=WorkpieceOwner.None)return;
            targetSettle=false;State=CycleState.COMPLETE;ActiveBox.Complete();CompletedCount++;completeTimer=rackTimer=0;
            rackFrom=ActiveBox.transform.position;rackTo=completedRack.position+rig.transform.right*((completed.Count%5)-2)*.065f+Vector3.up*.01f;
        }
        void AdvanceCompleted(float seconds)
        {
            completeTimer+=seconds;if(completeTimer<SuccessDisplaySeconds||ActiveBox==null)return;
            rackTimer=Mathf.Min(1,rackTimer+seconds/RackMoveSeconds);
            ActiveBox.transform.position=Vector3.Lerp(rackFrom,rackTo,Mathf.SmoothStep(0,1,rackTimer));
            if(rackTimer<1)return;
            completed.Add(ActiveBox);ActiveBox=null;if(completed.Count>5){Destroy(completed[0].gameObject);completed.RemoveAt(0);}
            State=CycleState.IDLE;if(AutoFeed&&(repeatLimit==0||CompletedCount<repeatLimit))SpawnNextBox();
        }
        void Update()
        {
            AdvanceVirtual(Time.deltaTime);
            if(!active||status==null)return;
            if(LayoutCheckOnly){
                string boxId=ActiveBox==null?"--":$"BOX_{ActiveBox.Id:000}";
                string state=ActiveBox==null?"UNAVAILABLE":"AT_SUPPLY";
                string owner=ActiveBox==null?"--":ActiveBox.Owner.ToString().ToUpperInvariant();
                status.text=$"GRIPPER CELL  |  CELL STATE: SINGLE BOX / {state}\nCurrent Box: {boxId}  |  Box State: {state}  |  Owner: {owner}\nLEFT FEEDER: DISABLED  |  RIGHT ARM: HUMAN MOTION / ACTIVE\nFeed / Spawn disabled; one fixed supply box only.  |  VIRTUAL ONLY / TX=0";
                return;
            }
            string id=ActiveBox==null?"--":ActiveBox.name;
            status.text=$"GRIPPER PICK & PLACE  |  LEFT ARM: BOX FEEDER / SCRIPTED  |  RIGHT ARM: HUMAN MOTION / ACTIVE\nCycle: {CycleNumber}  |  State: {State}  |  Feeder: {Feeder}\nCurrent Box: {id}  |  Owner: {(ActiveBox==null?"--":ActiveBox.Owner.ToString())}  |  Completed: {CompletedCount}\nSupply → Pickup → Target  |  Repeat: {(repeatLimit==0?"Infinite":repeatLimit.ToString())}  |  TX=0";
            if(repeatLabel!=null)repeatLabel.text="Repeat: "+(repeatLimit==0?"Infinite":repeatLimit.ToString());
            if(pauseLabel!=null)pauseLabel.text=FeedPaused?"Resume Feed":"Pause Feed";
        }
        void OnDestroy(){if(tool!=null)tool.GripperObservation-=OnGripperObservation;}

        void BuildUI(Transform canvas)
        {
            var font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            ui=new GameObject("Gripper Pick Place UI",typeof(RectTransform),typeof(Image));var rt=(RectTransform)ui.transform;rt.SetParent(canvas,false);rt.anchorMin=rt.anchorMax=rt.pivot=new Vector2(0,1);rt.anchoredPosition=new Vector2(25,-274);rt.sizeDelta=new Vector2(925,174);ui.GetComponent<Image>().color=new Color(.035f,.065f,.085f,.94f);
            var label=new GameObject("Cell status",typeof(RectTransform),typeof(Text));label.transform.SetParent(rt,false);var lr=(RectTransform)label.transform;lr.anchorMin=lr.anchorMax=lr.pivot=new Vector2(0,1);lr.anchoredPosition=new Vector2(12,-5);lr.sizeDelta=new Vector2(900,112);status=label.GetComponent<Text>();status.font=font;status.fontSize=14;status.color=new Color(.72f,.96f,1);status.raycastTarget=false;
            var feedLabel=Button(rt,font,10,118,168,"Start Auto Feed",StartAutoFeed);
            pauseLabel=Button(rt,font,188,118,145,"Pause Feed",PauseFeed);
            var resetLabel=Button(rt,font,343,118,145,"Reset Cell",ResetCell);
            var spawnLabel=Button(rt,font,498,118,185,"Spawn Next Box",()=>SpawnNextBox());
            repeatLabel=Button(rt,font,693,118,215,"Repeat: Infinite",()=>SetRepeatLimit(repeatLimit==0?5:repeatLimit==5?10:0));
            if(LayoutCheckOnly)foreach(var item in new[]{feedLabel,pauseLabel,resetLabel,spawnLabel,repeatLabel})item.transform.parent.GetComponent<Button>().interactable=false;
        }
        static Text Button(Transform parent,Font font,float x,float y,float width,string title,UnityEngine.Events.UnityAction action)
        {
            var go=new GameObject(title,typeof(RectTransform),typeof(Image),typeof(Button));var rect=(RectTransform)go.transform;rect.SetParent(parent,false);rect.anchorMin=rect.anchorMax=rect.pivot=new Vector2(0,1);rect.anchoredPosition=new Vector2(x,-y);rect.sizeDelta=new Vector2(width,32);go.GetComponent<Image>().color=new Color(.15f,.29f,.35f);go.GetComponent<Button>().onClick.AddListener(action);
            var child=new GameObject("Label",typeof(RectTransform),typeof(Text));child.transform.SetParent(rect,false);var cr=(RectTransform)child.transform;cr.anchorMin=Vector2.zero;cr.anchorMax=Vector2.one;cr.offsetMin=cr.offsetMax=Vector2.zero;var text=child.GetComponent<Text>();text.font=font;text.fontSize=13;text.color=Color.white;text.alignment=TextAnchor.MiddleCenter;text.text=title;return text;
        }
    }
}
