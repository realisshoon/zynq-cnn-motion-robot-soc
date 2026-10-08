using System;
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
        public bool Active=>router.Source=="CSV"&&Timeline!=null;
        public event Action<RecordedHumanRow> RowConsumed;
        public CsvInputMode Mode {get;private set;}
        public XyzSolveResult LastSolve {get;private set;}
        ControlStudioXyzPolicy xyz;
        bool faulted;
        public string ModeLabel=>Mode==CsvInputMode.RecordedHumanAngles?"Recorded human angles":Mode==CsvInputMode.XyzStoredBodyAuxGripper?"XYZ + stored BodyFrame + CSV gripper":"XYZ + stored BodyFrame / M4 HOLD";
        public bool Load(string path)
            =>Load(path,CsvInputMode.RecordedHumanAngles);
        public bool Load(string path,CsvInputMode mode)
        {
            ControlStudioXyzPolicy next=null;
            try{var pending=RecordedHumanCsv.Load(path,mode);if(mode!=CsvInputMode.RecordedHumanAngles)next=new ControlStudioXyzPolicy(router.Epoch+1,router.Applied[4],mode);if(!router.ResetOwner("CSV")){next?.Dispose();return false;}xyz?.Dispose();xyz=next;Mode=mode;Data=pending;Begin();Message="LOADED "+Data.Rows.Count+" rows / "+ModeLabel;return true;}
            catch(Exception e){next?.Dispose();Message="LOAD REJECTED (previous source kept): "+e.Message;return false;}
        }
        void Begin()
        {
            int epoch=router.Epoch;ConsumedRows=0;LastSolve=null;faulted=false;
            Timeline=new CsvReplayTimeline(Data,row=>{if(router.Source!="CSV"||router.Epoch!=epoch)throw new InvalidOperationException("SOURCE / EPOCH");var target=row;if(xyz!=null){LastSolve=xyz.Solve(row,epoch);target=LastSolve.Target;}router.SubmitHuman(target,epoch);ConsumedRows++;RowConsumed?.Invoke(row);},()=>router.CsvTick(epoch),()=>router.IsSettled||router.MotionHold!=0);
            router.SetPaused(true);
        }
        public void Restart(){if(Data==null){Message="Load CSV first";return;}ControlStudioXyzPolicy next=null;try{if(Mode!=CsvInputMode.RecordedHumanAngles)next=new ControlStudioXyzPolicy(router.Epoch+1,router.Applied[4],Mode);if(router.ResetOwner("CSV")){xyz?.Dispose();xyz=next;Begin();Message="RESTART / current Applied seed / new epoch / paused";}else next?.Dispose();}catch(Exception e){next?.Dispose();Message="XYZ UNAVAILABLE: "+e.Message;}}
        public void Play(){if(!Active){Restart();}if(faulted){Message="XYZ fault: Restart or switch source required";return;}if(Active&&router.Ready){if(Timeline.Finished){Message="EOF: Restart to replay";return;}router.SetPaused(false);Message="PLAYING / input then tick at ties";}}
        public void Pause(){if(!Active)return;router.SetPaused(true);Message="PAUSED / CSV clock and ticks frozen";}
        public void Step(){if(!Active||!router.Ready||faulted)return;router.SetPaused(true);try{Timeline.Step();Message=Timeline.Finished?EndStatus():"STEP / paused at next row time";}catch(Exception e){faulted=true;Message="XYZ REJECTED / Restart or switch source: "+e.Message;}}
        public void Advance(double dt)
        {
            if(!Active||!router.Ready||faulted)return;try{Timeline.Advance(dt);}catch(Exception e){faulted=true;router.SetPaused(true);Message="XYZ REJECTED / Restart or switch source: "+e.Message;return;}
            if(Timeline.Finished){Message=EndStatus();if(Loop){Restart();Play();}else router.SetPaused(true);}
            else if(Timeline.EndOfInput)Message="EOF / last approved trajectory settling";
        }
        string EndStatus()=>router.MotionHold!=0?"EOF / SAFETY HOLD (not settled at target)":"EOF / last approved target settled";
        public bool SelectManual()
        {
            if(!router.ResetOwner("MANUAL"))return false;
            GetComponent<ManualServoSource>().AdoptCurrent();Message="MANUAL / CSV stopped / no automatic Home";return true;
        }
        void OnDestroy(){xyz?.Dispose();}
    }
}
