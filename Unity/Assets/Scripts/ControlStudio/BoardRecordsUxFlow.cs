using System;
using System.Text.RegularExpressions;

namespace HumanMotion.ControlStudio
{
    // KO FINAL UI coordinator. The existing controller, gateway and wire contract
    // remain the only command path. A sequence starts only on an explicit click.
    public sealed class BoardRecordsUxFlow : IDisposable
    {
        enum Phase { None, Save, PlayQuery, Menu, Select, Playing, ReturnQuery, Cancel, Live, Follow, ConfirmFollow, HudPrepare, HudSettle, HudNames, HudSavedList, IdleStop, IdleS, IdleConfirm }
        readonly BoardRecordingController records;
        readonly IBoardCommandGateway gateway;
        readonly Func<bool> active;
        readonly Func<double> now;
        Phase phase;
        int epoch, observedEpoch;
        long errors, gaps;
        double stateAt=double.NegativeInfinity, deadline;
        BoardRecordState observedState;
        BoardRequestState reply;
        string replyDetail="", savingName;
        uint requestedId, savedNotice, verifiedSaved;
        bool replace, saveFailed;
        bool autoSave, wantIdle, idleStopSent;
        string autoBaseName;
        uint selectSaved;
        readonly Func<DateTime> localDate;
        public uint SavedId=>SaveConfirmed?verifiedSaved:0;
        public string SavedName {get;private set;}="";
        public bool TakeSavedSelection(out uint id){id=selectSaved;if(id==0||!records.CatalogValid)return false;selectSaved=0;return true;}
        public string Message {get;private set;}="";
        public bool Busy=>phase!=Phase.None;
        public bool SaveConfirmed=>verifiedSaved!=0&&!saveFailed&&observedEpoch==gateway.Session.Generation;
        public bool Fresh=>gateway.Session.Trusted&&ReferenceEquals(observedState,gateway.Session.State)&&now()>=stateAt&&now()-stateAt<=5;
        public BoardRecordsUxFlow(BoardRecordingController records,Func<bool> active,Func<double> now,Func<DateTime> localDate=null)
        {
            this.records=records??throw new ArgumentNullException(nameof(records));gateway=records.Gateway;this.active=active;this.now=now;
            this.localDate=localDate??(()=>DateTime.UtcNow.AddHours(9));
            observedEpoch=gateway.Session.Generation;gateway.Session.LineReceived+=Observe;
        }
        void Observe(string line,double time)
        {
            if(observedEpoch!=gateway.Session.Generation){observedEpoch=gateway.Session.Generation;verifiedSaved=savedNotice=selectSaved=0;SavedName="";saveFailed=false;observedState=null;stateAt=double.NegativeInfinity;}
            if(line.StartsWith("[REC_STATE]",StringComparison.Ordinal)&&gateway.Session.Trusted){observedState=gateway.Session.State;stateAt=time;}
            if(line.StartsWith("[REC] SAVE_FAILED",StringComparison.Ordinal)){saveFailed=true;verifiedSaved=0;}
            // A SAVED notice belongs to this explicit NAME request, and is still
            // provisional until the original controller correlates resulting state.
            if(phase==Phase.Save&&savingName!=null){
                var m=Regex.Match(line,@"^\[REC\] SAVED id=([0-9]+) name="+Regex.Escape(savingName)+@" samples=([0-9]+)(?:;|$)");
                if(m.Success&&uint.TryParse(m.Groups[1].Value,out uint id)&&id>=1&&id<=32&&uint.TryParse(m.Groups[2].Value,out uint samples)&&samples>=1&&samples<=2048)savedNotice=id;
            }
        }
        bool Deny(string text,out string why){why=text;return false;}
        bool Ready(out string why)
        {
            if(Busy||gateway.Busy)return Deny("보드 응답 대기 중입니다. 명령을 반복하지 마세요.",out why);
            if(!active()||gateway.IsSimulation)return Deny("실보드 송신 조건 미충족 또는 Fixture 수신 전용입니다.",out why);
            if(!gateway.Session.Connected)return Deny("보드가 연결되어 있지 않습니다.",out why);
            if(gateway.Session.Fault.Length!=0)return Deny(gateway.Session.Fault,out why);
            if(!gateway.BoundaryConfirmed||!gateway.Session.AtLineBoundary)return Deny("기존 입력 경계 확인이 필요합니다.",out why);
            why="";return true;
        }
        bool PlayState(uint id,bool replacement,bool fresh,out string why)
        {
            var s=gateway.Session;
            if(!s.Trusted||fresh&&!Fresh)return Deny("최신 보드 REC_STATE를 확인하세요.",out why);
            var st=s.State;
            if(!st.Pwm)return Deny("PWM OFF입니다. E는 자동 전송하지 않습니다.",out why);
            if(!(st.Ui=="IDLE"&&st.Mode=="LIVE"||st.Ui=="MENU"&&st.Mode=="HOLDING")||st.Delete!=0)
                return Deny("이름 입력·저장·정착·녹화·재생·삭제 확인 상태에서는 PLAY를 요청하지 않습니다.",out why);
            records.Sync();bool listed=false;foreach(var entry in records.Entries)if(entry.Id==id)listed=true;
            if(!records.CatalogValid||!listed)return Deny("현재 보드의 완전한 SD 목록에서 기록을 선택하세요.",out why);
            if(records.ManualOpen!=false)return Deny("그리퍼 수동 열림 상태를 확인하세요. 기존 H 응답이 필요하며 자동 H는 없습니다.",out why);
            if(st.Unsaved&&!replacement)return Deny("미저장 RAM 교체를 먼저 확인하세요.",out why);
            why="";return true;
        }
        public bool CanPlay(uint id,bool replacement,out string why)=>Ready(out why)&&PlayState(id,replacement,false,out why);
        public bool CanReturn(out string why)
        {
            if(!Ready(out why))return false;
            var s=gateway.Session;
            if(!s.Trusted)return Deny("상태 미확인 — 기존 Ports에서 V로 조회하세요.",out why);
            if(!ReturnState(false,out why))return false;
            return true;
        }
        void Begin(Phase next){phase=next;epoch=gateway.Session.Generation;errors=gateway.Session.ParseErrors;gaps=gateway.Session.MissingTicks;deadline=now()+35;reply=BoardRequestState.Idle;replyDetail="";}
        bool Stop(string text,out string why){Message=why="중단: "+text+" / 자동 재시도 없음.";phase=Phase.None;return false;}
        public bool Save(string name,out string why)
        {
            if(!Ready(out why)){Message=why;return false;}
            autoSave=false;
            Begin(Phase.Save);savingName=name;savedNotice=verifiedSaved=0;
            if(!records.SaveName(name,out why))return Stop(why,out why);
            Message="SD에 저장 요청 — SAVED와 최신 저장 완료 상태를 기다립니다.";return true;
        }
        public bool Play(uint id,bool replacement,out string why)
        {
            if(!CanPlay(id,replacement,out why)){Message=why;return false;}
            requestedId=id;replace=replacement;Begin(Fresh?Phase.Menu:Phase.PlayQuery);
            Message="선택 기록 재생 요청 — 추종 시작 없이 메뉴 정착과 재생 승인을 확인합니다.";
            if(phase==Phase.PlayQuery){if(!Query('V',out why))return Stop(why,out why);return true;}
            return EnterPlay(out why);
        }
        bool EnterPlay(out string why)
        {
            if(!PlayState(requestedId,replace,true,out why))return Stop(why,out why);
            if(gateway.Session.State.Ui=="MENU"){
                phase=Phase.Select;if(!records.Select(requestedId,replace,out why))return Stop(why,out why);
            }else{
                phase=Phase.Menu;if(!records.OpenMenu(out why))return Stop(why,out why);
            }
            return true;
        }
        bool Query(char key,out string why)
        {
            reply=BoardRequestState.Queued;
            return gateway.TryRequest(new BoardRequest{Name="Records "+key,Wire=new[]{(byte)key},RequireTrusted=false,IsRecovery=key=='V',TimeoutSeconds=5,
                Match=(s,line)=>key=='V'?line.StartsWith("[REC_STATE]",StringComparison.Ordinal)&&s.Trusted?BoardReply.Accepted:BoardReply.Ignore:
                    line.StartsWith("[ST]",StringComparison.Ordinal)&&s.FollowEnabled.HasValue?BoardReply.Accepted:BoardReply.Ignore,
                Completed=(state,detail)=>{reply=state;replyDetail=detail;}},out why);
        }
        bool ReturnState(bool fresh,out string why)
        {
            var st=gateway.Session.State;
            if(!gateway.Session.Trusted||fresh&&!Fresh)return Deny("REC_STATE 미확인/오래됨 — 상태 조회가 필요합니다.",out why);
            if(st.Unsaved||saveFailed)return Deny("미저장 RAM 또는 저장 실패입니다. 이름 저장을 완료하세요. CANCEL은 전송하지 않습니다.",out why);
            if(st.Delete!=0||!(st.Ui=="MENU"&&st.Mode=="HOLDING"||st.Ui=="IDLE"&&st.Mode=="LIVE"))
                return Deny("이름 입력·저장·정착·재생 상태에서 추종 복귀하지 않습니다.",out why);
            if(st.Ui=="MENU"&&(!SaveConfirmed||st.Selected!=verifiedSaved))return Deny("현재 세션의 SAVED 및 동일 기록 ID의 저장 완료 확인이 없습니다. 자동 CANCEL은 없습니다.",out why);
            if(!st.Pwm)return Deny("PWM OFF입니다. 자동 E는 없습니다.",out why);
            if(records.ManualOpen!=false)return Deny("그리퍼 수동 열림/상태 미확인 — 기존 H 응답 확인이 필요합니다.",out why);
            why="";return true;
        }
        public bool ReturnToFollow(out string why)
        {
            // V is harmless telemetry, but still requires the original TX guards.
            if(!Ready(out why)){Message=why;return false;}
            wantIdle=false;Begin(Phase.ReturnQuery);if(!Query('V',out why))return Stop(why,out why);
            Message="추종 복귀 요청 — V로 최신 상태 확인 중. 미저장 RAM을 자동 취소하지 않습니다.";return true;
        }
        public void StopWaiting(){phase=Phase.None;Message="사용자가 재생 중지를 요청했습니다. 기존 P 응답으로 실제 상태를 확인하세요.";}
        public void ClearNotice(){if(!Busy)Message="";}
        public bool CanHudRecord(out string why)
        {
            if(!Ready(out why))return false;var s=gateway.Session;
            if(!s.Trusted||!Fresh)return Deny("최신 보드 상태 확인이 필요합니다.",out why);
            if(!s.State.Pwm)return Deny("PWM 활성화 필요",out why);
            if(s.State.Ui=="IDLE"&&s.State.Mode=="RECORDING"&&records.RecordingActive){why="";return true;}
            if(s.State.Ui!="IDLE"||s.State.Mode!="LIVE"||s.State.Unsaved)return Deny("LIVE 상태 및 미저장 RAM 정리가 필요합니다.",out why);
            if(s.FollowEnabled!=true)return Deny("추종 시작 승인 필요",out why);
            why="";return true;
        }
        public bool HudRecord(out string why)
        {
            if(!CanHudRecord(out why)){Message=why;return false;}
            if(gateway.Session.State.Mode=="RECORDING"){
                autoBaseName=localDate().ToString("MMddHHmm",System.Globalization.CultureInfo.InvariantCulture);
                Begin(Phase.HudSettle);if(!records.FinishRecord(out why))return Stop(why,out why);
                Message="녹화 종료 요청 — 정착 및 NAME 확인 후 이름 중복 검사/SD 저장";
            }else{
                Begin(Phase.HudPrepare);if(!records.StartRecord(false,out why))return Stop(why,out why);
                Message="녹화 준비 중 — 실제 RECORDING 응답 대기";
            }return true;
        }
        public bool RefreshCatalog(out string why){if(!Ready(out why)){Message=why;return false;}ClearNotice();return records.RefreshList(out why);}
        public bool HudStopPlayback(out string why)
        {if(phase==Phase.Playing&&!gateway.Busy)StopWaiting();if(!Ready(out why)){Message=why;return false;}StopWaiting();return records.StopPlayback(out why);}
        public bool CanHudStop=>!gateway.Busy&&(!Busy||phase==Phase.Playing)&&active()&&!gateway.IsSimulation&&gateway.Session.Trusted&&gateway.Session.State.Ui=="IDLE"&&(gateway.Session.State.Mode=="ALIGNING"||gateway.Session.State.Mode=="PLAYING"||gateway.Session.State.Mode=="HOLDING");
        public bool ReturnToIdle(out string why)
        {
            if(!Ready(out why)){Message=why;return false;}
            wantIdle=true;idleStopSent=false;Begin(Phase.ReturnQuery);if(!Query('V',out why))return Stop(why,out why);
            Message="PWM 대기 요청 — 출력 변경 없이 최신 상태 확인";return true;
        }
        bool IdleState(out string why)
        {
            var s=gateway.Session;var st=s.State;
            if(!Fresh)return Deny("최신 보드 상태 확인 필요",out why);
            if(st.Unsaved||saveFailed)return Deny("미저장 RAM/저장 실패 — 취소하지 않습니다.",out why);
            if(!st.Pwm)return Deny("PWM 활성화 필요 — 자동 E 없음",out why);
            if(st.Delete!=0||!(st.Ui=="IDLE"&&(st.Mode=="LIVE"||st.Mode=="ALIGNING"||st.Mode=="PLAYING"||st.Mode=="HOLDING")||st.Ui=="MENU"&&st.Mode=="HOLDING"))return Deny("녹화·이름 입력·저장·정착 중 복귀할 수 없습니다.",out why);
            why="";return true;
        }
        bool StopFollow(out string why)
        {
            if(!BoardControlCommands.CanSend(gateway,'S',out why))return false;
            reply=BoardRequestState.Queued;
            return gateway.TryRequest(new BoardRequest{Name="Records explicit PWM idle S",IsStop=true,RequireTrusted=false,
                Wire=gateway.Session.StopEscape?new[]{(byte)27,(byte)'S'}:new[]{(byte)'S'},
                Match=(s,line)=>BoardControlCommands.Match(s.Side,'S',line),Completed=(state,detail)=>{reply=state;replyDetail=detail;}},out why);
        }
        public void Advance()
        {
            if(!Busy)return;
            var s=gateway.Session;string why;
            if(!active()||gateway.IsSimulation||!s.Connected||epoch!=s.Generation||s.Fault.Length!=0||errors!=s.ParseErrors||gaps!=s.MissingTicks){Stop("연결·역할·세션 또는 송신 승인 상태가 바뀌었습니다.",out _);return;}
            if(now()>deadline){Stop("정착 또는 LIVE/PLAYING 응답 시간 초과 — 최신 상태를 조회하세요.",out _);return;}
            if(gateway.Busy)return;
            bool query=phase==Phase.PlayQuery||phase==Phase.ReturnQuery||phase==Phase.Follow||phase==Phase.ConfirmFollow||phase==Phase.IdleS||phase==Phase.IdleConfirm;
            var result=query?reply:records.LastRequestState;
            if(result==BoardRequestState.Unknown||result==BoardRequestState.TimedOut||result==BoardRequestState.Rejected||result==BoardRequestState.Cancelled){Stop(query?"요청 실패: "+replyDetail:records.Message,out _);return;}
            if(phase==Phase.Save){
                if(result!=BoardRequestState.Accepted)return;
                if(savedNotice==0||!Fresh||s.State.Ui!="MENU"||s.State.Mode!="HOLDING"||s.State.Unsaved||s.State.Selected!=savedNotice){Stop("SD 저장 완료 증거가 일치하지 않습니다.",out _);return;}
                verifiedSaved=savedNotice;SavedName=savingName;saveFailed=false;phase=Phase.None;Message="저장 완료 · ["+SavedName+"] — 자동 A 없음";
                if(autoSave){autoSave=false;phase=Phase.HudSavedList;if(!records.RefreshList(out why))Stop("저장은 완료됐지만 목록 갱신 실패: "+why,out _);}return;
            }
            if(phase==Phase.HudPrepare){if(result==BoardRequestState.Accepted&&records.RecordingActive){phase=Phase.None;Message="녹화 중";}return;}
            if(phase==Phase.HudSettle){
                if(result!=BoardRequestState.Accepted||!Fresh||s.State.Ui!="NAME"||s.State.Mode!="HOLDING")return;
                if(!s.State.Unsaved){Stop("NAME의 RAM 기록이 확인되지 않습니다.",out _);return;}
                phase=Phase.HudNames;if(!records.RefreshList(out why))Stop("중복 이름 확인 실패 — RAM 보존: "+why,out _);return;
            }
            if(phase==Phase.HudNames){
                if(result!=BoardRequestState.Accepted)return;
                if(!Fresh||s.State.Ui!="NAME"||s.State.Mode!="HOLDING"||!s.State.Unsaved||!records.CatalogValid){Stop("이름 중복 확인 실패 — RAM 보존",out _);return;}
                if(records.Entries.Count>=32){Stop("SD 기록 슬롯 부족 — RAM 보존",out _);return;}
                var names=new System.Collections.Generic.HashSet<string>(StringComparer.Ordinal);foreach(var e in records.Entries)names.Add(e.Name);
                string name=autoBaseName;for(int i=1;names.Contains(name)&&i<=32;i++)name=autoBaseName+"_"+i.ToString("00",System.Globalization.CultureInfo.InvariantCulture);
                if(names.Contains(name)){Stop("중복되지 않는 이름을 확인할 수 없습니다 — RAM 보존",out _);return;}
                phase=Phase.None;if(Save(name,out why))autoSave=true;return;
            }
            if(phase==Phase.HudSavedList){
                if(result!=BoardRequestState.Accepted)return;
                bool found=false;foreach(var e in records.Entries)if(e.Id==verifiedSaved&&e.Name==SavedName)found=true;
                if(!records.CatalogValid||!found){Stop("저장 완료, 목록에서 동일 ID/이름 확인 실패 — 직접 목록 조회 필요",out _);return;}
                selectSaved=verifiedSaved;phase=Phase.None;Message="저장 완료 · ["+SavedName+"] · 선택됨";return;
            }
            // STOP_MENU can outlast the status age. No write is authorized until
            // a new settled MENU/HOLDING report arrives within the bounded wait.
            if(!Fresh&&phase==Phase.Menu)return;
            if(!Fresh){Stop("최신 REC_STATE가 없습니다. 상태를 조회하세요.",out _);return;}
            var st=s.State;
            switch(phase){
                case Phase.PlayQuery:if(result==BoardRequestState.Accepted)EnterPlay(out _);break;
                case Phase.Menu:
                    if(result!=BoardRequestState.Accepted||st.Ui!="MENU"||st.Mode!="HOLDING")return;
                    if(!PlayState(requestedId,replace,true,out why)){Stop(why,out _);return;}
                    phase=Phase.Select;if(!records.Select(requestedId,replace,out why))Stop(why,out _);break;
                case Phase.Select:if(result==BoardRequestState.Accepted)phase=Phase.Playing;break;
                case Phase.Playing:
                    if(st.Pwm&&st.Ui=="IDLE"&&st.Playing==requestedId&&st.Mode=="PLAYING"){phase=Phase.None;Message="선택 기록 PLAYING 확인 — 기존 ALIGN/안전검사 유지.";}
                    else if(st.Mode!="ALIGNING"||!st.Pwm)Stop("재생 승인 후 PLAYING 확인 실패 — 현재 상태를 조회하세요.",out _);break;
                case Phase.ReturnQuery:
                    if(result!=BoardRequestState.Accepted)return;
                    if(wantIdle){
                        if(!IdleState(out why)){Stop(why,out _);return;}
                        if(st.Ui=="MENU"){phase=Phase.Cancel;if(!records.Cancel(false,out why))Stop(why,out _);}
                        else if(st.Mode!="LIVE"){phase=Phase.IdleStop;if(!records.StopPlayback(out why))Stop(why,out _);}
                        else phase=Phase.Live;break;
                    }
                    if(!ReturnState(true,out why)){Stop(why,out _);return;}
                    if(st.Ui=="MENU"){phase=Phase.Cancel;if(!records.Cancel(false,out why))Stop(why,out _);}else phase=Phase.Live;break;
                case Phase.Cancel:if(result==BoardRequestState.Accepted)phase=Phase.Live;break;
                case Phase.IdleStop:if(result==BoardRequestState.Accepted)phase=Phase.Live;break;
                case Phase.Live:
                    if(st.Ui!="IDLE"||st.Mode!="LIVE")return;
                    if(wantIdle){if(!IdleState(out why)){Stop(why,out _);return;}phase=Phase.IdleConfirm;if(!Query('T',out why))Stop(why,out _);break;}
                    if(!ReturnState(true,out why)||!BoardControlCommands.CanSend(gateway,'A',out why)){Stop(why,out _);return;}
                    reply=BoardRequestState.Queued;phase=Phase.Follow;
                    if(!gateway.TryRequest(new BoardRequest{Name="Records explicit follow A",Wire=new[]{(byte)'A'},
                        Match=(session,line)=>BoardControlCommands.Match(session.Side,'A',line),Completed=(state,detail)=>{reply=state;replyDetail=detail;}},out why))Stop(why,out _);break;
                case Phase.Follow:if(result==BoardRequestState.Accepted){phase=Phase.ConfirmFollow;if(!Query('T',out why))Stop(why,out _);}break;
                case Phase.ConfirmFollow:
                    if(result!=BoardRequestState.Accepted)return;
                    if(st.Ui!="IDLE"||st.Mode!="LIVE"||!st.Pwm||s.FollowEnabled!=true){Stop("T 및 REC_STATE에서 추종 승인 확인 실패 — 재요청하지 않습니다.",out _);return;}
                    phase=Phase.None;Message="LIVE 추종 복귀 승인 — A 응답 및 T의 추종 ON 확인. 실제 서보 자세는 미확인.";break;
                case Phase.IdleS:if(result==BoardRequestState.Accepted){phase=Phase.IdleConfirm;if(!Query('T',out why))Stop(why,out _);}break;
                case Phase.IdleConfirm:
                    if(result!=BoardRequestState.Accepted)return;
                    if(!IdleState(out why)||st.Ui!="IDLE"||st.Mode!="LIVE"){Stop("PWM 대기 상태 확인 실패: "+why,out _);return;}
                    if(s.FollowEnabled==true){if(idleStopSent){Stop("S 이후 추종 OFF 확인 실패 — 재전송 없음",out _);return;}idleStopSent=true;phase=Phase.IdleS;if(!StopFollow(out why))Stop(why,out _);}
                    else if(s.FollowEnabled==false){phase=Phase.None;Message="PWM 대기 확인 · PWM ON / FOLLOW OFF / LIVE IDLE (실측 출력 미확인)";}
                    else Stop("추종 상태 미확인",out _);break;
            }
        }
        public void Dispose(){gateway.Session.LineReceived-=Observe;phase=Phase.None;}
    }
}
