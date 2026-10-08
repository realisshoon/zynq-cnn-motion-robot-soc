using System.Globalization;
using System.IO;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem.UI;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioRuntimeUI : MonoBehaviour
    {
        public SingleArmCommandRouter router;
        public ManualServoSource manual;
        public ControlStudioProfileStore profiles;
        public ControlStudioOrbitCamera orbit;
        public Slider[] Sliders {get;}=new Slider[5];
        public InputField[] Inputs {get;}=new InputField[5];
        public Text[] Readings {get;}=new Text[5];
        Text status,preset,selected,pauseLabel,hardware,measurement;Font font;
        Text sourceLabel,csvTime,csvFlags,csvMessage,loopLabel;
        readonly Text[] humanReadings=new Text[5];
        readonly List<Button> manualButtons=new List<Button>();
        GameObject csvPanel,csvBackdrop;
        public bool CsvPanelVisible=>router!=null&&router.Source=="CSV"&&(GetComponent<ControlStudioPresentationUI>()?.ControlsVisible??true);
        public InputField CsvPath {get;private set;}
        public CsvInputMode PendingMode {get;set;}
        readonly List<Text> modeLabels=new List<Text>();
        readonly string[] names={"ELBOW ROLL","ELBOW PITCH","WRIST PITCH","WRIST ROLL","GRIPPER"};
        readonly Color ink=new Color(.87f,.91f,.95f), muted=new Color(.53f,.63f,.71f),accent=new Color(.25f,.91f,.65f);
        Transform canvas;
        RectTransform servoPanel;GameObject servoContents;Text servoHeader;
        public bool ServoDetailsExpanded {get;private set;}
        public void ToggleServoDetails()
        {
            ServoDetailsExpanded=!ServoDetailsExpanded;
            servoContents.SetActive(ServoDetailsExpanded);
            servoPanel.sizeDelta=new Vector2(424,574);
            servoHeader.text=ServoDetailsExpanded?"Servo −":"Servo +";
        }
        public Transform RuntimeCanvas=>canvas==null?null:canvas.parent;
        RectTransform Rect(string name,Transform parent,float x,float y,float w,float h)
        {
            var g=new GameObject(name,typeof(RectTransform));g.transform.SetParent(parent,false);
            var r=(RectTransform)g.transform;r.anchorMin=r.anchorMax=new Vector2(0,1);r.pivot=new Vector2(0,1);
            r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);return r;
        }
        Image Box(string name,Transform parent,float x,float y,float w,float h,Color color)
        {var r=Rect(name,parent,x,y,w,h);var image=r.gameObject.AddComponent<Image>();image.color=color;return image;}
        Text Label(string name,Transform parent,float x,float y,float w,float h,string value,int size,Color color)
        {var r=Rect(name,parent,x,y,w,h);var t=r.gameObject.AddComponent<Text>();t.font=font;t.fontSize=size;t.color=color;t.text=value;t.alignment=TextAnchor.MiddleLeft;t.raycastTarget=false;return t;}
        Button Button(string name,Transform parent,float x,float y,float w,float h,string text,UnityEngine.Events.UnityAction action)
        {
            var bg=Box(name,parent,x,y,w,h,new Color(.14f,.21f,.27f));var b=bg.gameObject.AddComponent<Button>();
            b.targetGraphic=bg;b.onClick.AddListener(action);var t=Label(name+" label",bg.transform,12,0,w-24,h,text,18,ink);t.alignment=TextAnchor.MiddleCenter;return b;
        }
        void Start()
        {
            font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            var g=new GameObject("Control Studio Runtime Canvas",typeof(Canvas),typeof(CanvasScaler),typeof(GraphicRaycaster));
            g.transform.SetParent(transform,false);var c=g.GetComponent<Canvas>();c.renderMode=RenderMode.ScreenSpaceOverlay;
            var scaler=g.GetComponent<CanvasScaler>();scaler.uiScaleMode=CanvasScaler.ScaleMode.ScaleWithScreenSize;scaler.referenceResolution=new Vector2(1600,900);scaler.screenMatchMode=CanvasScaler.ScreenMatchMode.Expand;
            var design=Rect("Studio design area",g.transform,0,0,1600,900);design.anchorMin=design.anchorMax=design.pivot=new Vector2(.5f,.5f);design.anchoredPosition=Vector2.zero;canvas=design;
            if(EventSystem.current==null){var es=new GameObject("Studio EventSystem",typeof(EventSystem),typeof(InputSystemUIInputModule));es.transform.SetParent(transform,false);es.GetComponent<InputSystemUIInputModule>().AssignDefaultActions();}
            Box("Header",canvas,0,0,1600,96,new Color(.035f,.055f,.075f));
            Label("Title",canvas,28,14,830,40,"CONTROL STUDIO  /  DIGITAL TWIN",25,ink);
            sourceLabel=Label("Subtitle",canvas,28,57,910,24,"MANUAL / VIRTUAL ONLY",16,accent);
            hardware=Label("Hardware",canvas,960,12,610,37,"UART: DISCONNECTED  |  REAL TX = 0",18,muted);
            servoPanel=Box("Manual panel",canvas,1152,108,424,574,new Color(.065f,.095f,.125f)).rectTransform;
            var header=Button("Servo details toggle",canvas,1170,54,190,32,"Servo +",ToggleServoDetails);
            servoHeader=header.GetComponentInChildren<Text>();
            var side=Rect("Servo contents",servoPanel,0,0,424,574);servoContents=side.gameObject;
            for(int i=0;i<5;i++)
            {
                int a=i;float y=6+i*90;
                var row=Box("M"+i+" row",side,12,y,400,84,new Color(.085f,.125f,.165f)).transform;
                Button("Select M"+i,row,8,4,220,26,"M"+i+"  "+names[i],()=>{orbit.selectedAxis=a;});
                Label("Range",row,238,4,154,26,$"{router.Min[i]:0.##} - {router.Max[i]:0.##} "+(i==4?"normalized":"deg"),13,muted);
                var sr=Rect("M"+i+" Slider",row,14,34,260,24);var slider=sr.gameObject.AddComponent<Slider>();
                var track=Box("Track",sr,0,9,260,6,new Color(.2f,.29f,.36f));track.raycastTarget=true;
                var fillArea=Rect("Fill area",sr,0,9,260,6);
                var fill=Box("Fill",fillArea,0,0,0,0,accent);slider.fillRect=fill.rectTransform;
                var area=Rect("Handle area",sr,0,0,260,24);var knob=Box("Handle",area,0,0,18,24,ink);
                knob.rectTransform.pivot=new Vector2(.5f,.5f);knob.rectTransform.sizeDelta=new Vector2(18,0);
                slider.handleRect=knob.rectTransform;slider.targetGraphic=knob;slider.direction=Slider.Direction.LeftToRight;
                slider.minValue=router.Min[i];slider.maxValue=router.Max[i];slider.SetValueWithoutNotify(manual.Values[i]);
                slider.onValueChanged.AddListener(v=>{manual.Set(a,v);Sync();});Sliders[i]=slider;
                var bg=Box("M"+i+" Number",row,286,32,100,30,new Color(.025f,.045f,.065f));
                var input=bg.gameObject.AddComponent<InputField>();var text=Label("Value",bg.transform,8,0,84,30,"",19,ink);
                input.textComponent=text;input.targetGraphic=bg;input.contentType=InputField.ContentType.Standard;input.characterLimit=24;
                input.SetTextWithoutNotify(manual.Values[i].ToString("0.###",CultureInfo.InvariantCulture));
                input.onEndEdit.AddListener(v=>{manual.SetText(a,v);Sync();});Inputs[i]=input;
                humanReadings[i]=Label("Human and wrapped",row,14,32,372,28,"",13,accent);
                Readings[i]=Label("M"+i+" requested approved applied",row,14,62,372,20,"",12,muted);
            }
            manualButtons.Add(Button("Home",side,12,466,194,32,"Home",()=>{manual.Home();Sync();}));
            manualButtons.Add(Button("Save preset",side,218,466,194,32,"Save preset",()=>profiles.Save()));
            manualButtons.Add(Button("Load preset",side,12,506,194,32,"Load preset",()=>{profiles.Load();Sync();}));
            manualButtons.Add(Button("Default preset",side,218,506,194,32,"Default preset",()=>{profiles.Restore();Sync();}));
            preset=Label("Preset status",side,12,542,400,28,"",13,muted);
            servoContents.SetActive(false);
            Button("Reset View",canvas,30,115,170,42,"Reset View",()=>orbit.ResetView());
            var pause=Button("Pause virtual",canvas,215,115,195,42,"Pause virtual",()=>router.SetPaused(!router.Paused));pauseLabel=pause.GetComponentInChildren<Text>();
            // Cover pixels outside the reduced CSV camera viewport, including label backgrounds.
            csvBackdrop=Box("CSV lower backdrop",canvas,960,96,640,714,new Color(.035f,.055f,.075f)).gameObject;
            selected=Label("Selected joint",canvas,30,731,900,35,"",19,accent);
            Label("Camera help",canvas,30,766,890,28,"Right-drag: orbit   |   Middle-drag: pan   |   Wheel: zoom",17,muted);
            Box("Footer",canvas,0,810,1600,90,new Color(.035f,.055f,.075f));
            status=Label("Policy status",canvas,28,850,1540,25,"",17,ink);
            measurement=Label("Measurement note",canvas,28,876,1540,20,"XYZ uses processed relative coordinates + stored BodyFrame. UART uses provisional XYZ input; TX remains separate and OFF.",13,muted);
            BuildCsvPanel();
            Tool1RuntimeUI.Attach(this,canvas);
            RobotVisualProfiles.Attach(this,canvas);
            ControlStudioUartPanel.Attach(this,canvas);
            ControlStudioPresentationUI.Attach(this,canvas,g);
            ControlStudioStartupMenu.Attach(this,canvas);
            RobotControlMockPanel.Attach(this,canvas);
            HumanoidServoPreviewPanel.Attach(this,servoPanel,servoContents);
            ControlStudioHeaderLayout.Attach(this,canvas);
        }
        void BuildCsvPanel()
        {
            var panel=Box("CSV recorded human controls",canvas,960,96,640,714,new Color(.065f,.095f,.125f)).transform;csvPanel=panel.gameObject;
            Label("CSV title",panel,20,8,600,40,"CSV / XYZ INPUT",22,ink);
            Button("CNN sample",panel,20,55,110,32,"CNN",()=>SetSample("cnn"));
            Button("MediaPipe sample",panel,140,55,145,32,"MediaPipe",()=>SetSample("mediapipe"));
            for(int i=0;i<3;i++){int mode=i;var b=Button("CSV mode "+i,panel,20+i*202,100,194,32,"",()=>PendingMode=(CsvInputMode)mode);modeLabels.Add(b.GetComponentInChildren<Text>());}
            var bg=Box("CSV path",panel,20,145,600,42,new Color(.025f,.045f,.065f));
            CsvPath=bg.gameObject.AddComponent<InputField>();CsvPath.textComponent=Label("Path value",bg.transform,8,0,584,42,"",15,ink);CsvPath.targetGraphic=bg;
            CsvPath.characterLimit=2048;SetSample("cnn");
            Button("Load CSV",panel,20,200,600,38,"Load CSV",()=>router.Csv.Load(CsvPath.text,PendingMode));
            Button("CSV Play Resume",panel,20,250,145,38,"Play / Resume",()=>router.Csv.Play());
            Button("CSV Pause",panel,175,250,110,38,"Pause",()=>router.Csv.Pause());
            Button("CSV Step",panel,295,250,110,38,"Step",()=>router.Csv.Step());
            Button("CSV Restart",panel,415,250,205,38,"Restart",()=>router.Csv.Restart());
            var loop=Button("CSV Loop",panel,20,300,160,38,"Loop OFF",()=>router.Csv.Loop=!router.Csv.Loop);loopLabel=loop.GetComponentInChildren<Text>();
            var seek=Button("Seek unsupported",panel,190,300,430,38,"Seek: unavailable",()=>{});seek.interactable=false;
            csvTime=Label("CSV clock",panel,20,352,600,50,"",16,ink);
            csvFlags=Label("CSV flags",panel,20,410,600,96,"",14,accent);
            csvMessage=Label("CSV message",panel,20,518,600,120,"",13,muted);
            Label("CSV input note",panel,20,644,600,52,"Recorded / XYZ A / XYZ B use the existing Agent2 approval and HOLD policy.",14,muted);
        }
        public void SetSample(string name)=>CsvPath.SetTextWithoutNotify(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples",name+"_agent1_result.csv"));
        // Navigation only: the existing sources retain command ownership and initialization.
        public bool SelectStartupInput(int input,out string message)
        {
            message="";bool ok;
            if(input==0){ok=router.Source=="MANUAL"||router.Csv.SelectManual();if(ok){GetComponent<ControlStudioPresentationUI>()?.OpenServoControls();Sync();}}
            else if(input==1){ok=router.Csv.Active||router.Csv.Load(router.Csv.Data?.Path??CsvPath.text,router.Csv.Data==null?PendingMode:router.Csv.Mode);if(ok){GetComponent<ControlStudioPresentationUI>()?.OpenSourceControls();}else message=router.Csv.Message;}
            else if(input==2){var rx=GetComponent<UartPose3DSource>();ok=router.Source=="UART"||(rx!=null&&rx.SelectSource(rx.Mode));if(ok){GetComponent<ControlStudioPresentationUI>()?.OpenUartControls();}else message="UART unavailable; open the existing source controls.";}
            else {message="Unknown input";return false;}
            if(!ok&&message.Length==0)message=router.NativeError.Length>0?router.NativeError:router.Status;
            return ok;
        }
        public void Sync()
        {
            for(int i=0;i<5;i++){Sliders[i]?.SetValueWithoutNotify(manual.Values[i]);Inputs[i]?.SetTextWithoutNotify(manual.Values[i].ToString("0.###",CultureInfo.InvariantCulture));}
        }
        void Update()
        {
            if(status==null||router==null||sourceLabel==null||csvPanel==null||csvBackdrop==null)return;
            for(int i=0;i<5;i++)
                if(Readings[i]==null||humanReadings[i]==null||Sliders[i]==null||Inputs[i]==null)return;
            bool csv=router.Source=="CSV", uartInput=router.Source=="UART", observed=csv||uartInput;
            var visualSettings=GetComponent<RobotVisualProfiles>();var toolSettings=GetComponent<Tool1Runtime>();
            sourceLabel.text=(visualSettings?.Selected==RobotVisualProfileId.HumanoidRobot?"HUMANOID ROBOT":"ROBOT ARM")+"  /  "+router.Source+(csv?" "+(router.Csv.Mode==CsvInputMode.RecordedHumanAngles?"Recorded":router.Csv.Mode==CsvInputMode.XyzStoredBodyAuxGripper?"XYZ-A":"XYZ-B"):uartInput?"":"")+"  /  "+toolSettings?.Gate.Tool+"  /  "+(visualSettings?.EnvironmentMode==EnvironmentViewMode.RobotOnly?"ROBOT ONLY":visualSettings?.EnvironmentMode==EnvironmentViewMode.Workcell?"WORKCELL":"AUTO")+"  /  VIRTUAL";
            for(int i=0;i<modeLabels.Count;i++)modeLabels[i].text=((int)PendingMode==i?"[ ":"")+new[]{"Recorded","XYZ A: CSV M4","XYZ B: HOLD M4"}[i]+((int)PendingMode==i?" ]":"");
            csvPanel.SetActive(CsvPanelVisible);csvBackdrop.SetActive(CsvPanelVisible);
            for(int i=0;i<5;i++)
            {
                string applied=router.HasApplied?router.Applied[i].ToString("F2",CultureInfo.InvariantCulture):"--";
                string approved=router.HasApproved?router.Approved[i].ToString("F2",CultureInfo.InvariantCulture):"--";
                string candidate=router.Candidate==null||i>=router.Candidate.Length?"--":router.Candidate[i].ToString("F2",CultureInfo.InvariantCulture);
                Readings[i].text=observed?$"Candidate {candidate}   Approved {approved}   Applied {applied}":$"Requested {router.Requested[i]:F2}   Approved {approved}   Applied {applied}";
                humanReadings[i].gameObject.SetActive(observed);
                humanReadings[i].text=router.Human==null?"Human -- / Wrapped --":$"Human {router.Human[i]:F2}   Wrapped {router.Wrapped[i]:F2}";
                Sliders[i].gameObject.SetActive(!observed);Inputs[i].gameObject.SetActive(!observed);
                Readings[i].color=string.IsNullOrEmpty(manual.Errors[i])?muted:new Color(1f,.55f,.35f);
                Sliders[i].interactable=Inputs[i].interactable=router.Ready&&!observed;
            }
            foreach(var button in manualButtons)button.interactable=router.Ready&&!observed;
            var presentation=GetComponent<ControlStudioPresentationUI>();
            status.text=presentation!=null&&!presentation.DebugExpanded
                ?$"{router.Source}  |  {(router.Ready?"READY":"UNAVAILABLE")}  |  {(router.Paused?"PAUSED":"RUNNING")}  |  Applied M0-M4"
                :$"C {(router.Ready?"READY":"UNAVAILABLE")} | {router.Status} | {(router.Paused?router.PauseReason:"RUN")} | motionHold {router.MotionHold} | tick {router.TickCount} | stalls {router.TimingStalls}";
            status.fontSize=presentation!=null&&!presentation.DebugExpanded?17:14;
            var uart=GetComponent<ControlStudioUartOutput>();var rx=GetComponent<UartPose3DSource>();
            hardware.text=$"RX: {(rx!=null&&rx.Connected?(rx.Mock?"MOCK":"COM"):"OFF")}  |  TX: {(uart!=null&&uart.Connected?(uart.TxEnabled?"ON":"OFF"):"OFF")}  |  REAL TX = {uart?.HardwareTxCount??0}";
            measurement.gameObject.SetActive(presentation==null||presentation.DebugExpanded);
            var source=router.Csv;var row=source.Current;
            csvTime.text=source.Data==null?"No CSV loaded":$"{source.Timeline.Time:F3} / {source.Data.Duration:F3}s  row {source.ConsumedRows}/{source.Data.Rows.Count}  frame {(row==null?"--":row.FrameId.ToString())}  target {(row==null?"--":row.TargetFrameId.ToString())}";
            csvFlags.text=row==null?"Flags: no observation":$"target {row.Flags[0]} / major {row.Flags[1]} / finger {row.Flags[2]} / observable {row.Flags[3]}\nhand_fresh {row.Flags[4]} / wrist_valid {row.Flags[5]} / gripper_hold {row.Flags[6]} / body {row.Flags[7]}";
            if(source.Mode!=CsvInputMode.RecordedHumanAngles){var solve=source.LastSolve;csvFlags.text="Source: "+csvFlags.text+"\nSolver: "+(solve==null?"--":solve.State+" valid="+solve.SolverInfo[0]+" wrist="+solve.SolverInfo[1]+" fresh="+solve.SolverInfo[2])+" | point quality UNKNOWN";csvFlags.fontSize=12;}else csvFlags.fontSize=14;
            csvMessage.text=source.Message+(source.Data==null?"":"\n"+Path.GetFileName(source.Data.Path)+"  SHA256 "+source.Data.Sha256.Substring(0,16));
            loopLabel.text=source.Loop?"Loop ON":"Loop OFF";
            preset.text=profiles.Message;pauseLabel.text=router.Paused?"Resume virtual":"Pause virtual";
            selected.text=$"SELECTED M{orbit.selectedAxis}  /  {names[orbit.selectedAxis]}  /  logical axis highlighted";
        }
    }
}
