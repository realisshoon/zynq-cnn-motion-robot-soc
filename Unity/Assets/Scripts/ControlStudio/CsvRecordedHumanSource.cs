using System;
using System.Threading.Tasks;
using UnityEngine;
namespace HumanMotion.ControlStudio
{
    public sealed class CsvRecordedHumanSource : MonoBehaviour
    {
        public SingleArmCommandRouter router;
        public RecordedHumanCsv Data {get;private set;}
        public CsvReplayTimeline Timeline {get;private set;}
        public RecordedHumanRow Current=>Data!=null&&Timeline!=null&&Timeline.Index>=0?Data.Rows[Timeline.Index]:null;
        public bool Loop {get;set;}
        public string Message {get;private set;}="Load CNN or MediaPipe sample";
        public int ConsumedRows {get;private set;}
        public bool Active=>router.Source=="CSV"&&Timeline!=null&&router.Epoch==boundEpoch;
        public event Action<RecordedHumanRow> RowConsumed;
        public CsvInputMode Mode {get;private set;}
        public XyzSolveResult LastSolve {get;private set;}
        ControlStudioXyzPolicy xyz;
        bool faulted;
        public CsvRoleFile File {get;private set;}
        public BoardSide Role {get;private set;}=BoardSide.Right;
        public bool Loading {get;private set;}
        public CsvRoleFile PendingFile {get;private set;}
        CsvInputMode pendingMode;int pendingEpoch,lastSelection=-1;bool bindingConfirmed;
        public bool NeedsConfirmation=>PendingFile!=null;
        public bool SingleTarget=>GetComponent<ControlStudioBoardControlPanel>()?.SelectedTarget!=2;
        public bool RoleAvailable=>!NeedsConfirmation&&(File==null||(File.Unassigned!=null?bindingConfirmed&&SingleTarget&&Role==SelectedRole:File.Streams.ContainsKey(SelectedRole)));
        public BoardSide SelectedRole=>GetComponent<ControlStudioBoardControlPanel>()?.SelectedTarget==1?BoardSide.Left:BoardSide.Right;
        public bool Stopped {get;private set;}
        int boundEpoch;
        public bool PrepareInput()=>router.Source=="CSV"||router.ResetOwner("CSV",SelectedRole);
        int loadGeneration;BoardSide? missingRole;
        public async void LoadAsync(string path,CsvInputMode mode,bool resumeOnFailure=false)
        {
            if(Loading)return;int generation=++loadGeneration,epoch=router.Epoch;string owner=router.Source;
            Loading=true;Message="CSV 검증 중 · 기존 파일 유지";
            try{var file=await Task.Run(()=>CsvRoleFile.Load(path,mode));
                if(this==null||generation!=loadGeneration||router.Epoch!=epoch||router.Source!=owner)return;
                if(!Install(file,mode)&&resumeOnFailure){string error=Message;Resume();Message=error;}
            }catch(Exception e){if(this!=null&&generation==loadGeneration){if(resumeOnFailure&&router.Epoch==epoch&&router.Source==owner)Resume();Message="파일 거부 · 기존 상태 유지: "+e.Message;}}
            finally{if(this!=null&&generation==loadGeneration)Loading=false;}
        }
        bool Install(CsvRoleFile file,CsvInputMode mode,bool confirmed=false)
        {
            ControlStudioXyzPolicy next=null;
            try{
                file.ValidateCompatibility();
                if(file.Unassigned!=null&&!confirmed){
                    if(router.Source!="CSV"&&!PrepareInput())return false;
                    Pause();bindingConfirmed=false;PendingFile=file;pendingMode=mode;pendingEpoch=router.Epoch;
                    Message="HOLD · 역할 없는 단일 스트림 · LEFT/RIGHT 대상을 확인하세요. BOTH 복제 금지";return true;
                }
                var role=SelectedRole;
                RecordedHumanCsv data=file.Unassigned;
                if(data!=null&&!SingleTarget)throw new FormatException("단일 스트림은 BOTH에 복제할 수 없습니다.");
                if(data==null&&!file.Streams.TryGetValue(role,out data))throw new FormatException("파일에 "+role+" 데이터 없음 · 다른 역할로 복제하지 않습니다.");
                if(mode!=CsvInputMode.RecordedHumanAngles&&role==BoardSide.Left&&data.Rows[0].Servo==null)throw new FormatException("기존 XYZ Native는 RIGHT 전용입니다. LEFT XYZ 인터페이스 없음");
                using(var validation=new ControlStudioOutputPolicy(SingleArmCommandRouter.Home,role))
                    foreach(var row in data.Rows)if(row.Servo!=null&&validation.Validate(row.Servo)!=0)throw new FormatException("Native role command validation failed: "+role);
                if(mode!=CsvInputMode.RecordedHumanAngles&&data.Rows[0].Servo==null)next=new ControlStudioXyzPolicy(router.Epoch+1,router.Applied[4],mode);
                if(!router.ResetOwner("CSV",role)){next?.Dispose();return false;}
                xyz?.Dispose();xyz=next;File=file;Role=role;missingRole=null;Mode=mode;Data=data;PendingFile=null;bindingConfirmed=true;Begin();Message="LOADED "+role+" / "+Data.Rows.Count+" / CSV";return true;
            }catch(Exception e){next?.Dispose();Message="LOAD REJECTED: "+e.Message;return false;}
        }
        public bool ConfirmSingleTarget(BoardSide? displayedRole=null,CsvRoleFile displayedFile=null)
        {
            if(PendingFile==null||!SingleTarget||router.Source!="CSV"||router.Epoch!=pendingEpoch||displayedRole.HasValue&&displayedRole.Value!=SelectedRole||displayedFile!=null&&!ReferenceEquals(displayedFile,PendingFile)){Message="HOLD · 명시적 단일 대상 확인 또는 CSV 재로드 필요";return false;}
            return Install(PendingFile,pendingMode,true);
        }
        public void CancelBinding(){if(PendingFile==null)return;PendingFile=null;missingRole=SelectedRole;Message="단일 스트림 연결 취소 · 마지막 자세 HOLD";}
        void Update(){
            int selected=GetComponent<ControlStudioBoardControlPanel>()?.SelectedTarget??0;
            if(lastSelection!=selected){lastSelection=selected;missingRole=null;}
            if(PendingFile!=null&&(router.Source!="CSV"||router.Epoch!=pendingEpoch)){PendingFile=null;Message="대상 확인 취소 · Source/Epoch 변경";}
            if(router!=null&&router.Source=="CSV"&&File!=null&&!NeedsConfirmation&&(Role!=SelectedRole||File.Unassigned!=null&&(!SingleTarget||!bindingConfirmed))&&missingRole!=SelectedRole){
                Pause();if(File.Unassigned!=null)Install(File,Mode);else if(File.Streams.ContainsKey(SelectedRole))Install(File,Mode);else{missingRole=SelectedRole;Message="HOLD · 파일에 "+SelectedRole+" 데이터 없음";}
            }
        }
        public void PlayFromStart(){if(Loading||!RoleAvailable||Role!=SelectedRole)return;Restart();Play();}
        public void Resume(){if(!Stopped&&!Loading&&RoleAvailable)Play();}
        public void Stop(){Pause();Stopped=true;Message="STOP / 마지막 자세 HOLD · UART TX 없음";}

