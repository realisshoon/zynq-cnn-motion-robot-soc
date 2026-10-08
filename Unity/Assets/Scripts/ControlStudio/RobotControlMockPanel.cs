using UnityEngine;
using UnityEngine.UI;
using UnityEngine.InputSystem;
using UnityEngine.EventSystems;

namespace HumanMotion.ControlStudio
{
    // Presentation-only MVP: deliberately no router, serial, native or robot reference.
    [DefaultExecutionOrder(4500)]
    public sealed class RobotControlMockPanel : MonoBehaviour
    {
        readonly RobotControlMockState[] robots={new RobotControlMockState(),new RobotControlMockState()};
        readonly Color ink=new Color(.89f,.94f,.97f),muted=new Color(.57f,.69f,.75f),accent=new Color(.35f,.84f,.9f);
        readonly Slider[] margins=new Slider[3];readonly Text[] marginValues=new Text[3];
        readonly Button[] selections=new Button[3];
        ControlStudioPresentationUI presentation;ControlStudioStartupMenu startup;
        GameObject root,advanced,previewImage,sizeToggle,sizeMenu;RectTransform preview;
        readonly Button[] sizeChoices=new Button[3];Text sizeLabel;bool sizeMenuOpen;int hiddenPreviewSize=2;
        Font font;Text pwm,follow,notice,applied,advancedLabel,previewLabel;
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
        public bool CanPreviewInteract=>isActiveAndEnabled&&startup!=null&&!startup.Visible&&presentation!=null&&!presentation.PresentationMode;
        public GameObject PanelRoot=>root;
        public Slider[] MarginSliders=>margins;
        public bool CanInteract=>isActiveAndEnabled&&startup!=null&&!startup.Visible&&presentation!=null&&!presentation.PresentationMode&&presentation.ControlsVisible;
        public static void Attach(ControlStudioRuntimeUI owner,Transform design)
        {
            var panel=owner.gameObject.AddComponent<RobotControlMockPanel>();
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
            sizeMenu=Rect("Camera size choices",design,1370,54,206,32).gameObject;
            for(int i=0;i<3;i++){int choice=i+1;sizeChoices[i]=Button("Camera size "+choice,sizeMenu.transform,i*70,0,66,32,choice.ToString(),()=>{hiddenPreviewSize=choice;sizeMenuOpen=false;},true);}
            previewImage=Box("Placeholder / no capture",preview,0,0,272,153,new Color(.07f,.11f,.14f)).gameObject;
            for(int i=1;i<4;i++){Box("Grid V "+i,previewImage.transform,272*i/4f,0,1,153,new Color(.11f,.17f,.20f)).raycastTarget=false;Box("Grid H "+i,previewImage.transform,0,153*i/4f,272,1,new Color(.11f,.17f,.20f)).raycastTarget=false;}
            var placeholder=Label("Placeholder label",previewImage.transform,8,34,256,80,"MOCK PREVIEW\n영상 입력 없음",19,muted);placeholder.alignment=TextAnchor.MiddleCenter;
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
            previewLabel.text=previewOpen?"Camera / MOCK −":"Camera / MOCK +";previewImage.SetActive(previewOpen);
        }
        public void RefreshVisibility()
        {
            if(root!=null)root.SetActive(CanInteract);
            if(preview==null)return;
            preview.gameObject.SetActive(CanPreviewInteract&&previewOpen);
            bool sizes=CanPreviewInteract&&!presentation.ControlsVisible;
            if(!sizes)sizeMenuOpen=false;
            sizeToggle.SetActive(sizes);sizeMenu.SetActive(sizes&&sizeMenuOpen);
            sizeLabel.text="Size "+hiddenPreviewSize+" ▾";
            for(int i=0;i<3;i++)sizeChoices[i].GetComponent<Image>().color=i+1==hiddenPreviewSize?new Color(.2f,.43f,.49f):new Color(.13f,.23f,.29f);
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
            if(presentation!=null&&!presentation.ControlsVisible){
                // 360/450/540 px at 1920x1080; regular preview is already 326 px.
                // All sizes share the large preview's top-left origin: no position jump.
                float fit=Mathf.Max(.01f,Mathf.Min(Screen.width/1600f,Screen.height/900f));
                float maximum=Mathf.Min(450,Mathf.Min((Screen.width-32)/fit,(Screen.height-108*fit-16)/fit*16/9));
                maximum=Mathf.Max(1,maximum);
                float hiddenWidth=maximum*(hiddenPreviewSize==1?2f/3:hiddenPreviewSize==2?5f/6:1);
                preview.anchoredPosition=new Vector2(1576-maximum,-108);
                preview.localScale=Vector3.one*(hiddenWidth/272);return;
            }
            // Narrow screens reserve less of the robot viewport for the optional preview.
            float width=Screen.width<1200?224:272;
            bool sidebar=presentation!=null&&!presentation.WideCamera;
            float right=sidebar?presentation.CameraDesignWidth-12:1576;
            preview.anchoredPosition=new Vector2(right-width,-108);preview.localScale=Vector3.one*(width/272);
        }
        void OnDestroy(){if(root!=null)Destroy(root);if(preview!=null)Destroy(preview.gameObject);if(sizeToggle!=null)Destroy(sizeToggle);if(sizeMenu!=null)Destroy(sizeMenu);if(font!=null)Destroy(font);}
    }
}
