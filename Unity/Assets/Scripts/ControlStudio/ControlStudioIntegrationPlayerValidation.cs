using System;
using System.IO;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Dormant in normal use. Explicit fixture-only Player validation never opens hardware.
    public sealed class ControlStudioIntegrationPlayerValidation : MonoBehaviour
    {
        int stage, checks;
        double started, next;
        bool finished;
        SingleArmCommandRouter router;
        BoardUartSource boards;
        RobotVisualProfiles visuals;
        float[] applied;
        Quaternion[] rightPose,leftPose;
        static Quaternion[] Pose(HumanoidVisualRig.Arm arm)=>new[]{arm.shoulderYaw,arm.shoulderPitch,arm.shoulderRoll,
            arm.elbowPitch,arm.elbowRoll,arm.wristPitch,arm.wristRoll,arm.gripperVisual.leftJawPivot,arm.gripperVisual.rightJawPivot}
            .Select(joint=>joint.localRotation).ToArray();
        static bool SamePose(HumanoidVisualRig.Arm arm,Quaternion[] expected)=>Pose(arm).Select((rotation,i)=>Quaternion.Angle(rotation,expected[i])<.01f).All(ok=>ok);
        string output;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void Initialize()
        {
            if (Application.isEditor || Array.IndexOf(Environment.GetCommandLineArgs(), "--integration-player-validation") < 0) return;
            if (Application.platform != RuntimePlatform.WindowsPlayer) {
                Debug.LogError("Integration Player validation requires Windows x64 Player"); Application.Quit(2); return;
            }
            var go = new GameObject("Fixture-only Integration Player validation"); DontDestroyOnLoad(go);
            go.AddComponent<ControlStudioIntegrationPlayerValidation>();
        }
        void Awake()
        {
            started = Time.realtimeSinceStartupAsDouble;
            var args = Environment.GetCommandLineArgs(); int index = Array.IndexOf(args, "--player-result");
            output = Path.GetFullPath(index >= 0 && index + 1 < args.Length ? args[index + 1] :
                Path.Combine(Path.GetDirectoryName(Application.dataPath), "integration-player-result.txt"));
        }
        void Need(bool condition, string message)
        { if (!condition) throw new Exception(message); checks++; Debug.Log("PLAYER VALIDATION PASS: " + message); }
        void SerialBackend()
        {
            const string invalidPort = "ControlStudioValidation_InvalidPort_512706757b414bf8a86ee09a2c568852";
            var assembly = typeof(System.IO.Ports.SerialPort).Assembly;
            using (var file = File.OpenRead(assembly.Location))
            using (var sha = System.Security.Cryptography.SHA256.Create())
                Debug.Log("PLAYER SERIAL assembly=" + assembly.FullName + " location=" + assembly.Location +
                    " sha256=" + BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "").ToLowerInvariant());
            Need(System.IO.Ports.SerialPort.GetPortNames() != null, "real Windows SerialPort.GetPortNames executes; opened=0");
            using (var port = new System.IO.Ports.SerialPort(invalidPort, 921600, System.IO.Ports.Parity.None, 8, System.IO.Ports.StopBits.One)) {
                port.Handshake = System.IO.Ports.Handshake.None; port.DtrEnable = false; port.RtsEnable = false;
                port.ReadTimeout = 30; port.WriteTimeout = 250; port.ReadBufferSize = 65536;
                Need(port.PortName == invalidPort && port.BaudRate == 921600 && port.DataBits == 8 &&
                    port.Parity == System.IO.Ports.Parity.None && port.StopBits == System.IO.Ports.StopBits.One &&
                    port.Handshake == System.IO.Ports.Handshake.None, "real Windows SerialPort constructor and 921600 / 8N1 options execute");
                Need(!port.DtrEnable && !port.RtsEnable && !port.IsOpen, "Player DTR/RTS stay false before open");
                Exception rejected = null;
                try { port.Open(); }
                catch (ArgumentException error) { rejected = error; }
                catch (IOException error) { rejected = error; }
                bool platformFailure = false;
                for (var error = rejected; error != null; error = error.InnerException)
                    platformFailure |= error is PlatformNotSupportedException || error is DllNotFoundException || error is EntryPointNotFoundException;
                Need(rejected != null && !platformFailure && !port.IsOpen, "Player invalid non-COM name rejects normally without platform/native-loader failure");
                Debug.Log("PLAYER SERIAL invalid_open=" + rejected.GetType().FullName + ": " + rejected.Message);
            }
        }
        void Native()
        {
            float rightRoll = 0, leftRoll = 0;
            foreach (var side in new[] { BoardSide.Right, BoardSide.Left }) {
                using (var policy = new ControlStudioOutputPolicy(SingleArmCommandRouter.Home, side)) {
                    Need(policy.Min[0] == 10 && policy.Max[0] == 170, side + " ABI/role DLL loads with current limits in Player");
                    var row = new RecordedHumanRow { Human = new[] { 20f, 50f, 20f, -3f, .8f }, Flags = new[] { 1, 1, 1, 1, 1, 1, 0, 1 }, TargetFrameId = 1, GripperValid = true };
                    var wrapped = new float[5]; var candidate = new float[5]; var approved = new float[5]; var info = new int[4];
                    policy.SubmitHuman(row, wrapped, candidate, approved, info);
                    Need(info[0] == 1, side + " virtual A2 entry executes in Player");
                    if (side == BoardSide.Right) rightRoll = approved[0]; else leftRoll = approved[0];
                    Need(policy.Tick(new float[5], new float[4]) >= 0, side + " virtual tick executes in Player");
                }
            }
            Need(Math.Abs(rightRoll - 110) < .001f && Math.Abs(leftRoll - 70) < .001f, "RIGHT/LEFT role mapping remains independent in Player");
            var csv = RecordedHumanCsv.Load(Path.Combine(Application.streamingAssetsPath, "ControlStudioSamples/cnn_agent1_result.csv"), CsvInputMode.XyzStoredBodyAuxGripper);
            using (var xyz = new ControlStudioXyzPolicy(1, .05f, CsvInputMode.XyzStoredBodyAuxGripper)) {
                var solve = xyz.Solve(csv.Rows[0], 1); Need(solve.Target.Human.Length == 5, "processed-relative XYZ DLL loads and executes in Player");
            }
        }
        void Update()
        {
            if (finished) return;
            double time = Time.realtimeSinceStartupAsDouble;
            if (time - started > 60) { Finish("FAIL Player validation timeout", 2); return; }
            if (time < next) return;
            try {
                if (stage == 0) {
                    router = FindFirstObjectByType<SingleArmCommandRouter>();
                    if (router == null || router.GetComponent<ControlStudioStartupMenu>() == null || router.Status == "STARTING") return;
                    Need(router.Ready, "Demo_07 Native router initializes in Player: " + router.Status + " / " + router.NativeError);
                    SerialBackend(); Native(); boards = router.GetComponent<BoardUartSource>(); visuals = router.GetComponent<RobotVisualProfiles>();
                    Need(boards != null && visuals != null, "board source and visual profiles initialize in Player");
                    visuals.Preview.SetShoulder(2,0,10);visuals.Preview.SetShoulder(2,1,8);visuals.Preview.SetLeft(4,1);
                    for(int i=0;i<30;i++)visuals.Preview.Advance(.05f);
                    var menu = router.GetComponent<ControlStudioStartupMenu>();
                    menu.Cycle(0, 1 - menu.Choice(0)); menu.Cycle(1, 2 - menu.Choice(1));
                    Need(menu.StartStudio() && router.Source == "BOARD", "Player startup chooses BOARD ownership");
                    Need(boards.LoadFixture(BoardSide.Right, Path.Combine(Application.streamingAssetsPath, "ControlStudioSamples/board_right.uart")), "Player RIGHT fixture loads");
                    Need(boards.LoadFixture(BoardSide.Left, Path.Combine(Application.streamingAssetsPath, "ControlStudioSamples/board_left.uart")), "Player LEFT fixture loads");
                } else if (stage == 1) {
                    Need(router.HasApplied && Math.Abs(router.Applied[0] - 100) < .001f, "Player TK applies directly without duplicate A2 mapping");
                    Need(boards.Left.LastApplied != null && Math.Abs(boards.Left.LastApplied.Command[0] - 80) < .001f, "Player LEFT board command remains independent");
                    Need(Quaternion.Angle(visuals.Humanoid.right.elbowRoll.localRotation, visuals.Humanoid.right.rest.elbowRoll * Quaternion.AngleAxis(10, Vector3.up)) < .01f, "Player RIGHT rig follows board command");
                    Need(Quaternion.Angle(visuals.Humanoid.left.elbowRoll.localRotation, visuals.Humanoid.left.rest.elbowRoll * Quaternion.AngleAxis(-10, Vector3.up)) < .01f, "Player LEFT rig follows board command");
                    Need(!router.HasApproved && router.Candidate == null, "Player board data is not labelled virtual A2 approval");
                    Need(router.GetComponent<ControlStudioBoardControlPanel>() != null && router.GetComponent<ControlStudioRecordingPanel>() != null && router.GetComponent<ControlStudioConfigurationPanel>() != null,
                        "Player control / recording / configuration UI initializes");
                    var buttons = Resources.FindObjectsOfTypeAll<Button>().Where(b => b.gameObject.scene.IsValid()).ToArray();
                    Need(buttons.Count(b => b.name.StartsWith("Hardware tab ", StringComparison.Ordinal)) == 5, "Player builds all five hardware tabs");
                    Need(buttons.Where(b => b.name.StartsWith("Board control ", StringComparison.Ordinal)).All(b => !b.interactable), "Player RX-only fixture disables real command controls");
                    Need(!boards.Control(BoardSide.Right, 'E') && !boards.Control(BoardSide.Left, 'A') && boards.TxBytes(BoardSide.Right) == 0 && boards.TxBytes(BoardSide.Left) == 0,
                        "Player fixtures cannot transmit robot commands");
                    Need(!router.GetComponent<ControlStudioUartOutput>().Connected && !router.GetComponent<UartPose3DSource>().Connected, "Player legacy COM workers stay disconnected");
                    var capture = router.GetComponent<ControlStudioVideoCapture>();
                    Need(capture != null && !capture.IsCapturing && !capture.HasFrame && capture.Session.Devices.Length == 0, "Player capture stays unopened with no device enumeration");
                    applied = (float[])router.Applied.Clone(); long ticks = router.TickCount; router.AdvanceVirtual(.2);
                    Need(router.TickCount == ticks && router.Applied.SequenceEqual(applied), "Player BOARD ownership bypasses virtual policy clock");
                    visuals.Sync();rightPose=Pose(visuals.Humanoid.right);leftPose=Pose(visuals.Humanoid.left);
                    boards.Disconnect(BoardSide.Right); boards.Disconnect(BoardSide.Left);
                } else {
                    Need(router.Applied.SequenceEqual(applied) && !boards.Right.AppliedCurrent(BoardUartTransport.Now), "Player disconnect preserves held pose and invalidates current state");
                    var menu=router.GetComponent<ControlStudioStartupMenu>();menu.ShowMenu();menu.Cycle(1,1);
                    Need(menu.StartStudio()&&router.Source=="MANUAL"&&router.Ready, "Player MENU to Manual START recreates current Native context"); router.AdvanceVirtual(.020);
                    Need(router.Applied.Select((v, i) => Math.Abs(v - applied[i]) < .001f).All(v => v), "Player Manual takeover preserves the displayed seed pose");
                    visuals.Sync();
                    Need(SamePose(visuals.Humanoid.right,rightPose)&&SamePose(visuals.Humanoid.left,leftPose),"Player Manual takeover preserves both arm joints, shoulders and grippers");
                    Need(router.GetComponent<ManualServoSource>().Values.SequenceEqual(applied)&&Math.Abs(visuals.Preview.LeftValue(0)-80)<.001f&&Math.Abs(visuals.Preview.LeftValue(1)-70)<.001f,
                        "Player manual targets retain independent board commands outside LEFT edit envelope");
                    for(int i=0;i<30;i++)visuals.Preview.Advance(.05f);visuals.Sync();
                    Need(SamePose(visuals.Humanoid.right,rightPose)&&SamePose(visuals.Humanoid.left,leftPose),"Player manual preview does not drift towards old targets");
                    Need(boards.SelectSource()&&!router.HasApplied,"Player BOARD entry without RX does not claim applied evidence");visuals.Sync();
                    Need(router.ResetOwner("MANUAL"),"Player no-RX Manual takeover");router.AdvanceVirtual(.020);visuals.Preview.Advance(.05f);visuals.Sync();
                    Need(SamePose(visuals.Humanoid.right,rightPose)&&SamePose(visuals.Humanoid.left,leftPose)&&boards.TxBytes(BoardSide.Right)==0&&boards.TxBytes(BoardSide.Left)==0,
                        "Player no-RX BOARD roundtrip preserves both arms with zero hardware TX");
                    Finish("RESULT PASS checks=" + checks + " unity=" + Application.unityVersion + " platform=" + Application.platform + " hardware=none", 0); return;
                }
                stage++; next = time + .05;
            } catch (Exception e) { Debug.LogException(e); Finish("FAIL " + e, 1); }
        }
        void Finish(string result, int code)
        {
            finished = true;
            try { Directory.CreateDirectory(Path.GetDirectoryName(output)); File.WriteAllText(output, result); }
            catch (Exception e) { Debug.LogException(e); code = 1; }
            Debug.Log(result); Application.Quit(code);
        }
    }
}
