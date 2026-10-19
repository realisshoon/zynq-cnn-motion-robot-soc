using UnityEngine;
using UnityEngine.UI;
namespace HumanMotion.ControlStudio
{
    public sealed class Tool1RuntimeUI : MonoBehaviour
    {
        Tool1Runtime tool;ControlStudioRuntimeUI studio;Transform canvas;GameObject panel,joints;Font font;
        public bool ShowingToolPanel=>visible;
        public void ShowToolPanel(bool show){visible=show;}
        Text detail,enableLabel,summary,brushLabel,intensityLabel;Slider manual,brush,weldWidth,fastenerSize,intensity;GameObject[] paintButtons;Button clear,place;bool visible;Button[] offsets;
        static readonly Color Ink=new Color(.9f,.94f,.98f),Bg=new Color(.055f,.085f,.12f);
        public static void Attach(ControlStudioRuntimeUI studio,Transform canvas)
        {
            var runtime=studio.gameObject.AddComponent<Tool1Runtime>();runtime.Initialize(studio.router);
            var ui=studio.gameObject.AddComponent<Tool1RuntimeUI>();ui.studio=studio;ui.tool=runtime;ui.canvas=canvas;ui.Build();
        }
        void Choose(ToolKind kind)
        {var presentation=GetComponent<RobotVisualProfiles>();if(presentation!=null)presentation.SelectTool(kind);else tool.Select(kind);}
        RectTransform Rect(string name,Transform parent,float x,float y,float w,float h)
        {var g=new GameObject(name,typeof(RectTransform));g.transform.SetParent(parent,false);var r=(RectTransform)g.transform;r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);return r;}
        Text Label(Transform p,float x,float y,float w,float h,string s,int size=16)
        {var r=Rect(s,p,x,y,w,h);var t=r.gameObject.AddComponent<Text>();t.font=font;t.fontSize=size;t.color=Ink;t.text=s;t.alignment=TextAnchor.MiddleLeft;t.raycastTarget=false;return t;}
        Button Button(Transform p,float x,float y,float w,string s,UnityEngine.Events.UnityAction a)
        {var r=Rect(s,p,x,y,w,32);var im=r.gameObject.AddComponent<Image>();im.color=new Color(.15f,.23f,.3f);var b=r.gameObject.AddComponent<Button>();b.targetGraphic=im;b.onClick.AddListener(a);Label(r,8,0,w-16,32,s,15).alignment=TextAnchor.MiddleCenter;return b;}
        Slider Slider(Transform p,float x,float y,float w,float min,float max,UnityEngine.Events.UnityAction<float> action)
        {var r=Rect("Tool setting slider",p,x,y,w,28);var s=r.gameObject.AddComponent<Slider>();var track=Rect("Track",r,0,10,w,6).gameObject.AddComponent<Image>();track.color=Color.gray;var h=Rect("Handle area",r,8,0,w-16,28);var knob=Rect("Handle",h,0,0,18,28).gameObject.AddComponent<Image>();knob.color=new Color(.2f,.9f,.7f);knob.rectTransform.pivot=new Vector2(.5f,.5f);knob.rectTransform.sizeDelta=new Vector2(18,0);s.handleRect=knob.rectTransform;s.targetGraphic=knob;s.minValue=min;s.maxValue=max;s.onValueChanged.AddListener(action);return s;}
        void Build()
        {
            font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");joints=canvas.Find("Manual panel").gameObject;
            Button(canvas,1390,58,180,"TOOLS / JOINTS",()=>{var presentation=GetComponent<ControlStudioPresentationUI>();if(presentation!=null)presentation.ToggleToolControls();else visible=!visible;});
            var bg=Rect("TOOL-1 sidebar",canvas,960,96,640,714).gameObject.AddComponent<Image>();bg.color=Bg;panel=bg.gameObject;
            Label(bg.transform,24,10,590,35,"TOOL-1  /  VIRTUAL EFFECTS",24);
            Button(bg.transform,20,55,140,"Gripper",()=>Choose(ToolKind.Gripper));Button(bg.transform,172,55,140,"Spray",()=>Choose(ToolKind.Spray));Button(bg.transform,324,55,140,"Welding",()=>Choose(ToolKind.Welding));Button(bg.transform,476,55,140,"Nailing",()=>Choose(ToolKind.Nailing));
            enableLabel=Button(bg.transform,20,99,594,"Gesture effects enable -- Virtual only",()=>tool.SetEnabled(!tool.Gate.Enabled)).GetComponentInChildren<Text>();
            detail=Label(bg.transform,24,143,590,148,"",15);
            Label(bg.transform,24,290,590,24,"Open once to arm; close to run; open to stop.",17);
            Label(bg.transform,24,321,590,22,"Manual M4 preview: 0=CLOSE / 1=OPEN (not sensor)",16);
            manual=Slider(bg.transform,24,348,590,0,1,v=>{studio.manual.Set(4,v);studio.Sync();});
            paintButtons=new[]{Button(bg.transform,24,385,112,"Paint blue",()=>tool.PaintColor=new Color(.08f,.42f,.94f)).gameObject,Button(bg.transform,145,385,112,"Paint red",()=>tool.PaintColor=new Color(.9f,.16f,.1f)).gameObject,Button(bg.transform,266,385,112,"Paint green",()=>tool.PaintColor=new Color(.12f,.72f,.28f)).gameObject};
            clear=Button(bg.transform,390,385,220,"Clear Paint",()=>{if(tool.Gate.Tool==ToolKind.Welding)tool.WeldSurface?.Clear();else if(tool.Gate.Tool==ToolKind.Nailing)tool.FastenSurface?.Clear();else tool.Surface?.Clear();});
            brushLabel=Label(bg.transform,24,425,230,25,"Spray radius .01 - .06",15);brush=Slider(bg.transform,263,425,345,.01f,.06f,v=>tool.SprayRadius=v);
            weldWidth=Slider(bg.transform,263,425,345,.001f,.012f,v=>tool.WeldWidth=v);
            fastenerSize=Slider(bg.transform,263,425,345,.005f,.018f,v=>tool.FastenerSize=v);
            intensityLabel=Label(bg.transform,24,463,230,25,"Virtual intensity (not amps)",15);intensity=Slider(bg.transform,263,463,345,0,1,v=>tool.EffectIntensity=v);
            place=Button(bg.transform,24,505,590,"Place white panel ahead (effect OFF only)",()=>tool.PlaceSurface());
            Label(bg.transform,24,546,590,25,"Socket offset +/- .01, max .10 (effect OFF only)",15);
            offsets=new Button[7];for(int i=0;i<6;i++){int a=i;offsets[i]=Button(bg.transform,24+i*82,580,74,new[]{"X-","X+","Y-","Y+","Z-","Z+"}[i],()=>{var d=Vector3.zero;d[a/2]=(a%2==0?-.01f:.01f);tool.ChangeOffset(d);});}
            offsets[6]=Button(bg.transform,524,580,88,"Reset",()=>tool.ChangeOffset(Vector3.zero,true));
            Label(bg.transform,24,627,590,67,"Red/Green = fingertip tracking, NOT tool choice.\nNo live marker input. Pinch, not fist detection.\nJoint HOLD and Tool OFF are separate; UART state is shown above.",15);
            var strip=Rect("Tool status strip",canvas,25,168,910,48).gameObject.AddComponent<Image>();strip.color=new Color(.035f,.055f,.075f,.93f);strip.raycastTarget=false;
            summary=Label(strip.transform,10,0,890,48,"",15);
            // Load failure also disarms effects even when the previous joint owner is kept.
            foreach(var b in canvas.GetComponentsInChildren<Button>(true))if(b.name=="Load CSV"||b.name=="CSV Restart")b.onClick.AddListener(()=>tool.Invalidate("LOAD / RESTART: OPEN REQUIRED"));
            panel.SetActive(false);
        }
        void Update()
        {
            if(tool==null||panel==null)return;
            var presentation=GetComponent<ControlStudioPresentationUI>();
            panel.SetActive(visible&&(presentation==null||presentation.ToolExpanded));
            var uart=GetComponent<ControlStudioUartPanel>();
            joints.SetActive(!visible&&(uart==null||!uart.Visible)&&(presentation==null||presentation.ServoExpanded));
            var g=tool.Gate;string age=double.IsInfinity(g.Age(tool.Now))?"--":(g.Age(tool.Now)*1000).ToString("F0");
            summary.text=$"TOOL {g.Tool} / {g.State} / {g.Reason}\nGesture {g.Gesture.ToString().ToUpperInvariant()} | Effects {(g.Enabled?"enabled":"disabled")} | Open once, then close to run";
            enableLabel.text="Gesture effects "+(g.Enabled?"ENABLED":"DISABLED")+" -- Virtual only";
            bool weld=g.Tool==ToolKind.Welding,nail=g.Tool==ToolKind.Nailing;var effect=tool.Visual==null?null:tool.Visual.welding;var fastening=tool.Visual==null?null:tool.Visual.nailing;
            string effectText=weld?$"Weld request {(g.Running?"ON":"OFF")} / Arc {(effect!=null&&effect.ArcOn?"ON":"OFF")} / {(effect==null?"NO_WORK_SURFACE":effect.SurfaceStatus)}":nail?$"Fasten request {(g.Running?"ON":"OFF")} / Impact {(fastening!=null&&fastening.ImpactOn?"ON":"OFF")} / {(fastening==null?"NO_WORK_SURFACE":fastening.SurfaceStatus)}":$"Brush {tool.SprayRadius:F3} | Offset {tool.Offset:F2}";
            detail.text=$"{tool.Provenance}\n{g.Gesture.ToString().ToUpperInvariant()} / {g.State}: {g.Reason}\nnorm {tool.ObservedNorm:F3} / observation {tool.ObservationId} / age {age} ms\n{tool.Freshness}\nTool epoch {tool.ToolEpoch} / Source epoch {studio.router.Epoch}\n{effectText}";
            manual.interactable=studio.router.Source=="MANUAL"&&studio.router.Ready;manual.SetValueWithoutNotify(studio.manual.Values[4]);
            foreach(var b in paintButtons)b.SetActive(!weld&&!nail);
            clear.GetComponentInChildren<Text>().text=weld?"Clear Weld":nail?"Clear Fasteners":"Clear Paint";
            place.GetComponentInChildren<Text>().text=weld?"Place WeldableSurface ahead (request OFF only)":nail?"Place FastenableSurface ahead (request OFF only)":"Place white panel ahead (effect OFF only)";place.interactable=!g.Running;
            brushLabel.text=weld?$"Weld width {tool.WeldWidth:F3}":nail?$"Fastener mark {tool.FastenerSize:F3}":$"Spray radius {tool.SprayRadius:F3}";
            brush.gameObject.SetActive(!weld&&!nail);weldWidth.gameObject.SetActive(weld);fastenerSize.gameObject.SetActive(nail);
            brush.SetValueWithoutNotify(tool.SprayRadius);weldWidth.SetValueWithoutNotify(tool.WeldWidth);fastenerSize.SetValueWithoutNotify(tool.FastenerSize);
            intensity.gameObject.SetActive(weld||nail);intensityLabel.gameObject.SetActive(weld||nail);intensity.SetValueWithoutNotify(tool.EffectIntensity);intensityLabel.text=$"Virtual intensity {tool.EffectIntensity:F2}";
            foreach(var b in offsets)b.interactable=!g.Running;
            if(tool.SelectionNotice.Length>0)summary.text=tool.SelectionNotice;
        }
    }
}
