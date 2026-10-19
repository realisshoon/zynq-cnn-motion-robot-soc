using UnityEngine;
using UnityEngine.UI;
using UnityEngine.InputSystem;
using UnityEngine.EventSystems;

namespace HumanMotion.ControlStudio
{
    // Robot controls are Mock; optional camera preview uses its independent capture owner.
    [DefaultExecutionOrder(4500)]
    public sealed class RobotControlMockPanel : MonoBehaviour
    {
        readonly RobotControlMockState[] robots={new RobotControlMockState(),new RobotControlMockState()};
        readonly Color ink=new Color(.89f,.91f,.94f),muted=new Color(.60f,.65f,.71f),accent=new Color(.36f,.57f,.78f);
        readonly Slider[] margins=new Slider[3];readonly Text[] marginValues=new Text[3];
        readonly Button[] selections=new Button[3];
        ControlStudioPresentationUI presentation;ControlStudioStartupMenu startup;
        GameObject root,advanced,previewImage,sizeToggle,sizeMenu;RectTransform preview,captureViewport;
        readonly Button[] sizeChoices=new Button[4];Text sizeLabel;bool sizeMenuOpen;int hiddenPreviewSize=2;
        Font font;Text pwm,follow,notice,applied,advancedLabel,previewLabel;
        ControlStudioVideoCapture capture;RawImage captureImage;Text captureLabel;
        Button start,stop,pwmOn,pwmOff;bool advancedOpen,previewOpen;int selected;
        public RobotControlMockState State=>robots[selected==1?1:0];
        public int SelectedRobot=>selected;
        public RobotControlMockState StateFor(int side)=>robots[side];
        public enum MockResult { NotRequested, Accepted, Denied, Timeout }
        public MockResult[] Results {get;}={MockResult.NotRequested,MockResult.NotRequested};
        public string ResultMessage {get;private set;}="MOCK ONLY · 실제 송신 없음";
        void ForSelection(System.Action<RobotControlMockState> action){for(int i=0;i<2;i++)if(selected==2||selected==i)action(robots[i]);}
        public void Dispatch(string command)
        {
            for(int i=0;i<2;i++){
                Results[i]=MockResult.NotRequested;if(selected!=2&&selected!=i)continue;
                var s=robots[i];bool ok=true;
                switch(command){case "E":s.EnablePwm();break;case "A":ok=s.StartFollow();break;case "S":s.StopFollow();break;case "X":s.DisablePwm();break;case "RGB":s.Apply();break;case "REFRESH":s.Refresh();break;default:ok=false;break;}
                Results[i]=ok?MockResult.Accepted:MockResult.Denied;
            }
            bool partial=selected==2&&Results[0]!=Results[1];
            ResultMessage=(partial?"PARTIAL · ":"MOCK · ")+"R "+Results[0]+" / L "+Results[1];
            RefreshView();
        }
        public bool AdvancedOpen=>advancedOpen;
        public bool PreviewOpen=>previewOpen;
        public int HiddenPreviewSize=>hiddenPreviewSize;
        public RectTransform PreviewRect=>preview;
        public bool CanPreviewInteract=>isActiveAndEnabled&&startup!=null&&!startup.Visible&&presentation!=null;
        public bool PreviewVisible=>preview!=null&&preview.gameObject.activeInHierarchy;
        public GameObject PanelRoot=>root;
        public Slider[] MarginSliders=>margins;
        public bool CanInteract=>GetComponent<ControlStudioRuntimeUI>()?.router?.Source!="BOARD"&&isActiveAndEnabled&&startup!=null&&!startup.Visible&&presentation!=null&&!presentation.PresentationMode&&presentation.ControlsVisible;
        public static void Attach(ControlStudioRuntimeUI owner,Transform design)
        {
            var panel=owner.gameObject.AddComponent<RobotControlMockPanel>();
            panel.capture=ControlStudioVideoCapture.GetOrAttach(owner.gameObject);
            panel.presentation=owner.GetComponent<ControlStudioPresentationUI>();panel.startup=owner.GetComponent<ControlStudioStartupMenu>();
            panel.Build(design);
        }
        RectTransform Rect(string name,Transform parent,float x,float y,float w,float h)
        {
            var go=new GameObject(name,typeof(RectTransform));var r=(RectTransform)go.transform;r.SetParent(parent,false);
            r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);return r;
        }
        Image Box(string name,Transform parent,float x,float y,float w,float h,Color color)
        {var r=Rect(name,parent,x,y,w,h);var image=r.gameObject.AddComponent<Image>();image.color=color;return image;}
        Text Label(string name,Transform parent,float x,float y,float w,float h,string value,int size=18,Color? color=null)
        {
            var r=Rect(name,parent,x,y,w,h);var t=r.gameObject.AddComponent<Text>();t.font=font;t.fontSize=size;t.color=color??ink;t.text=value;
            t.alignment=TextAnchor.MiddleLeft;t.raycastTarget=false;t.horizontalOverflow=HorizontalWrapMode.Wrap;return t;
        }
        Button Button(string name,Transform parent,float x,float y,float w,float h,string caption,UnityEngine.Events.UnityAction action,bool previewAction=false)
        {
            var image=Box(name,parent,x,y,w,h,new Color(.13f,.23f,.29f));var b=image.gameObject.AddComponent<Button>();b.targetGraphic=image;
            b.onClick.AddListener(()=>{if(previewAction?CanPreviewInteract:CanInteract){action();RefreshView();}});
            var t=Label("Label",image.transform,4,0,w-8,h,caption,18);t.alignment=TextAnchor.MiddleCenter;
            var colors=b.colors;colors.highlightedColor=new Color(.65f,.91f,1);colors.disabledColor=new Color(.38f,.43f,.46f,.65f);b.colors=colors;return b;
        }
        void Build(Transform design)
        {
            font=Font.CreateDynamicFontFromOSFont(new[]{"Malgun Gothic","맑은 고딕","Arial"},18);
            root=Rect("Robot Control MOCK / no hardware",design,0,0,1600,900).gameObject;
            var core=Box("Mock core",root.transform,24,108,280,238,new Color(.035f,.065f,.085f,.97f)).transform;
            Label("Title",core,12,6,256,30,"ROBOT CONTROL",20);
            selections[0]=Button("Robot 0 RIGHT",core,12,42,80,34,"RIGHT",()=>SelectRobot(0));
            selections[1]=Button("Robot 1 LEFT",core,100,42,80,34,"LEFT",()=>SelectRobot(1));
            selections[2]=Button("Robot BOTH",core,188,42,80,34,"BOTH",()=>SelectRobot(2));
            pwm=Label("PWM state",core,12,80,124,34,"PWM OFF",14);follow=Label("FOLLOW state",core,144,80,124,34,"FOLLOW OFF",14);
            start=Button("Follow start",core,12,118,124,36,"추종 시작  A",()=>Dispatch("A"));
            stop=Button("Follow stop",core,144,118,124,36,"추종 정지  S",()=>Dispatch("S"));
            var toggle=Button("Advanced toggle",core,12,166,256,32,"고급 설정  +",()=>{advancedOpen=!advancedOpen;});advancedLabel=toggle.GetComponentInChildren<Text>();
            notice=Label("Mock notice",core,12,202,256,32,"",14,muted);

            advanced=Box("Mock advanced",root.transform,24,358,304,374,new Color(.035f,.065f,.085f,.98f)).gameObject;
            Label("Advanced title",advanced.transform,12,6,280,30,"ADVANCED / MOCK",19,accent);
            pwmOn=Button("PWM enable",advanced.transform,12,42,136,34,"PWM 활성화  E",()=>Dispatch("E"));
            pwmOff=Button("PWM disable",advanced.transform,156,42,136,34,"PWM 해제  X",()=>Dispatch("X"));
            Button("Refresh mock status",advanced.transform,12,86,280,30,"상태 새로고침 · MOCK",()=>Dispatch("REFRESH"));
            string[] names={"Red Margin","Green Margin","Blue Margin"};
            for(int i=0;i<3;i++){
                int index=i;float y=128+i*50;
                Label(names[i],advanced.transform,12,y,220,24,names[i],17);
                marginValues[i]=Label(names[i]+" value",advanced.transform,246,y,46,24,"",18,accent);marginValues[i].alignment=TextAnchor.MiddleRight;
                var sr=Rect(names[i]+" slider",advanced.transform,16,y+27,272,18);var slider=sr.gameObject.AddComponent<Slider>();
                Box("Track",sr,0,6,272,6,new Color(.19f,.28f,.33f));var area=Rect("Handle area",sr,6,0,260,18);
                var handle=Box("Handle",area,0,0,12,18,accent);handle.rectTransform.pivot=new Vector2(.5f,.5f);handle.rectTransform.sizeDelta=new Vector2(12,0);
                slider.handleRect=handle.rectTransform;slider.targetGraphic=handle;slider.minValue=0;slider.maxValue=100;slider.wholeNumbers=true;
                slider.onValueChanged.AddListener(v=>{if(!CanInteract)return;ForSelection(s=>{if(index==0)s.Red=(int)v;else if(index==1)s.Green=(int)v;else s.Blue=(int)v;});RefreshView();});margins[i]=slider;
            }
            Button("Apply mock margins",advanced.transform,12,286,280,34,"Apply · MOCK ONLY",()=>Dispatch("RGB"));
            applied=Label("Applied mock margins",advanced.transform,12,321,280,30,"",12,accent);
            Label("Margin scope",advanced.transform,12,349,280,18,"UI range 0–100 · 실제 규약 미정",12,muted);

            // Preview has its own visibility, independent of hidden control panels.
            preview=Rect("Mock preview",design,1304,108,272,153);
            var show=Button("Preview toggle",design,950,54,210,32,"Camera / MOCK +",()=>{previewOpen=!previewOpen;},true);previewLabel=show.GetComponentInChildren<Text>();
            var sizes=Button("Camera size menu toggle",design,1170,54,190,32,"Size 2 ▾",()=>sizeMenuOpen=!sizeMenuOpen,true);
            sizeToggle=sizes.gameObject;sizeLabel=sizes.GetComponentInChildren<Text>();sizeLabel.fontSize=16;
            sizeMenu=Rect("Camera size choices",design,1300,54,276,32).gameObject;
            for(int i=0;i<sizeChoices.Length;i++){int choice=i+1;sizeChoices[i]=Button("Camera size "+choice,sizeMenu.transform,i*70,0,66,32,choice.ToString(),()=>{hiddenPreviewSize=choice;sizeMenuOpen=false;},true);}
            previewImage=Box("Placeholder / no capture",preview,0,0,272,153,new Color(.07f,.11f,.14f)).gameObject;
            for(int i=1;i<4;i++){Box("Grid V "+i,previewImage.transform,272*i/4f,0,1,153,new Color(.11f,.17f,.20f)).raycastTarget=false;Box("Grid H "+i,previewImage.transform,0,153*i/4f,272,1,new Color(.11f,.17f,.20f)).raycastTarget=false;}
            var placeholder=Label("Placeholder label",previewImage.transform,8,34,256,80,"MOCK PREVIEW\n영상 입력 없음",19,muted);placeholder.alignment=TextAnchor.MiddleCenter;
            captureViewport=Rect("Live capture viewport",preview,0,24,272,153);
            captureImage=Rect("Live capture preview",captureViewport,0,0,272,153).gameObject.AddComponent<RawImage>();captureImage.raycastTarget=false;captureImage.gameObject.SetActive(false);
            captureLabel=Label("Capture preview source",preview,0,0,272,20,"",12,accent);captureLabel.gameObject.SetActive(false);
            RefreshView();RefreshVisibility();
        }
        public void SelectRobot(int index){if(index<0||index>2)return;selected=index;RefreshView();}
        public void RefreshView()
        {
            if(root==null)return;var state=State;
            pwm.text=selected==2?$"R PWM {(robots[0].Pwm?"ON":"OFF")}\nL PWM {(robots[1].Pwm?"ON":"OFF")}":state.Pwm?"PWM ON":"PWM OFF";
            follow.text=selected==2?$"R FOLLOW {(robots[0].Follow?"ON":"OFF")}\nL FOLLOW {(robots[1].Follow?"ON":"OFF")}":state.Follow?"FOLLOW ON":"FOLLOW OFF";
            pwm.color=state.Pwm?accent:muted;follow.color=state.Follow?accent:muted;
            bool both=selected==2;
            start.interactable=both?(robots[0].Pwm||robots[1].Pwm):state.Pwm&&!state.Follow;
            stop.interactable=both?(robots[0].Follow||robots[1].Follow):state.Follow;
            pwmOn.interactable=both?(!robots[0].Pwm||!robots[1].Pwm):!state.Pwm;
            pwmOff.interactable=both?(robots[0].Pwm||robots[1].Pwm):state.Pwm;
            for(int i=0;i<3;i++)selections[i].GetComponent<Image>().color=i==selected?new Color(.2f,.43f,.49f):new Color(.13f,.23f,.29f);
            int[] values={state.Red,state.Green,state.Blue},left={robots[1].Red,robots[1].Green,robots[1].Blue};
            for(int i=0;i<3;i++){margins[i].SetValueWithoutNotify(values[i]);marginValues[i].fontSize=both?12:18;marginValues[i].text=both?values[i]+"/"+left[i]:values[i].ToString();}
            notice.text=ResultMessage;
            applied.text=both?$"R {robots[0].AppliedRed}/{robots[0].AppliedGreen}/{robots[0].AppliedBlue} · L {robots[1].AppliedRed}/{robots[1].AppliedGreen}/{robots[1].AppliedBlue}\nRGB 드래그만 양측 변경 · 독립 MOCK":$"적용 R {state.AppliedRed} / G {state.AppliedGreen} / B {state.AppliedBlue}";
            advancedLabel.text=advancedOpen?"고급 설정  −":"고급 설정  +";advanced.SetActive(advancedOpen);
            string source=capture!=null&&capture.HasFrame?"LIVE":"MOCK";
            previewLabel.text="Camera "+(previewOpen?"ON":"OFF");previewImage.SetActive(false);
        }
        public void RefreshVisibility()
        {
            if(root!=null)root.SetActive(CanInteract);
            if(preview==null)return;
            preview.gameObject.SetActive(CanPreviewInteract&&previewOpen&&capture!=null&&capture.HasFrame);
            bool sizes=CanPreviewInteract&&!presentation.PresentationMode;
            if(!sizes)sizeMenuOpen=false;
            sizeToggle.SetActive(sizes);sizeMenu.SetActive(sizes&&sizeMenuOpen);
            if(sizes&&sizeMenuOpen)sizeMenu.transform.SetAsLastSibling();
            sizeLabel.text="Size "+hiddenPreviewSize+" ▾";
            for(int i=0;i<sizeChoices.Length;i++)sizeChoices[i].GetComponent<Image>().color=i+1==hiddenPreviewSize?new Color(.2f,.43f,.49f):new Color(.13f,.23f,.29f);
        }
        public void HandleShortcut(Key key)
        {
            if(!CanInteract)return;
            if(EventSystem.current?.currentSelectedGameObject?.GetComponentInParent<InputField>()!=null)return;
            if(key==Key.E)Dispatch("E");else if(key==Key.A)Dispatch("A");else if(key==Key.S)Dispatch("S");else if(key==Key.X)Dispatch("X");else return;
            RefreshView();
        }
        void Update()
        {
            var k=Keyboard.current;if(!CanInteract||k==null)return;
            if(k.xKey.wasPressedThisFrame)HandleShortcut(Key.X);
            else if(k.eKey.wasPressedThisFrame)HandleShortcut(Key.E);
            else if(k.sKey.wasPressedThisFrame)HandleShortcut(Key.S);
            else if(k.aKey.wasPressedThisFrame)HandleShortcut(Key.A);
        }
        void LateUpdate()
        {
            RefreshVisibility();if(preview==null)return;
            bool live=capture!=null&&capture.HasFrame;
            // Use the displayed frame's aspect, including webcam rotation, rather than
            // letterboxing stereo video inside a fixed 16:9 parent.
            float aspect=16f/9;
            if(live){
                var frame=capture.Session;float angle=frame.Rotation*Mathf.Deg2Rad;
                float c=Mathf.Abs(Mathf.Cos(angle)),s=Mathf.Abs(Mathf.Sin(angle));
                aspect=(frame.Width*c+frame.Height*s)/(frame.Width*s+frame.Height*c);
            }
            float fit=Mathf.Max(.01f,Mathf.Min(Screen.width/1600f,Screen.height/900f));
            float heading=live?24:0;
            bool expanded=presentation!=null&&!presentation.ControlsVisible;
            float previewTop=expanded&&hiddenPreviewSize==4?96:108;
            float availableWidth=(Screen.width-32)/fit;
            float availableHeight=(Screen.height-previewTop*fit-16)/fit-heading;
            float width,right;
            if(expanded){
                // Size 1/2 retain their original widths; Size 3 is up to 620 screen pixels.
                // Size 4 uses 800px at 1920 and 480px at 1280 to leave the robot visible.
                // Only Size 4 moves up below the toolbar; all sizes retain their right-edge policy.
                float limit=hiddenPreviewSize==4?Mathf.Lerp(480,800,Mathf.Clamp01((Screen.width-1280f)/640f)):620;
                float maximum=Mathf.Max(1,Mathf.Min(limit/fit,Mathf.Min(availableWidth,availableHeight*aspect)));
                width=Mathf.Min(hiddenPreviewSize==1?300:hiddenPreviewSize==2?375:maximum,maximum);
                right=1576-maximum+width;
            }else{
                width=Mathf.Min(Screen.width<1200?224:272,Mathf.Max(1,Mathf.Min(availableWidth,availableHeight*aspect)));
                bool sidebar=presentation!=null&&!presentation.WideCamera;
                right=sidebar?presentation.CameraDesignWidth-12:1576;
            }
            float imageHeight=width/aspect;
            preview.anchoredPosition=new Vector2(right-width,-previewTop);preview.localScale=Vector3.one;
            preview.sizeDelta=new Vector2(width,imageHeight+heading);
            captureViewport.anchoredPosition=new Vector2(0,-heading);captureViewport.sizeDelta=new Vector2(width,imageHeight);
            captureLabel.rectTransform.sizeDelta=new Vector2(width,20);
            previewImage.transform.localScale=Vector3.one*(width/272);
            if(capture!=null)live=capture.Render(captureImage,width,imageHeight);
            previewImage.SetActive(false);captureLabel.gameObject.SetActive(previewOpen&&live);
            if(live)captureLabel.text="LIVE CAPTURE / "+capture.Session.Device;
            previewLabel.text="Camera "+(previewOpen?"ON":"OFF");
        }
        void OnDestroy(){if(root!=null)Destroy(root);if(preview!=null)Destroy(preview.gameObject);if(sizeToggle!=null)Destroy(sizeToggle);if(sizeMenu!=null)Destroy(sizeMenu);if(font!=null)Destroy(font);}
    }
}