        public string ModeLabel=>Mode==CsvInputMode.RecordedHumanAngles?"Recorded human angles":Mode==CsvInputMode.XyzStoredBodyAuxGripper?"XYZ + stored BodyFrame + CSV gripper":"XYZ + stored BodyFrame / M4 HOLD";
        public bool Load(string path)
            =>Load(path,CsvInputMode.RecordedHumanAngles);
        public bool Load(string path,CsvInputMode mode)
        {
            try{return Install(CsvRoleFile.Load(path,mode),mode);}catch(Exception e){Message="LOAD REJECTED (previous source kept): "+e.Message;return false;}
        }

        void Begin()
        {
            int epoch=router.Epoch;boundEpoch=epoch;ConsumedRows=0;LastSolve=null;faulted=false;Stopped=false;
            Timeline=new CsvReplayTimeline(Data,row=>{if(router.Source!="CSV"||router.Epoch!=epoch)throw new InvalidOperationException("SOURCE / EPOCH");if(row.Servo!=null){if(!router.SubmitCsvCommand(row.Servo,epoch))throw new InvalidOperationException(router.Status);ConsumedRows++;return; // Final commands are not human hand observations.
}var target=row;if(xyz!=null){LastSolve=xyz.Solve(row,epoch);target=LastSolve.Target;}router.SubmitHuman(target,epoch);ConsumedRows++;RowConsumed?.Invoke(row);},()=>router.CsvTick(epoch),()=>router.IsSettled||router.MotionHold!=0);
            router.SetPaused(true);
        }
        public void Restart(){if(Data==null){Message="Load CSV first";return;}if(File!=null&&!NeedsConfirmation&&RoleAvailable){Install(File,Mode,File.Unassigned!=null);return;}}
        public void Play(){if(Loading||!RoleAvailable||Role!=SelectedRole)return;if(!Active){Restart();}Stopped=false;if(faulted){Message="XYZ fault: Restart or switch source required";return;}if(Active&&router.Ready){if(Timeline.Finished){Message="EOF: Restart to replay";return;}router.SetPaused(false);Message="PLAYING / input then tick at ties";}}
        public void Pause(){if(!Active)return;router.SetPaused(true);Message="PAUSED / CSV clock and ticks frozen";}
        public void Step(){if(!Active||!RoleAvailable||Role!=SelectedRole||!router.Ready||faulted)return;router.SetPaused(true);try{Timeline.Step();Message=Timeline.Finished?EndStatus():"STEP / paused at next row time";}catch(Exception e){faulted=true;Message="XYZ REJECTED / Restart or switch source: "+e.Message;}}
        public void Advance(double dt)
        {
            if(!Active||!router.Ready||faulted||Stopped)return;if(Role!=SelectedRole||!RoleAvailable){Pause();return;}try{Timeline.Advance(dt);}catch(Exception e){faulted=true;router.SetPaused(true);Message="XYZ REJECTED / Restart or switch source: "+e.Message;return;}
            if(Timeline.Finished){Message=EndStatus();if(Loop){Restart();Play();}else router.SetPaused(true);}
            else if(Timeline.EndOfInput)Message="EOF / last approved trajectory settling";
        }
        string EndStatus()=>router.MotionHold!=0?"EOF / SAFETY HOLD (not settled at target)":"COMPLETE / last approved target settled";
        public bool SelectManual()
        {
            if(!router.ResetOwner("MANUAL"))return false;
            GetComponent<ManualServoSource>().AdoptCurrent();Message="MANUAL / CSV stopped / no automatic Home";return true;
        }
        void OnDestroy(){loadGeneration++;xyz?.Dispose();}
    }
}
