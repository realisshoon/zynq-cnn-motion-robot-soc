using System;
using System.IO;
using System.Globalization;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioUartPanel : MonoBehaviour
    {
        BoardUartSource boards;
        GameObject panel;
        Font font;
        bool visible;
        ControlStudioPortsView portsView;
        public ControlStudioPortsView PortsView=>portsView;
        readonly GameObject[] pages = new GameObject[5];
        int page;
        public bool Visible=>visible;
        public int SelectedPage=>page;
        public void SelectPage(int index){if(index>=0&&index<pages.Length)page=index;}
        public static void Attach(ControlStudioRuntimeUI studio,Transform canvas)
        {
            // Legacy fixture APIs remain available; their serial workers start disconnected.
            var output=studio.GetComponent<ControlStudioUartOutput>()??studio.gameObject.AddComponent<ControlStudioUartOutput>();output.router=studio.router;
            var legacy=studio.GetComponent<UartPose3DSource>()??studio.gameObject.AddComponent<UartPose3DSource>();legacy.router=studio.router;
            var source=studio.gameObject.AddComponent<BoardUartSource>();source.router=studio.router;
            var ui=studio.gameObject.AddComponent<ControlStudioUartPanel>();ui.boards=source;ui.Build(canvas);
        }
        RectTransform Rect(string name,Transform parent,float x,float y,float w,float h)
        {
            var go=new GameObject(name,typeof(RectTransform));var r=(RectTransform)go.transform;r.SetParent(parent,false);
            r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);return r;
        }
        Text Label(string name,Transform parent,float x,float y,float w,float h,string value,int size=15)
        {
            var r=Rect(name,parent,x,y,w,h);var t=r.gameObject.AddComponent<Text>();t.font=font;t.fontSize=size;
            t.color=new Color(.88f,.93f,.97f);t.alignment=TextAnchor.UpperLeft;t.raycastTarget=false;t.text=value;return t;
        }
        Button Button(string name,Transform parent,float x,float y,float w,float h,string caption,UnityEngine.Events.UnityAction action)
        {
            var r=Rect(name,parent,x,y,w,h);var img=r.gameObject.AddComponent<Image>();img.color=new Color(.15f,.24f,.31f);
            var b=r.gameObject.AddComponent<Button>();b.targetGraphic=img;b.onClick.AddListener(action);
            Label(name+" label",r,4,0,w-8,h,caption,14).alignment=TextAnchor.MiddleCenter;return b;
        }
        InputField Input(string name,Transform parent,float x,float y,float w,string value)
        {
            var r=Rect(name,parent,x,y,w,34);var bg=r.gameObject.AddComponent<Image>();bg.color=new Color(.025f,.045f,.065f);
            var input=r.gameObject.AddComponent<InputField>();input.targetGraphic=bg;
            input.textComponent=Label(name+" value",r,8,0,w-16,34,value,15);input.textComponent.alignment=TextAnchor.MiddleLeft;
            input.text=value;return input;
        }
        void Build(Transform canvas)
        {
            font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            var root=Rect("UART / Hardware panel",canvas,960,96,640,714);
            root.gameObject.AddComponent<Image>().color=new Color(.055f,.085f,.12f,.98f);panel=root.gameObject;
            Label("Board UART title",root,20,10,600,30,"DUAL BOARD / UART",22);
            Label("Board UART contract",root,20,46,600,42,"921600 TRACE / 115200 non-TRACE | explicit board commands\nTK is a servo command, not measured position. Connect sends no command.",13);
            for(int k=0;k<5;k++) { int chosen=k; Button("Hardware tab "+k,root,20+k*120,92,114,30,new[]{"Ports","Control","기록","Settings","Capture"}[k],()=>page=chosen); }
            var overview=Rect("Hardware ports page",root,0,132,640,574);pages[0]=overview.gameObject;
            portsView=new ControlStudioPortsView(overview,boards,Display);
            var control=Rect("Hardware control page",root,20,140,600,540);pages[1]=control.gameObject;
            gameObject.AddComponent<ControlStudioBoardControlPanel>().Initialize(control,boards);
            for(int k=2;k<5;k++) { var p=Rect("Hardware module page "+k,root,20,140,600,540);pages[k]=p.gameObject; }
            gameObject.AddComponent<ControlStudioRecordingPanel>().Initialize((RectTransform)pages[2].transform,boards);
            gameObject.AddComponent<ControlStudioConfigurationPanel>().Initialize((RectTransform)pages[3].transform,boards);
            ControlStudioVideoCapturePanel.Attach((RectTransform)pages[4].transform,boards);
            panel.SetActive(false);
        }
        void OnDestroy()=>portsView?.Dispose();
        public void ShowPanel(bool show){visible=show;if(show)GetComponent<Tool1RuntimeUI>()?.ShowToolPanel(false);}
        static string Command(float[] values)=>values==null?"--":string.Join(" / ",Array.ConvertAll(values,v=>v.ToString("0.##",CultureInfo.InvariantCulture)));
        string Display(BoardSide side)
        {
            var s=boards.Session(side);var state=s.State;var tick=s.Latest;double now=BoardUartTransport.Now;
            var verified=s.LastApplied;
            string pose=s.AppliedCurrent(now)?"CURRENT":verified!=null?"HELD / STALE":"NOT VERIFIED";
            string parsedTick=tick==null?"--":tick.Tick.ToString(CultureInfo.InvariantCulture);
            string age=tick==null?"--":Math.Max(0,(now-tick.ReceivedAt)*1000).ToString("F0",CultureInfo.InvariantCulture);
            string pwm=tick==null?"--":string.Join(" / ",tick.Pwm);
            string received="Parsed (raw --) g"+s.Generation+" TK "+parsedTick;
            string unity="Unity Applied NOT OBSERVED / Humanoid separate path";
            string applicationState="";
            string visual="NOT OBSERVED";
            {
                var rawTick=side==BoardSide.Right?boards.RightDiagnosticsSnapshot?.LastReceivedTick:null;
                if(rawTick.HasValue)received="Raw TK "+rawTick.Value+" | Parsed g"+s.Generation+" TK "+parsedTick;
                var application=boards.ApplicationState(side);
                bool applied=application!=null&&application.UnityAppliedTick.HasValue;
                bool current=applied&&boards.VisualSource==side&&boards.Active&&s.AppliedCurrent(now)&&application.UnityAppliedGeneration==s.Generation&&
                    application.UnityAppliedTick==tick?.Tick&&application.UnityAppliedEpoch==boards.router.Epoch;
                unity="Unity "+(applied?(application.UnityAppliedIsFixture?"FIXTURE ":"LIVE ")+(current?"CURRENT ":"HELD ")+
                    "g"+application.UnityAppliedGeneration+" TK "+application.UnityAppliedTick.Value:"NOT APPLIED")+
                    " / age "+(applied?Math.Max(0,(now-application.UnityAppliedAt)*1000).ToString("F0",CultureInfo.InvariantCulture):"--")+" ms"+
                    " | cmd "+Command(application?.UnityAppliedCommand);
                string hold=application?.HoldReason??"NOT OBSERVED";
                if(hold=="DUPLICATE / ALREADY APPLIED")hold="SAME TK";
                else if(hold=="ROUTER EXCEPTION / APPLY NOT CONFIRMED")hold="ROUTER EXCEPTION";
                else if(hold=="ROUTER REJECT: BOARD / CONTROLLER REJECTED")hold="CONTROLLER REJECTED";
                string joint=application?.JointApplyStatus??"NOT OBSERVED";
                if(joint=="CONTROLLER REJECTED")joint="REJECTED";
                applicationState="\nTotal Submit/Apply "+(application?.RouterSubmitCount??0)+"/"+(application?.UnityApplyCount??0)+
                    " | Joint "+joint+" | Hold "+hold;
                visual=application?.VisualStatus??"NOT OBSERVED";
            }
            return (boards.IsFixture(side)?"FIXTURE / no hardware":boards.Message(side))+" | "+
                (s.Fault.Length!=0?s.Fault:state==null?"State UNKNOWN":state.Ui+" / "+state.Mode+" / PWM "+(state.Pwm?"ON":"OFF"))+
                "\n"+received+" / age "+age+" ms | cmd "+Command(tick?.Command)+
                "\nBoard verified TK "+(verified==null?"--":verified.Tick.ToString(CultureInfo.InvariantCulture))+" | cmd "+Command(verified?.Command)+" | "+pose+
                "\n"+unity+applicationState+
                "\nPWM us "+pwm+(tick==null?"":" | w="+tick.Writes+" err="+tick.Errors)+
                "\nRX lines "+s.Lines+" / errors "+s.ParseErrors+" / old "+s.OutOfOrder+" / gaps "+s.MissingTicks+" | Visual "+visual;
        }
        void Update()
        {
            if(panel==null||boards==null)return;
            var presentation=GetComponent<ControlStudioPresentationUI>();panel.SetActive(visible&&(presentation==null||presentation.UartExpanded));
            for(int k=0;k<pages.Length;k++)if(pages[k]!=null)pages[k].SetActive(k==page);
            var contract = panel.transform.Find("Board UART contract").GetComponent<Text>();
            contract.text = page == 2
                ? "921600 TRACE / 115200 non-TRACE | 보드 명령은 직접 실행\nTK는 서보 명령값이며 실측 위치가 아닙니다. 연결 시 자동 송신하지 않습니다."
                : "921600 TRACE / 115200 non-TRACE | explicit board commands\nTK is a servo command, not measured position. Connect sends no command.";
            portsView?.Update();
        }
    }
}
