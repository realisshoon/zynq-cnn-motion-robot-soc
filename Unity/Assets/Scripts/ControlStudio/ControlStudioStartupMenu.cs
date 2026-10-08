using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Presentation/navigation only. No command writer, transport or pose reset.
    [DefaultExecutionOrder(4000)]
    public sealed class ControlStudioStartupMenu : MonoBehaviour
    {
        static readonly string[][] Options={new[]{"Robot Arm","Humanoid Robot"},new[]{"Manual","CSV / XYZ","UART"},new[]{"Gripper","Spray","Welding","Nailing"}};
        static readonly ToolKind[] Tools={ToolKind.Gripper,ToolKind.Spray,ToolKind.Welding,ToolKind.Nailing};
        readonly int[] choices=new int[3];
        readonly Text[] values=new Text[3];
        readonly Color accent=new Color(.43f,.87f,.96f);
        ControlStudioRuntimeUI studio;RobotVisualProfiles visuals;Tool1Runtime tools;
        GameObject menuRoot;CanvasGroup controls;Font latin,titleFont;Text notice;
        Camera cameraView;Rect savedRect;bool framePreview;int selectedRow;
        public bool Visible {get;private set;}
        public Transform MenuCanvas=>menuRoot.transform;
        public int Choice(int row)=>choices[row];
        public float PreviewScale=>visuals.Selected==RobotVisualProfileId.HumanoidRobot?.78f:.86f;

        public static void Attach(ControlStudioRuntimeUI owner,Transform design)
        {
            var menu=owner.gameObject.AddComponent<ControlStudioStartupMenu>();
            menu.Initialize(owner,design);
        }
        void Initialize(ControlStudioRuntimeUI owner,Transform design)
        {
            studio=owner;visuals=GetComponent<RobotVisualProfiles>();tools=GetComponent<Tool1Runtime>();
            cameraView=owner.orbit.GetComponent<Camera>();
            controls=owner.RuntimeCanvas.gameObject.AddComponent<CanvasGroup>();
            latin=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            titleFont=Font.CreateDynamicFontFromOSFont(new[]{"Malgun Gothic","맑은 고딕","Arial Unicode MS"},58);
            titleFont.RequestCharactersInTexture("움이움",58,FontStyle.Normal);
            menuRoot=new GameObject("Startup Menu Canvas",typeof(Canvas),typeof(CanvasScaler),typeof(GraphicRaycaster));
            menuRoot.transform.SetParent(transform,false);
            var canvas=menuRoot.GetComponent<Canvas>();canvas.renderMode=RenderMode.ScreenSpaceOverlay;canvas.sortingOrder=40;
            var scaler=menuRoot.GetComponent<CanvasScaler>();scaler.uiScaleMode=CanvasScaler.ScaleMode.ScaleWithScreenSize;scaler.referenceResolution=new Vector2(1600,900);scaler.screenMatchMode=CanvasScaler.ScreenMatchMode.Expand;
            var area=Rect("Startup design area",menuRoot.transform,0,0,1600,900);area.anchorMin=area.anchorMax=area.pivot=new Vector2(.5f,.5f);area.anchoredPosition=Vector2.zero;
            var veil=Rect("Scene dimmer / input shield",menuRoot.transform,0,0,0,0);veil.anchorMin=Vector2.zero;veil.anchorMax=Vector2.one;veil.offsetMin=veil.offsetMax=Vector2.zero;
            veil.gameObject.AddComponent<Image>().color=new Color(.02f,.03f,.04f,.13f);veil.SetAsFirstSibling();
            var panel=Rect("Startup menu panel",area,64,65,470,770);panel.gameObject.AddComponent<Image>().color=new Color(.025f,.04f,.055f,.88f);
            var brand=CenteredRect("Startup brand outline",panel,38,360,100);
            var border=new Color(.59f,.76f,.80f,.85f);
            foreach(var edge in new[]{Rect("Top",brand,0,0,360,1.5f),Rect("Bottom",brand,0,98.5f,360,1.5f),Rect("Left",brand,0,0,1.5f,100),Rect("Right",brand,358.5f,0,1.5f,100)}){var image=edge.gameObject.AddComponent<Image>();image.color=border;image.raycastTarget=false;}
            var title=Label(brand,"움이움",0,0,360,100,58,new Color(.94f,.97f,.98f));title.font=titleFont;
            var subtitle=Label(panel,"ROBOT CONTROL STUDIO",0,151,394,25,14,new Color(.61f,.72f,.77f));Center(subtitle.rectTransform,151);
            string[] labels={"ROBOT","INPUT","TOOL"};
            for(int row=0;row<3;row++){
                int index=row;float y=205+row*105;
                var group=CenteredRect(labels[row]+" section",panel,y,394,76);
                Label(group,labels[row],0,0,394,25,15,new Color(.63f,.7f,.75f));
                var option=Rect(labels[row]+" OptionRow",group,0,31,394,45);
                var layout=option.gameObject.AddComponent<HorizontalLayoutGroup>();layout.spacing=8;layout.childAlignment=TextAnchor.MiddleCenter;layout.childControlWidth=layout.childControlHeight=true;layout.childForceExpandWidth=layout.childForceExpandHeight=false;
                Button(option,labels[row]+" previous",0,0,48,45,"<",()=>Cycle(index,-1));
                values[row]=Label(option,"",0,0,282,45,26,accent);
                Button(option,labels[row]+" next",0,0,48,45,">",()=>Cycle(index,1));
                for(int child=0;child<3;child++){var element=option.GetChild(child).gameObject.AddComponent<LayoutElement>();element.minWidth=element.preferredWidth=child==1?282:48;element.minHeight=element.preferredHeight=45;}
            }
            Button(panel,"START",0,560,350,58,"START",()=>StartStudio());Center((RectTransform)panel.Find("START"),560);
            notice=Label(panel,"",0,647,434,46,14,new Color(.95f,.65f,.45f));Center(notice.rectTransform,647);
            notice.horizontalOverflow=HorizontalWrapMode.Wrap;
            Button(design,"MENU",837,14,108,32,"MENU",ShowMenu);
            Snapshot();ShowMenu();framePreview=true;
        }
        RectTransform Rect(string name,Transform parent,float x,float y,float w,float h)
        {
            var go=new GameObject(name,typeof(RectTransform));var r=(RectTransform)go.transform;r.SetParent(parent,false);
            r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);return r;
        }
        void Center(RectTransform rect,float y){rect.anchorMin=rect.anchorMax=new Vector2(.5f,1);rect.pivot=new Vector2(.5f,1);rect.anchoredPosition=new Vector2(0,-y);}
        RectTransform CenteredRect(string name,Transform parent,float y,float w,float h){var rect=Rect(name,parent,0,y,w,h);Center(rect,y);return rect;}
        Text Label(Transform parent,string caption,float x,float y,float w,float h,int size,Color color)
        {
            var r=Rect(caption.Length==0?"Option value":caption,parent,x,y,w,h);var t=r.gameObject.AddComponent<Text>();
            t.font=latin;t.text=caption;t.fontSize=size;t.color=color;t.alignment=TextAnchor.MiddleCenter;t.raycastTarget=false;return t;
        }
        void Button(Transform parent,string name,float x,float y,float w,float h,string caption,UnityEngine.Events.UnityAction action)
        {
            var r=Rect(name,parent,x,y,w,h);var image=r.gameObject.AddComponent<Image>();image.color=new Color(.15f,.23f,.28f,.9f);
            var b=r.gameObject.AddComponent<Button>();b.targetGraphic=image;b.onClick.AddListener(action);
            var colors=b.colors;colors.highlightedColor=new Color(.65f,.92f,1);b.colors=colors;
            Label(r,caption,0,0,w,h,22,Color.white);
        }
        void Snapshot()
        {
            choices[0]=visuals.Selected==RobotVisualProfileId.HumanoidRobot?1:0;
            choices[1]=studio.router.Source=="CSV"?1:studio.router.Source=="UART"?2:0;
            choices[2]=System.Array.IndexOf(Tools,tools.Gate.Tool);if(choices[2]<0)choices[2]=0;
            Refresh();
        }
        void Refresh(){for(int i=0;i<3;i++){values[i].text=Options[i][choices[i]];values[i].color=i==selectedRow?Color.white:accent;}}
        public void Cycle(int row,int delta)
        {
            if(!Visible||row<0||row>=3)return;
            // Enter belongs to START; do not also submit the last clicked arrow.
            if(EventSystem.current!=null)EventSystem.current.SetSelectedGameObject(null);
            choices[row]=(choices[row]+delta%Options[row].Length+Options[row].Length)%Options[row].Length;selectedRow=row;notice.text="";
            if(row==0){var id=choices[0]==0?RobotVisualProfileId.G51:RobotVisualProfileId.HumanoidRobot;if(!visuals.Select(id))notice.text=visuals.SelectionMessage;else framePreview=true;}
            Refresh();
        }
        public bool StartStudio()
        {
            if(!Visible)return false;
            if(!visuals.Select(choices[0]==0?RobotVisualProfileId.G51:RobotVisualProfileId.HumanoidRobot)){notice.text=visuals.SelectionMessage;return false;}
            // A source change alone uses its existing initialization contract; unchanged sources are retained.
            if(!studio.SelectStartupInput(choices[1],out var error)){notice.text=error;return false;}
            if(!visuals.SelectTool(Tools[choices[2]])){notice.text=visuals.SelectionMessage;return false;}
            visuals.SetEnvironmentMode(EnvironmentViewMode.RobotOnly);
            Visible=false;menuRoot.SetActive(false);controls.alpha=1;controls.interactable=true;controls.blocksRaycasts=true;cameraView.rect=savedRect;cameraView.ResetProjectionMatrix();
            GetComponent<ControlStudioPresentationUI>().ApplyInputLayout();
            return true;
        }
        public void ShowMenu()
        {
            if(Visible)return;
            var presentation=GetComponent<ControlStudioPresentationUI>();
            if(presentation.PresentationMode)return;
            Snapshot();savedRect=cameraView.rect;Visible=true;menuRoot.SetActive(true);
            controls.alpha=0;controls.interactable=false;controls.blocksRaycasts=false;notice.text="";
        }
        void Update()
        {
            var key=Keyboard.current;if(key==null)return;
            if(!Visible){if(key.escapeKey.wasPressedThisFrame&&GetComponent<ControlStudioPresentationUI>()?.PresentationMode!=true)ShowMenu();return;}
            if(key.upArrowKey.wasPressedThisFrame)selectedRow=(selectedRow+2)%3;
            if(key.downArrowKey.wasPressedThisFrame)selectedRow=(selectedRow+1)%3;
            if(key.leftArrowKey.wasPressedThisFrame)Cycle(selectedRow,-1);
            if(key.rightArrowKey.wasPressedThisFrame)Cycle(selectedRow,1);
            if(key.enterKey.wasPressedThisFrame||key.numpadEnterKey.wasPressedThisFrame)StartStudio();
            Refresh();
        }
        void LateUpdate()
        {
            if(!Visible)return;
            if(framePreview){studio.orbit.SetPresentationCamera();framePreview=false;}
            cameraView.rect=new Rect(0,0,1,1);
            var projection=Matrix4x4.Perspective(cameraView.fieldOfView,cameraView.aspect,cameraView.nearClipPlane,cameraView.farClipPlane);
            projection.m02=-.34f;cameraView.projectionMatrix=projection;
            projection.m00*=PreviewScale;projection.m11*=PreviewScale;cameraView.projectionMatrix=projection;
        }
        void OnDestroy(){if(Visible&&cameraView!=null)cameraView.ResetProjectionMatrix();if(titleFont!=null)Destroy(titleFont);}
    }
}
