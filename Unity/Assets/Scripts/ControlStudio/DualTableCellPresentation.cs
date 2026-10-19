using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Scripted visual handling only. No command writer, IK, or physical collision claim.
    [DefaultExecutionOrder(1450)]
    public sealed class DualTableCellPresentation : MonoBehaviour
    {
        public enum CellState { WAITING_FOR_PART, HANDLER_PICK, HANDLER_PLACE, HANDLER_CLEAR, PROCESS_READY, PROCESS_RUNNING, PROCESS_COMPLETE, HANDLER_REMOVE }
        public CellState State {get;private set;}=CellState.WAITING_FOR_PART;
        public bool HandlerClear=>State==CellState.PROCESS_READY||State==CellState.PROCESS_RUNNING||State==CellState.PROCESS_COMPLETE;
        public string WorkpieceName {get;private set;}="";
        public Transform Workpiece=>piece;
        public Vector3 ProcessPosition=>processPosition;
        public Vector3 OutfeedPosition=>outfeedPosition;
        RobotVisualProfiles profiles;SingleArmCommandRouter router;Tool1Runtime tool;
        Transform piece,environmentParent,handler,grip,zoneRoot;Vector3 processPosition,infeedPosition,outfeedPosition,clearGripPosition,handlerRest,moveFrom,moveTo;
        Quaternion processRotation;float phase;bool active,carried,seenProcess;Text status;GameObject ui;
        const float PickSeconds=.85f,PlaceSeconds=1.15f,ClearSeconds=.85f,RemoveSeconds=1.25f;

        public void Initialize(RobotVisualProfiles profile,SingleArmCommandRouter command,Tool1Runtime runtime,Transform canvas)
        {profiles=profile;router=command;tool=runtime;BuildUI(canvas);RefreshSelection();}

        public void RefreshSelection()
        {
            if(piece!=null&&carried&&environmentParent!=null)piece.SetParent(environmentParent,true);
            if(handler!=null)handler.localPosition=handlerRest;
            if(zoneRoot!=null)Destroy(zoneRoot.gameObject);
            piece=null;carried=false;seenProcess=false;phase=0;State=CellState.WAITING_FOR_PART;
            var rig=profiles.Current;active=profiles.Selected==RobotVisualProfileId.MechanicalDualTable&&tool.Gate.Tool!=ToolKind.Gripper&&rig!=null&&rig.leftHandlerGrip!=null;
            if(ui!=null)ui.SetActive(active);
            if(!active){tool.SetPresentationEffectsAllowed(true);return;}
            handler=rig.leftArmPreview;grip=rig.leftHandlerGrip;handlerRest=handler.localPosition;
            var kind=tool.Gate.Tool;
            piece=kind==ToolKind.Spray?tool.Surface.transform:kind==ToolKind.Welding?tool.WeldSurface.transform:kind==ToolKind.Nailing?tool.FastenSurface.transform:profiles.Environment.Current.root.Find("Loose block 0");
            WorkpieceName=kind==ToolKind.Spray?"CAR DOOR / PAINTABLE":kind==ToolKind.Welding?"METAL PLATES / WELDABLE":kind==ToolKind.Nailing?"PANEL / FASTENABLE":"SMALL BLOCK / TRAY";
            if(piece==null){active=false;tool.SetPresentationEffectsAllowed(false);return;}
            environmentParent=profiles.Environment.Current.root;
            piece.SetParent(environmentParent,true);
            processPosition=piece.position;processRotation=piece.rotation;
            if(kind==ToolKind.Gripper){var bench=environmentParent.Find("Compact workbench top");if(bench!=null)processPosition=bench.position+Vector3.up*.035f;}
            var lateral=rig.transform.right;
            infeedPosition=processPosition+lateral*.42f;
            outfeedPosition=processPosition-lateral*.42f;
            clearGripPosition=infeedPosition+lateral*.12f+Vector3.up*.05f;
            zoneRoot=new GameObject("Dual cell zones / EnvironmentRoot only").transform;zoneRoot.SetParent(environmentParent,false);
            Zone("INFEED",infeedPosition,new Color(.14f,.48f,.77f));
            Zone("PROCESS",processPosition,new Color(.84f,.60f,.13f));
            Zone("OUTFEED",outfeedPosition,new Color(.20f,.68f,.43f));
            piece.position=infeedPosition;
            tool.Invalidate("DUAL CELL RESET: OPEN REQUIRED");
            tool.SetPresentationEffectsAllowed(false);
            Physics.SyncTransforms();
        }
        void Zone(string title,Vector3 position,Color color)
        {
            var pad=GameObject.CreatePrimitive(PrimitiveType.Cube);pad.name=title+" zone / visual only";pad.transform.SetParent(zoneRoot,true);pad.transform.position=position-Vector3.up*.19f;pad.transform.localScale=new Vector3(.20f,.008f,.16f);
            var collider=pad.GetComponent<Collider>();if(collider!=null)Destroy(collider);
            var renderer=pad.GetComponent<Renderer>();renderer.sharedMaterial=Resources.Load<Material>("VisualProfiles/SignalTeal");var tint=new MaterialPropertyBlock();tint.SetColor("_BaseColor",color);tint.SetColor("_Color",color);renderer.SetPropertyBlock(tint);
            var label=new GameObject(title+" label",typeof(TextMesh));label.transform.SetParent(zoneRoot,true);label.transform.position=position-Vector3.up*.175f;label.transform.rotation=Quaternion.Euler(90,0,0);var text=label.GetComponent<TextMesh>();text.text=title;text.fontSize=40;text.characterSize=.014f;text.anchor=TextAnchor.MiddleCenter;text.color=Color.white;
        }

        public void RunCellDemo()
        {
            if(!active||State!=CellState.WAITING_FOR_PART||piece==null)return;
            if(Vector3.Distance(piece.position,infeedPosition)>.002f)ResetCell();
            State=CellState.HANDLER_PICK;phase=0;moveFrom=handler.position;
            moveTo=handler.position+(infeedPosition-grip.position)+grip.forward*.025f;
            tool.SetPresentationEffectsAllowed(false);
        }
        public void ResetCell()
        {
            if(active&&piece!=null){tool.Invalidate("DUAL CELL RESET: OPEN REQUIRED");if(carried&&environmentParent!=null)piece.SetParent(environmentParent,true);piece.SetPositionAndRotation(processPosition,processRotation);carried=false;}
            RefreshSelection();
        }
        public void NextPart()
        {
            if(!active)return;
            if(State==CellState.PROCESS_COMPLETE){BeginRemove();return;}
            if(State==CellState.WAITING_FOR_PART){RunCellDemo();return;}
            if(State==CellState.PROCESS_READY&&tool.Gate.Tool==ToolKind.Gripper){State=CellState.PROCESS_COMPLETE;phase=0;}
        }
        void BeginRemove()
        {
            if(State!=CellState.PROCESS_COMPLETE||piece==null)return;
            tool.SetPresentationEffectsAllowed(false);
            State=CellState.HANDLER_REMOVE;phase=0;moveFrom=handler.position;
            moveTo=handler.position+(processPosition-grip.position)+grip.forward*.025f;
        }
        public void AdvanceVirtual(float seconds)
        {
            if(!active||piece==null||router.Paused||seconds<=0)return;
            // Only a placed workpiece and a cleared handler may receive the existing effect.
            tool.SetPresentationEffectsAllowed((State==CellState.PROCESS_READY||State==CellState.PROCESS_RUNNING)&&piece.parent==environmentParent&&Vector3.Distance(piece.position,processPosition)<.002f);
            switch(State)
            {
                case CellState.HANDLER_PICK:
                    phase+=seconds/PickSeconds;handler.position=Vector3.Lerp(moveFrom,moveTo,Mathf.SmoothStep(0,1,Mathf.Clamp01(phase)));
                    if(phase>=1){piece.SetParent(grip,true);carried=true;State=CellState.HANDLER_PLACE;phase=0;moveFrom=handler.position;moveTo=handler.position+(processPosition-piece.position);}
                    break;
                case CellState.HANDLER_PLACE:
                    phase+=seconds/PlaceSeconds;float place=Mathf.SmoothStep(0,1,Mathf.Clamp01(phase));handler.position=Vector3.Lerp(moveFrom,moveTo,place);piece.rotation=Quaternion.Slerp(piece.rotation,processRotation,place);
                    if(phase>=1){piece.SetParent(environmentParent,true);piece.SetPositionAndRotation(processPosition,processRotation);carried=false;State=CellState.HANDLER_CLEAR;phase=0;moveFrom=handler.position;moveTo=handler.position+(clearGripPosition-grip.position);}
                    break;
                case CellState.HANDLER_CLEAR:
                    phase+=seconds/ClearSeconds;handler.position=Vector3.Lerp(moveFrom,moveTo,Mathf.SmoothStep(0,1,Mathf.Clamp01(phase)));
                    if(phase>=1&&Vector3.Distance(grip.position,processPosition)>.30f){State=CellState.PROCESS_READY;tool.Invalidate("HANDLER CLEAR: NEW OPEN REQUIRED");tool.SetPresentationEffectsAllowed(true);}
                    break;
                case CellState.PROCESS_READY:
                    if(tool.Gate.Tool!=ToolKind.Gripper&&tool.Gate.Running){State=CellState.PROCESS_RUNNING;seenProcess=true;}
                    break;
                case CellState.PROCESS_RUNNING:
                    if(!tool.Gate.Running){State=CellState.PROCESS_COMPLETE;phase=0;tool.SetPresentationEffectsAllowed(false);}
                    break;
                case CellState.PROCESS_COMPLETE:
                    phase+=seconds;if(phase>=.3f)BeginRemove();
                    break;
                case CellState.HANDLER_REMOVE:
                    phase+=seconds/(carried?RemoveSeconds:PickSeconds);
                    handler.position=Vector3.Lerp(moveFrom,moveTo,Mathf.SmoothStep(0,1,Mathf.Clamp01(phase)));
                    if(phase>=1&&!carried){piece.SetParent(grip,true);carried=true;moveFrom=handler.position;moveTo=handler.position+(outfeedPosition-piece.position);phase=0;}
                    else if(phase>=1&&carried){piece.SetParent(environmentParent,true);piece.position=outfeedPosition;carried=false;State=CellState.WAITING_FOR_PART;tool.SetPresentationEffectsAllowed(false);}
                    break;
            }
            Physics.SyncTransforms();
        }
        void Update(){AdvanceVirtual(Time.deltaTime);if(status!=null&&active)status.text=$"RIGHT ARM: PROCESS / ACTIVE  |  RIGHT: HUMAN MOTION ACTIVE\nLEFT ARM: HANDLER / SCRIPTED  |  LEFT: VIRTUAL HANDLER / SCRIPTED  |  LEFT TOOL: HANDLER GRIPPER\nPROCESS STATE: {State}  |  WORKPIECE: {WorkpieceName}\nINFEED → PROCESS → OUTFEED  |  EFFECT INTERLOCK: {(tool.PresentationEffectsAllowed?"CLEAR":"BLOCKED")} (virtual only)"+(tool.Gate.Tool==ToolKind.Gripper&&State==CellState.PROCESS_READY?"  |  Next Part after right-arm work":"");}
        void OnDisable(){if(tool!=null)tool.SetPresentationEffectsAllowed(false);}

        void BuildUI(Transform canvas)
        {
            var font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            ui=new GameObject("Dual Table Cell UI",typeof(RectTransform),typeof(Image));var rect=(RectTransform)ui.transform;rect.SetParent(canvas,false);rect.anchorMin=rect.anchorMax=rect.pivot=new Vector2(0,1);rect.anchoredPosition=new Vector2(25,-274);rect.sizeDelta=new Vector2(925,138);ui.GetComponent<Image>().color=new Color(.035f,.065f,.085f,.92f);
            var label=new GameObject("Cell state",typeof(RectTransform),typeof(Text));label.transform.SetParent(rect,false);var t=(RectTransform)label.transform;t.anchorMin=t.anchorMax=t.pivot=new Vector2(0,1);t.anchoredPosition=new Vector2(12,-5);t.sizeDelta=new Vector2(895,86);status=label.GetComponent<Text>();status.font=font;status.fontSize=13;status.color=new Color(.72f,.96f,1);status.raycastTarget=false;
            AddButton(rect,font,12,"Run Cell Demo",RunCellDemo);AddButton(rect,font,195,"Reset Cell",ResetCell);AddButton(rect,font,378,"Next Part",NextPart);
        }
        static void AddButton(Transform parent,Font font,float x,string title,UnityEngine.Events.UnityAction action)
        {
            var go=new GameObject(title,typeof(RectTransform),typeof(Image),typeof(Button));var r=(RectTransform)go.transform;r.SetParent(parent,false);r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-98);r.sizeDelta=new Vector2(176,30);go.GetComponent<Image>().color=new Color(.15f,.29f,.35f);go.GetComponent<Button>().onClick.AddListener(action);
            var label=new GameObject("Label",typeof(RectTransform),typeof(Text));label.transform.SetParent(r,false);var lr=(RectTransform)label.transform;lr.anchorMin=Vector2.zero;lr.anchorMax=Vector2.one;lr.offsetMin=lr.offsetMax=Vector2.zero;var text=label.GetComponent<Text>();text.font=font;text.fontSize=13;text.alignment=TextAnchor.MiddleCenter;text.color=Color.white;text.text=title;
        }
    }
}
