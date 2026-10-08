using System.Globalization;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // UI adapter only. Right arm edits still enter ManualServoSource / Agent2 / router.
    [DefaultExecutionOrder(4600)]
    public sealed class HumanoidServoPreviewPanel : MonoBehaviour
    {
        ControlStudioRuntimeUI studio;RobotVisualProfiles visuals;RobotControlMockPanel mock;
        RectTransform panel,content,shoulderRows,armRows;GameObject original;
        Text target,notice,shoulderTitle,armTitle;Font font;
        bool shoulderOpen,armOpen,syncing;
        public Slider[] ShoulderSliders {get;}=new Slider[2];
        public Slider[] ArmSliders {get;}=new Slider[5];
        readonly Text[] readings=new Text[7];readonly InputField[] inputs=new InputField[7];
        public bool ShoulderOpen=>shoulderOpen;
        public bool ArmOpen=>armOpen;
        public string Message {get;private set;}="SHOULDER + LEFT: VISUAL ONLY / PREVIEW";
        public bool Active=>visuals!=null&&visuals.Selected==RobotVisualProfileId.HumanoidRobot;
        bool Editable=>Active&&studio.router.Ready&&studio.router.Source=="MANUAL"&&studio.GetComponent<ControlStudioStartupMenu>()?.Visible!=true;
        int Selection=>mock.SelectedRobot;
        HumanoidPreviewControl Preview=>visuals.Preview;
        public static void Attach(ControlStudioRuntimeUI owner,RectTransform parent,GameObject oldContents)
        {
            var self=owner.gameObject.AddComponent<HumanoidServoPreviewPanel>();self.studio=owner;self.panel=parent;self.original=oldContents;
            self.visuals=owner.GetComponent<RobotVisualProfiles>();self.mock=owner.GetComponent<RobotControlMockPanel>();self.Build();
        }
        RectTransform Rect(string name,Transform parent,float x,float y,float w,float h)
        {
            var g=new GameObject(name,typeof(RectTransform));var r=(RectTransform)g.transform;r.SetParent(parent,false);
            r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);return r;
        }
        Text Label(string name,Transform parent,float x,float y,float w,float h,string value,int size=15)
        {
            var r=Rect(name,parent,x,y,w,h);var t=r.gameObject.AddComponent<Text>();t.font=font;t.fontSize=size;t.color=new Color(.86f,.93f,.96f);
            t.text=value;t.alignment=TextAnchor.MiddleLeft;t.raycastTarget=false;return t;
        }
        Button Button(string name,Transform parent,float x,float y,float w,float h,string value,UnityEngine.Events.UnityAction action)
        {
            var r=Rect(name,parent,x,y,w,h);var image=r.gameObject.AddComponent<Image>();image.color=new Color(.14f,.23f,.30f);
            var b=r.gameObject.AddComponent<Button>();b.targetGraphic=image;b.onClick.AddListener(action);
            var text=Label("Label",r,4,0,w-8,h,value);text.alignment=TextAnchor.MiddleCenter;return b;
        }
        void Build()
        {
            font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");content=Rect("Humanoid servo preview",panel,0,0,424,170);
            target=Label("Preview target",content,12,6,400,46,"",14);
            var shoulder=Button("Shoulder section toggle",content,12,56,400,30,"SHOULDER +",()=>shoulderOpen=!shoulderOpen);shoulderTitle=shoulder.GetComponentInChildren<Text>();
            shoulderRows=Rect("Shoulder preview rows",content,12,92,400,104);
            for(int i=0;i<2;i++)ShoulderSliders[i]=Row(shoulderRows,i,i==0?"Shoulder Pitch / X":"Shoulder Roll / Z",i);
            var arm=Button("Arm joints section toggle",content,12,96,400,30,"ARM JOINTS +",()=>armOpen=!armOpen);armTitle=arm.GetComponentInChildren<Text>();
            armRows=Rect("Arm preview rows",content,12,132,400,354);
            string[] names={"M0 Elbow Roll","M1 Elbow Pitch","M2 Wrist Pitch","M3 Wrist Roll","M4 Gripper"};
            for(int i=0;i<5;i++)ArmSliders[i]=Row(armRows,i+2,names[i],i);
            Label("Preset scope",armRows,4,261,392,24,"Existing presets: RIGHT M0-M4 only",13);
            Button("Humanoid right Home",armRows,4,289,92,28,"R Home",()=>studio.manual.Home());
            Button("Humanoid right Save",armRows,104,289,92,28,"R Save",()=>studio.profiles.Save());
            Button("Humanoid right Load",armRows,204,289,92,28,"R Load",()=>studio.profiles.Load());
            Button("Humanoid right Default",armRows,304,289,92,28,"R Default",()=>studio.profiles.Restore());
            Label("Preset note",armRows,4,323,392,28,"Shoulders / LEFT retain session values",12);
            notice=Label("Preview scope",content,12,136,400,48,"",12);LateUpdate();
        }
        Slider Row(RectTransform parent,int id,string name,int row)
        {
            float y=row*52;
            readings[id]=Label(name+" values",parent,4,y,390,22,"",13);
            var r=Rect(name+" preview slider",parent,8,y+24,280,20);var slider=r.gameObject.AddComponent<Slider>();
            var track=Rect("Track",r,0,7,280,6).gameObject.AddComponent<Image>();track.color=new Color(.22f,.30f,.34f);
            var area=Rect("Handle area",r,0,0,280,20);var handle=Rect("Handle",area,0,0,14,20).gameObject.AddComponent<Image>();
            handle.color=new Color(.3f,.86f,.81f);handle.rectTransform.pivot=new Vector2(.5f,.5f);handle.rectTransform.sizeDelta=new Vector2(14,0);
            slider.handleRect=handle.rectTransform;slider.targetGraphic=handle;
            slider.onValueChanged.AddListener(v=>Edit(id,v));
            var box=Rect(name+" preview number",parent,302,y+23,94,25).gameObject.AddComponent<Image>();box.color=new Color(.025f,.045f,.065f);
            var input=box.gameObject.AddComponent<InputField>();input.targetGraphic=box;input.textComponent=Label("Number",box.transform,5,0,84,25,"",15);input.characterLimit=12;
            input.onEndEdit.AddListener(s=>{if(float.TryParse(s,NumberStyles.Float,CultureInfo.InvariantCulture,out var value))Edit(id,value);else Message="Finite value required";});inputs[id]=input;
            return slider;
        }
        public bool Edit(int id,float value)
        {
            if(syncing||!Editable||float.IsNaN(value)||float.IsInfinity(value))return false;
            if(id<2){bool ok=Preview.SetShoulder(Selection,id,value);Message=ok?"Shoulder: VISUAL ONLY / no servo channel":"Shoulder rejected";return ok;}
            int axis=id-2;if(axis<0||axis>4)return false;
            if(Selection!=0&&(value<HumanoidPreviewControl.LeftMin[axis]||value>HumanoidPreviewControl.LeftMax[axis])){Message="Outside LEFT preview envelope";return false;}
            bool right=Selection==1||studio.manual.Set(axis,value);
            bool left=Selection==0||Preview.SetLeft(axis,value);
            Message=$"R {(Selection==1?"unchanged":right?"Agent2 target":"REJECTED")} / L {(Selection==0?"unchanged":left?"PREVIEW":"REJECTED")}";
            return right&&left;
        }
        void LateUpdate()
        {
            if(content==null)return;
            bool show=Active&&studio.ServoDetailsExpanded;content.gameObject.SetActive(show);original.SetActive(!Active&&studio.ServoDetailsExpanded);
            if(!show){if(!Active)panel.sizeDelta=new Vector2(424,574);return;}
            target.text="TARGET "+(Selection==0?"RIGHT":Selection==1?"LEFT":"BOTH · edit applies each side")+"\nR: Agent2 arm / L + shoulders: VISUAL ONLY";
            shoulderTitle.text=shoulderOpen?"SHOULDER −":"SHOULDER +";armTitle.text=armOpen?"ARM JOINTS −":"ARM JOINTS +";
            shoulderRows.gameObject.SetActive(shoulderOpen);armRows.gameObject.SetActive(armOpen);
            float armY=shoulderOpen?202:96;((RectTransform)armTitle.transform.parent).anchoredPosition=new Vector2(12,-armY);
            armRows.anchoredPosition=new Vector2(12,-armY-36);float noteY=armY+40+(armOpen?354:0);
            notice.rectTransform.anchoredPosition=new Vector2(12,-noteY);panel.sizeDelta=new Vector2(424,noteY+56);content.sizeDelta=panel.sizeDelta;
            notice.text=Message+"\nPreview limits only; physical shoulder limits UNKNOWN";
            if(studio.router.Paused)notice.text="PAUSED · preview targets retained\n"+Message;
            syncing=true;
            for(int id=0;id<7;id++){
                int axis=id-2;var slider=id<2?ShoulderSliders[id]:ArmSliders[axis];
                float right=id<2?Preview.Shoulder(0,id):studio.manual.Values[axis],left=id<2?Preview.Shoulder(1,id):Preview.LeftValue(axis);
                slider.minValue=id<2?(id==0?HumanoidPreviewControl.PitchMin:HumanoidPreviewControl.RollMin):Selection==0?studio.router.Min[axis]:HumanoidPreviewControl.LeftMin[axis];
                slider.maxValue=id<2?(id==0?HumanoidPreviewControl.PitchMax:HumanoidPreviewControl.RollMax):Selection==0?studio.router.Max[axis]:HumanoidPreviewControl.LeftMax[axis];
                slider.SetValueWithoutNotify(Selection==1?left:right);slider.interactable=Editable;inputs[id].interactable=Editable;
                if(!inputs[id].isFocused)inputs[id].SetTextWithoutNotify((Selection==1?left:right).ToString("0.##",CultureInfo.InvariantCulture));
                string label=id==0?"Pitch X":id==1?"Roll Z":new[]{"M0 Elbow roll","M1 Elbow pitch","M2 Wrist pitch","M3 Wrist roll","M4 Gripper"}[axis];
                readings[id].text=$"{label}   R {right:0.##} / L {left:0.##}"+(id>=2?$"  R applied {studio.router.Applied[axis]:0.##}":"");
            }
            syncing=false;
        }
    }
}
