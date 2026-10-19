using System;
using System.Collections.Generic;
using System.Globalization;
using UnityEngine;
namespace HumanMotion.ControlStudio
{
    [DefaultExecutionOrder(1400)]
    public sealed class Tool1Runtime : MonoBehaviour
    {
        public SingleArmCommandRouter Router {get;private set;}
        public ToolRunGate Gate {get;}=new ToolRunGate();
        public Tool1Visual Visual {get;private set;}
        public Transform Socket {get;private set;} public Transform VisualMount {get;private set;}
        public Tool1PaintableSurface Surface {get;private set;}
        public Tool1WeldableSurface WeldSurface {get;private set;}
        public Tool1FastenableSurface FastenSurface {get;private set;}
        public string SelectionNotice {get;private set;}="";
        public Color PaintColor= new Color(.08f,.42f,.94f);public float SprayRadius=.025f,WeldWidth=.004f,FastenerSize=.009f,EffectIntensity=.65f;
        public string Provenance {get;private set;}="SIMULATED_GESTURE / UI intent, not sensor";
        public string Freshness {get;private set;}="SENSOR FRESH: N/A";
        public string Error {get;private set;}="";
        public float ObservedNorm {get;private set;}=float.NaN;
        public long ObservationId {get;private set;}=-1;
        public Vector3 Offset {get;private set;}
        public int ToolEpoch {get;private set;}
        // Visual-only cell interlock. It never changes Agent2, Router, or gesture gate state.
        public bool PresentationEffectsAllowed {get;private set;}=true;
        public event Action<ToolKind> ToolChanged;
        // Observed M4 only; consumers may not submit commands through this event.
        public event Action<HandClosureObservation> GripperObservation;
        readonly Dictionary<Renderer,bool> rendererStates=new Dictionary<Renderer,bool>();
        Vector3 restPosition;Quaternion restRotation;bool focused=true,initialized;float pendingSeconds;long manualId;
        bool wasPaused;int lastEpoch;ToolKind selected;bool permitted;float[] lastManualRequest;
        public double Now=>Router!=null&&Router.Source=="CSV"&&Router.Csv.Timeline!=null?Router.Csv.Timeline.Time:Time.realtimeSinceStartupAsDouble;
        public void Initialize(SingleArmCommandRouter router)
        {
            if(initialized)return;initialized=true;Router=router;
            var adapter=router.robot.GetComponentInChildren<Demo06RealisticVisualAdapter>(true);
            if(adapter==null||!adapter.IsConfigured){Error="Visual adapter unavailable";return;}
            VisualMount=adapter.m3.visual.Find("ToolMount");
            var connector=adapter.m3.visual.Find("M3GripperMountPoint_Visual");
            var gripper=connector==null?null:connector.Find("GripperVisualMount_Visual");
            if(VisualMount==null||connector==null||gripper==null||!VisualMount.IsChildOf(adapter.transform)){
                Error="Verified visual mount not found; tools unavailable";return;
            }
            // Socket under the adapter's visual ToolMount, placed at the fixed M3 flange.
            // No logical joint, jaw, gear, controller or adapter reference is changed.
            Socket=new GameObject("ToolSocket (TOOL-1 virtual only)").transform;Socket.SetParent(VisualMount,false);
            Socket.position=connector.position;Socket.rotation=Quaternion.LookRotation(adapter.m3.visual.up,adapter.m3.visual.forward);
            restPosition=Socket.localPosition;restRotation=Socket.localRotation;
            foreach(var r in gripper.GetComponentsInChildren<Renderer>(true))rendererStates[r]=r.enabled;
            CreateSurface();lastEpoch=Router.Epoch;wasPaused=Router.Paused;lastManualRequest=Router.Requested;
            Router.Changed+=OnRouterChanged;Router.OutputApplied+=OnOutputTick;Router.Csv.RowConsumed+=OnCsvRow;
            Refresh();
        }
        void CreateSurface()
        {
            var material=Resources.Load<Material>("Tool1/Panel");if(material==null){Error="TOOL-1 assets unavailable";return;}
            var panel=GameObject.CreatePrimitive(PrimitiveType.Quad);panel.name="TOOL-1 PaintableSurface (virtual)";
            panel.transform.localScale=Vector3.one*.48f;Surface=panel.AddComponent<Tool1PaintableSurface>();Surface.Initialize(material);
            var weldPanel=GameObject.CreatePrimitive(PrimitiveType.Quad);weldPanel.name="TOOL-1 WeldableSurface (virtual)";
            weldPanel.transform.localScale=Vector3.one*.24f;weldPanel.GetComponent<Renderer>().sharedMaterial=Resources.Load<Material>("Tool1/WeldPanel");
            WeldSurface=weldPanel.AddComponent<Tool1WeldableSurface>();WeldSurface.Initialize(Resources.Load<Material>("Tool1/WeldBead"));
            var fastenerMaterial=Resources.Load<Material>("Tool1/Fastener");if(fastenerMaterial==null){Error="Nailing visual asset unavailable";return;}
            var fastenPanel=GameObject.CreatePrimitive(PrimitiveType.Quad);fastenPanel.name="TOOL-1 FastenableSurface (virtual)";
            fastenPanel.transform.localScale=Vector3.one*.28f;fastenPanel.GetComponent<Renderer>().sharedMaterial=Resources.Load<Material>("Tool1/WeldPanel");
            FastenSurface=fastenPanel.AddComponent<Tool1FastenableSurface>();FastenSurface.Initialize(fastenerMaterial);
            weldPanel.SetActive(false);fastenPanel.SetActive(false);PlaceSurface();
        }
        public bool Select(ToolKind kind)
        {
            if(Socket==null)return false;
            if(kind!=ToolKind.Gripper&&kind!=ToolKind.Spray&&kind!=ToolKind.Welding&&kind!=ToolKind.Nailing){
                Invalidate("UNSUPPORTED TOOL: OPEN REQUIRED");
                if((int)kind==2){permitted=false;Select(ToolKind.Gripper);SelectionNotice="Unsupported legacy Tool ID: Gripper / effects OFF";}
                else SelectionNotice="Unsupported tool ID: previous Tool retained / effects OFF";
                Debug.LogWarning(SelectionNotice);return false;
            }
            SelectionNotice="";
            GameObject prefab=kind==ToolKind.Gripper?null:Resources.Load<GameObject>("Tool1/"+(kind==ToolKind.Welding?"WeldingTorch":kind==ToolKind.Nailing?"NailingHead":kind.ToString()));
            if(kind!=ToolKind.Gripper&&(prefab==null||prefab.GetComponent<Tool1Visual>()==null)){
                SelectionNotice="Tool prefab unavailable: previous Tool retained / effects OFF";Invalidate(SelectionNotice);Debug.LogWarning(SelectionNotice);return false;
            }
            if(Visual!=null){Visual.Stop();Visual.gameObject.SetActive(false);Destroy(Visual.gameObject);Visual=null;}
            foreach(var p in rendererStates)if(p.Key!=null)p.Key.enabled=kind==ToolKind.Gripper?p.Value:false;
            selected=kind;ToolEpoch++;pendingSeconds=0;
            Surface.gameObject.SetActive(kind==ToolKind.Gripper||kind==ToolKind.Spray);WeldSurface.gameObject.SetActive(kind==ToolKind.Welding);FastenSurface.gameObject.SetActive(kind==ToolKind.Nailing);
            if(prefab!=null)Visual=Instantiate(prefab,Socket,false).GetComponent<Tool1Visual>();
            Gate.Configure(permitted,selected,Router.Epoch,ToolEpoch);Gate.Rearm("TOOL CHANGED: OPEN REQUIRED");Refresh();ToolChanged?.Invoke(selected);return true;
        }
        public void SetEnabled(bool enable){permitted=enable;Gate.Configure(permitted,selected,Router.Epoch,ToolEpoch);pendingSeconds=0;if(Visual!=null)Visual.Stop();Refresh();}
        public void Invalidate(string reason){Gate.Rearm(reason);pendingSeconds=0;if(Visual!=null)Visual.Stop();}
        public void SetPresentationEffectsAllowed(bool allowed)
        {PresentationEffectsAllowed=allowed;if(!allowed){pendingSeconds=0;if(Visual!=null)Visual.Stop();}}
        public void NotifyFocus(bool value){focused=value;Refresh();}
        void OnApplicationFocus(bool value){focused=value;if(initialized&&Router!=null)Refresh();}
        void OnApplicationPause(bool pause){if(pause){focused=false;if(initialized&&Router!=null)Refresh();}}
        public bool ChangeOffset(Vector3 delta,bool reset=false)
        {
            if(Socket==null||Gate.Running)return false;
            Invalidate("OFFSET CHANGED: OPEN REQUIRED");Offset=reset?Vector3.zero:Vector3.ClampMagnitude(Offset+delta,.1f);
            Socket.localPosition=restPosition+Offset;Socket.localRotation=restRotation;return true;
        }
        public bool PlaceSurface()
        {
            if(Surface==null||WeldSurface==null||FastenSurface==null||Socket==null||Gate.Running)return false;
            var tip=Visual!=null?Visual.toolTip:Socket;
            var panel=selected==ToolKind.Welding?WeldSurface.transform:selected==ToolKind.Nailing?FastenSurface.transform:Surface.transform;
            panel.position=tip.position+tip.forward*(selected==ToolKind.Welding||selected==ToolKind.Nailing?.025f:.28f);
            // Unity Quad's mesh normal is local -Z; its front must face the nozzle.
            panel.rotation=Quaternion.LookRotation(tip.forward,tip.up);Physics.SyncTransforms();Invalidate("PANEL MOVED: OPEN REQUIRED");return true;
        }
        void OnRouterChanged()
        {
            Refresh();
            // Submit creates a new Requested array. Pause/Resume/other notifications
            // must not turn the retained UI value into a new Manual observation.
            if(Router.Source=="MANUAL"&&Router.Status=="APPROVED"&&!ReferenceEquals(lastManualRequest,Router.Requested)){
                lastManualRequest=Router.Requested;
                var o=new HandClosureObservation{Id=++manualId,SourceEpoch=Router.Epoch,Time=Now,Norm=Router.Requested[4],Valid=true,MaintainedManualIntent=true,Provenance="SIMULATED_GESTURE / maintained UI request; not sensor"};
                Consume(o,"SENSOR FRESH: N/A (new UI request)");
            }
        }
        void OnCsvRow(RecordedHumanRow row)
        {
            Refresh();if(Router.Source!="CSV"||Router.Csv.Mode==CsvInputMode.XyzStoredBodyHoldGripper)return;
            float norm=Router.Csv.Mode==CsvInputMode.RecordedHumanAngles?row.Human[4]:row.Pose.AuxiliaryGripper;
            bool reasonOk=!row.Fields.TryGetValue("gripper_hold_reason",out var text)||text=="0";
            bool hasState=row.Fields.TryGetValue("gripper_state",out var stateText);
            bool stateOk=!hasState||stateText=="0"||stateText=="1";
            var o=new HandClosureObservation{Id=row.FrameId,SourceEpoch=Router.Epoch,Time=row.Time,Norm=norm,
                Valid=row.Flags[0]!=0&&row.Flags[6]==0&&reasonOk&&stateOk,Fresh=row.Flags[2]!=0,
                HasClosureState=hasState,ProvidedClosure=stateText=="1"?ClosureState.Open:ClosureState.Closed,
                InvalidReason=row.Flags[6]!=0||!reasonOk?"GRIPPER HOLD / INVALID":"SOURCE TARGET INVALID",
                Provenance=Router.Csv.Mode==CsvInputMode.RecordedHumanAngles?"RECORDED_GRIPPER / CSV SIMULATION":"RECORDED_GRIPPER AUX / XYZ A / CSV SIMULATION"};
            Consume(o,$"finger_fresh={row.Flags[2]} hold={row.Flags[6]} state={(hasState?stateText:"norm hysteresis")}; wrist separate");
        }
        void Consume(HandClosureObservation o,string freshness)
        {
            Provenance=o.Provenance;Freshness=freshness;ObservedNorm=o.Norm;ObservationId=o.Id;
            Gate.Observe(o,Now);
            if(selected==ToolKind.Gripper&&o.Valid&&Gate.HasObservation&&Gate.Last.Id==o.Id&&Gate.Last.SourceEpoch==o.SourceEpoch&&Gate.Last.Time==o.Time)
                GripperObservation?.Invoke(o);
            if(!Gate.Running){pendingSeconds=0;if(Visual!=null)Visual.Stop();}
        }
        public void Refresh()
        {
            if(Router==null)return;
            if(lastEpoch!=Router.Epoch||wasPaused!=Router.Paused){Invalidate("SOURCE / PAUSE TRANSITION: OPEN REQUIRED");lastEpoch=Router.Epoch;wasPaused=Router.Paused;}
            Gate.Configure(permitted,selected,Router.Epoch,ToolEpoch);
            string block=Error.Length>0?Error:!focused?"FOCUS LOST":!Router.Ready?"NATIVE / OUTPUT UNAVAILABLE":Router.Paused?"PAUSE / STEP / STALL":
                Router.MotionHold!=0||Router.Status.StartsWith("REJECTED")||Router.Status.StartsWith("OUT OF RANGE")||Router.Status.StartsWith("C SAFETY")?"JOINT OUTPUT REJECTED / SAFETY HOLD":"";
            if(Router.Source=="UART")block="NO_GESTURE_SOURCE / UART RX";
            if(Router.Source=="CSV"){
                if(Router.Csv.Mode==CsvInputMode.XyzStoredBodyHoldGripper){block="NO_GESTURE_SOURCE / XYZ B";Provenance=block;Freshness="N/A: Applied M4 HOLD is not an observation";ObservedNorm=float.NaN;}
                else if(Router.Csv.Timeline!=null&&Router.Csv.Timeline.EndOfInput)block="EOF";
            }
            Gate.SetExternalBlock(block);Gate.Advance(Now);
            if(!Gate.Running){pendingSeconds=0;if(Visual!=null)Visual.Stop();}
        }
        void OnOutputTick(){Refresh();if(Gate.Running)pendingSeconds=Mathf.Min(.5f,pendingSeconds+(float)SingleArmCommandRouter.TickSeconds);}
        void LateUpdate(){Refresh();if(Visual!=null){var command=Gate.Command;if(!PresentationEffectsAllowed)command.Run=false;Visual.Advance(command,PresentationEffectsAllowed?pendingSeconds:0,PaintColor,selected==ToolKind.Welding?WeldWidth:selected==ToolKind.Nailing?FastenerSize:SprayRadius,EffectIntensity);}pendingSeconds=0;}
        void OnDisable(){permitted=false;if(Router!=null){Gate.Configure(false,selected,Router.Epoch,ToolEpoch);Invalidate("COMPONENT DISABLED");}foreach(var p in rendererStates)if(p.Key!=null)p.Key.enabled=p.Value;}
        void OnDestroy(){if(Router!=null){Router.Changed-=OnRouterChanged;Router.OutputApplied-=OnOutputTick;if(Router.Csv!=null)Router.Csv.RowConsumed-=OnCsvRow;}if(Socket!=null)Destroy(Socket.gameObject);if(Surface!=null)Destroy(Surface.gameObject);if(WeldSurface!=null)Destroy(WeldSurface.gameObject);if(FastenSurface!=null)Destroy(FastenSurface.gameObject);}
    }
}
