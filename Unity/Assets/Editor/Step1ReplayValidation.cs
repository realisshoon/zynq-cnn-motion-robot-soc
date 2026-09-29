using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using Object = UnityEngine.Object;

[InitializeOnLoad]
public static class Step1ReplayValidation
{
    private const string Pending="Step1.Pending", Active="Step1.Active", Joined="Step1.Joined";
    static Step1ReplayValidation(){EditorApplication.update+=Tick;EditorApplication.playModeStateChanged+=Changed;}
    [MenuItem("Tools/Human Motion/STEP 1/Run Main CSV Validation %#F8")]
    public static void Run()
    {
        if(EditorApplication.isPlayingOrWillChangePlaymode)return;
        if(SceneManager.GetActiveScene().isDirty)throw new InvalidOperationException("현재 Scene을 먼저 저장하세요.");
        EditorSceneManager.OpenScene("Assets/Scenes/Main.unity",OpenSceneMode.Single);
        SessionState.SetBool(Pending,true);SessionState.SetBool(Active,true);SessionState.SetBool(Joined,false);
        EditorApplication.isPlaying=true;
    }
    private static void Require(bool value,string label){if(!value)throw new InvalidOperationException(label);}
    private static void Tick()
    {
        if(!SessionState.GetBool(Pending,false) || !EditorApplication.isPlaying || Time.timeSinceLevelLoad<0.5f)return;
        SessionState.SetBool(Pending,false);
        try
        {
            var c=Object.FindObjectsByType<RobotArmController>(FindObjectsSortMode.None).Single();
            var receiver=Object.FindObjectsByType<UdpJointCommandReceiver>(FindObjectsSortMode.None).Single();
            Require(receiver.controller==c,"Main receiver reference");
            Require(c.gripperVisual!=null,"Main G51 linkage gripper");
            var original=c.testCommand;
            c.inputMode=RobotArmController.InputMode.Manual;
            var command=JointCommandData.Neutral;
            command.base_deg=120;command.shoulder_deg=140;command.elbow_deg=40;command.wrist_pitch_deg=120;command.wrist_roll_deg=140;command.gripper_norm=0;
            c.testCommand=command;Require(c.ApplyTestCommand(),"Main Manual command");
            Require(Mathf.Abs(Mathf.DeltaAngle(c.baseYaw.pivot.localEulerAngles.y,30))<0.001f,"Main Base");
            Require(Mathf.Abs(Mathf.DeltaAngle(c.shoulderPitch.pivot.localEulerAngles.x,50))<0.001f,"Main Shoulder");
            Require(Mathf.Abs(Mathf.DeltaAngle(c.elbowPitch.pivot.localEulerAngles.x,-50))<0.001f,"Main Elbow");
            Require(Mathf.Abs(Mathf.DeltaAngle(c.wristPitch.pivot.localEulerAngles.x,30))<0.001f,"Main Wrist Pitch");
            Require(Mathf.Abs(Mathf.DeltaAngle(c.wristRoll.pivot.localEulerAngles.y,50))<0.001f,"Main Wrist Roll");
            var closed=c.gripperVisual.leftJawPivot.localRotation;command.gripper_norm=1;c.ApplyCommand(command);
            Require(Quaternion.Angle(closed,c.gripperVisual.leftJawPivot.localRotation)>1,"Main Gripper 0..1");
            var before=c.baseYaw.pivot.localRotation;command.valid=false;command.base_deg=0;
            Require(!c.ApplyCommand(command)&&Quaternion.Angle(before,c.baseYaw.pivot.localRotation)<0.001f,"Main invalid HOLD");
            command.valid=true;command.base_deg=float.NaN;Require(!c.ApplyCommand(command),"Main NaN 거부");
            c.testCommand=original;c.ApplyTestCommand();c.inputMode=RobotArmController.InputMode.UDP;
            Require(!c.ApplyTestCommand(),"UDP 모드에서 Manual overwrite 금지");
            receiver.ListenerStopped+=joined=>SessionState.SetBool(Joined,joined);
            receiver.enabled=true;receiver.RestartListener();
            Require(receiver.IsListening,"Main UDP bind 127.0.0.1:5005");
            c.gameObject.AddComponent<JointCommandReplayProbe>().Begin(receiver,true);
            Debug.Log("STEP 1 Main 준비 완료. verify_step1_unity.py를 실행하세요. Scene asset은 수정하지 않았습니다.");
        }
        catch(Exception e){SessionState.SetBool(Active,false);Debug.LogException(e);}
    }
    private static void Changed(PlayModeStateChange change)
    {
        if(change!=PlayModeStateChange.EnteredEditMode || !SessionState.GetBool(Active,false))return;
        bool free=false;
        try{using(var socket=new UdpClient(AddressFamily.InterNetwork)){socket.ExclusiveAddressUse=true;socket.Client.Bind(new IPEndPoint(IPAddress.Loopback,5005));free=true;}}
        catch(SocketException){}
        Directory.CreateDirectory("Validation");
        File.WriteAllText("Validation/Step1MainCleanup.md","# Main 종료 검증\n\n- "+(SessionState.GetBool(Joined,false)?"PASS":"FAIL")+": receiver thread Join\n- "+(free?"PASS":"FAIL")+": UDP 5005 exclusive 재bind\n");
        SessionState.SetBool(Active,false);
    }
}
