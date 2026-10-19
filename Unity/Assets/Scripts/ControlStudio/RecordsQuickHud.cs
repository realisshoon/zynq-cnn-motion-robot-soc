using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Overlay only. All actions address the Records panel's current Controller/Flow.
    // The Control target is canonical; no HUD target or hardware output path exists.
    [DefaultExecutionOrder(7000)]
    public sealed class RecordsQuickHud : MonoBehaviour
    {
        ControlStudioRecordingPanel records;BoardUartSource boards;GameObject canvasRoot;
        RectTransform viewport,bar,popup;Text status;Button rec,play,list,back;
        Font font;string notice="",catalogKey="";int popupMode,lastTarget=-1;
        readonly List<Button> popupActions=new List<Button>();
        public RectTransform HudRect=>bar;
        public RectTransform PopupRect=>popup;
        public bool Visible=>bar!=null&&canvasRoot.activeSelf;
        public string StatusText=>status.text;
        public void Initialize(ControlStudioRecordingPanel panel,BoardUartSource source)
        {
            records=panel;boards=source;font=Font.CreateDynamicFontFromOSFont("Malgun Gothic",12);
            canvasRoot=new GameObject("Records Quick HUD canvas",typeof(Canvas),typeof(CanvasScaler),typeof(GraphicRaycaster));canvasRoot.transform.SetParent(transform,false);
            var canvas=canvasRoot.GetComponent<Canvas>();canvas.renderMode=RenderMode.ScreenSpaceOverlay;canvas.sortingOrder=15;
            canvasRoot.GetComponent<CanvasScaler>().uiScaleMode=CanvasScaler.ScaleMode.ConstantPixelSize;
            viewport=Rect(canvasRoot.transform,"Records HUD viewport anchor",0,0,1,1);
            bar=Rect(viewport,"Records Quick HUD",32,45,200,54);bar.gameObject.AddComponent<Image>().color=new Color(.10f,.14f,.17f,.96f);
            rec=Button(bar,"Quick REC",6,9,56,36,()=>Run(()=>records.CurrentFlow.HudRecord(out var why)?"":why));
            Icon(rec.transform,QuickHudIcon.Kind.Dot,6,11,14,14,new Color(.85f,.2f,.25f));Label(rec.transform,"Quick REC label",21,0,35,36,"REC",12);
            play=Button(bar,"Quick PLAY",66,9,40,36,Play);Icon(play.transform,QuickHudIcon.Kind.Play,10,8,20,20,Color.white);
            list=Button(bar,"Quick list",110,9,40,36,()=>TogglePopup(1));Icon(list.transform,QuickHudIcon.Kind.Menu,10,8,20,20,Color.white);
            back=Button(bar,"Quick return",154,9,40,36,()=>TogglePopup(2));Icon(back.transform,QuickHudIcon.Kind.Return,10,8,20,20,Color.white);
            status=Label(viewport,"Records HUD status",32,105,460,72,"",12);status.alignment=TextAnchor.LowerLeft;status.transform.SetAsFirstSibling();
            popup=Rect(bar,"Records HUD popup",0,60,290,200);popup.gameObject.AddComponent<Image>().color=new Color(.12f,.16f,.19f,.98f);popup.gameObject.SetActive(false);
            canvasRoot.SetActive(false);
        }
        void Run(Func<string> action){records.SynchronizeTarget();if(records.SharedTarget==2){notice="BOTH는 단일 기록 제어 미검증 — RIGHT/LEFT 선택 필요";return;}notice=action();}
        void Play()
        {
            Run(()=>{var c=records.CurrentController;var st=c.Gateway.Session.State;
                if(st!=null&&st.Ui=="IDLE"&&(st.Mode=="PLAYING"||st.Mode=="ALIGNING"||st.Mode=="HOLDING"))return records.CurrentFlow.HudStopPlayback(out var why)?"":why;
                if(records.SelectedRecordId==0)return "먼저 목록에서 기록을 선택하세요.";
                return records.CurrentFlow.Play(records.SelectedRecordId,false,out var reason)?"":reason;});
        }
        void TogglePopup(int mode)
        {
            if(popupMode==mode){popupMode=0;popup.gameObject.SetActive(false);return;}
            popupMode=mode;catalogKey=CatalogKey();
            if(mode==1&&!records.CurrentController.CatalogValid)Run(()=>records.CurrentFlow.RefreshCatalog(out var why)?"":why);
            BuildPopup();
        }
        void BuildPopup()
        {
            foreach(var child in popup.Cast<Transform>().ToArray()){child.gameObject.SetActive(false);child.SetParent(null);Destroy(child.gameObject);}popupActions.Clear();
            if(popupMode==0){popup.gameObject.SetActive(false);return;}
            popup.gameObject.SetActive(true);
            Label(popup,"Quick popup title",8,168,274,28,records.SelectedBoard.ToString().ToUpperInvariant()+" / "+(popupMode==1?"SD 기록":"복귀"),13);
            if(popupMode==2){
                popup.sizeDelta=new Vector2(290,116);
                var title=popup.Find("Quick popup title");((RectTransform)title).anchoredPosition=new Vector2(8,84);
                popupActions.Add(Button(popup,"Quick follow return",8,44,274,34,()=>{Run(()=>records.CurrentFlow.ReturnToFollow(out var why)?"":why);ClosePopup();},"추종 복귀"));
                popupActions.Add(Button(popup,"Quick PWM idle",8,6,274,34,()=>{Run(()=>records.CurrentFlow.ReturnToIdle(out var why)?"":why);ClosePopup();},"PWM 대기"));return;
            }
            popup.sizeDelta=new Vector2(290,244);
            ((RectTransform)popup.Find("Quick popup title")).anchoredPosition=new Vector2(8,210);
            popupActions.Add(Button(popup,"Quick SD refresh",174,212,108,24,()=>Run(()=>records.CurrentFlow.RefreshCatalog(out var why)?"":why),"새로고침"));
            var root=Rect(popup,"Quick SD scroll",8,8,274,198);var scroll=root.gameObject.AddComponent<ScrollRect>();scroll.horizontal=false;scroll.movementType=ScrollRect.MovementType.Clamped;scroll.scrollSensitivity=28;
            var view=Rect(root,"Viewport",0,0,274,198);view.gameObject.AddComponent<RectMask2D>();view.gameObject.AddComponent<Image>().color=new Color(.09f,.12f,.15f);
            var body=Rect(view,"Content",0,198,274,Math.Max(198,records.CurrentController.Entries.Count*34));body.anchorMin=body.anchorMax=new Vector2(0,1);body.pivot=new Vector2(0,1);body.anchoredPosition=Vector2.zero;
            scroll.viewport=view;scroll.content=body;
            if(!records.CurrentController.CatalogValid){var pending=Label(body,"Quick list pending",8,-70,258,64,"SD 목록 확인 필요 / 응답 대기\n기존 송신 승인 조건 유지",12);pending.rectTransform.anchorMin=pending.rectTransform.anchorMax=new Vector2(0,1);return;}
            int n=0;foreach(var entry in records.CurrentController.Entries){uint id=entry.Id;string name=entry.Name;bool date=System.Text.RegularExpressions.Regex.IsMatch(name,"^[0-9]{8}(?:_[0-9]{2})?$");
                var b=Button(body,"Quick SD ID "+id,0,-(++n)*34,274,32,()=>{if(records.SelectRecord(id)){notice="선택 · "+name;ClosePopup();}},id+"  "+(date?"["+name+"]":name)+"  "+(entry.DurationMs/1000.0).ToString("0.0",System.Globalization.CultureInfo.InvariantCulture)+"초");
                ((RectTransform)b.transform).anchorMin=((RectTransform)b.transform).anchorMax=new Vector2(0,1);
                SetColor(b,true,id==records.SelectedRecordId?new Color(.14f,.43f,.44f):new Color(.24f,.28f,.32f));
            }
            if(n==0){var empty=Label(body,"Quick empty list",8,-40,258,32,"저장된 SD 기록 없음",12);empty.rectTransform.anchorMin=empty.rectTransform.anchorMax=new Vector2(0,1);}
        }
        string CatalogKey(){var c=records.CurrentController;return c.CatalogValid?string.Join(";",c.Entries.Select(e=>e.Id+"/"+e.Name))+"/"+records.SelectedRecordId:"unknown";}
        void ClosePopup(){popupMode=0;popup.gameObject.SetActive(false);}
        void LateUpdate()
        {
            if(records==null)return;
            var menu=GetComponent<ControlStudioStartupMenu>();var view=GetComponent<ControlStudioPresentationUI>();var profiles=GetComponent<RobotVisualProfiles>();
            bool show=menu!=null&&!menu.Visible&&view!=null&&!view.PresentationMode&&profiles!=null&&profiles.Selected==RobotVisualProfileId.G51;
            canvasRoot.SetActive(show);if(!show)return;
            var camera=GetComponent<ControlStudioRuntimeUI>().orbit.GetComponent<Camera>();var pixels=camera.pixelRect;
            viewport.anchoredPosition=pixels.position;viewport.sizeDelta=pixels.size;
            records.SynchronizeTarget();int target=records.SharedTarget;
            if(target!=lastTarget){lastTarget=target;notice="";ClosePopup();catalogKey="";}
            var c=records.CurrentController;var f=records.CurrentFlow;var g=c.Gateway;var s=g.Session;var st=s.State;
            bool single=target<2,recording=s.Trusted&&st.Mode=="RECORDING"&&c.RecordingActive;
            bool playing=s.Trusted&&st.Ui=="IDLE"&&(st.Mode=="PLAYING"||st.Mode=="ALIGNING");
            rec.interactable=single&&f.CanHudRecord(out _);
            play.interactable=single&&(f.CanHudStop||!f.Busy&&!g.Busy&&f.CanPlay(records.SelectedRecordId,false,out _));
            list.interactable=back.interactable=single&&!f.Busy&&!g.Busy;
            SetColor(rec,rec.interactable,recording?new Color(.55f,.15f,.20f):new Color(.24f,.28f,.32f));
            SetColor(play,play.interactable,playing?new Color(.14f,.43f,.44f):new Color(.29f,.43f,.52f));
            SetColor(list,list.interactable,new Color(.24f,.28f,.32f));SetColor(back,back.interactable,new Color(.24f,.28f,.32f));
            foreach(var b in popupActions)if(b!=null){b.interactable=single&&!f.Busy&&!g.Busy;SetColor(b,b.interactable,new Color(.29f,.43f,.52f));}
            string port=boards.ConnectedPortName(records.SelectedBoard);
            string role=target==2?"BOTH · HUD 단일 보드 제어 미검증":(target==0?"RIGHT / robot0":"LEFT / robot1")+" · "+(s.Connected?(port.Length>0?port:"연결 포트 UNKNOWN"):"DISCONNECTED");
            string state=target==2?"PWM/FOLLOW · 개별 보드 확인 필요":!s.Trusted?"PWM UNKNOWN · FOLLOW UNKNOWN":"PWM "+(st.Pwm?"ON":"OFF")+" · FOLLOW "+(s.FollowEnabled.HasValue?s.FollowEnabled.Value?"ON":"OFF":"UNKNOWN");
            string message=target==2?"BOTH 기록 제어 미검증 — 단일 보드를 선택하세요.":recording?"녹화 중 · "+TimeSpan.FromSeconds(c.RecordingSeconds(BoardUartTransport.Now)).ToString(@"mm\:ss"):playing?st.Mode=="ALIGNING"?"재생 ALIGN 중":"재생 중":f.Busy?f.Message:notice.Length>0?notice:f.Message.Length>0?f.Message:rec.interactable?"녹화 준비 가능 · 보드 보고값 기준":play.interactable?f.Fresh?"재생 준비 완료 · 보드 보고값 기준":"재생 요청 시 최신 보드 상태 조회 필요":!s.Connected?"보드 연결 필요":st!=null&&!st.Pwm?"PWM 활성화 필요":"보드 상태/추종 승인/기록 선택 확인 필요";
            if(message.Length>82)message=message.Substring(0,82)+"…";
            status.text=role+"\n"+state+(s.Trusted&&!f.Fresh?" · 상태 오래됨":"")+"\n"+message+"\n"+boards.VisualSummary;
            string next=CatalogKey();
            if(popupMode==1&&next!=catalogKey){catalogKey=next;BuildPopup();}
        }
        void SetColor(Button b,bool enabled,Color color){b.transition=Selectable.Transition.None;var image=b.GetComponent<Image>();var value=enabled?color:new Color(.18f,.20f,.22f);if(image.color!=value)image.color=value;}
        RectTransform Rect(Transform parent,string name,float x,float y,float w,float h){var r=(RectTransform)new GameObject(name,typeof(RectTransform)).transform;r.SetParent(parent,false);r.anchorMin=r.anchorMax=r.pivot=Vector2.zero;r.anchoredPosition=new Vector2(x,y);r.sizeDelta=new Vector2(w,h);return r;}
        Text Label(Transform parent,string name,float x,float y,float w,float h,string value,int size){var t=Rect(parent,name,x,y,w,h).gameObject.AddComponent<Text>();t.font=font;t.fontSize=size;t.color=new Color(.86f,.92f,.95f);t.alignment=TextAnchor.MiddleLeft;t.text=value;t.raycastTarget=false;return t;}
        Button Button(Transform parent,string name,float x,float y,float w,float h,UnityEngine.Events.UnityAction action,string caption=null){var r=Rect(parent,name,x,y,w,h);var b=r.gameObject.AddComponent<Button>();b.targetGraphic=r.gameObject.AddComponent<Image>();b.GetComponent<Image>().color=new Color(.29f,.43f,.52f);b.transition=Selectable.Transition.None;b.onClick.AddListener(()=>{if(b.interactable)action();});if(caption!=null)Label(r,name+" label",4,0,w-8,h,caption,12).alignment=TextAnchor.MiddleCenter;return b;}
        void Icon(Transform parent,QuickHudIcon.Kind kind,float x,float y,float w,float h,Color color){var icon=Rect(parent,"Quick HUD icon",x,y,w,h).gameObject.AddComponent<QuickHudIcon>();icon.kind=kind;icon.color=color;icon.raycastTarget=false;}
        void OnDestroy(){if(canvasRoot!=null)Destroy(canvasRoot);}
    }
    public sealed class QuickHudIcon : Graphic
    {
        public enum Kind{Dot,Play,Menu,Return}public Kind kind;
        protected override void OnPopulateMesh(VertexHelper vh)
        {
            vh.Clear();var r=rectTransform.rect;Vector2 P(float x,float y)=>new Vector2(r.xMin+x*r.width,r.yMin+y*r.height);
            void Tri(Vector2 a,Vector2 b,Vector2 c){int n=vh.currentVertCount;vh.AddVert(a,color,Vector2.zero);vh.AddVert(b,color,Vector2.zero);vh.AddVert(c,color,Vector2.zero);vh.AddTriangle(n,n+1,n+2);}
            void Line(Vector2 a,Vector2 b,float width){var n=new Vector2(-(b-a).y,(b-a).x).normalized*width*.5f;Tri(a-n,a+n,b+n);Tri(a-n,b+n,b-n);}
            if(kind==Kind.Dot){for(int i=0;i<24;i++){float a=i*Mathf.PI*2/24,b=(i+1)*Mathf.PI*2/24;Tri(P(.5f,.5f),P(.5f+Mathf.Cos(a)*.42f,.5f+Mathf.Sin(a)*.42f),P(.5f+Mathf.Cos(b)*.42f,.5f+Mathf.Sin(b)*.42f));}}
            else if(kind==Kind.Play)Tri(P(.23f,.13f),P(.84f,.5f),P(.23f,.87f));
            else if(kind==Kind.Menu){foreach(float y in new[]{.25f,.5f,.75f})Line(P(.18f,y),P(.82f,y),2);}
            else{for(int i=0;i<14;i++){float a=(-65+i*230f/14)*Mathf.Deg2Rad,b=(-65+(i+1)*230f/14)*Mathf.Deg2Rad;Line(P(.5f+Mathf.Cos(a)*.32f,.47f+Mathf.Sin(a)*.32f),P(.5f+Mathf.Cos(b)*.32f,.47f+Mathf.Sin(b)*.32f),2);}Tri(P(.12f,.66f),P(.13f,.95f),P(.38f,.78f));}
        }
    }
}
