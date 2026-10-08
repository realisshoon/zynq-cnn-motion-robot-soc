using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;
namespace HumanMotion.ControlStudio
{
    public enum EnvironmentViewMode { RobotOnly, Workcell, AutoToolEnvironment }
    // Presentation only: reuses the existing adapter, command router and ToolSocket.
    [DefaultExecutionOrder(1500)]
    public sealed class RobotVisualProfiles : MonoBehaviour
    {
        public RobotVisualProfileId Selected {get;private set;}=RobotVisualProfileId.G51;
        public RobotVisualProfileRig Current=>rigs.TryGetValue(Selected,out var rig)?rig:null;
        public HumanoidVisualRig Humanoid {get;private set;}
        public Demo06RealisticVisualAdapter Adapter {get;private set;}
        public RobotWorkEnvironment Environment {get;private set;}
        public SimulationEnvironment Simulation {get;private set;}
        public DualTableCellPresentation DualCell {get;private set;}
        public DualGripperCellPresentation GripperCell {get;private set;}
        public string Error {get;private set;}="";
        public string SelectionMessage {get;private set;}="";
        public int PresentationEpoch {get;private set;}
        public EnvironmentViewMode EnvironmentMode {get;private set;}=EnvironmentViewMode.RobotOnly;
        public bool WorkcellVisible=>EnvironmentMode!=EnvironmentViewMode.RobotOnly;
        public bool SoloRobotView=>EnvironmentMode==EnvironmentViewMode.RobotOnly;
        ToolKind workcellTool=ToolKind.Gripper;
        readonly Dictionary<RobotVisualProfileId,RobotVisualProfileRig> rigs=new Dictionary<RobotVisualProfileId,RobotVisualProfileRig>();
        readonly Dictionary<RobotVisualProfileId,Transform> legacyPlates=new Dictionary<RobotVisualProfileId,Transform>();
        readonly Dictionary<Renderer,bool> original=new Dictionary<Renderer,bool>();
        readonly HashSet<Renderer> gripper=new HashSet<Renderer>();
        Tool1Runtime tool;SingleArmCommandRouter router;Text status,environmentLabel;G51IndustrialVisual g51Detail;
        RobotVisualQualityBinding visualQuality;
        public HumanoidPreviewControl Preview {get;}=new HumanoidPreviewControl();
        public static void Attach(ControlStudioRuntimeUI studio,Transform canvas)
        {var host=studio.gameObject.AddComponent<RobotVisualProfiles>();host.Initialize(studio.router);host.BuildUI(canvas);host.DualCell=studio.gameObject.AddComponent<DualTableCellPresentation>();host.DualCell.Initialize(host,studio.router,host.tool,canvas);host.GripperCell=studio.gameObject.AddComponent<DualGripperCellPresentation>();host.GripperCell.Initialize(host,studio.router,host.tool,canvas);}
        public void Initialize(SingleArmCommandRouter router)
        {
            this.router=router;tool=router.GetComponent<Tool1Runtime>();Adapter=router.robot.GetComponentInChildren<Demo06RealisticVisualAdapter>(true);
            if(tool==null||Adapter==null||!Adapter.IsConfigured||tool.Socket==null){Error="Visual adapter / ToolSocket unavailable";return;}
            foreach(var renderer in router.robot.transform.root.GetComponentsInChildren<Renderer>(true))original[renderer]=renderer.enabled;
            g51Detail=Adapter.gameObject.AddComponent<G51IndustrialVisual>();
            if(!g51Detail.Initialize(Adapter))Error="G51 visual material mapping unavailable";
            visualQuality=new RobotVisualQualityBinding();
            visualQuality.Apply(Adapter.controller.transform.root);
            var hand=Adapter.m3.visual.Find("M3GripperMountPoint_Visual/GripperVisualMount_Visual");
            if(hand==null){Error="Original G51 gripper visual unavailable";return;}
            foreach(var renderer in hand.GetComponentsInChildren<Renderer>(true))gripper.Add(renderer);
            foreach(var id in new[]{RobotVisualProfileId.MechanicalDualTable,RobotVisualProfileId.MechanicalHumanoid})
            {
                var profile=Resources.Load<RobotVisualProfileDefinition>("VisualProfiles/"+id);
                if(profile==null||profile.id!=id||profile.prefab==null){SelectionMessage="Robot family assets unavailable: "+id;continue;}
                var rig=Instantiate(profile.prefab,Adapter.transform.parent,false);
                rig.transform.localPosition=Adapter.transform.localPosition;rig.transform.localRotation=Adapter.transform.localRotation;rig.transform.localScale=Adapter.transform.localScale;
                rig.Bind(Adapter);rig.gameObject.SetActive(false);rigs[id]=rig;
                visualQuality.Apply(rig.transform,true);
                if(id==RobotVisualProfileId.MechanicalDualTable)
                    legacyPlates[id]=rig.GetComponentsInChildren<Transform>(true).FirstOrDefault(item=>item.name=="G51 source dual table / fixed frame");
            }
            var humanoidPrefab=Resources.Load<HumanoidVisualRig>("VisualProfiles/HumanoidRobot");
            if(humanoidPrefab!=null&&humanoidPrefab.IsConfigured)
            {
                Humanoid=Instantiate(humanoidPrefab,Adapter.transform.parent,false);
                Humanoid.transform.localPosition=Adapter.transform.localPosition;
                Humanoid.transform.rotation=Quaternion.identity;
                Humanoid.transform.localScale=Adapter.transform.localScale;
                Humanoid.gameObject.SetActive(false);
                visualQuality.Apply(Humanoid.transform,true);
            }
            else SelectionMessage="Humanoid Robot prefab unavailable";
            Environment=new RobotWorkEnvironment(tool);if(Environment.Error.Length>0)Error=Environment.Error;
            Simulation=new SimulationEnvironment(Camera.main);
            tool.ToolChanged+=OnToolChanged;Sync();
            Simulation.PlaceUnder(Adapter.controller.transform.root);
        }
        bool Available(RobotVisualProfileId id,ToolKind selectedTool)
        {return id==RobotVisualProfileId.G51||id==RobotVisualProfileId.HumanoidRobot&&Humanoid!=null&&Humanoid.IsConfigured&&Humanoid.RightToolMount!=null||rigs.TryGetValue(id,out var rig)&&rig!=null&&rig.Supports(selectedTool)&&rig.toolMount!=null;}
        public bool Select(RobotVisualProfileId id)
        {
            if(tool==null)return false;if(id==Selected)return true;
            tool.Invalidate("ROBOT FAMILY CHANGED: OPEN REQUIRED");
            if(Environment==null||!Environment.Supports(tool.Gate.Tool)||!Available(id,tool.Gate.Tool))
            {SelectionMessage="Robot family unavailable: previous combination retained / effects OFF";Debug.LogWarning(SelectionMessage);return false;}
            Selected=id;PresentationEpoch++;SelectionMessage="";Sync();
            Simulation?.PlaceUnder(id==RobotVisualProfileId.HumanoidRobot?Humanoid.transform:
                id==RobotVisualProfileId.G51?Adapter.controller.transform.root:Current?.transform);
            var mount=ActiveToolMount();Environment.SetFamily(id,mount==null?tool.Socket.position:mount.position);
            if(id==RobotVisualProfileId.HumanoidRobot)FindFirstObjectByType<ControlStudioOrbitCamera>()?.SetHumanoidPresentationCamera(Humanoid.transform);
            if(tool.Gate.Tool!=ToolKind.Gripper)tool.PlaceSurface();Sync();DualCell?.RefreshSelection();GripperCell?.RefreshSelection();return true;
        }
        public bool SelectTool(ToolKind kind)
        {
            if(tool==null)return false;if(kind==tool.Gate.Tool)return true;
            tool.Invalidate("TOOL PRESENTATION CHANGED: OPEN REQUIRED");
            if(kind!=ToolKind.Gripper&&kind!=ToolKind.Spray&&kind!=ToolKind.Welding&&kind!=ToolKind.Nailing)return tool.Select(kind);
            if(Environment==null||!Environment.Supports(kind)||!Available(Selected,kind))
            {SelectionMessage="Tool presentation unavailable: previous combination retained / effects OFF";Debug.LogWarning(SelectionMessage);return false;}
            if(!tool.Select(kind)){SelectionMessage=tool.SelectionNotice;return false;}
            return true;
        }
        public void SetWorkcellVisible(bool visible)
        {
            SetEnvironmentMode(visible?EnvironmentViewMode.Workcell:EnvironmentViewMode.RobotOnly);
        }
        public void SetEnvironmentMode(EnvironmentViewMode mode)
        {
            if(EnvironmentMode==mode)return;
            if(mode==EnvironmentViewMode.Workcell)workcellTool=tool.Gate.Tool;
            EnvironmentMode=mode;
            if(mode==EnvironmentViewMode.RobotOnly)
                tool?.Invalidate("WORKCELL HIDDEN: effects OFF / OPEN REQUIRED");
            Simulation?.ApplyBackground(mode==EnvironmentViewMode.RobotOnly);
            Sync();
        }
        void OnToolChanged(ToolKind kind)
        {
            if(Environment==null||!Environment.Apply(kind))
            {tool.Invalidate("WORK ENVIRONMENT UNAVAILABLE: OPEN REQUIRED");SelectionMessage="Work environment unavailable / effects OFF";return;}
            // Place once while disarmed; the panel does not follow the nozzle during replay.
            PresentationEpoch++;SelectionMessage="";Sync();
            if(kind==ToolKind.Spray||kind==ToolKind.Welding||kind==ToolKind.Nailing)tool.PlaceSurface();
            DualCell?.RefreshSelection();
            GripperCell?.RefreshSelection();
        }
        public void Sync()
        {
            if(tool==null||Adapter==null)return;
            bool hand=tool.Gate.Tool==ToolKind.Gripper;
            var presentationUI=GetComponent<ControlStudioPresentationUI>();
            bool presentation=presentationUI!=null&&presentationUI.PresentationMode;
            foreach(var entry in original)if(entry.Key!=null)
                entry.Key.enabled=Selected==RobotVisualProfileId.G51&&entry.Value&&(!gripper.Contains(entry.Key)||hand);
            if(g51Detail!=null)g51Detail.SetVisible(Selected==RobotVisualProfileId.G51);
            foreach(var pair in rigs)
            {
                bool active=pair.Key==Selected;var rig=pair.Value;
                rig.gameObject.SetActive(active);
                if(pair.Key==RobotVisualProfileId.MechanicalDualTable&&legacyPlates.TryGetValue(pair.Key,out var legacyPlate)&&legacyPlate!=null)
                    legacyPlate.gameObject.SetActive(active&&WorkcellVisible&&!hand);
                if(active)rig.Sync(hand,tool.Gate.Tool,router.Applied[4]);
            }
            if(Humanoid!=null)
            {
                bool active=Selected==RobotVisualProfileId.HumanoidRobot;
                Humanoid.gameObject.SetActive(active);
                if(active)
                {
                    Humanoid.ApplyArmTarget(HumanoidArmSide.Right,Preview.RightTarget(router.Applied));
                    Humanoid.ApplyArmTarget(HumanoidArmSide.Left,Preview.LeftTarget());
                    Humanoid.SetTool(tool.Gate.Tool);
                    Humanoid.presentationMode=presentation;
                }
            }
            if(Environment?.Root!=null)
            {
                var visibleTool=EnvironmentMode==EnvironmentViewMode.Workcell?workcellTool:tool.Gate.Tool;
                bool showToolEnvironment=WorkcellVisible &&
                    !(Selected==RobotVisualProfileId.MechanicalDualTable&&hand) &&
                    !(Selected==RobotVisualProfileId.HumanoidRobot&&(hand||presentation));
                Environment.ShowOnly(visibleTool,showToolEnvironment);
                Environment.Root.gameObject.SetActive(WorkcellVisible);
            }
            if(tool.Visual!=null){
                var mount=ActiveToolMount();
                if(mount==null){tool.Visual.transform.localPosition=Vector3.zero;tool.Visual.transform.localRotation=Quaternion.identity;}
                else if(Selected==RobotVisualProfileId.HumanoidRobot)tool.Visual.transform.SetPositionAndRotation(mount.position+mount.rotation*tool.Offset,mount.rotation);
                else tool.Visual.transform.SetPositionAndRotation(mount.position+tool.Socket.rotation*tool.Offset,tool.Socket.rotation);
            }
            if(status!=null)
            {
                string family=Selected==RobotVisualProfileId.G51?"G51 ORIGINAL":Selected==RobotVisualProfileId.MechanicalDualTable?"MECHANICAL DUAL TABLE":Selected==RobotVisualProfileId.HumanoidRobot?"HUMANOID ROBOT":"MECHANICAL HUMANOID";
                string shell=Selected==RobotVisualProfileId.HumanoidRobot?"DEMO_01 ARMS / WHITE TORSO":Current==null?"ORIGINAL G51":Current.ShellName(tool.Gate.Tool);
                string cell=Environment?.Current==null?"UNAVAILABLE":Environment.Current.displayName;
                string left=Selected==RobotVisualProfileId.G51?"LEFT N/A":Selected==RobotVisualProfileId.MechanicalDualTable&&tool.Gate.Tool==ToolKind.Gripper?"LEFT FEEDER / DISABLED; LAYOUT CHECK":Selected==RobotVisualProfileId.MechanicalDualTable?"LEFT VIRTUAL HANDLER / SCRIPTED; HANDLER GRIPPER":(Preview.LeftEdited?"LEFT MANUAL PREVIEW / NO UART":"LEFT REST PREVIEW / NO UART");
                var uart=GetComponent<ControlStudioUartOutput>();
                string hardware=uart==null||!uart.Connected?"NOT CONNECTED / TX=0":uart.Mock?"MOCK / REAL TX=0":uart.PortName+" / REAL TX="+uart.HardwareTxCount;
                status.text=$"ROBOT FAMILY: {family}  |  TOOL: {tool.Gate.Tool}  |  SHELL: {shell}\nENVIRONMENT: {EnvironmentMode} / {(WorkcellVisible?cell:"GRID ONLY")}  |  RIGHT ACTIVE / {left}  |  {hardware}";
                if(Error.Length>0)status.text=Error;else if(SelectionMessage.Length>0)status.text=SelectionMessage;
            }
            if(environmentLabel!=null)environmentLabel.text="ENV: "+(EnvironmentMode==EnvironmentViewMode.RobotOnly?"ROBOT ONLY":EnvironmentMode==EnvironmentViewMode.Workcell?"WORKCELL":"AUTO");
        }
        Transform ActiveToolMount()=>Selected==RobotVisualProfileId.HumanoidRobot?Humanoid?.RightToolMount:Current?.toolMount;
        void Update(){if(router!=null&&!router.Paused&&Selected==RobotVisualProfileId.HumanoidRobot&&GetComponent<ControlStudioStartupMenu>()?.Visible!=true)Preview.Advance(Time.deltaTime);if(Adapter!=null)Adapter.SyncVisuals();Sync();}
        void LateUpdate(){Sync();}
        void BuildUI(Transform canvas)
        {
            var font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            var ids=new[]{RobotVisualProfileId.G51,RobotVisualProfileId.MechanicalDualTable,RobotVisualProfileId.HumanoidRobot};
            var labels=new[]{"G51 Original","Dual Table","Humanoid Robot"};
            for(int i=0;i<ids.Length;i++)
            {
                var id=ids[i];var go=new GameObject("Robot family "+id,typeof(RectTransform),typeof(Image),typeof(Button));
                var rt=(RectTransform)go.transform;rt.SetParent(canvas,false);rt.anchorMin=rt.anchorMax=rt.pivot=new Vector2(0,1);rt.anchoredPosition=new Vector2(970+i*140,-58);rt.sizeDelta=new Vector2(134,28);
                go.GetComponent<Image>().color=new Color(.13f,.25f,.30f);go.GetComponent<Button>().onClick.AddListener(()=>Select(id));
                var label=new GameObject("Label",typeof(RectTransform),typeof(Text));label.transform.SetParent(rt,false);var lr=(RectTransform)label.transform;lr.anchorMin=Vector2.zero;lr.anchorMax=Vector2.one;lr.offsetMin=lr.offsetMax=Vector2.zero;
                var txt=label.GetComponent<Text>();txt.font=font;txt.fontSize=13;txt.color=Color.white;txt.alignment=TextAnchor.MiddleCenter;txt.text=labels[i];
            }
            var envText=new GameObject("Environment mode status",typeof(RectTransform),typeof(Text));envText.transform.SetParent(canvas,false);
            var envRect=(RectTransform)envText.transform;envRect.anchorMin=envRect.anchorMax=envRect.pivot=new Vector2(0,1);envRect.anchoredPosition=new Vector2(424,-115);envRect.sizeDelta=new Vector2(139,42);
            environmentLabel=envText.GetComponent<Text>();environmentLabel.font=font;environmentLabel.fontSize=15;environmentLabel.color=new Color(.72f,.90f,.96f);environmentLabel.alignment=TextAnchor.MiddleLeft;
            EnvironmentButton(canvas,font,566,"Robot Only",EnvironmentViewMode.RobotOnly);
            EnvironmentButton(canvas,font,692,"Workcell",EnvironmentViewMode.Workcell);
            EnvironmentButton(canvas,font,818,"Auto",EnvironmentViewMode.AutoToolEnvironment);
            var text=new GameObject("Robot / Tool / Environment status",typeof(RectTransform),typeof(Text));text.transform.SetParent(canvas,false);
            var tr=(RectTransform)text.transform;tr.anchorMin=tr.anchorMax=tr.pivot=new Vector2(0,1);tr.anchoredPosition=new Vector2(25,-218);tr.sizeDelta=new Vector2(925,45);
            status=text.GetComponent<Text>();status.font=font;status.fontSize=13;status.color=new Color(.45f,.9f,1);status.raycastTarget=false;Sync();
        }
        void EnvironmentButton(Transform canvas,Font font,float x,string caption,EnvironmentViewMode mode)
        {
            var button=new GameObject("Environment "+caption,typeof(RectTransform),typeof(Image),typeof(Button));
            var rect=(RectTransform)button.transform;rect.SetParent(canvas,false);rect.anchorMin=rect.anchorMax=rect.pivot=new Vector2(0,1);rect.anchoredPosition=new Vector2(x,-115);rect.sizeDelta=new Vector2(120,42);
            var image=button.GetComponent<Image>();image.color=new Color(.14f,.23f,.30f);
            button.GetComponent<Button>().onClick.AddListener(()=>SetEnvironmentMode(mode));
            var label=new GameObject("Label",typeof(RectTransform),typeof(Text));label.transform.SetParent(rect,false);
            var labelRect=(RectTransform)label.transform;labelRect.anchorMin=Vector2.zero;labelRect.anchorMax=Vector2.one;labelRect.offsetMin=labelRect.offsetMax=Vector2.zero;
            var text=label.GetComponent<Text>();text.font=font;text.fontSize=15;text.color=Color.white;text.alignment=TextAnchor.MiddleCenter;text.text=caption;
        }
        void OnDisable()
        {
            foreach(var entry in original)if(entry.Key!=null)entry.Key.enabled=entry.Value&&(tool==null||!gripper.Contains(entry.Key)||tool.Gate.Tool==ToolKind.Gripper);
            foreach(var rig in rigs.Values)if(rig!=null)rig.gameObject.SetActive(false);
            if(Humanoid!=null)Humanoid.gameObject.SetActive(false);
        }
        void OnDestroy()
        {if(tool!=null)tool.ToolChanged-=OnToolChanged;visualQuality?.Destroy();foreach(var rig in rigs.Values)if(rig!=null)Destroy(rig.gameObject);if(Humanoid!=null)Destroy(Humanoid.gameObject);Environment?.Destroy();Simulation?.Destroy();}
    }
}
