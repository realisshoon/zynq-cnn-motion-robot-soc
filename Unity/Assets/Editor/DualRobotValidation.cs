using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using UnityEditor;
using UnityEngine;
using Object = UnityEngine.Object;

[InitializeOnLoad]
public static class DualRobotValidation
{
    private const string Key="DualRobot.Validation.Active";
    private static int phase;
    private static double deadline, nextTelemetry;
    private static readonly List<string> results=new List<string>();
    private static readonly HashSet<DualArmDemoController.Stage> stages=new HashSet<DualArmDemoController.Stage>();
    private static Vector3 leftBefore;
    private static Quaternion leftRotation;
    private static bool lastPlaying;
    static DualRobotValidation() {EditorApplication.update+=Tick;}

    [MenuItem("Tools/Human Motion/Dual Robot Demo/Run Validation (Play Mode) %#F10")]
    public static void Run()
    {
        if(Object.FindFirstObjectByType<DualArmDemoController>()==null) throw new InvalidOperationException("DualRobotDemo Scene을 먼저 여세요.");
        results.Clear(); stages.Clear(); phase=0;
        SessionState.SetBool(Key,true); EditorApplication.isPlaying=true;
    }
    private static void Check(bool condition,string label)
    {if(!condition) throw new InvalidOperationException(label); results.Add("- PASS: "+label);}
    private static void Tick()
    {
        if(!SessionState.GetBool(Key,false)) return;
        if(!EditorApplication.isPlaying)
        {
            if(lastPlaying)
            {
                try {using(var socket=new UdpClient(5005)) Check(true,"Play 종료 후 UDP 5005 재bind 가능");}
                catch(Exception ex) {results.Add("- FAIL: Play 종료 port 해제: "+ex.Message);}
                WriteReport(); SessionState.SetBool(Key,false); lastPlaying=false;
            }
            return;
        }
        lastPlaying=true;
        var d=Object.FindFirstObjectByType<DualArmDemoController>();
        if(d==null) return;
        try
        {
            if(EditorApplication.timeSinceStartup>=nextTelemetry)
            {
                // Windows reader가 잠깐 파일을 잡고 있으면 다음 sample에서 재시도한다.
                try { WriteTelemetry(d); } catch(IOException) { }
                nextTelemetry=EditorApplication.timeSinceStartup+0.05;
            }
            if(phase==0 && Time.timeSinceLevelLoad>0.5f)
            {
                DualRobotDemoBuilder.ValidateReferences(d.leftArm);DualRobotDemoBuilder.ValidateReferences(d.rightArm);
                Check(d.leftArm!=d.rightArm,"두 팔 내부 component reference 독립 / 양수 scale");
                Check(Object.FindObjectsByType<UdpJointCommandReceiver>(FindObjectsInactive.Include,FindObjectsSortMode.None).Length==1,"UDP receiver component 정확히 1개");
                var frame=d.transfer.transform.Find("CenterFrame");
                Check(frame.Find("VerticalColumn")!=null && frame.Find("HorizontalBeam")!=null,"T-frame: 중앙 기둥 / 수평 빔");
                Check(Vector3.Distance(frame.Find("LeftMount").position,d.leftArm.transform.position)<0.00001f &&
                    Vector3.Distance(frame.Find("RightMount").position,d.rightArm.transform.position)<0.00001f,"좌우 robot root가 해당 Mount 위치에 고정");
                Check(Vector3.Dot(d.leftArm.transform.up,Vector3.down)>0.999f && Vector3.Dot(d.rightArm.transform.up,Vector3.down)>0.999f,"root 회전만으로 아래쪽 장착 / pivot 방향 보존");
                foreach(var c in new[]{d.leftArm,d.rightArm})
                {
                    Check(c.GetComponentsInChildren<Transform>().Count(t=>t.name.StartsWith("__G51V2_"))>=35,c.name+" G51 v2 visual 보존");
                    Check(c.GetComponentsInChildren<Transform>().Count(t=>t.name.StartsWith("__G51V3_"))==8,c.name+" G51 v3 bolt visual 8개 보존 (Scene 조명 제외)");
                }
                var c0=Object.Instantiate(d.leftArm.gameObject).GetComponent<RobotArmController>();
                try
                {
                    c0.inputMode=RobotArmController.InputMode.Manual;
                    var c=JointCommandData.Neutral; c.shoulder_deg=140;c.elbow_deg=40;
                    c0.testCommand=c; Check(c0.ApplyTestCommand(),"기존 Manual command API 적용");
                    Check(Mathf.Abs(Mathf.DeltaAngle(c0.shoulderPitch.pivot.localEulerAngles.x,50))<0.01f,"Shoulder 90→140");
                    Check(Mathf.Abs(Mathf.DeltaAngle(c0.elbowPitch.pivot.localEulerAngles.x,-50))<0.01f,"Elbow 90→40");
                    c.elbow_deg=90;c0.ApplyCommand(c);
                    Check(Mathf.Abs(Mathf.DeltaAngle(c0.elbowPitch.pivot.localEulerAngles.x,0))<0.01f,"Elbow 40→90");
                    c.gripper_norm=0;c0.ApplyCommand(c);var q=c0.gripperVisual.leftJawPivot.localRotation;
                    c.gripper_norm=1;c0.ApplyCommand(c);
                    Check(Quaternion.Angle(q,c0.gripperVisual.leftJawPivot.localRotation)>1,"G51 linkage gripper 0→1");
                    var before=c0.wristPitch.pivot.localRotation;c.valid=false;c.wrist_pitch_deg=0;
                    Check(!c0.ApplyCommand(c)&&Quaternion.Angle(before,c0.wristPitch.pivot.localRotation)<0.001f,"invalid command HOLD");
                }
                finally {Object.DestroyImmediate(c0.gameObject);}
                d.SelectMode(DualArmDemoController.DemoMode.ScriptedHandoff);d.StartDemo();phase=1;
                deadline=EditorApplication.timeSinceStartup+30;
            }
            else if(phase==1)
            {
                stages.Add(d.CurrentStage);
                if(d.leftArm.wristRoll.pivot.position.x>=-d.transfer.ballRadius || d.rightArm.wristRoll.pivot.position.x<=d.transfer.ballRadius)
                    throw new Exception("scripted handoff 중 wrist 중심선의 좌우 작업 영역 이탈");
                if(d.CurrentStage==DualArmDemoController.Stage.Failed) throw new Exception(d.Error);
                if(EditorApplication.timeSinceStartup>deadline) throw new Exception("Scripted sequence timeout");
                if(d.CurrentStage==DualArmDemoController.Stage.Done)
                {
                    Check(stages.Count==8,"실제 Play Update로 전체 8단계 진행");
                    Check(true,"scripted sequence에서 좌우 wrist 중심선 작업 영역 분리");
                    Check(d.transfer.Owner==BallTransferController.Ownership.HeldByRight && d.transfer.ball.parent==d.transfer.rightGrab,"Left grab/carry → Right handoff/carry");
                    Check(d.transfer.LastTransferJump<0.00001f,"handoff world position 보존");
                    d.ResetDemo(); Check(d.transfer.Owner==BallTransferController.Ownership.None && Vector3.Distance(d.transfer.ball.position,d.transfer.resetPosition)<0.00001f,"Reset ownership / ball 위치 복원");
                    d.SelectMode(DualArmDemoController.DemoMode.CsvRightArm);
                    leftBefore=d.transfer.leftGrab.position;leftRotation=d.leftArm.baseYaw.pivot.localRotation;
                    Send(1001,120,true);phase=2;deadline=EditorApplication.timeSinceStartup+2;
                }
            }
            else if(phase==2 && d.rightReceiver.LastAppliedFrameId==1001)
            {
                Check(Mathf.Abs(Mathf.DeltaAngle(d.rightArm.baseYaw.pivot.localEulerAngles.y,30))<0.01f,"UDP 127.0.0.1:5005 실제 수신 → 오른팔 Base 적용");
                Check(Vector3.Distance(leftBefore,d.transfer.leftGrab.position)<0.00001f && Quaternion.Angle(leftRotation,d.leftArm.baseYaw.pivot.localRotation)<0.001f,"오른팔 UDP 입력 중 왼팔 transform 독립");
                Send(1002,10,false);Send(1001,10,true);SendRaw("{broken");
                phase=3;deadline=EditorApplication.timeSinceStartup+0.4;
            }
            else if(phase==3 && EditorApplication.timeSinceStartup>deadline)
            {
                var stats=d.rightReceiver.GetStatistics();
                Check(stats.invalid==1 && stats.stale==1 && stats.malformed==1 && d.rightReceiver.LastAppliedFrameId==1001,"invalid HOLD / duplicate stale / malformed 회귀");
                for(uint i=1003;i<=1200;i++) Send(i,90,true);
                phase=4;deadline=EditorApplication.timeSinceStartup+3;
            }
            else if(phase==4 && d.rightReceiver.LastAppliedFrameId==1200)
            {
                Check(true,"burst 최신 frame 1200 적용");
                d.SelectMode(DualArmDemoController.DemoMode.ScriptedHandoff);
                Check(!d.rightReceiver.IsListening && d.rightReceiver.LastStopJoined,"mode 전환 receiver thread Join");
                using(var socket=new UdpClient(5005)) Check(true,"mode 전환 후 port 재bind");
                d.SelectMode(DualArmDemoController.DemoMode.CsvRightArm);phase=5;WriteReport();
                Debug.Log("Dual validation PASS. CSV replay 대기: 127.0.0.1:5005 / Validation/dual_status.json");
            }
            else if((phase==2 || phase==4) && EditorApplication.timeSinceStartup>deadline) throw new Exception("UDP timeout");
        }
        catch(Exception ex)
        {results.Add("- FAIL: "+ex);WriteReport();SessionState.SetBool(Key,false);Debug.LogException(ex);}
    }
    private static void Send(uint frame,float b,bool valid)
    {var c=JointCommandData.Neutral;c.frame_id=frame;c.base_deg=b;c.valid=valid;SendRaw(JsonUtility.ToJson(c));}
    private static void SendRaw(string text)
    {using(var s=new UdpClient()){var bytes=System.Text.Encoding.UTF8.GetBytes(text);s.Send(bytes,bytes.Length,new IPEndPoint(IPAddress.Loopback,5005));}}
    private static void WriteReport()
    {Directory.CreateDirectory("Validation");File.WriteAllText("Validation/DualRobotValidation.md","# Dual Robot 검증\n\n"+string.Join("\n",results)+"\n");}
    [Serializable] private sealed class Status
    {
        public int phase; public long utcMs;public bool playing,listening;public string stage,owner;
        public uint frame;public long applied,received,stale;public JointCommandData command;
        public Vector3 leftGrab,rightGrab;public float baseAngle;public string error;
    }
    private static void WriteTelemetry(DualArmDemoController d)
    {
        var stats=d.rightReceiver.GetStatistics();
        var s=new Status{phase=phase,utcMs=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),playing=Application.isPlaying,listening=d.rightReceiver.IsListening,
            stage=d.CurrentStage.ToString(),owner=d.transfer.Owner.ToString(),frame=d.rightReceiver.LastAppliedFrameId,applied=d.rightReceiver.AppliedCount,
            received=stats.received,stale=stats.stale,command=d.rightReceiver.LastAppliedCommand,leftGrab=d.transfer.leftGrab.position,rightGrab=d.transfer.rightGrab.position,
            baseAngle=Mathf.DeltaAngle(0,d.rightArm.baseYaw.pivot.localEulerAngles.y),error=d.Error+d.rightReceiver.LastError};
        Directory.CreateDirectory("Validation");string path="Validation/dual_status.json";File.WriteAllText(path+".tmp",JsonUtility.ToJson(s,true));
        if(File.Exists(path))File.Replace(path+".tmp",path,null);else File.Move(path+".tmp",path);
    }
}
