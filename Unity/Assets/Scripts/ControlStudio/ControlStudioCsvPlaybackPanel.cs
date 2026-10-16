using System;
using System.IO;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioCsvPlaybackPanel : MonoBehaviour
    {
        public float Height {get;private set;}
        public RectTransform Root {get;private set;}
        ControlStudioRuntimeUI studio;ControlStudioBoardControlPanel control;Text title,file,status,clock,output;
        Font font;Button load,confirm,cancel;readonly Button[] roles=new Button[3],transport=new Button[4];
        bool choosing;string chooserError="";double nextUpdate;
        BoardSide displayedRole;CsvRoleFile displayedFile;
        static readonly Color Enabled=new Color(.30f,.43f,.51f),Disabled=new Color(.17f,.19f,.21f),Selected=new Color(.12f,.43f,.44f);
        RectTransform Rect(string name,Transform parent)=>ControlStudioDockLayout.Place(new GameObject(name,typeof(RectTransform)).transform,parent,0,0,300,32);
        Text Label(string name){var t=Rect(name,Root).gameObject.AddComponent<Text>();t.font=font;t.fontSize=14;t.color=new Color(.81f,.85f,.90f);t.raycastTarget=false;t.horizontalOverflow=HorizontalWrapMode.Wrap;return t;}
        Button Button(string name,string caption,UnityEngine.Events.UnityAction action){var r=Rect(name,Root);var image=r.gameObject.AddComponent<Image>();var b=r.gameObject.AddComponent<Button>();b.targetGraphic=image;b.transition=Selectable.Transition.None;b.onClick.AddListener(action);var t=Rect(name+" label",r).gameObject.AddComponent<Text>();t.font=font;t.fontSize=14;t.text=caption;t.color=Color.white;t.alignment=TextAnchor.MiddleCenter;t.raycastTarget=false;t.rectTransform.anchorMin=Vector2.zero;t.rectTransform.anchorMax=Vector2.one;t.rectTransform.offsetMin=t.rectTransform.offsetMax=Vector2.zero;return b;}
        public void Initialize(ControlStudioRuntimeUI owner,Transform parent){studio=owner;control=GetComponent<ControlStudioBoardControlPanel>();font=Font.CreateDynamicFontFromOSFont(new[]{"Malgun Gothic","Segoe UI"},14);Root=Rect("CSV compact playback",parent);
            title=Label("CSV compact title");title.text="CSV PLAYBACK";
            for(int i=0;i<3;i++){int target=i;roles[i]=Button("CSV target "+i,new[]{"RIGHT","LEFT","BOTH"}[i],()=>control.TrySelectTarget(target));}
            load=Button("CSV choose file","CSV 파일 불러오기",ChooseFile);file=Label("CSV selected file");
            output=Label("CSV board delivery");
            confirm=Button("CSV confirm single target","단일 스트림 대상 연결 확인",()=>studio.router.Csv.ConfirmSingleTarget(displayedRole,displayedFile));
            cancel=Button("CSV cancel binding","취소",()=>studio.router.Csv.CancelBinding());
            transport[0]=Button("CSV start","PLAY",()=>studio.router.Csv.PlayFromStart());transport[1]=Button("CSV pause compact","PAUSE",()=>studio.router.Csv.Pause());transport[2]=Button("CSV resume compact","RESUME",()=>studio.router.Csv.Resume());transport[3]=Button("CSV stop compact","STOP",()=>studio.router.Csv.Stop());clock=Label("CSV progress");status=Label("CSV playback state");
        }
        public async void ChooseFile(){if(choosing||studio.router.Csv.Loading)return;choosing=true;chooserError="";var csv=studio.router.Csv;int epoch=studio.router.Epoch;bool playing=csv.Active&&!studio.router.Paused;csv.Pause();
            try{string path=await WindowsCsvFilePicker.Pick();if(this==null)return;if(studio.router.Source!="CSV"||studio.router.Epoch!=epoch)return;
                if(path==null){if(playing)csv.Resume();return;}studio.CsvPath.text=path;csv.LoadAsync(path,studio.PendingMode,playing);
            }catch(Exception e){if(this!=null){chooserError="파일 선택 실패: "+e.Message;Debug.LogWarning(chooserError);}}finally{if(this!=null)choosing=false;}
        }
        void Put(Component c,float x,float y,float w,float h)=>ControlStudioDockLayout.Place(c.transform,Root,x,y,w,h);
        public void Layout(float width,float fit,bool visible){if(Root==null)return;Root.gameObject.SetActive(visible);if(!visible)return;float unit=1/fit,gap=6;float w=(width-2*gap)/3;
            Put(title,0,0,width,28*unit);for(int i=0;i<3;i++)Put(roles[i],i*(w+gap),34*unit,w,34*unit);
            Put(load,0,78*unit,width,38*unit);Put(file,0,122*unit,width,65*unit);
            
            Put(output,0,193*unit,width,74*unit);Put(confirm,0,273*unit,width*.75f-gap,35*unit);Put(cancel,width*.75f,273*unit,width*.25f,35*unit);w=(width-gap)/2;
            for(int i=0;i<4;i++)Put(transport[i],i%2*(w+gap),(316+i/2*40)*unit,w,34*unit);
            Put(clock,0,399*unit,width,44*unit);Put(status,0,448*unit,width,160*unit);Height=615*unit;Root.sizeDelta=new Vector2(width,Height);
            if(Time.realtimeSinceStartupAsDouble<nextUpdate)return;nextUpdate=Time.realtimeSinceStartupAsDouble+.1;
            var csv=studio.router.Csv;bool ready=csv.Active&&csv.RoleAvailable&&!csv.Loading&&!choosing;
            var shown=csv.PendingFile??csv.File;
            file.text=shown==null?"파일 미선택 · Source 상세에서 입력 모드 선택 가능":Path.GetFileName(shown.Path)+"\n"+shown.Schema+" · "+(csv.NeedsConfirmation?"대상 연결 확인 필요":csv.RoleAvailable?csv.SelectedRole.ToString():csv.SelectedRole+" 데이터 없음 / HOLD")+(control.SelectedTarget==2?" (BOTH → RIGHT Visual)":"");
            if(csv.NeedsConfirmation){file.text+="\n"+shown.Unassigned.Rows.Count+" rows / "+shown.Unassigned.Duration.ToString("F2")+"s";}
            output.text=CsvBoardDeliveryStatus.Describe(studio.GetComponent<BoardUartSource>(),control.SelectedTarget);
            displayedRole=csv.SelectedRole;displayedFile=csv.PendingFile;
            confirm.gameObject.SetActive(csv.NeedsConfirmation);cancel.gameObject.SetActive(csv.NeedsConfirmation);
            confirm.GetComponentInChildren<Text>().text=csv.SingleTarget?csv.SelectedRole+" / Robot"+(csv.SelectedRole==BoardSide.Left?"1":"0")+" 연결 확인":"LEFT 또는 RIGHT를 선택하세요";
            confirm.interactable=csv.SingleTarget&&!csv.Loading;confirm.GetComponent<Image>().color=confirm.interactable?Enabled:Disabled;cancel.GetComponent<Image>().color=Enabled;
            clock.text=csv.Timeline==null?"00:00 / --":$"{csv.Timeline.Time:F2}s / {csv.Data.Duration:F2}s · {Math.Min(100,csv.Timeline.Time/Math.Max(.001,csv.Data.Duration)*100):F0}%";
            if(csv.NeedsConfirmation)clock.text="대상 확인 대기 / "+shown.Unassigned.Duration.ToString("F2")+"s";
            status.text=(chooserError.Length>0?chooserError:csv.Message)+"\nCSV → 기존 Native 안전 궤적 → G51\n관절 명령이며 실측 위치가 아닙니다.\n보드 승인과 Unity 적용은 별도 · 자동 E/A 없음";
            for(int i=0;i<3;i++){roles[i].GetComponent<Image>().color=control.SelectedTarget==i?Selected:Enabled;}
            load.interactable=!choosing&&!csv.Loading;load.GetComponent<Image>().color=load.interactable?Enabled:Disabled;
            for(int i=0;i<4;i++){transport[i].interactable=ready&&(i==0||i==1&&!studio.router.Paused||i==2&&studio.router.Paused&&!csv.Stopped&&!csv.Timeline.Finished||i==3);transport[i].GetComponent<Image>().color=transport[i].interactable?Enabled:Disabled;}
        }
    }
}