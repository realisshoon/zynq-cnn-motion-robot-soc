using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using Object=UnityEngine.Object;

[InitializeOnLoad]
public static class DualStep1ReplayValidation
{
    private const string Pending="DualStep1.Pending", Active="DualStep1.Active", Joined="DualStep1.Joined";
    static DualStep1ReplayValidation(){EditorApplication.update+=Tick;EditorApplication.playModeStateChanged+=Changed;}
    [MenuItem("Tools/Human Motion/STEP 1/Run Dual CSV Validation %#F7")]
    public static void Run()
    {
        if(EditorApplication.isPlayingOrWillChangePlaymode)return;
        if(SceneManager.GetActiveScene().isDirty)
        {
            // Main 원본은 덮지 않고 현재 미저장 편집을 별도 scene으로 보존한다.
            Directory.CreateDirectory("Assets/Scenes/ValidationBackups");
            string backup="Assets/Scenes/ValidationBackups/"+SceneManager.GetActiveScene().name+"."+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".unity";
            if(!EditorSceneManager.SaveScene(SceneManager.GetActiveScene(),backup,true))
                throw new IOException("미저장 Scene backup 실패: "+backup);
            Debug.Log("미저장 Scene 보존: "+backup);
        }
        EditorSceneManager.OpenScene("Assets/Scenes/DualRobotDemo.unity",OpenSceneMode.Single);
        SessionState.SetBool(Pending,true);SessionState.SetBool(Active,true);SessionState.SetBool(Joined,false);
        EditorApplication.isPlaying=true;
    }
    private static void Require(bool ok,string label){if(!ok)throw new InvalidOperationException(label);}
    private static void Tick()
    {
        if(!SessionState.GetBool(Pending,false) || !EditorApplication.isPlaying || Time.timeSinceLevelLoad<0.5f)return;
        SessionState.SetBool(Pending,false);
        try
        {
            var d=Object.FindObjectsByType<DualArmDemoController>(FindObjectsSortMode.None).Single();
            var receiver=Object.FindObjectsByType<UdpJointCommandReceiver>(FindObjectsInactive.Include,FindObjectsSortMode.None).Single();
            Require(d.leftArm!=d.rightArm && d.leftArm.name=="RobotArm_L" && d.rightArm.name=="RobotArm_R","두 독립 RobotArm");
            Require(receiver==d.rightReceiver && receiver.controller==d.rightArm,"receiver는 오른팔만 참조");
            Require(d.mode==DualArmDemoController.DemoMode.CsvRightArm,"저장된 기본 CSV mode");
            Require(d.leftArm.inputMode==RobotArmController.InputMode.Manual && d.rightArm.inputMode==RobotArmController.InputMode.UDP,"control ownership");
            var joints=new[]{d.rightArm.baseYaw,d.rightArm.shoulderPitch,d.rightArm.elbowPitch,d.rightArm.wristPitch,d.rightArm.wristRoll};
            var before=joints.Select(j=>j.pivot.localRotation).ToArray();
            d.StartDemo();d.Advance(10);d.ResetDemo();
            for(int i=0;i<joints.Length;i++)Require(Mathf.Abs(Quaternion.Dot(before[i],joints[i].pivot.localRotation))>0.999999f,"CSV START/RESET의 오른팔 overwrite 금지");
            Require(d.CurrentStage==DualArmDemoController.Stage.Idle,"CSV 왼팔 Idle");
            receiver.ListenerStopped+=joined=>SessionState.SetBool(Joined,joined);
            Require(receiver.IsListening,"single UDP 5005 listener");
            d.rightArm.gameObject.AddComponent<JointCommandReplayProbe>().Begin(receiver,true,"DualStep1");
            d.gameObject.AddComponent<DualCsvOwnershipProbe>().Begin(d,true);
            Debug.Log("Dual STEP 1 준비 완료: verify_step1_unity.py --dual 실행. Left Idle / Right UDP_CSV.");
        }
        catch(Exception e){SessionState.SetBool(Active,false);Debug.LogException(e);}
    }
    private static void Changed(PlayModeStateChange change)
    {
        if(change!=PlayModeStateChange.EnteredEditMode || !SessionState.GetBool(Active,false))return;
        bool free=false;
        try{using(var socket=new UdpClient(AddressFamily.InterNetwork)){socket.ExclusiveAddressUse=true;socket.Client.Bind(new IPEndPoint(IPAddress.Loopback,5005));free=true;}}
        catch(SocketException){}
        Directory.CreateDirectory("Validation/DualStep1");
        File.WriteAllText("Validation/DualStep1/Cleanup.md","# Dual 종료 검증\n\n- "+(SessionState.GetBool(Joined,false)?"PASS":"FAIL")+": receiver thread Join\n- "+(free?"PASS":"FAIL")+": UDP 5005 exclusive 재bind\n");
        SessionState.SetBool(Active,false);
    }
}
