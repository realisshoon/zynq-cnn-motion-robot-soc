using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.UI;

// Explicit disposable-project check. Does not open COM, move hardware, or save scenes.
public static class ControlStudioIntegrationBatch
{
    [DllImport("kernel32",CharSet=CharSet.Unicode,SetLastError=true,EntryPoint="LoadLibraryW")]
    static extern IntPtr LoadLibrary(string path);
    public static void DiagnoseNative()
    {
        if(!ControlStudioBatchPaths.Allowed)throw new Exception("Disposable batch copy required");
        foreach(var name in new[]{"control_studio_right","control_studio_left","control_studio_xyz_integration"})
        {
            string asset="Assets/Plugins/x86_64/"+name+".dll";
            var importer=AssetImporter.GetAtPath(asset) as PluginImporter;
            Debug.Log("NATIVE_IMPORT "+name+" exists="+File.Exists(asset)+" editor="+importer?.GetCompatibleWithEditor()+" cpu="+importer?.GetEditorData("CPU")+" os="+importer?.GetEditorData("OS"));
            var handle=LoadLibrary(Path.GetFullPath(asset));Debug.Log("NATIVE_LOAD "+name+" handle="+handle+" win32="+Marshal.GetLastWin32Error());
        }
    }
    public static void ConfigureNativeImporters()
    {
        if(!ControlStudioBatchPaths.Allowed)throw new Exception("Disposable batch copy required");
        foreach(var name in new[]{"control_studio_right","control_studio_left","control_studio_xyz_integration"})
        {
            var importer=(PluginImporter)AssetImporter.GetAtPath("Assets/Plugins/x86_64/"+name+".dll");
            importer.SetCompatibleWithAnyPlatform(false);importer.SetCompatibleWithEditor(true);
            importer.SetEditorData("CPU","x86_64");importer.SetEditorData("OS","Windows");
            importer.SetCompatibleWithPlatform(BuildTarget.StandaloneWindows64,true);
            importer.SetPlatformData(BuildTarget.StandaloneWindows64,"CPU","x86_64");importer.SaveAndReimport();
        }
        Run();
    }
    const string Key="IntegrationNativeBoardValidation";
    static int stage, checks;
    static double next;
    static SingleArmCommandRouter router;
    static BoardUartSource boards;
    static RobotVisualProfiles visuals;
    static float[] applied;
    static Quaternion[] rightPose,leftPose;
    static Quaternion[] Pose(HumanoidVisualRig.Arm arm)=>new[]{arm.shoulderYaw,arm.shoulderPitch,arm.shoulderRoll,
        arm.elbowPitch,arm.elbowRoll,arm.wristPitch,arm.wristRoll,arm.gripperVisual.leftJawPivot,arm.gripperVisual.rightJawPivot}
        .Select(joint=>joint.localRotation).ToArray();
    static bool SamePose(HumanoidVisualRig.Arm arm,Quaternion[] expected)=>Pose(arm).Select((rotation,i)=>Quaternion.Angle(rotation,expected[i])<.01f).All(ok=>ok);
    static T Private<T>(object target,string name)=>(T)target.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(target);
    static void VerifyG51Transforms(float[] command)
    {
        var arm=router.robot;
        Need(arm.IsConfigured,"actual production G51 controller references are configured");
        float roll=Private<float>(arm,"elbowRollVisualOffsetDeg")+Private<float>(arm,"elbowRollVisualSign")*(command[0]-90f);
        float pitch=Private<float>(arm,"elbowPitchVisualOffsetDeg")+Private<float>(arm,"elbowPitchVisualSign")*(command[1]-90f);
        float wrist=Private<float>(arm,"wristPitchVisualOffsetDeg")+Private<float>(arm,"wristPitchVisualSign")*(command[2]-90f);
        Need(Quaternion.Angle(arm.elbowRoll.localRotation,Private<Quaternion>(arm,"elbowRollRest")*Quaternion.AngleAxis(roll,Vector3.up))<.01f,"actual G51 elbow roll transform matches its configured TK mapping");
        Need(Quaternion.Angle(arm.elbowPitch.localRotation,Private<Quaternion>(arm,"elbowPitchRest")*Quaternion.AngleAxis(pitch,Vector3.right))<.01f,"actual G51 elbow pitch transform matches its configured TK mapping");
        Need(Quaternion.Angle(arm.wristPitch.localRotation,Private<Quaternion>(arm,"wristPitchRest")*Quaternion.AngleAxis(wrist,Vector3.right))<.01f,"actual G51 wrist pitch transform matches its configured TK mapping");
        Need(Quaternion.Angle(arm.wristRoll.localRotation,Private<Quaternion>(arm,"wristRollRest")*Quaternion.AngleAxis(-(command[3]-90f),Vector3.up))<.01f,"actual G51 wrist roll transform matches its TK mapping");
        var grip=arm.gripperVisual; float drive=Mathf.Lerp(grip.closedDriveDeg,grip.openDriveDeg,command[4]);
        Need(grip.leftGear!=null&&grip.rightGear!=null&&Quaternion.Angle(grip.leftGear.localRotation,Private<Quaternion>(grip,"leftGearRest")*Quaternion.AngleAxis(drive,Vector3.forward))<.01f&&Quaternion.Angle(grip.rightGear.localRotation,Private<Quaternion>(grip,"rightGearRest")*Quaternion.AngleAxis(-drive,Vector3.forward))<.01f,"actual G51 gripper gear transforms match the fixture normalized command");
        var adapter=visuals.Adapter;
        Need(adapter!=null&&adapter.IsConfigured,"existing G51 visual adapter is configured");
        foreach(var binding in new[]{adapter.m0,adapter.m1,adapter.m2,adapter.m3})
        {
            var expected=binding.visualRest*binding.axisBasis*(Quaternion.Inverse(binding.logicalRest)*binding.logical.localRotation)*Quaternion.Inverse(binding.axisBasis);
            Need(Quaternion.Angle(binding.visual.localRotation,expected)<.01f,"actual G51 visual pivot follows its logical joint after normal frame updates");
        }
        Need(visuals.Select(RobotVisualProfileId.G51)&&adapter.GetComponentsInChildren<Renderer>(true).Any(renderer=>renderer.enabled),"G51 selection enables the existing robot visual renderers");
        Need(visuals.Select(RobotVisualProfileId.HumanoidRobot),"visual verification restores the independent Humanoid fixture");
    }
    static string Output=>Path.Combine(ControlStudioBatchPaths.Root,"Validation/IntegrationCore/result.txt");
    static void Need(bool ok,string message)
    {
        if(!ok)throw new Exception(message);checks++;
        Debug.Log("INTEGRATION PASS: "+message);
    }
    public static void Run()
    {
        if(!ControlStudioBatchPaths.Allowed)throw new Exception("--isolated-validation required in disposable project");
        Directory.CreateDirectory(Path.GetDirectoryName(Output));
        SessionState.SetBool(Key,true);SessionState.SetString(Key+"Start",DateTime.UtcNow.ToString("O"));
        EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod] static void Register()
    {
        EditorApplication.update+=()=>
        {
            if(!SessionState.GetBool(Key,false))return;
            if(DateTime.UtcNow-DateTime.Parse(SessionState.GetString(Key+"Start",DateTime.UtcNow.ToString("O")))>TimeSpan.FromMinutes(3)){Finish("FAIL timeout",2);return;}
            if(!EditorApplication.isPlaying||EditorApplication.isCompiling||EditorApplication.timeSinceStartup<next)return;
            try
            {
                if(stage==0)
                {
                    router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();
                    if(router?.GetComponent<ControlStudioStartupMenu>()==null)return;
                    if(router.Status=="STARTING")return;
                    Need(router.Ready,"latest RIGHT Native initializes in scene: "+router.Status+" / "+router.NativeError);
                    boards=router.GetComponent<BoardUartSource>();visuals=router.GetComponent<RobotVisualProfiles>();
                    // Prior manual preview intent must not reappear after a board handoff.
                    visuals.Preview.SetShoulder(2,0,10);visuals.Preview.SetShoulder(2,1,8);visuals.Preview.SetLeft(4,1);
                    for(int i=0;i<30;i++)visuals.Preview.Advance(.05f);
                    var menu=router.GetComponent<ControlStudioStartupMenu>();menu.Cycle(0,1);menu.Cycle(1,2);
                    Need(menu.StartStudio()&&router.Source=="BOARD","startup UART chooses BOARD owner");
                    Need(visuals.Selected==RobotVisualProfileId.HumanoidRobot,"humanoid selected for independent board arms");
                    Need(boards.LoadFixture(BoardSide.Right,Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/board_right.uart")),"RIGHT fixture load");
                    Need(boards.LoadFixture(BoardSide.Left,Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/board_left.uart")),"LEFT fixture load");
                }
                else if(stage==1)
                {
                    Need(router.HasApplied&&Math.Abs(router.Applied[0]-100)<.001,"TK servo command applied directly without A2 remapping");
                    var application=boards.RightApplication;
                    Need(application.UnityApplyCount==1&&application.UnityAppliedTick==boards.Right.LastApplied.Tick&&application.UnityAppliedGeneration==boards.Right.Generation,"application telemetry acknowledges the actual production router/controller TK");
                    Need(application.UnityAppliedCommand.SequenceEqual(router.Applied)&&application.UnityAppliedCommand.SequenceEqual(boards.Right.LastApplied.Command)&&application.JointApplyStatus=="SUCCEEDED","application telemetry command equals actual controller-approved router Applied and fixture TK");
                    Need(application.VisualStatus=="NOT OBSERVED"&&application.UnityAppliedAt>=boards.Right.LastApplied.ReceivedAt,"source application acknowledgement records PC time without claiming a renderer observation");
                    Need(application.UnityAppliedIsFixture&&application.UnityAppliedEpoch==router.Epoch,"actual Unity application fixture records simulation identity and owner epoch");
                    VerifyG51Transforms(application.UnityAppliedCommand);
                    Need(Math.Abs(boards.Left.LastApplied.Command[0]-80)<.001,"LEFT has independent applied command");
                    Need(Quaternion.Angle(visuals.Humanoid.right.elbowRoll.localRotation,visuals.Humanoid.right.rest.elbowRoll*Quaternion.AngleAxis(10,Vector3.up))<.01f,"RIGHT actual rig rotation follows TK");
                    Need(Quaternion.Angle(visuals.Humanoid.left.elbowRoll.localRotation,visuals.Humanoid.left.rest.elbowRoll*Quaternion.AngleAxis(-10,Vector3.up))<.01f,"LEFT actual rig rotation follows its TK");
                    Need(!router.HasApproved&&router.Candidate==null,"board output is not labelled virtual A2 approval");
                    var ui=router.GetComponent<ControlStudioRuntimeUI>();
                    Need(ui.Sliders.All(s=>!s.gameObject.activeInHierarchy),"manual sliders hidden in BOARD mode");
                    Need(!router.GetComponent<RobotControlMockPanel>().CanInteract,"mock command controls disabled in BOARD mode");
                    Need(!boards.Query(BoardSide.Right,'V'),"unconfirmed and fixture queries transmit no bytes");
                    Need(!router.GetComponent<ControlStudioUartOutput>().Connected&&!router.GetComponent<UartPose3DSource>().Connected,"legacy serial workers remain disconnected");
                    Need(router.GetComponent<ControlStudioBoardControlPanel>()!=null&&router.GetComponent<ControlStudioRecordingPanel>()!=null&&router.GetComponent<ControlStudioConfigurationPanel>()!=null,"real control, recording and settings panels are attached");
                    Need(!boards.Control(BoardSide.Right,'E')&&!boards.Control(BoardSide.Left,'A')&&boards.TxBytes(BoardSide.Right)==0&&boards.TxBytes(BoardSide.Left)==0,"fixture blocks real control and transmits zero bytes on both ports");
                    var buttons=Resources.FindObjectsOfTypeAll<Button>().Where(b=>b.gameObject.scene.IsValid()).ToArray();
                    Need(buttons.Count(b=>b.name.StartsWith("Hardware tab ",StringComparison.Ordinal))==5,"five hardware tabs are constructed");
                    Need(buttons.Where(b=>b.name.StartsWith("Board control ",StringComparison.Ordinal)).All(b=>!b.interactable),"real control buttons are disabled for RX-only fixtures");
                    var capture=router.GetComponent<ControlStudioVideoCapture>();
                    Need(capture!=null&&capture.Session.Devices.Length==0&&!capture.IsCapturing&&!capture.HasFrame,"capture initialization enumerates no devices and opens no camera");
                    Need(!capture.StartCapture("",1280,720,30)&&!capture.IsCapturing&&!capture.HasFrame,"empty capture selection cannot open default camera or request permission");
                    capture.StopCapture();Need(capture.Mode==VideoCaptureMode.Mock&&!capture.HasFrame,"capture stop restores Mock fallback");
                    applied=(float[])router.Applied.Clone();long ticks=router.TickCount;router.AdvanceVirtual(.2);
                    Need(router.TickCount==ticks&&router.Applied.SequenceEqual(applied),"BOARD bypasses virtual 20 ms policy ticks");
                    visuals.Sync();rightPose=Pose(visuals.Humanoid.right);leftPose=Pose(visuals.Humanoid.left);
                    boards.Disconnect(BoardSide.Right);boards.Disconnect(BoardSide.Left);
                }
                else if(stage==2)
                {
                    Need(router.Applied.SequenceEqual(applied)&&!boards.Right.AppliedCurrent(BoardUartTransport.Now),"disconnect holds displayed pose and invalidates current state");
                    Need(!router.SubmitBoard(new BoardTick{Command=new[]{90f,70f,100f,90f,.05f},Writes=1},router.Epoch-1),"old owner epoch cannot apply");
                    var menu=router.GetComponent<ControlStudioStartupMenu>();menu.ShowMenu();
                    Need(menu.Choice(1)==2,"return to menu preserves UART selection");menu.Cycle(1,1);
                    Need(menu.StartStudio()&&router.Source=="MANUAL"&&router.Ready,"manual takeover creates fresh latest Native context");
                    visuals.Sync();
                    Need(SamePose(visuals.Humanoid.right,rightPose)&&SamePose(visuals.Humanoid.left,leftPose),"BOARD to MENU to Manual START preserves both arm joints, shoulders and grippers");
                    Need(router.GetComponent<ManualServoSource>().Values.SequenceEqual(applied)&&Math.Abs(visuals.Preview.LeftValue(0)-80)<.001f&&Math.Abs(visuals.Preview.LeftValue(1)-70)<.001f,
                        "manual targets retain independent board values including LEFT outside the preview edit envelope");
                    for(int i=0;i<30;i++)visuals.Preview.Advance(.05f);visuals.Sync();
                    Need(SamePose(visuals.Humanoid.right,rightPose)&&SamePose(visuals.Humanoid.left,leftPose),"manual preview does not drift back towards pre-board targets");
                    var held=(float[])router.Applied.Clone();
                    Need(boards.SelectSource()&&!router.HasApplied,"BOARD entry without RX does not claim an applied board command");
                    visuals.Sync();
                    Need(router.ResetOwner("MANUAL"),"manual takeover without any board RX");router.AdvanceVirtual(.020);
                    Need(router.Applied.Select((v,i)=>Math.Abs(v-held[i])<.001f).All(v=>v),"no-RX BOARD roundtrip keeps displayed pose instead of Home jump");
                    visuals.Preview.Advance(.05f);visuals.Sync();
                    Need(SamePose(visuals.Humanoid.right,rightPose)&&SamePose(visuals.Humanoid.left,leftPose)&&boards.TxBytes(BoardSide.Right)==0&&boards.TxBytes(BoardSide.Left)==0,
                        "no-RX BOARD roundtrip preserves both displayed arms and transmits no hardware bytes");
                    Finish("RESULT PASS checks="+checks+" unity="+Application.unityVersion+" hardware=none",0);return;
                }
                stage++;next=EditorApplication.timeSinceStartup+.1;
            }
            catch(Exception e){Debug.LogException(e);Finish("FAIL "+e,1);}
        };
    }
    static void Finish(string result,int exit)
    {
        SessionState.SetBool(Key,false);Directory.CreateDirectory(Path.GetDirectoryName(Output));File.WriteAllText(Output,result);
        Debug.Log(result);EditorApplication.isPlaying=false;EditorApplication.Exit(exit);
    }
}
