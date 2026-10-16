using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Layout only. Every command widget and listener is the original instance.
    [DefaultExecutionOrder(6500)]
    public sealed class ControlStudioDockLayout : MonoBehaviour
    {
        public enum Workspace { Robot, Ports, Settings, Records, Capture, Source, Tool, Status, Control }
        public Workspace Selected {get;private set;}
        public bool DetailOpen {get;private set;}
        bool BoardInput=>studio.router.Source=="BOARD"||studio.router.Source=="UART";
        bool UartDetail=>Selected==Workspace.Ports||Selected==Workspace.Control||Selected==Workspace.Settings||Selected==Workspace.Records||Selected==Workspace.Capture;
        public bool WantsUart=>BoardInput||DetailOpen&&UartDetail;
        public RectTransform Dock {get;private set;}
        public ScrollRect InspectorScroll {get;private set;}
        public ScrollRect DetailScroll {get;private set;}
        public GameObject OverlayRoot {get;private set;}
        public GameObject DetailRoot {get;private set;}
        public bool Ready=>studio!=null;
        public float ViewportDesignWidth {get;private set;}=1250;
        public float CameraSectionHeight {get;private set;}
        ControlStudioRuntimeUI studio;ControlStudioPresentationUI view;ControlStudioStartupMenu menu;RobotControlMockPanel preview;
        ControlStudioUartPanel uart;ControlStudioVideoCapture capture;Transform design,overlayDesign,detailDesign;
        RectTransform content,detailContent,control,uartRoot,manual,csv,tool,statusRoot,core,advanced;
        GameObject expandButton,viewportChrome,sourceHint,legacyHidden;Text cameraState,systemState,viewportLabel,cameraHeading,robotHeading,detailTitle;
        ControlStudioCsvPlaybackPanel csvPlayback;
        Button detailsButton;Image[] gutters;Font font;readonly Dictionary<Workspace,Button> detailTabs=new Dictionary<Workspace,Button>();
        static readonly Color Panel=new Color(.115f,.125f,.14f),ButtonColor=new Color(.18f,.20f,.23f),Blue=new Color(.31f,.48f,.66f);
        public static void Attach(ControlStudioRuntimeUI owner,Transform parent){var dock=owner.gameObject.AddComponent<ControlStudioDockLayout>();dock.Initialize(owner,parent);}
        public static RectTransform Place(Transform t,Transform parent,float x,float y,float w,float h)
        {var r=(RectTransform)t;r.SetParent(parent,false);r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);r.localScale=Vector3.one;return r;}
        RectTransform Rect(string name,Transform parent,float x,float y,float w,float h)=>Place(new GameObject(name,typeof(RectTransform)).transform,parent,x,y,w,h);
        Image Box(string name,Transform parent,float x,float y,float w,float h,Color color){var r=Rect(name,parent,x,y,w,h);var image=r.gameObject.AddComponent<Image>();image.color=color;return image;}
        Text Label(string name,Transform parent,float x,float y,float w,float h,string value,int size=14)
        {var t=Rect(name,parent,x,y,w,h).gameObject.AddComponent<Text>();t.font=font;t.fontSize=size;t.color=new Color(.78f,.82f,.87f);t.alignment=TextAnchor.MiddleLeft;t.text=value;t.raycastTarget=false;return t;}
        Button Button(string name,Transform parent,float x,float y,float w,float h,string value,UnityEngine.Events.UnityAction action)
        {var image=Box(name,parent,x,y,w,h,ButtonColor);var b=image.gameObject.AddComponent<Button>();b.targetGraphic=image;b.onClick.AddListener(action);Label(name+" label",image.transform,4,0,w-8,h,value).alignment=TextAnchor.MiddleCenter;return b;}
        Transform Find(string name)=>design.GetComponentsInChildren<Transform>(true).Concat(DetailRoot==null?Array.Empty<Transform>():DetailRoot.GetComponentsInChildren<Transform>(true)).First(t=>t.name==name);
        Transform Child(Transform parent,string name)=>parent.GetComponentsInChildren<Transform>(true).First(t=>t.name==name);
        ScrollRect Scroll(string name,Transform parent,out RectTransform body)
        {
            var root=Rect(name,parent,0,0,400,400);var scroll=root.gameObject.AddComponent<ScrollRect>();
            var viewport=Box(name+" viewport",root,0,0,388,400,new Color(.095f,.105f,.12f)).rectTransform;viewport.gameObject.AddComponent<RectMask2D>();
            body=Rect(name+" content",viewport,0,0,388,700);scroll.viewport=viewport;scroll.content=body;scroll.horizontal=false;scroll.vertical=true;scroll.movementType=ScrollRect.MovementType.Clamped;scroll.scrollSensitivity=36;
            var bg=Box(name+" scrollbar",root,390,0,8,400,new Color(.08f,.09f,.10f));var bar=bg.gameObject.AddComponent<Scrollbar>();var handle=Box("Handle",bg.transform,0,0,8,400,new Color(.30f,.33f,.37f));handle.rectTransform.offsetMin=handle.rectTransform.offsetMax=Vector2.zero;bar.handleRect=handle.rectTransform;bar.targetGraphic=handle;bar.direction=UnityEngine.UI.Scrollbar.Direction.BottomToTop;scroll.verticalScrollbar=bar;return scroll;
        }
        void ScrollSize(ScrollRect scroll,float x,float y,float width,float height)
        {Place(scroll.transform,scroll.transform.parent,x,y,width,height);Place(scroll.viewport,scroll.transform,0,0,width-12,height);Place(scroll.verticalScrollbar.transform,scroll.transform,width-8,0,8,height);}
        GameObject CanvasRoot(string name,int order,out Transform area)
        {
            var root=new GameObject(name,typeof(Canvas),typeof(CanvasScaler),typeof(GraphicRaycaster));root.transform.SetParent(transform,false);var canvas=root.GetComponent<Canvas>();canvas.renderMode=RenderMode.ScreenSpaceOverlay;canvas.sortingOrder=order;
            var scaler=root.GetComponent<CanvasScaler>();scaler.uiScaleMode=CanvasScaler.ScaleMode.ScaleWithScreenSize;scaler.referenceResolution=new Vector2(1600,900);scaler.screenMatchMode=CanvasScaler.ScreenMatchMode.Expand;
            var a=Rect(name+" design area",root.transform,0,0,1600,900);a.anchorMin=a.anchorMax=a.pivot=new Vector2(.5f,.5f);a.anchoredPosition=Vector2.zero;area=a;return root;
        }
        void Initialize(ControlStudioRuntimeUI owner,Transform parent)
        {
            studio=owner;design=parent;view=owner.GetComponent<ControlStudioPresentationUI>();menu=owner.GetComponent<ControlStudioStartupMenu>();preview=owner.GetComponent<RobotControlMockPanel>();uart=owner.GetComponent<ControlStudioUartPanel>();capture=owner.GetComponent<ControlStudioVideoCapture>();font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            viewportChrome=Box("Viewport caption bar",design,0,48,1250,48,new Color(.09f,.10f,.115f)).gameObject;viewportLabel=Label("Viewport caption",viewportChrome.transform,16,0,1100,48,"Perspective / G51   ·   3D VIEWPORT",13);
            Dock=Box("Right dock",design,1250,48,350,852,Panel).rectTransform;cameraHeading=Label("Stereo camera heading",Dock,12,8,326,28,"STEREO CAMERA");cameraState=Label("Camera display policy",Dock,12,40,326,36,"",12);robotHeading=Label("Robot workspace heading",Dock,12,90,326,28,"ROBOT CONTROL");
            InspectorScroll=Scroll("Dock inspector",Dock,out content);detailsButton=Button("Open detailed controls",Dock,12,736,326,34,"DETAILS  /  Ports · Control · Settings",()=>Select(BoardInput?Workspace.Ports:studio.router.Source=="CSV"?Workspace.Source:Workspace.Robot));systemState=Label("System status summary",Dock,12,780,326,66,"",12);
            DetailRoot=CanvasRoot("Studio details canvas",30,out detailDesign);var shield=Box("Details input shield",detailDesign,0,0,1600,900,new Color(0,0,0,.48f));var dialog=Box("Details dialog",detailDesign,400,100,800,700,Panel).rectTransform;
            detailTitle=Label("Details title",dialog,16,10,660,30,"DETAILED CONTROLS",17);Button("Close detailed controls",dialog,748,8,36,32,"×",CloseDetails);
            DetailScroll=Scroll("Details scroll",dialog,out detailContent);
            uartRoot=(RectTransform)Find("UART / Hardware panel");control=(RectTransform)Find("Hardware control page");manual=(RectTransform)Find("Manual panel");csv=(RectTransform)Find("CSV recorded human controls");tool=(RectTransform)Find("TOOL-1 sidebar");statusRoot=(RectTransform)Find("Policy status");core=(RectTransform)Find("Mock core");advanced=(RectTransform)Find("Mock advanced");
            // Only one navigation set: the original UART tabs live in the full-width detail dialog.
            Workspace[] pages={Workspace.Ports,Workspace.Control,Workspace.Records,Workspace.Settings,Workspace.Capture};
            for(int i=0;i<5;i++){int index=i;var b=Find("Hardware tab "+i).GetComponent<Button>();Place(b.transform,dialog,16+i*84,48,80,32);FitButtonLabel(b,14);b.onClick.AddListener(()=>Select(pages[index]));detailTabs[pages[i]]=b;}
            Workspace[] extras={Workspace.Robot,Workspace.Source,Workspace.Tool,Workspace.Status};
            for(int i=0;i<extras.Length;i++){var choice=extras[i];detailTabs[choice]=Button("Details tab "+choice,dialog,436+i*84,48,80,32,choice==Workspace.Robot?"Servo":choice.ToString(),()=>Select(choice));}
            Place(uartRoot,detailContent,0,0,640,714);Place(manual,detailContent,0,0,424,574);Place(csv,detailContent,0,0,640,714);Place(tool,detailContent,0,0,640,714);Place(statusRoot,detailContent,12,12,600,230);
            Find("Board UART title").gameObject.SetActive(false);Find("CSV lower backdrop").gameObject.SetActive(false);
            legacyHidden=Rect("Hidden legacy layout widgets",design,0,0,1,1).gameObject;Find("Servo details toggle").SetParent(legacyHidden.transform,false);legacyHidden.SetActive(false);
            Place(preview.PanelRoot.transform,content,0,0,340,700);Place(core,preview.PanelRoot.transform,0,0,340,240);Place(advanced,preview.PanelRoot.transform,0,250,340,340);
            // E/X remain the same Mock buttons; expose them in the basic panel, without new handlers.
            var mockOn=Child(advanced,"PWM enable");var mockOff=Child(advanced,"PWM disable");Place(mockOn,core,12,118,148,36);Place(mockOff,core,172,118,148,36);Child(core,"Title").gameObject.SetActive(false);
            csvPlayback=gameObject.AddComponent<ControlStudioCsvPlaybackPanel>();csvPlayback.Initialize(studio,content);
            sourceHint=Label("Source selection hint",detailContent,12,0,600,50,"Input selection remains in MENU / initial setup.").gameObject;
            expandButton=Button("Expand right dock",design,1566,60,28,36,"‹",()=>view.SetControlsVisible(true)).gameObject;
            OverlayRoot=CanvasRoot("Studio camera overlay canvas",20,out overlayDesign);OverlayRoot.GetComponent<GraphicRaycaster>().enabled=false;gutters=new Image[4];for(int i=0;i<4;i++){gutters[i]=Box("Presentation gutter "+i,overlayDesign,0,0,1,1,Color.clear);gutters[i].raycastTarget=false;}preview.PreviewRect.SetParent(overlayDesign,false);
            foreach(var t in new[]{core,advanced,uartRoot,manual,csv,tool,statusRoot})Style(t);CloseDetails();LateUpdate();
        }
        static void Style(Transform root)
        {foreach(var b in root.GetComponentsInChildren<Button>(true)){var colors=b.colors;colors.highlightedColor=new Color(.65f,.74f,.85f);colors.pressedColor=new Color(.47f,.61f,.77f);b.colors=colors;b.GetComponent<Image>().color=ButtonColor;}}
        static void FitButtonLabel(Button button,int size)
        {var text=button.GetComponentInChildren<Text>();text.fontSize=size;var r=text.rectTransform;r.anchorMin=Vector2.zero;r.anchorMax=Vector2.one;r.offsetMin=new Vector2(4,0);r.offsetMax=new Vector2(-4,0);text.alignment=TextAnchor.MiddleCenter;}
        public void Select(Workspace workspace)
        {Selected=workspace;DetailOpen=true;if(workspace==Workspace.Robot&&studio.router.Source=="MANUAL"&&!studio.ServoDetailsExpanded)studio.ToggleServoDetails();uart.SelectPage(workspace==Workspace.Control?1:workspace==Workspace.Records?2:workspace==Workspace.Settings?3:workspace==Workspace.Capture?4:0);DetailScroll.normalizedPosition=new Vector2(0,1);view.ApplyInputLayout();}
        public void CloseDetails(){DetailOpen=false;Selected=Workspace.Robot;uart.SelectPage(1);view.ApplyInputLayout();}
        void TextFit(Transform t,float x,float y,float width,float minHeight,int size)
        {var text=t.GetComponent<Text>();Place(t,t.parent,x,y,width,minHeight);text.fontSize=size;text.horizontalOverflow=HorizontalWrapMode.Wrap;text.verticalOverflow=VerticalWrapMode.Overflow;text.rectTransform.SetSizeWithCurrentAnchors(RectTransform.Axis.Vertical,Mathf.Max(minHeight,text.preferredHeight+4));}
        void CompactControl(float width,float fit)
        {
            GetComponent<ControlStudioBoardControlPanel>().SetDetailed(false);
            Place(control,content,0,0,width,550);var title=Child(control,"Real board control title");title.gameObject.SetActive(false);
            float gap=6,w=(width-2*gap)/3;int size=Mathf.RoundToInt(13/fit);
            for(int i=0;i<3;i++){var b=Child(control,"Control target "+i);Place(b,control,i*(w+gap),0,w,34/fit);b.GetComponentInChildren<Text>().text=new[]{"RIGHT","LEFT","BOTH"}[i];b.GetComponentInChildren<Text>().fontSize=size;}
            TextFit(Child(control,"Control selection"),0,40/fit,width,24/fit,size);
            w=(width-gap)/2;
            for(int i=0;i<6;i++){var b=Child(control,"Board control "+"EASXOH"[i]);Place(b,control,(i%2)*(w+gap),(74+i/2*40)/fit,w,34/fit);b.GetComponentInChildren<Text>().fontSize=size;}
            Place(Child(control,"Camera overlay toggle"),control,0,200/fit,width,34/fit);Child(control,"Camera overlay toggle").GetComponentInChildren<Text>().fontSize=size;
            TextFit(Child(control,"Camera overlay reported state"),0,240/fit,width,68/fit,size);
            float overlayBottom=240/fit+Child(control,"Camera overlay reported state").GetComponent<RectTransform>().rect.height+12/fit;
            Child(control,"Control semantics").gameObject.SetActive(false);TextFit(Child(control,"Control response status"),0,overlayBottom,width,120/fit,size);control.sizeDelta=new Vector2(width,overlayBottom+Child(control,"Control response status").GetComponent<RectTransform>().rect.height+12);content.sizeDelta=new Vector2(width,control.rect.height);
        }
        void FullControl()
        {
            var panel=GetComponent<ControlStudioBoardControlPanel>();panel.SetDetailed(true);Place(control,uartRoot,20,0,600,panel.DetailedHeight);
            for(int i=0;i<3;i++){var b=Child(control,"Control target "+i);b.GetComponentInChildren<Text>().text=new[]{"RIGHT / robot0","LEFT / robot1","BOTH"}[i];b.GetComponentInChildren<Text>().fontSize=14;}
        }
        void CompactMock(float width,float fit)
        {
            Place(preview.PanelRoot.transform,content,0,0,width,700);Place(core,preview.PanelRoot.transform,0,0,width,250/fit);float gap=6,w=(width-24-2*gap)/3;int size=Mathf.RoundToInt(13/fit);
            string[] targets={"Robot 0 RIGHT","Robot 1 LEFT","Robot BOTH"};for(int i=0;i<3;i++){var b=Child(core,targets[i]);Place(b,core,12+i*(w+gap),0,w,34/fit);FitButtonLabel(b.GetComponent<Button>(),size);}
            w=(width-30)/2;TextFit(Child(core,"PWM state"),12,40/fit,w,30/fit,size);TextFit(Child(core,"FOLLOW state"),18+w,40/fit,w,30/fit,size);
            string[] buttons={"Follow start","Follow stop","PWM enable","PWM disable"};for(int i=0;i<4;i++){var b=Child(core,buttons[i]);Place(b,core,12+i%2*(w+6),(76+i/2*40)/fit,w,34/fit);FitButtonLabel(b.GetComponent<Button>(),size);}
            Place(Child(core,"Advanced toggle"),core,12,160/fit,width-24,32/fit);Child(core,"Advanced toggle").GetComponentInChildren<Text>().fontSize=size;TextFit(Child(core,"Mock notice"),12,198/fit,width-24,32/fit,size);
            Place(advanced,preview.PanelRoot.transform,0,242/fit,width,374);content.sizeDelta=new Vector2(width,preview.AdvancedOpen?242/fit+374:240/fit);
        }
        void LayoutDetails(float fit)
        {
            var dialog=(RectTransform)DetailRoot.GetComponentsInChildren<Transform>(true).First(t=>t.name=="Details dialog");float pixelWidth=Mathf.Min(840,Screen.width-48),pixelHeight=Mathf.Min(920,Screen.height-96);
            // Legacy pages retain a full 640 physical pixel width and readable original fonts.
            Place(dialog,detailDesign,(Screen.width-pixelWidth)/2/fit,(Screen.height-pixelHeight)/2/fit,pixelWidth,pixelHeight);dialog.localScale=Vector3.one/fit;
            Place(detailTitle.transform,dialog,16,10,pixelWidth-72,30);Place(Child(dialog,"Close detailed controls"),dialog,pixelWidth-52,8,36,32);
            ScrollSize(DetailScroll,16,92,pixelWidth-32,pixelHeight-108);detailContent.sizeDelta=new Vector2(pixelWidth-44,820);
            uartRoot.localScale=manual.localScale=csv.localScale=tool.localScale=statusRoot.localScale=Vector3.one;
            uartRoot.gameObject.SetActive(UartDetail);manual.gameObject.SetActive(Selected==Workspace.Robot&&studio.router.Source=="MANUAL");csv.gameObject.SetActive(Selected==Workspace.Source&&studio.router.Source=="CSV");tool.gameObject.SetActive(Selected==Workspace.Tool);statusRoot.gameObject.SetActive(Selected==Workspace.Status);sourceHint.SetActive(Selected==Workspace.Source&&studio.router.Source!="CSV");
            var contract=Find("Board UART contract");contract.gameObject.SetActive(uart.SelectedPage>1);TextFit(contract,20,0,600,44,13);
            var page=uartRoot.GetComponentsInChildren<RectTransform>(true).First(t=>t.name==(uart.SelectedPage==0?"Hardware ports page":uart.SelectedPage==1?"Hardware control page":"Hardware module page "+uart.SelectedPage));
            if(page!=control)Place(page,uartRoot,uart.SelectedPage==0?0:20,52,uart.SelectedPage==0?640:600,700);
            float extent=714;
            if(UartDetail&&uart.SelectedPage==0){
                Place(page,uartRoot,0,0,640,uart.PortsView.ContentHeight);extent=uart.PortsView.ContentHeight;
            }
            if(UartDetail&&uart.SelectedPage==1)extent=GetComponent<ControlStudioBoardControlPanel>().DetailedHeight+20;
            if(UartDetail){uartRoot.sizeDelta=new Vector2(640,extent);detailContent.sizeDelta=new Vector2(pixelWidth-44,Mathf.Max(extent,pixelHeight-108));}
            else if(Selected==Workspace.Robot)detailContent.sizeDelta=new Vector2(pixelWidth-44,590);
            foreach(var pair in detailTabs){pair.Value.GetComponent<Image>().color=pair.Key==Selected?Blue:ButtonColor;if(pair.Key==Workspace.Robot)pair.Value.interactable=studio.router.Source=="MANUAL";}
        }
        void LateUpdate()
        {
            if(studio==null)return;float fit=Mathf.Min(Screen.width/1600f,Screen.height/900f);float pixels=Mathf.Lerp(344,420,Mathf.Clamp01((Screen.width-1280)/640f));float width=pixels/fit;ViewportDesignWidth=1600-width;
            bool normal=!menu.Visible&&!view.PresentationMode,open=normal&&view.ControlsVisible;Dock.gameObject.SetActive(open);viewportChrome.SetActive(normal);expandButton.SetActive(normal&&!open);DetailRoot.SetActive(open&&DetailOpen);
            Place(Dock,design,ViewportDesignWidth,48,width,852);((RectTransform)viewportChrome.transform).sizeDelta=new Vector2(view.ControlsVisible?ViewportDesignWidth:1600,48);viewportLabel.text="Perspective / "+GetComponent<RobotVisualProfiles>().Selected+"   ·   "+studio.router.Source+"   ·   3D VIEWPORT";
            if(!DetailOpen&&BoardInput){uart.SelectPage(1);CompactControl(width-36,fit);}else {FullControl();control.gameObject.SetActive(DetailOpen&&Selected==Workspace.Control);}
            preview.PanelRoot.SetActive(!BoardInput&&studio.router.Source!="CSV");CompactMock(width-36,fit);if(BoardInput&&!DetailOpen)content.sizeDelta=new Vector2(width-36,control.rect.height);
            csvPlayback.Layout(width-36,fit,studio.router.Source=="CSV"&&!DetailOpen);if(studio.router.Source=="CSV"&&!DetailOpen)content.sizeDelta=new Vector2(width-36,csvPlayback.Height);
            bool live=preview.PreviewVisible&&capture.HasFrame;
            float angle=capture.Session.Rotation*Mathf.Deg2Rad,c=Mathf.Abs(Mathf.Cos(angle)),sn=Mathf.Abs(Mathf.Sin(angle));
            float aspect=live?(capture.Session.Width*c+capture.Session.Height*sn)/(capture.Session.Width*sn+capture.Session.Height*c):1;
            CameraSectionHeight=live?Mathf.Min((width-24)/aspect,150/fit)+96:74;
            Place(cameraHeading.transform,Dock,12,8,width-24,24);cameraHeading.fontSize=robotHeading.fontSize=Mathf.RoundToInt(14/fit);Place(cameraState.transform,Dock,12,live?CameraSectionHeight-32:34,width-24,32);cameraState.fontSize=Mathf.RoundToInt(12/fit);
            var s=capture.Session;cameraState.text=live?"Camera ON  ·  "+s.Width+"×"+s.Height+"  ·  RECEIVING":!preview.PreviewOpen?"Camera OFF":!s.DeviceAvailable?"Camera ON  ·  DEVICE UNAVAILABLE":"Camera ON  ·  STALE / WAITING";
            Place(robotHeading.transform,Dock,12,CameraSectionHeight+8,width-24,26);ScrollSize(InspectorScroll,12,CameraSectionHeight+40,width-24,852-CameraSectionHeight-158);
            Place(detailsButton.transform,Dock,12,736,width-24,34);detailsButton.GetComponentInChildren<Text>().fontSize=Mathf.RoundToInt(12/fit);Place(systemState.transform,Dock,12,780,width-24,66);systemState.fontSize=Mathf.RoundToInt(13/fit);
            var boards=GetComponent<BoardUartSource>();var output=GetComponent<ControlStudioUartOutput>();var visualSide=boards.VisualSource??BoardSide.Right;var visualSession=boards.Session(visualSide);var state=visualSession.State;
            systemState.text="SYSTEM / "+studio.router.Source+"  ·  TX "+output.HardwareTxCount+" · G51 "+(boards.VisualSource.HasValue?visualSide==BoardSide.Right?"R":"L":"--")+
                "\nR "+(boards.Right.Connected?"COM":"OFF")+" / L "+(boards.Left.Connected?"COM":"OFF")+"  ·  PWM "+(state==null?"?":state.Pwm?"1":"0")+"  ·  Follow "+(visualSession.FollowEnabled.HasValue?(visualSession.FollowEnabled.Value?"1":"0"):"?")+
                "\nVerified "+(visualSession.AppliedCurrent(BoardUartTransport.Now)?"CURRENT":visualSession.LastApplied!=null?"HELD":"--")+"  ·  Unity TK "+(boards.ApplicationState(visualSide)?.UnityAppliedTick?.ToString()??"--");
            if(DetailOpen)LayoutDetails(fit);LayoutPreview(normal,open,fit,width);UpdateGutters(fit);
        }
        void LayoutPreview(bool normal,bool open,float fit,float dockWidth)
        {
            if(!preview.PreviewVisible||!capture.HasFrame)return;var s=capture.Session;float angle=s.Rotation*Mathf.Deg2Rad,c=Mathf.Abs(Mathf.Cos(angle)),sn=Mathf.Abs(Mathf.Sin(angle));float aspect=(s.Width*c+s.Height*sn)/(s.Width*sn+s.Height*c);float width,x,y;
            if(view.PresentationMode){width=Mathf.Min(360,Screen.width*.25f)/fit;width=Mathf.Min(width,(Screen.height-48)/fit*aspect);x=1600-width-16/fit;y=16/fit;}
            else if(normal&&open){width=dockWidth-24;float maxHeight=150/fit;width=Mathf.Min(width,maxHeight*aspect);x=1600-12-width;y=80;}
            else return;
            var panel=preview.PreviewRect;panel.anchoredPosition=new Vector2(x,-y);panel.sizeDelta=new Vector2(width,width/aspect+24);var viewport=(RectTransform)panel.Find("Live capture viewport");viewport.anchoredPosition=new Vector2(0,-24);viewport.sizeDelta=new Vector2(width,width/aspect);panel.Find("Capture preview source").GetComponent<Text>().rectTransform.sizeDelta=new Vector2(width,20);capture.Render(panel.Find("Live capture viewport/Live capture preview").GetComponent<RawImage>(),width,width/aspect);
        }
        void UpdateGutters(float fit)
        {bool show=view.PresentationMode&&!menu.Visible;var camera=studio.orbit.GetComponent<Camera>();var p=camera.pixelRect;var rects=new[]{new Rect(0,0,Screen.width,Screen.height-p.yMax),new Rect(0,Screen.height-p.yMin,Screen.width,p.yMin),new Rect(0,Screen.height-p.yMax,p.xMin,p.height),new Rect(p.xMax,Screen.height-p.yMax,Screen.width-p.xMax,p.height)};for(int i=0;i<4;i++){gutters[i].gameObject.SetActive(show);gutters[i].color=camera.backgroundColor;var r=rects[i];Place(gutters[i].transform,overlayDesign,r.x/fit,r.y/fit,r.width/fit,r.height/fit);}}
        void OnDestroy(){if(OverlayRoot!=null)Destroy(OverlayRoot);if(DetailRoot!=null)Destroy(DetailRoot);}
    }
}

