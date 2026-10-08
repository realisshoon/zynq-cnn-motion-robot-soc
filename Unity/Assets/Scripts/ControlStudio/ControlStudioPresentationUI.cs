using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Visibility and camera presentation only; no command or output ownership.
    [DefaultExecutionOrder(3000)]
    public sealed class ControlStudioPresentationUI : MonoBehaviour
    {
        enum Section { Source, Servo, Tool, Status, Uart }
        static readonly string[][] SectionNames={
            new[]{"CSV lower backdrop","CSV recorded human controls"},
            new[]{"Manual panel"},
            new[]{"TOOL-1 sidebar"},
            new[]{"Tool status strip","Robot / Tool / Environment status","Dual Table Cell UI","Gripper Pick Place UI","Selected joint","Camera help","Measurement note"},
            new[]{"UART / Hardware panel"}
        };
        readonly Dictionary<Section,List<GameObject>> groups=new Dictionary<Section,List<GameObject>>();
        readonly Dictionary<GameObject,bool> previousActive=new Dictionary<GameObject,bool>();
        readonly Dictionary<Section,Text> headers=new Dictionary<Section,Text>();
        readonly bool[] expanded={false,false,false,false,false};
        ControlStudioRuntimeUI studio;ControlStudioOrbitCamera orbit;Tool1RuntimeUI toolUi;PresentationSprayDemo demo;G51PickPresentationDemo pickDemo;
        GameObject canvasRoot,presentationStatusRoot;Text presentationStatus,controlsLabel;Transform design;bool presentation,previousCursor,controlsHidden;
        public ControlStudioBackgroundView Background {get;private set;}
        public bool ControlsVisible=>!controlsHidden;
        EnvironmentViewMode previousEnvironmentMode;
        public bool PresentationMode=>presentation;
        public bool SourceExpanded=>ControlsVisible&&studio!=null&&studio.router.Source=="CSV";
        public bool ServoExpanded=>ControlsVisible&&studio!=null&&studio.router.Source=="MANUAL"&&studio.ServoDetailsExpanded;
        public bool ToolExpanded=>expanded[(int)Section.Tool];
        public bool DebugExpanded=>expanded[(int)Section.Status];
        public bool UartExpanded=>ControlsVisible&&studio!=null&&studio.router.Source=="UART";
        public bool WideCamera=>controlsHidden||(studio!=null&&studio.router.Source=="MANUAL"&&!studio.ServoDetailsExpanded);
        public float CameraDesignWidth=>WideCamera?1600:studio!=null&&studio.router.Source=="MANUAL"?1140:960;
        public void SetControlsVisible(bool visible){controlsHidden=!visible;ApplyInputLayout();}
        public void ToggleToolControls(){expanded[(int)Section.Tool]=!expanded[(int)Section.Tool];toolUi?.ShowToolPanel(ToolExpanded);ApplyInputLayout();}
        public void OpenSourceControls()=>ApplyInputLayout();
        public void OpenServoControls()=>ApplyInputLayout();
        public void OpenUartControls()=>ApplyInputLayout();
        public GameObject SectionGroup(int index)
        {
            if(index<0||index>=4)return null;
            EnsureGroups();
            if(!groups.TryGetValue((Section)index,out var items))return null;
            return items.Count>0?items[0]:null;
        }
        public static void Attach(ControlStudioRuntimeUI owner,Transform canvas,GameObject root)
        {
            var ui=owner.gameObject.AddComponent<ControlStudioPresentationUI>();
            ui.Initialize(owner,canvas,root);
        }
        void Initialize(ControlStudioRuntimeUI owner,Transform canvas,GameObject root)
        {
            studio=owner;orbit=owner.orbit;toolUi=GetComponent<Tool1RuntimeUI>();canvasRoot=root;design=canvas;
            demo=gameObject.AddComponent<PresentationSprayDemo>();demo.Initialize(owner,this);
            pickDemo=gameObject.AddComponent<G51PickPresentationDemo>();pickDemo.Initialize(owner,this);
            BindGroups();
            Background=gameObject.AddComponent<ControlStudioBackgroundView>();Background.Initialize(GetComponent<RobotVisualProfiles>());
            var font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            var bar=new GameObject("Presentation control strip",typeof(RectTransform),typeof(Image));
            var rect=(RectTransform)bar.transform;rect.SetParent(design,false);rect.anchorMin=rect.anchorMax=rect.pivot=new Vector2(0,1);
            rect.anchoredPosition=new Vector2(0,-812);rect.sizeDelta=new Vector2(1600,37);
            bar.GetComponent<Image>().color=new Color(.035f,.055f,.075f,.98f);
            MakeButton(rect,font,10,180,"SIMULATION GRID",()=>Background.Select(ControlStudioBackgroundView.BackgroundKind.SimulationGrid));
            MakeButton(rect,font,200,170,"FACTORY",()=>Background.Select(ControlStudioBackgroundView.BackgroundKind.Factory));
            var factoryButton=rect.Find("FACTORY").GetComponent<Button>();factoryButton.interactable=Background.FactoryAvailable;
            var reason=new GameObject("Background status",typeof(RectTransform),typeof(Text));var rr=(RectTransform)reason.transform;rr.SetParent(rect,false);rr.anchorMin=rr.anchorMax=rr.pivot=new Vector2(0,1);rr.anchoredPosition=new Vector2(390,-3);rr.sizeDelta=new Vector2(1150,31);
            var reasonText=reason.GetComponent<Text>();reasonText.font=font;reasonText.fontSize=14;reasonText.color=new Color(.65f,.74f,.80f);reasonText.alignment=TextAnchor.MiddleLeft;reasonText.text="BACKGROUND: SIMULATION GRID  |  "+Background.FactoryStatus;
            var viewButtons=new GameObject("View controls",typeof(RectTransform));var vr=(RectTransform)viewButtons.transform;vr.SetParent(design,false);vr.anchorMin=vr.anchorMax=vr.pivot=new Vector2(0,1);vr.anchoredPosition=new Vector2(420,-115);vr.sizeDelta=new Vector2(530,42);
            MakeButton(vr,font,0,205,"HIDE CONTROLS",()=>SetControlsVisible(!ControlsVisible));controlsLabel=vr.Find("HIDE CONTROLS/Label").GetComponent<Text>();
            MakeButton(vr,font,220,180,"PRESENTATION",()=>SetPresentationMode(true));
            BuildPresentationStatus(font);
            // Bind the actual factory-created selection objects once; backend selection APIs stay intact.
            foreach(var name in new[]{"Robot family G51","Robot family MechanicalDualTable","Robot family HumanoidRobot","TOOLS / JOINTS","Environment Robot Only","Environment Workcell","Environment Auto","Environment mode status"})
            {var item=design.GetComponentsInChildren<RectTransform>(true).FirstOrDefault(t=>t.name==name);if(item!=null)item.gameObject.SetActive(false);}
            var toolPanel=design.Find("TOOL-1 sidebar");
            if(toolPanel!=null){
                foreach(var name in new[]{"Gripper","Spray","Welding","Nailing"}){var item=toolPanel.Find(name);if(item!=null)item.gameObject.SetActive(false);}
                var toolRect=(RectTransform)toolPanel;toolRect.anchoredPosition=new Vector2(25,-168);toolRect.localScale=Vector3.one*.78f;
            }
            ApplyInputLayout();
            foreach(var name in new[]{"Subtitle","Policy status","Footer"}){var item=design.Find(name);if(item!=null)item.gameObject.SetActive(false);}
        }
        public void ApplyInputLayout()
        {
            if(studio==null||design==null)return;
            EnsureGroups();
            foreach(var item in groups[Section.Source])item.SetActive(SourceExpanded);
            foreach(var item in groups[Section.Servo])item.SetActive(ServoExpanded);
            var uart=GetComponent<ControlStudioUartPanel>();if(uart!=null&&uart.Visible!=UartExpanded)uart.ShowPanel(UartExpanded);
            foreach(var item in groups[Section.Uart])item.SetActive(UartExpanded);
            foreach(var item in groups[Section.Tool])item.SetActive(false);
            foreach(var item in groups[Section.Status])item.SetActive(false);
            if(controlsLabel!=null)controlsLabel.text=ControlsVisible?"HIDE CONTROLS":"SHOW CONTROLS";
        }
        void BuildPresentationStatus(Font font)
        {
            presentationStatusRoot=new GameObject("Presentation status canvas",typeof(Canvas),typeof(CanvasScaler));
            presentationStatusRoot.transform.SetParent(transform,false);
            var canvas=presentationStatusRoot.GetComponent<Canvas>();canvas.renderMode=RenderMode.ScreenSpaceOverlay;canvas.sortingOrder=20;
            var scaler=presentationStatusRoot.GetComponent<CanvasScaler>();scaler.uiScaleMode=CanvasScaler.ScaleMode.ScaleWithScreenSize;scaler.referenceResolution=new Vector2(1600,900);
            var bg=new GameObject("Compact status",typeof(RectTransform),typeof(Image));var rect=(RectTransform)bg.transform;rect.SetParent(presentationStatusRoot.transform,false);
            rect.anchorMin=new Vector2(0,0);rect.anchorMax=new Vector2(1,0);rect.pivot=new Vector2(.5f,0);rect.anchoredPosition=Vector2.zero;rect.sizeDelta=new Vector2(0,36);
            bg.GetComponent<Image>().color=new Color(.025f,.038f,.055f,.82f);
            var label=new GameObject("Status text",typeof(RectTransform),typeof(Text));var lr=(RectTransform)label.transform;lr.SetParent(rect,false);lr.anchorMin=Vector2.zero;lr.anchorMax=Vector2.one;lr.offsetMin=new Vector2(16,0);lr.offsetMax=new Vector2(-16,0);
            presentationStatus=label.GetComponent<Text>();presentationStatus.font=font;presentationStatus.fontSize=16;presentationStatus.color=Color.white;presentationStatus.alignment=TextAnchor.MiddleLeft;
            presentationStatusRoot.SetActive(false);
        }
        void BindGroups()
        {
            groups.Clear();
            foreach(Section section in System.Enum.GetValues(typeof(Section))){
                var list=new List<GameObject>();
                foreach(var name in SectionNames[(int)section]){
                    var item=design.GetComponentsInChildren<RectTransform>(true).FirstOrDefault(t=>t.name==name);
                    if(item==null)continue;
                    list.Add(item.gameObject);
                }
                groups[section]=list;
            }
        }
        void EnsureGroups()
        {
            if(groups.Count==5&&groups.All(pair=>pair.Value.Count>0&&pair.Value[0]!=null))return;
            design=studio.RuntimeCanvas.GetChild(0);
            BindGroups();
        }
        void MakeButton(Transform parent,Font font,float x,float width,string caption,UnityEngine.Events.UnityAction action,Section? section=null)
        {
            var go=new GameObject(caption,typeof(RectTransform),typeof(Image),typeof(Button));var r=(RectTransform)go.transform;
            r.SetParent(parent,false);r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-3);r.sizeDelta=new Vector2(width,31);
            var image=go.GetComponent<Image>();image.color=new Color(.14f,.23f,.30f);var button=go.GetComponent<Button>();button.targetGraphic=image;button.onClick.AddListener(action);
            var label=new GameObject("Label",typeof(RectTransform),typeof(Text));var lr=(RectTransform)label.transform;
            lr.SetParent(r,false);lr.anchorMin=Vector2.zero;lr.anchorMax=Vector2.one;lr.offsetMin=lr.offsetMax=Vector2.zero;
            var text=label.GetComponent<Text>();text.font=font;text.fontSize=14;text.color=Color.white;text.alignment=TextAnchor.MiddleCenter;text.text=caption;
            if(section.HasValue)headers[section.Value]=text;
        }
        void Toggle(Section section)
        {
            int i=(int)section;expanded[i]=!expanded[i];
            if(expanded[i]&&(section==Section.Servo||section==Section.Tool||section==Section.Uart))
            {
                foreach(var other in new[]{Section.Servo,Section.Tool,Section.Uart})
                    if(other!=section){expanded[(int)other]=false;Apply(other);}
                if(section!=Section.Tool)toolUi?.ShowToolPanel(false);
                if(section!=Section.Uart)GetComponent<ControlStudioUartPanel>()?.ShowPanel(false);
            }
            if(section==Section.Tool)toolUi?.ShowToolPanel(expanded[i]);
            if(section==Section.Uart)GetComponent<ControlStudioUartPanel>()?.ShowPanel(expanded[i]);
            Apply(section);
        }
        void ToggleRobotOnly()
        {
            var visuals=GetComponent<RobotVisualProfiles>();
            if(visuals==null)return;
            visuals.SetEnvironmentMode(visuals.SoloRobotView?EnvironmentViewMode.Workcell:EnvironmentViewMode.RobotOnly);
            orbit.SetPresentationCamera();
        }
        void ApplyAll(){foreach(Section section in System.Enum.GetValues(typeof(Section)))Apply(section);}
        void Apply(Section section)
        {
            EnsureGroups();
            bool show=expanded[(int)section];
            foreach(var item in groups[section]){
                if(item==null)continue;
                if(!show){if(!previousActive.ContainsKey(item))previousActive[item]=item.activeSelf;item.SetActive(false);}
                else if(previousActive.TryGetValue(item,out var prior)){item.SetActive(prior);previousActive.Remove(item);}
            }
            if(headers.TryGetValue(section,out var label))label.text=(show?"▼  ":"▲  ")+new[]{"SOURCE / CSV","SERVO M0-M4","TOOL / EFFECTS","CELL / DEBUG","UART / HARDWARE"}[(int)section];
        }
        public void SetPresentationMode(bool enabled)
        {
            if(presentation==enabled)return;
            presentation=enabled;
            var visuals=GetComponent<RobotVisualProfiles>();
            if(enabled){previousCursor=Cursor.visible;Cursor.visible=false;previousEnvironmentMode=visuals==null?EnvironmentViewMode.RobotOnly:visuals.EnvironmentMode;orbit.SetPresentationMode(true);canvasRoot.SetActive(false);presentationStatusRoot.SetActive(false);}
            else{canvasRoot.SetActive(true);presentationStatusRoot.SetActive(false);orbit.SetPresentationMode(false);Cursor.visible=previousCursor;ApplyInputLayout();}
        }
        void OnGUI()
        {
            if(!presentation||orbit==null||Event.current.type!=EventType.Repaint)return;
            // The unchanged camera viewport does not redraw its surrounding gutters.
            // Clear stale overlay pixels there without enlarging or reframing the camera.
            var cameraView=orbit.GetComponent<Camera>();var pixels=cameraView.pixelRect;
            var old=GUI.color;GUI.color=cameraView.backgroundColor;
            float top=Screen.height-pixels.yMax,bottom=Screen.height-pixels.yMin;
            GUI.DrawTexture(new Rect(0,0,Screen.width,top),Texture2D.whiteTexture);
            GUI.DrawTexture(new Rect(0,bottom,Screen.width,Screen.height-bottom),Texture2D.whiteTexture);
            GUI.DrawTexture(new Rect(0,top,pixels.xMin,pixels.height),Texture2D.whiteTexture);
            GUI.DrawTexture(new Rect(pixels.xMax,top,Screen.width-pixels.xMax,pixels.height),Texture2D.whiteTexture);
            GUI.color=old;
        }
        void Update()
        {
            if(GetComponent<ControlStudioStartupMenu>()?.Visible==true)return;
            var keyboard=Keyboard.current;
            if(keyboard==null)return;
            if(keyboard.f1Key.wasPressedThisFrame)SetPresentationMode(!presentation);
            else if(presentation&&keyboard.escapeKey.wasPressedThisFrame)SetPresentationMode(false);
        }
        void LateUpdate()
        {
            if(presentation&&presentationStatus!=null)
            {
                var visuals=GetComponent<RobotVisualProfiles>();var uart=GetComponent<ControlStudioUartOutput>();
                var rx=GetComponent<UartPose3DSource>();
                presentationStatus.text=$"{(visuals==null?"G51":visuals.Selected.ToString())}   |   {studio.router.Source}   |   {(studio.router.Paused?"PAUSED":"RUNNING")}   |   RX {(rx!=null&&rx.Connected?(rx.Mock?"MOCK":rx.PortName):"OFF")} / TX {(uart!=null&&uart.Connected?(uart.TxEnabled?"ON":"OFF"):"OFF")} / REAL TX {uart?.HardwareTxCount??0}   |   F1: Controls";
            }
            ApplyInputLayout();
        }
        void OnDisable(){if(presentation){presentation=false;Cursor.visible=previousCursor;if(orbit!=null)orbit.SetPresentationMode(false);if(canvasRoot!=null)canvasRoot.SetActive(true);if(presentationStatusRoot!=null)presentationStatusRoot.SetActive(false);GetComponent<RobotVisualProfiles>()?.SetEnvironmentMode(previousEnvironmentMode);}if(demo!=null)demo.StopDemo();if(pickDemo!=null)pickDemo.StopDemo();}
    }
}
