using System;
using System.Collections.Generic;
using System.Globalization;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioBoardControlPanel : MonoBehaviour
    {
        BoardUartSource boards;
        Font font;
        RectTransform root;
        Text status, selection, safety, guidance, semantics;
        readonly Text[] summaries=new Text[2];
        readonly Button[] targets=new Button[3];
        readonly bool[] observedLiveSession=new bool[2];
        readonly int[] observedGeneration={-1,-1};
        readonly List<RectTransform> detailOnly=new List<RectTransform>();
        Button advancedToggle;
        Button overlayToggle;
        Text overlayState;
        readonly BoardCameraOverlayController[] overlays = new BoardCameraOverlayController[2];
        public BoardCameraOverlayController Overlay(BoardSide side) => overlays[side == BoardSide.Right ? 0 : 1];
        public bool ToggleCameraOverlay()
        {
            bool ok = BoardCameraOverlayController.ToggleSelected(overlays[0], overlays[1], target, out var reason);
            notice=ok?"카메라 b 요청 · 실제 표시 상태는 보드 응답으로 확인합니다.":reason;
            nextDisplay = 0; return ok;
        }
        bool detailed=true,advancedOpen,selectionRefused;
        double nextDisplay;
        public int SelectedTarget=>target;
        public bool TrySelectTarget(int selected)
        {
            if(selected<0||selected>2)return false;
            var records=GetComponent<ControlStudioRecordingPanel>();
            if(selected!=target&&records!=null&&!records.CanChangeTarget(out var reason)){notice=reason;selectionRefused=true;nextDisplay=0;return false;}
            if(selectionRefused&&selected!=target){notice="보드 선택 변경 완료 · 선택만으로 명령을 전송하지 않습니다.";selectionRefused=false;}target=selected;nextDisplay=0;records?.SynchronizeTarget();return true;
        }
        public bool AdvancedOpen=>advancedOpen;
        public float DetailedHeight {get;private set;}=850;
        readonly Button[] actions = new Button[6];
        int target; // 0 right, 1 left, 2 both; host selection never appears on wire.
        string notice = "요청 없음 · 보드 선택과 탭 진입은 명령을 송신하지 않습니다.";
        static readonly string[] Captions={"PWM 활성화 (E)","추종 시작 (A)","추종 명령 중지 (S)","PWM 비활성화 (X)","그리퍼 열기 (O)","그리퍼 자동 제어 (H)"};
        static readonly string[] CompactCaptions={"E / PWM ON","A / FOLLOW ON","S / FOLLOW STOP","X / PWM OFF","O / GRIP OPEN","H / GRIP AUTO"};
        RectTransform Rect(string name, float x, float y, float w, float h)
        {
            var go = new GameObject(name, typeof(RectTransform)); var r = (RectTransform)go.transform;
            r.SetParent(root, false); r.anchorMin = r.anchorMax = r.pivot = new Vector2(0, 1);
            r.anchoredPosition = new Vector2(x, -y); r.sizeDelta = new Vector2(w, h); return r;
        }
        Text Label(string name, float x, float y, float w, float h, string value, int size = 15)
        {
            var t = Rect(name, x, y, w, h).gameObject.AddComponent<Text>(); t.font = font; t.fontSize = size;
            t.color = new Color(.88f, .93f, .97f); t.text = value; t.raycastTarget = false; return t;
        }
        Text DetailLabel(string name,string value,int size=14)
        {var t=Label(name,0,0,600,24,value,size);detailOnly.Add(t.rectTransform);return t;}
        Button AddButton(string name, float x, float y, float w, string caption, UnityEngine.Events.UnityAction action)
        {
            var rect = Rect(name, x, y, w, 40); var img = rect.gameObject.AddComponent<Image>(); img.color = new Color(.15f, .24f, .31f);
            var button = rect.gameObject.AddComponent<Button>(); button.targetGraphic = img; button.transition = Selectable.Transition.None; button.onClick.AddListener(action);
            var text = new GameObject(name + " label", typeof(RectTransform), typeof(Text)).GetComponent<Text>();
            text.transform.SetParent(rect, false); var tr = text.rectTransform; tr.anchorMin = Vector2.zero; tr.anchorMax = Vector2.one; tr.offsetMin = tr.offsetMax = Vector2.zero;
            text.font = font; text.fontSize = 15; text.color = Color.white; text.alignment = TextAnchor.MiddleCenter; text.text = caption; text.raycastTarget = false;
            return button;
        }
        public void Initialize(RectTransform parent, BoardUartSource source)
        {
            root = parent; boards = source; font = Font.CreateDynamicFontFromOSFont("Malgun Gothic",15);
            Label("Real board control title", 0, 0, 600, 30, "로봇 제어 (Control)",18);
            DetailLabel("Control target heading","제어 보드 선택",14);
            for (int i = 0; i < 3; i++) { int selected = i; targets[i]=AddButton("Control target " + i, i * 200, 68, 190, new[] { "RIGHT / robot0", "LEFT / robot1", "BOTH" }[i], () => TrySelectTarget(selected)); }
            selection = Label("Control selection", 0, 88, 600, 28, "");
            safety=DetailLabel("Control hardware warning","실제 하드웨어 명령 · E 실행 시 Follow OFF여도 서보가 움직일 수 있습니다.\nVirtual 표시나 UI 잠금은 보드 PWM 차단을 보장하지 않습니다.",13);safety.color=new Color(.86f,.73f,.46f);
            for (int i = 0; i < 6; i++) { char key = "EASXOH"[i]; actions[i] = AddButton("Board control " + key, (i % 2) * 300, 124 + (i / 2) * 52, 290, Captions[i], () => Send(key)); }
            DetailLabel("Control PWM heading","PWM 제어 · 보드 출력",15);
            DetailLabel("Control follow heading","추종 제어 · PWM과 별도 요청",15);
            DetailLabel("Control gripper heading","Gripper 제어 · 실제 그리퍼 명령",15);
            for(int i=0;i<2;i++)overlays[i]=new BoardCameraOverlayController(boards.Gateway(i==0?BoardSide.Right:BoardSide.Left));
            DetailLabel("Control camera overlay heading","FPGA 카메라 관절점 표시",15);
            overlayToggle=AddButton("Camera overlay toggle",0,0,600,"6점 / 전체 관절점 토글 (b)",()=>ToggleCameraOverlay());
            overlayState=Label("Camera overlay reported state",0,0,600,76, "RIGHT: UNKNOWN · LEFT: UNKNOWN",13);
            DetailLabel("Control stop limitations","X는 PWM 출력 비활성화, S는 신규 추종 명령 중지입니다.\n두 명령 모두 비상정지 또는 서보 전원 차단이 아닙니다.",13);
            DetailLabel("Control state heading","현재 상태 · RIGHT / LEFT 독립 보고",15);
            for(int i=0;i<2;i++)summaries[i]=DetailLabel((i==0?"Right":"Left")+" Control summary","",13);
            DetailLabel("Control physical unknown","서보 연결 · 전원 · 실측 관절 위치: UNKNOWN (현재 피드백 없음)",13);
            guidance=DetailLabel("Control next guidance","",13);
            advancedToggle=AddButton("Advanced board control",0,0,600,"▶ 고급 제어 · 상세 응답 및 안전 안내",()=>{advancedOpen=!advancedOpen;nextDisplay=0;});detailOnly.Add((RectTransform)advancedToggle.transform);
            semantics=Label("Control semantics",0,0,600,96,
                "E: 기준 관절 명령 적용 후 PWM 활성화. A는 별도의 추종 요청입니다.\nS: 새 추종 목표 채택 중지. 이미 승인된 목표까지 움직임이 남을 수 있습니다.\nX: PWM 출력 비활성화 요청. 전원·토크·실제 정지를 검증한 결과가 아닙니다.\nBOTH: 두 보드에 독립 요청/응답. 원자적 동시 실행이나 안전 승인이 아닙니다.\n서보 준비·안전성: NOT VERIFIED. 연결 및 버튼 선택은 구동 허가가 아닙니다.",13);
            status = Label("Control response status", 0, 370, 600, 164, "", 14);
            LayoutDetailed();
        }
        static void Place(RectTransform r,float x,float y,float w,float h)
        {r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);}
        float PlaceText(Text t,float y,float minimum,int size=13)
        {
            t.fontSize=size;t.horizontalOverflow=HorizontalWrapMode.Wrap;t.verticalOverflow=VerticalWrapMode.Truncate;
            Place(t.rectTransform,0,y,600,minimum);float h=Mathf.Max(minimum,t.preferredHeight+4);t.rectTransform.sizeDelta=new Vector2(600,h);return h;
        }
        public void SetDetailed(bool value)
        {
            detailed=value;foreach(var r in detailOnly)r.gameObject.SetActive(value);
            root.Find("Real board control title").gameObject.SetActive(value);
            semantics.gameObject.SetActive(value&&advancedOpen);status.gameObject.SetActive(!value||advancedOpen);
            for(int i=0;i<6;i++){var t=actions[i].GetComponentInChildren<Text>();var caption=value?Captions[i]:CompactCaptions[i];if(t.text!=caption)t.text=caption;}
            if(value)LayoutDetailed();
        }
        void LayoutDetailed()
        {
            PlaceText(root.Find("Real board control title").GetComponent<Text>(),0,28,18);
            PlaceText(root.Find("Control target heading").GetComponent<Text>(),40,22,14);
            for(int i=0;i<3;i++)Place((RectTransform)targets[i].transform,i*204,68,192,36);
            PlaceText(selection,110,26,13);float y=140+PlaceText(safety,140,42)+12;
            string[] headings={"Control PWM heading","Control follow heading","Control gripper heading"};
            int[] pairs={0,3,1,2,4,5};
            for(int row=0;row<3;row++){
                PlaceText(root.Find(headings[row]).GetComponent<Text>(),y,22,15);y+=28;
                for(int col=0;col<2;col++){var b=actions[pairs[row*2+col]];Place((RectTransform)b.transform,col*306,y,294,38);b.GetComponentInChildren<Text>().fontSize=14;}
                y+=52;
            }
            y+=PlaceText(root.Find("Control camera overlay heading").GetComponent<Text>(),y,24,15)+6;
            Place((RectTransform)overlayToggle.transform,0,y,600,38);y+=46;
            y+=PlaceText(overlayState,y,68)+10;
            y+=PlaceText(root.Find("Control stop limitations").GetComponent<Text>(),y,38)+12;
            y+=PlaceText(root.Find("Control state heading").GetComponent<Text>(),y,24,15)+6;
            for(int i=0;i<2;i++)y+=PlaceText(summaries[i],y,82)+10;
            y+=PlaceText(root.Find("Control physical unknown").GetComponent<Text>(),y,26)+6;
            y+=PlaceText(guidance,y,56)+12;
            Place((RectTransform)advancedToggle.transform,0,y,600,36);y+=48;
            semantics.gameObject.SetActive(advancedOpen);status.gameObject.SetActive(advancedOpen);
            if(advancedOpen){y+=PlaceText(semantics,y,100)+12;y+=PlaceText(status,y,92)+12;}
            DetailedHeight=y+8;root.sizeDelta=new Vector2(600,DetailedHeight);
        }
        // Rendering only. Interactable remains the exact result of the existing command contract.
        internal static void StableButtonVisual(Button button,bool selected=false,bool selectionButton=false)
        {
            if(button.transition!=Selectable.Transition.None){button.transition=Selectable.Transition.None;button.targetGraphic.canvasRenderer.SetColor(Color.white);}
            Color color=!button.interactable?new Color(.18f,.20f,.22f):selected?new Color(.14f,.43f,.44f):selectionButton?new Color(.24f,.28f,.32f):new Color(.29f,.43f,.52f);
            var image=button.GetComponent<Image>();if(image.color!=color)image.color=color;
            var text=button.GetComponentInChildren<Text>();if(text!=null){Color foreground=button.interactable?new Color(.91f,.94f,.96f):new Color(.58f,.62f,.65f);if(text.color!=foreground)text.color=foreground;}
        }
        void Send(char key)
        {
            if (target == 2)
            {
                BoardControlCommands.TrySendBoth(boards.Gateway(BoardSide.Right), boards.Gateway(BoardSide.Left), key, out notice);
                return;
            }
            if (!Allowed(key, out var reason)) { notice = reason; return; }
            bool right = target != 1, left = target != 0;
            bool r = !right || boards.Control(BoardSide.Right, key);
            bool l = !left || boards.Control(BoardSide.Left, key);
            notice = r && l ? "Request queued. Success requires the matching board response." : "One or more ports refused the request; inspect each result.";
        }
        bool Allowed(char key, out string reason)
        {
            if (target == 2) return BoardControlCommands.CanSendBoth(boards.Gateway(BoardSide.Right), boards.Gateway(BoardSide.Left), key, out reason);
            reason = "";
            if (target != 1 && !BoardControlCommands.CanSend(boards.Gateway(BoardSide.Right), key, out reason)) return false;
            if (target != 0 && !BoardControlCommands.CanSend(boards.Gateway(BoardSide.Left), key, out reason)) return false;
            return true;
        }
        void Update()
        {
            if (boards == null || status == null) return;
            for(int i=0;i<targets.Length;i++)StableButtonVisual(targets[i],i==target,true);
            for (int i = 0; i < actions.Length; i++){
                bool allowed=Allowed("EASXOH"[i],out _);if(actions[i].interactable!=allowed)actions[i].interactable=allowed;
                StableButtonVisual(actions[i]);
            }
            StableButtonVisual(advancedToggle,advancedOpen);
            foreach(var overlay in overlays)overlay.Refresh();
            bool canOverlay=BoardCameraOverlayController.CanToggleSelected(overlays[0],overlays[1],target,out var overlayReason);
            if(overlayToggle.interactable!=canOverlay)overlayToggle.interactable=canOverlay;
            StableButtonVisual(overlayToggle);
            double now=BoardUartTransport.Now;if(now<nextDisplay)return;nextDisplay=now+.2;
            string overlayReply=target==2?"R: "+overlays[0].Status+" / L: "+overlays[1].Status:overlays[target].Status;
            SetText(overlayState,"RIGHT: "+overlays[0].ReportLabel+" · LEFT: "+overlays[1].ReportLabel+" (보드 응답)\n"+
                overlayReply+(canOverlay?"":"\n"+overlayReason));
            SetText(selection,"선택: "+new[]{"RIGHT / robot0","LEFT / robot1","BOTH · 독립 요청/응답"}[target]+" · 선택은 보드 승인 상태가 아닙니다.");
            for(int i=0;i<2;i++)SetText(summaries[i],Summary(i==0?BoardSide.Right:BoardSide.Left,now));
            string requests="";
            foreach(var side in target==2?new[]{BoardSide.Right,BoardSide.Left}:new[]{target==0?BoardSide.Right:BoardSide.Left}){
                bool e=BoardControlCommands.CanSend(boards.Gateway(side),'E',out var er),a=BoardControlCommands.CanSend(boards.Gateway(side),'A',out var ar);
                requests+=side.ToString().ToUpperInvariant()+"  E: "+(e?"통신 조건 충족":Reason(er))+"  /  A: "+(a?"통신 조건 충족":Reason(ar))+"\n";
            }
            SetText(guidance,requests+"조건 충족은 실물 구동 준비/안전 승인이 아닙니다. 실제 안전성: NOT VERIFIED.");
            SetText(status,notice+"\n\n"+Describe(BoardSide.Right)+"\n"+Describe(BoardSide.Left));
            var label=advancedToggle.GetComponentInChildren<Text>();SetText(label,(advancedOpen?"▼":"▶")+" 고급 제어 · 상세 응답 및 안전 안내");
        }
        static void SetText(Text text,string value){if(text.text!=value)text.text=value;}
        static string Reason(string reason)
        {
            switch(reason){
                case "Board disconnected":return "미연결";
                case "Fixture is RX only":return "Fixture · 실제 제어 불가";
                case "Query V for verified REC_STATE":return "REC_STATE 미확인";
                case "Console boundary unconfirmed":return "Input Boundary 미확인";
                case "Enable PWM with E and verify the board response first":return "PWM ON 보고값 필요 (자동 활성화 없음)";
                case "Control requires IDLE / LIVE":return "IDLE / LIVE 조건 불충족";
                case "Awaiting board response":return "보드 응답 대기";
                case "Recover console and query V after unknown result":return "이전 결과 미확정 · 별도 복구/상태 확인 필요";
                default:return reason;
            }
        }
        string Summary(BoardSide side,double now)
        {
            var s=boards.Session(side);bool fixture=boards.IsFixture(side);
            int i=side==BoardSide.Right?0:1;string message=boards.Message(side);
            if(observedGeneration[i]!=s.Generation){observedGeneration[i]=s.Generation;observedLiveSession[i]=false;}
            if(s.Connected&&!fixture)observedLiveSession[i]=true;
            if(message.StartsWith("DISCONNECTED",StringComparison.Ordinal))observedLiveSession[i]=false;
            string com=fixture?"FIXTURE / RX ONLY":!s.Connected?"OFF":s.Lines>0||boards.Message(side).StartsWith("COM /",StringComparison.Ordinal)?"CONNECTED":"CONNECTING / OPEN 미확인";
            var tick=s.Latest;double age=tick==null?double.NaN:Math.Max(0,now-tick.ReceivedAt);
            string rx=!s.Connected?"미수신":double.IsNaN(age)?(s.Lines>0?"RX 있음 · TK 미확인":"데이터 대기"):age>1?"STALE":"TK 수신 중";
            string verified=s.AppliedCurrent(now)?"CURRENT":s.LastApplied!=null?"HELD":s.State==null?"UNKNOWN":"NOT VERIFIED";
            string pwm=s.State==null?"UNKNOWN":s.State.Pwm?"ON (보고값)":"OFF (보고값)";
            string follow=!s.Connected||!s.FollowEnabled.HasValue?"UNKNOWN":s.FollowEnabled.Value?"ON (보고값)":"OFF (보고값)";
            string unity="관측 없음 (LEFT 별도 경로)";
            if(side==BoardSide.Right){var a=boards.RightApplication;bool applied=a.UnityAppliedTick.HasValue;
                bool current=applied&&boards.Active&&s.AppliedCurrent(now)&&a.UnityAppliedGeneration==s.Generation&&a.UnityAppliedTick==tick?.Tick&&a.UnityAppliedEpoch==boards.router.Epoch;
                unity=applied?(a.UnityAppliedIsFixture?"FIXTURE ":"")+(current?"CURRENT":"HELD")+" · TK "+a.UnityAppliedTick+" · Count "+a.UnityApplyCount:"NOT APPLIED";}
            string ageText=double.IsNaN(age)?"--":age<1?(age*1000).ToString("F0",CultureInfo.InvariantCulture)+" ms":age.ToString("F1",CultureInfo.InvariantCulture)+" s";
            string error=s.Fault.Length>0?"\n통신/파싱 오류: "+s.Fault:"";
            if(!s.Connected&&observedLiveSession[i])error+="\n연결 오류: "+message;
            return side.ToString().ToUpperInvariant()+" / robot"+(side==BoardSide.Right?0:1)+"  ·  COM "+com+
                "\nRX "+rx+" · Age "+ageText+" · Tick "+(tick?.Tick.ToString(CultureInfo.InvariantCulture)??"--")+" · Parse Error "+s.ParseErrors+
                "\nBoard Verified "+verified+" · PWM "+pwm+" · Follow "+follow+
                "\nUnity Applied "+unity+error;
        }
        string Describe(BoardSide side)
        {
            var s = boards.Session(side); var c = boards.Commands(side);
            return side + ": " + (s.State == null ? "UNKNOWN" : s.State.Ui + "/" + s.State.Mode + " PWM=" + (s.State.Pwm ? "1" : "0"))
                + " follow=" + (s.FollowEnabled.HasValue ? (s.FollowEnabled.Value ? "1" : "0") : "?") + "\n" + c.State + ": " + c.Status+
                "\nSource: "+boards.Message(side)+(s.Fault.Length==0?"":" | "+s.Fault);
        }
        void OnDestroy(){foreach(var overlay in overlays)overlay?.Dispose();}
    }
}
