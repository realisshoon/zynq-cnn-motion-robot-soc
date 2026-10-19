using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Ports-only presentation. Serial ownership, command gates and wire bytes remain in BoardUartSource.
    public sealed class ControlStudioPortsView : IDisposable
    {
        readonly RectTransform root;
        readonly BoardUartSource boards;
        readonly Func<BoardSide,string> details;
        readonly Font font;
        readonly Dropdown[] ports=new Dropdown[2], bauds=new Dropdown[2];
        readonly List<string>[] choices={new List<string>(),new List<string>()};
        readonly string[] selected={"",""}, localError={"",""};
        readonly Text[] connection=new Text[2], summary=new Text[2], instruction=new Text[2], verbose=new Text[2], boundary=new Text[2];
        readonly Button[] connect=new Button[2], disconnect=new Button[2];
        readonly RectTransform[] advancedSides=new RectTransform[2];
        readonly RectTransform[] areas=new RectTransform[2];
        readonly Action<string,double>[] observers=new Action<string,double>[2];
        readonly int[] generation={-1,-1};
        readonly double[] receivedAt={double.NaN,double.NaN};
        readonly bool[] opened=new bool[2];
        readonly bool[] connectAttempted=new bool[2];
        readonly bool[] lastWasFixture=new bool[2];
        readonly Text scanStatus, raw;
        readonly RectTransform advanced;
        readonly Button advancedToggle;
        string[] available=Array.Empty<string>();
        bool expanded;
        double nextUpdate;
        public float ContentHeight {get;private set;}=648;
        public bool AdvancedExpanded=>expanded;
        public string[] AvailablePorts=>(string[])available.Clone();
        public string SelectedPort(int i)=>selected[i];
        static BoardSide Side(int i)=>i==0?BoardSide.Right:BoardSide.Left;
        static string Prefix(int i)=>Side(i).ToString();
        static readonly Color Background=new Color(.105f,.13f,.155f), Field=new Color(.075f,.09f,.11f), Blue=new Color(.19f,.29f,.36f);
        public ControlStudioPortsView(RectTransform root,BoardUartSource boards,Func<BoardSide,string> details)
        {
            this.root=root;this.boards=boards;this.details=details;
            font=Font.CreateDynamicFontFromOSFont("Malgun Gothic",15);
            Label("Ports title",root,20,12,330,28,"보드 연결 (Ports)",18);
            Button("Refresh COM ports",root,420,10,200,34,"COM 포트 새로고침",RefreshPorts);
            scanStatus=Label("COM scan status",root,20,48,600,22,"",13);
            for(int i=0;i<2;i++)BuildSide(i,78+i*256);
            advancedToggle=Button("Advanced communication diagnostics",root,20,600,600,34,"▶ 고급 통신 진단 및 상태 조회",()=>{expanded=!expanded;LayoutAdvanced();nextUpdate=0;});
            advanced=Rect("Ports advanced diagnostics",root,0,646,640,1);
            for(int i=0;i<2;i++)BuildAdvanced(i);
            raw=Label("Board UART raw line",advanced,20,0,600,90,"",13);
            for(int i=0;i<2;i++){
                int index=i;observers[i]=(line,at)=>{
                    generation[index]=boards.Session(Side(index)).Generation;
                    receivedAt[index]=at;opened[index]=true;
                };
                boards.Session(Side(i)).LineReceived+=observers[i];
            }
            RefreshPorts();LayoutAdvanced();
        }
        RectTransform Rect(string name,Transform parent,float x,float y,float w,float h)
        {
            var r=(RectTransform)new GameObject(name,typeof(RectTransform)).transform;r.SetParent(parent,false);
            r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);return r;
        }
        Text Label(string name,Transform parent,float x,float y,float w,float h,string text,int size=14)
        {
            var r=Rect(name,parent,x,y,w,h);var t=r.gameObject.AddComponent<Text>();t.font=font;t.fontSize=size;t.text=text;
            t.color=new Color(.84f,.88f,.91f);t.raycastTarget=false;t.horizontalOverflow=HorizontalWrapMode.Wrap;t.verticalOverflow=VerticalWrapMode.Truncate;return t;
        }
        Button Button(string name,Transform parent,float x,float y,float w,float h,string text,UnityEngine.Events.UnityAction action)
        {
            var r=Rect(name,parent,x,y,w,h);var bg=r.gameObject.AddComponent<Image>();bg.color=Blue;
            var b=r.gameObject.AddComponent<Button>();b.targetGraphic=bg;b.onClick.AddListener(action);
            Label(name+" label",r,6,0,w-12,h,text).alignment=TextAnchor.MiddleCenter;return b;
        }
        Dropdown Dropdown(string name,Transform parent,float x,float y,float w,IEnumerable<string> options)
        {
            var r=Rect(name,parent,x,y,w,36);var bg=r.gameObject.AddComponent<Image>();bg.color=Field;
            var d=r.gameObject.AddComponent<Dropdown>();d.targetGraphic=bg;
            d.captionText=Label(name+" value",r,10,0,w-40,36,"",15);d.captionText.alignment=TextAnchor.MiddleLeft;
            Label(name+" arrow",r,w-26,0,22,36,"▾",15).alignment=TextAnchor.MiddleCenter;
            var template=Rect("Template",r,0,36,w,180);template.gameObject.AddComponent<Image>().color=Background;
            var scroll=template.gameObject.AddComponent<ScrollRect>();scroll.horizontal=false;scroll.movementType=ScrollRect.MovementType.Clamped;
            var viewport=Rect("Viewport",template,0,0,w,180);viewport.gameObject.AddComponent<RectMask2D>();
            var content=Rect("Content",viewport,0,0,w,36);scroll.viewport=viewport;scroll.content=content;
            var item=Rect("Item",content,0,0,w,36);var image=item.gameObject.AddComponent<Image>();image.color=Blue;
            var toggle=item.gameObject.AddComponent<Toggle>();toggle.targetGraphic=image;
            var check=Rect("Selected",item,0,0,3,36).gameObject.AddComponent<Image>();check.color=new Color(.34f,.64f,.86f);toggle.graphic=check;
            d.itemText=Label("Item label",item,10,0,w-18,36,"",15);d.itemText.alignment=TextAnchor.MiddleLeft;
            d.template=template;template.gameObject.SetActive(false);d.AddOptions(options.ToList());return d;
        }
        void BuildSide(int i,float y)
        {
            string p=Prefix(i);var area=Rect(p+" connection area",root,20,y,600,244);areas[i]=area;area.gameObject.AddComponent<Image>().color=Background;
            Label(p+" UART heading",area,12,10,280,26,p.ToUpperInvariant()+" / robot"+i,17);
            connection[i]=Label(p+" connection status",area,300,12,288,24,"미연결",14);connection[i].alignment=TextAnchor.MiddleRight;
            Label(p+" COM caption",area,12,44,284,20,"COM 포트",13);Label(p+" baud caption",area,308,44,280,20,"Baud Rate",13);
            ports[i]=Dropdown(p+" COM",area,12,68,284,new[]{"포트 선택"});
            bauds[i]=Dropdown(p+" baud",area,308,68,280,new[]{"921600 (TRACE)","115200 (non-TRACE)"});
            selected[i]=PlayerPrefs.GetString("ControlStudio.Ports."+p,"");
            ports[i].onValueChanged.AddListener(value=>{selected[i]=choices[i][value];localError[i]="";PlayerPrefs.SetString("ControlStudio.Ports."+p,selected[i]);nextUpdate=0;});
            connect[i]=Button(p+" Connect",area,12,114,284,36,"연결",()=>Connect(i));
            disconnect[i]=Button(p+" Disconnect",area,308,114,280,36,"연결 해제",()=>{boards.Disconnect(Side(i));localError[i]="";opened[i]=connectAttempted[i]=false;nextUpdate=0;});
            summary[i]=Label(p+" connection summary",area,12,162,576,46,"",13);
            instruction[i]=Label(p+" connection guidance",area,12,210,576,30,"",13);
        }
        void BuildAdvanced(int i)
        {
            string p=Prefix(i);var area=Rect(p+" advanced diagnostics",advanced,20,0,600,380);advancedSides[i]=area;
            Label(p+" advanced title",area,0,0,600,24,p.ToUpperInvariant()+" / robot"+i+" · 고급 통신 진단",16);
            boundary[i]=Button(p+" boundary",area,0,30,600,34,"",()=>{boards.ConfirmCommandBoundary(Side(i),!boards.CommandBoundaryConfirmed(Side(i)));nextUpdate=0;}).GetComponentInChildren<Text>();
            string[] captions={"로봇 상태 조회 (V)","추종 상태 조회 (T)","CNN 상태 조회 (t)"};
            for(int k=0;k<3;k++){char key="VTt"[k];Button(p+" query "+key,area,k*204,74,192,34,captions[k],()=>{boards.Query(Side(i),key);nextUpdate=0;});}
            verbose[i]=Label(p+" UART state",area,0,120,600,160,"",13);
            Label(p+" fixture warning",area,0,0,600,22,"오프라인 Fixture · 실제 COM 수신 데이터가 아닙니다",13);
            var r=Rect(p+" fixture",area,0,0,452,36);var bg=r.gameObject.AddComponent<Image>();bg.color=Field;
            var input=r.gameObject.AddComponent<InputField>();input.targetGraphic=bg;input.textComponent=Label(p+" fixture value",r,8,0,436,36,"",13);input.textComponent.alignment=TextAnchor.MiddleLeft;
            input.text=Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples","board_"+p.ToLowerInvariant()+".uart");
            Button(p+" Load fixture",area,464,0,136,36,"Fixture 불러오기",()=>{boards.LoadFixture(Side(i),input.text);localError[i]="";nextUpdate=0;});
        }
        public void RefreshPorts()
        {
            // Enumeration only: no SerialPort constructor/Open/Write and no auto device mapping.
            try {SetPorts(SerialPort.GetPortNames());scanStatus.text=available.Length==0?"검색된 COM 포트 없음":"검색된 포트 "+available.Length+"개 · RIGHT/LEFT 장치를 직접 선택하세요.";}
            catch(Exception e){SetPorts(Array.Empty<string>());scanStatus.text="COM 포트 검색 오류: "+e.Message;}
        }
        internal void SetPorts(IEnumerable<string> names)
        {
            available=names.Where(s=>!string.IsNullOrWhiteSpace(s)).Select(s=>s.Trim()).Distinct(StringComparer.OrdinalIgnoreCase).OrderBy(PortOrder).ThenBy(s=>s,StringComparer.OrdinalIgnoreCase).ToArray();
            for(int i=0;i<2;i++){
                var values=choices[i];values.Clear();values.Add("");values.AddRange(available);
                if(selected[i].Length>0&&!values.Contains(selected[i],StringComparer.OrdinalIgnoreCase))values.Add(selected[i]);
                var labels=values.Select(s=>s.Length==0?(available.Length==0?"검색된 COM 포트 없음":"포트 선택"):s+(available.Contains(s,StringComparer.OrdinalIgnoreCase)?"":" (미검색 / 이전 선택)")).ToList();
                ports[i].ClearOptions();ports[i].AddOptions(labels);ports[i].SetValueWithoutNotify(Math.Max(0,values.FindIndex(s=>string.Equals(s,selected[i],StringComparison.OrdinalIgnoreCase))));ports[i].RefreshShownValue();
            }
            nextUpdate=0;
        }
        static int PortOrder(string s)=>s.StartsWith("COM",StringComparison.OrdinalIgnoreCase)&&int.TryParse(s.Substring(3),out var n)?n:int.MaxValue;
        bool Duplicate(int i)=>selected[i].Length>0&&string.Equals(selected[i],selected[1-i],StringComparison.OrdinalIgnoreCase);
        void Connect(int i)
        {
            localError[i]=selected[i].Length==0?"연결할 COM 포트를 선택하세요.":Duplicate(i)?"RIGHT와 LEFT에 동일 COM 포트를 연결할 수 없습니다.":!available.Contains(selected[i],StringComparer.OrdinalIgnoreCase)?"선택한 포트가 검색되지 않았습니다. 새로고침하세요.":"";
            if(localError[i].Length==0){connectAttempted[i]=true;opened[i]=false;receivedAt[i]=double.NaN;boards.Connect(Side(i),selected[i],bauds[i].value==0?921600:115200);}
            nextUpdate=0;
        }
        // UI-only classification; never feeds back into parser, ApplyCommand or command approval.
        internal static string ConnectionState(bool connected,bool fixture,string message,bool openEvidence,double age)
        {
            if(fixture)return "FIXTURE · 오프라인 테스트";
            if(!connected)return message.StartsWith("DISCONNECTED",StringComparison.Ordinal)?"미연결":"연결 오류";
            if(!double.IsNaN(age))return age>1?"수신 지연 / STALE":"정상 수신 중";
            return openEvidence?"연결됨 / 수신 대기":"연결 중";
        }
        static void TextChanged(Text t,string value){if(t.text!=value)t.text=value;}
        public void Update()
        {
            double now=BoardUartTransport.Now;if(now<nextUpdate)return;nextUpdate=now+.2;
            for(int i=0;i<2;i++){
                var side=Side(i);var s=boards.Session(side);var message=boards.Message(side);bool fixture=boards.IsFixture(side);
                if(generation[i]!=s.Generation){generation[i]=s.Generation;receivedAt[i]=double.NaN;opened[i]=false;lastWasFixture[i]=fixture;}
                if(s.Connected)lastWasFixture[i]=fixture;
                if(s.Connected&&message.StartsWith("COM /",StringComparison.Ordinal))opened[i]=true;
                // Latest TK survives Disconnect; it is age information, never connection evidence.
                double at=!double.IsNaN(receivedAt[i])?receivedAt[i]:s.Latest?.ReceivedAt??double.NaN;
                double age=double.IsNaN(at)?double.NaN:Math.Max(0,now-at);
                string state=ConnectionState(s.Connected,fixture,connectAttempted[i]||opened[i]||s.Connected?message:"DISCONNECTED",opened[i],age);
                TextChanged(connection[i],state);connection[i].color=fixture?new Color(.86f,.73f,.4f):state=="정상 수신 중"?new Color(.42f,.71f,.61f):new Color(.84f,.88f,.91f);
                bool current=s.AppliedCurrent(now);string verified=current?"CURRENT":s.LastApplied!=null?"HELD":"미확인";
                string applied="관측 없음 (LEFT 별도 경로)";
                if(i==0){var a=boards.RightApplication;bool exists=a.UnityAppliedTick.HasValue;bool fresh=exists&&boards.Active&&current&&a.UnityAppliedGeneration==s.Generation&&a.UnityAppliedTick==s.Latest?.Tick&&a.UnityAppliedEpoch==boards.router.Epoch;
                    applied=exists?(a.UnityAppliedIsFixture?"FIXTURE ":"")+(fresh?"CURRENT":"HELD")+" · "+a.UnityApplyCount:"미확인";}
                // Korean wording only; CURRENT/HELD are separately labelled board and Unity evidence.
                if(i==1)applied="관측 없음 (LEFT 별도 경로)";
                string tick=s.Latest?.Tick.ToString(CultureInfo.InvariantCulture)??"--";
                string ageText=double.IsNaN(age)?"없음":age<1?(age*1000).ToString("F0",CultureInfo.InvariantCulture)+" ms":age.ToString("F1",CultureInfo.InvariantCulture)+" s";
                TextChanged(summary[i],(lastWasFixture[i]?"FIXTURE ":"")+"RX "+s.Lines+"줄 · 마지막 수신 "+ageText+" · Tick "+tick+" · Parse Error "+s.ParseErrors+"\nBoard Verified "+verified+"  |  Unity Applied "+applied);
                string hint=localError[i].Length>0?localError[i]:fixture?"테스트 데이터입니다. 실제 보드 연결/수신으로 판정하지 않습니다.":state=="연결 오류"?message:s.Connected?(state=="연결 중"?"COM 포트를 여는 중입니다.":double.IsNaN(age)?"연결되었습니다. 보드 데이터 수신 대기 중입니다.":age>1?"수신이 지연되었습니다. 마지막 가상 자세를 유지합니다.":"데이터를 정상 수신하고 있습니다."):Duplicate(i)?"RIGHT/LEFT의 COM 포트를 서로 다르게 선택하세요.":selected[i].Length==0?"연결할 COM 포트를 선택하세요.":"연결 버튼을 눌러주세요.";
                TextChanged(instruction[i],hint);ports[i].interactable=bauds[i].interactable=!s.Connected;connect[i].interactable=!s.Connected;disconnect[i].interactable=s.Connected||state=="연결 오류";
                TextChanged(boundary[i],boards.CommandBoundaryConfirmed(side)?"Input boundary: CONFIRMED (no partial input)":"Confirm input boundary before commands");
                if(expanded)TextChanged(verbose[i],details(side)+"\n마지막 응답: "+s.LastLine+"\nParse 상세: "+(s.Fault.Length==0?"없음":s.Fault));
            }
            LayoutBasic();
            if(expanded)TextChanged(raw,"R raw: "+boards.Right.LastLine+"\nL raw: "+boards.Left.LastLine+"\nRaw COM RX logs: "+Path.Combine(Application.persistentDataPath,"BoardUartLogs"));
            LayoutAdvanced();
        }
        void LayoutBasic()
        {
            float y=78;
            for(int i=0;i<2;i++){
                float h=Mathf.Max(32,instruction[i].preferredHeight+6);instruction[i].rectTransform.sizeDelta=new Vector2(576,h);
                areas[i].anchoredPosition=new Vector2(20,-y);areas[i].sizeDelta=new Vector2(600,212+h);y+=areas[i].rect.height+12;
            }
            advancedToggle.GetComponent<RectTransform>().anchoredPosition=new Vector2(20,-y-10);advanced.anchoredPosition=new Vector2(0,-y-56);
        }
        void LayoutAdvanced()
        {
            advanced.gameObject.SetActive(expanded);TextChanged(advancedToggle.GetComponentInChildren<Text>(),(expanded?"▼":"▶")+" 고급 통신 진단 및 상태 조회");
            float top=-advanced.anchoredPosition.y;
            if(!expanded){ContentHeight=top+2;return;}
            float y=0;
            for(int i=0;i<2;i++){
                var area=advancedSides[i];area.anchoredPosition=new Vector2(20,-y);float h=Mathf.Max(160,verbose[i].preferredHeight+8);verbose[i].rectTransform.sizeDelta=new Vector2(600,h);
                area.Find(Prefix(i)+" fixture warning").GetComponent<RectTransform>().anchoredPosition=new Vector2(0,-(128+h));
                area.Find(Prefix(i)+" fixture").GetComponent<RectTransform>().anchoredPosition=new Vector2(0,-(154+h));
                area.Find(Prefix(i)+" Load fixture").GetComponent<RectTransform>().anchoredPosition=new Vector2(464,-(154+h));
                area.sizeDelta=new Vector2(600,206+h);y+=area.rect.height+22;
            }
            raw.rectTransform.anchoredPosition=new Vector2(20,-y);raw.rectTransform.sizeDelta=new Vector2(600,Mathf.Max(90,raw.preferredHeight+8));
            ContentHeight=top+y+raw.rectTransform.rect.height+20;advanced.sizeDelta=new Vector2(640,ContentHeight-top);
        }
        public void Dispose(){for(int i=0;i<2;i++)boards.Session(Side(i)).LineReceived-=observers[i];}
    }
}
