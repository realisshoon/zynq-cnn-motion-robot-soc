using System;
using System.IO;
using System.Linq;
using System.Reflection;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.Rendering.Universal;

// Explicit isolated batch validation only. Does not save the scene or run a Player.
public static class ControlStudioStartupMenuBatch
{
    static readonly string Main=ControlStudioBatchPaths.Root;
    public static string ValidationOutput=Main+"/Validation/StartupMenuUiRework";
    static string Output=>ValidationOutput;
    static void Call(object target,string name)=>target.GetType().GetMethod(name,BindingFlags.Instance|BindingFlags.NonPublic).Invoke(target,null);
    static void Need(bool ok,string label){if(!ok)throw new Exception(label);Debug.Log("STARTUP PASS: "+label);}
    static SingleArmCommandRouter router;static ControlStudioRuntimeUI ui;static ControlStudioStartupMenu menu;
    const string PlayKey="StartupMenuUiReworkIsolatedPlay";
    static double nextStageAt;static int playStage;static int playEpoch;static float[] playApplied;
    public static void RunPlay()
    {
        if(!ControlStudioBatchPaths.Allowed)throw new Exception("Isolated batch copy required");
        Directory.CreateDirectory(Output);EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        SessionState.SetBool(PlayKey,true);SessionState.SetString(PlayKey+"Start",DateTime.UtcNow.ToString("O"));EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod]static void RegisterPlay()
    {
        EditorApplication.update+=()=>{
            if(!SessionState.GetBool(PlayKey,false))return;
            if(DateTime.UtcNow-DateTime.Parse(SessionState.GetString(PlayKey+"Start",DateTime.UtcNow.ToString("O")))>TimeSpan.FromMinutes(3)) {FinishPlay("FAIL: Play Mode timeout",2);return;}
            if(!EditorApplication.isPlaying||EditorApplication.isCompiling||EditorApplication.timeSinceStartup<nextStageAt)return;
            try{
                if(playStage==0){
                    router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();if(router==null)return;
                    ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();if(menu==null)return;
                    Need(router.Ready&&menu.Visible&&ui.RuntimeCanvas.GetComponent<CanvasGroup>().alpha==0,"Play Mode first frame menu");
                    playEpoch=router.Epoch;playApplied=(float[])router.Applied.Clone();Capture("01_startup_robot_arm.png");
                    menu.Cycle(0,1);
                }else if(playStage==1){Need(router.GetComponent<RobotVisualProfiles>().Selected==RobotVisualProfileId.HumanoidRobot,"Play Mode humanoid selection");Capture("02_startup_humanoid.png");Need(menu.StartStudio(),"Play Mode START");}
                else if(playStage==2){Need(!menu.Visible&&router.Source=="MANUAL"&&router.Epoch==playEpoch&&router.Applied.SequenceEqual(playApplied),"Play Mode Control UI / no reset");CheckLayout();Capture("03_control_studio.png");menu.ShowMenu();}
                else if(playStage==3){Need(menu.Visible&&router.Epoch==playEpoch&&router.Applied.SequenceEqual(playApplied),"Play Mode MENU return");Capture("04_menu_return.png");menu.Cycle(1,1);Need(menu.StartStudio(),"Play Mode CSV START");}
                else if(playStage==4){CheckLayout();Capture("05_csv_controls.png");menu.ShowMenu();menu.Cycle(1,1);Need(menu.StartStudio(),"Play Mode UART START");}
                else if(playStage==5){
                    CheckLayout();var rx=router.GetComponent<UartPose3DSource>();Need(rx.ConnectMock(),"isolated RX mock only");
                    rx.FeedMockBytes(System.Text.Encoding.ASCII.GetBytes(File.ReadLines(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/cnn_pose3d_v1.uart")).First()+"\n"));Call(rx,"Update");Call(router.GetComponent<ControlStudioUartPanel>(),"Update");
                    Need(ui.RuntimeCanvas.GetComponentsInChildren<Text>(true).Single(t=>t.name=="RX detail").text.Contains("Shoulder R"),"latest processed RX XYZ displayed");
                    Capture("06_uart_controls.png");var presentation=router.GetComponent<ControlStudioPresentationUI>();presentation.ToggleToolControls();CheckLayout();presentation.ToggleToolControls();rx.Disconnect();
                    Need(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"Play Mode TX=0");FinishPlay("PASS: 실제 Editor Play Mode Startup/START/MENU, Manual/CSV/UART 패널 분리, 중복 선택 UI 없음, Mock RX 최신 XYZ 표시, Hardware TX=0. Player 직접 실행 없음.",0);return;
                }
                playStage++;nextStageAt=EditorApplication.timeSinceStartup+.5;
            }catch(Exception e){FinishPlay("FAIL\n"+e,2);}
        };
    }
    static void FinishPlay(string result,int code){SessionState.SetBool(PlayKey,false);File.WriteAllText(Output+"/play_result.txt",result);EditorApplication.Exit(code);}
    static void CheckLayout()
    {
        Call(ui,"Update");Call(router.GetComponent<Tool1RuntimeUI>(),"Update");Call(router.GetComponent<ControlStudioUartPanel>(),"Update");Call(router.GetComponent<ControlStudioPresentationUI>(),"LateUpdate");
        var design=ui.RuntimeCanvas.GetChild(0);
        Need(design.Find("Manual panel").gameObject.activeSelf==(router.Source=="MANUAL"),"Manual panel exclusive");
        Need(design.Find("CSV recorded human controls").gameObject.activeSelf==(router.Source=="CSV"),"CSV panel exclusive");
        Need(design.GetComponentsInChildren<RectTransform>(true).Single(t=>t.name=="UART / Hardware panel").gameObject.activeSelf==(router.Source=="UART"),"UART panel exclusive");
        string[] forbidden={"Manual source","CSV source","UART RX source","TOOLS / JOINTS","Environment Robot Only","Environment Workcell","Environment Auto","Gripper","Spray","Welding","Nailing","ROBOT ONLY","SOURCE / CSV","SERVO M0-M4","UART / HARDWARE","TX enable","TX Connect"};
        Need(!ui.RuntimeCanvas.GetComponentsInChildren<Button>(true).Any(b=>b.gameObject.activeInHierarchy&&(forbidden.Contains(b.name)||b.name.StartsWith("Robot family "))),"no in-game duplicate selection buttons");
    }
    public static void Validate()
    {
        if(!ControlStudioBatchPaths.Allowed)throw new Exception("Isolated batch copy required");
        Directory.CreateDirectory(Output);
        try{
            EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
            router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();
            Call(router.robot,"Awake");Call(router,"Start");Need(router.Ready,"native output ready");
            ui=router.GetComponent<ControlStudioRuntimeUI>();Call(ui.orbit,"Start");Call(ui,"Start");
            menu=router.GetComponent<ControlStudioStartupMenu>();
            var visuals=router.GetComponent<RobotVisualProfiles>();var tools=router.GetComponent<Tool1Runtime>();
            Need(menu.Visible&&ui.RuntimeCanvas.GetComponent<CanvasGroup>().alpha==0,"initial menu / controls hidden");
            var title=menu.MenuCanvas.GetComponentsInChildren<Text>().Single(t=>t.text=="움이움");
            Need(title.font.HasCharacter('움')&&title.font.HasCharacter('이'),"Korean title glyphs");
            Need(menu.MenuCanvas.GetComponentsInChildren<Button>().Length==9,"eight arrows and START");
            Need(menu.Choice(0)==0&&menu.Choice(1)==0&&menu.Choice(2)==0&&menu.Choice(3)==0,"G51 / Manual / Gripper / Robot Only defaults");
            int epoch=router.Epoch;var applied=(float[])router.Applied.Clone();var root=router.robot.transform.position;
            Capture("01_startup_robot_arm.png");
            menu.Cycle(0,1);Need(visuals.Selected==RobotVisualProfileId.HumanoidRobot,"humanoid preview");Capture("02_startup_humanoid.png");
            menu.Cycle(2,1);menu.Cycle(3,1);Need(router.Epoch==epoch&&router.Applied.SequenceEqual(applied),"pending arrows do not alter command or source");
            Need(menu.StartStudio(),"START");Need(!menu.Visible&&ui.RuntimeCanvas.GetComponent<CanvasGroup>().alpha==1,"controls restored");
            CheckLayout();
            Need(visuals.Selected==RobotVisualProfileId.HumanoidRobot&&tools.Gate.Tool==ToolKind.Spray&&visuals.EnvironmentMode==EnvironmentViewMode.Workcell,"Robot / Tool / Environment applied independently");
            Need(router.Source=="MANUAL"&&router.Epoch==epoch,"unchanged Manual retains epoch");Capture("03_control_studio.png");
            menu.ShowMenu();Need(menu.Visible&&router.Epoch==epoch&&router.Applied.SequenceEqual(applied),"MENU retains source / epoch / Applied");Capture("04_menu_return.png");
            Need(menu.StartStudio(),"same selections START");Need(router.Epoch==epoch,"same selections no source reset");
            menu.ShowMenu();menu.Cycle(1,1);Need(menu.StartStudio()&&router.Source=="CSV"&&router.Paused,"CSV existing Load / paused source");
            CheckLayout();Capture("05_csv_controls.png");
            router.Csv.Step();var csvFrame=router.Csv.Current.FrameId;var csvTime=router.Csv.Timeline.Time;epoch=router.Epoch;
            menu.ShowMenu();Need(menu.StartStudio()&&router.Epoch==epoch&&router.Csv.Current.FrameId==csvFrame&&router.Csv.Timeline.Time==csvTime,"CSV menu roundtrip preserves timeline");
            menu.ShowMenu();menu.Cycle(1,1);Need(menu.StartStudio()&&router.Source=="UART","UART existing source selection");
            CheckLayout();Capture("06_uart_controls.png");
            var rx=router.GetComponent<UartPose3DSource>();Need(!rx.Connected,"UART selection does not connect hardware");epoch=router.Epoch;
            menu.ShowMenu();Need(menu.StartStudio()&&router.Epoch==epoch,"UART menu roundtrip retains epoch");
            menu.ShowMenu();menu.Cycle(1,1);Need(menu.StartStudio()&&router.Source=="MANUAL","Manual existing AdoptCurrent");
            menu.ShowMenu();menu.Cycle(3,1);Need(menu.StartStudio()&&visuals.EnvironmentMode==EnvironmentViewMode.AutoToolEnvironment,"Startup Auto environment");
            epoch=router.Epoch;menu.ShowMenu();Need(menu.Choice(3)==2&&menu.StartStudio()&&router.Epoch==epoch,"Auto retained on MENU");
            var presentation=router.GetComponent<ControlStudioPresentationUI>();presentation.ToggleToolControls();CheckLayout();Capture("07_effect_controls.png");presentation.ToggleToolControls();
            presentation.SetPresentationMode(true);presentation.SetPresentationMode(false);Need(visuals.EnvironmentMode==EnvironmentViewMode.AutoToolEnvironment,"Presentation does not change environment selection");
            Need(Math.Abs(menu.PreviewScale-.78f)<.0001,"Humanoid preview 0.78 projection scale");
            foreach(var kind in new[]{ToolKind.Welding,ToolKind.Nailing,ToolKind.Gripper}){menu.ShowMenu();while(new[]{ToolKind.Gripper,ToolKind.Spray,ToolKind.Welding,ToolKind.Nailing}[menu.Choice(2)]!=kind)menu.Cycle(2,1);Need(menu.StartStudio()&&tools.Gate.Tool==kind&&visuals.Selected==RobotVisualProfileId.HumanoidRobot,"Tool "+kind+" preserves Robot");}
            epoch=router.Epoch;Need(ui.manual.Set(0,105),"Manual existing command accepted");router.AdvanceVirtual(.2);Need(router.Applied[0]!=applied[0],"existing router applies Manual");
            applied=(float[])router.Applied.Clone();menu.ShowMenu();Need(menu.StartStudio()&&router.Epoch==epoch&&router.Applied.SequenceEqual(applied),"MENU does not Home current pose");
            menu.ShowMenu();var before=(float[])router.Applied.Clone();ui.CsvPath.SetTextWithoutNotify("missing.csv");
            // Existing loaded data is intentionally retained when selecting CSV again.
            menu.Cycle(1,1);Need(menu.StartStudio()&&router.Source=="CSV","loaded CSV reused without replacing path");
            Need(router.Applied.SequenceEqual(before),"source switch seeds current Applied");
            foreach(var mode in new[]{CsvInputMode.RecordedHumanAngles,CsvInputMode.XyzStoredBodyAuxGripper,CsvInputMode.XyzStoredBodyHoldGripper}){
                Need(router.Csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/cnn_agent1_result.csv"),mode),"existing CSV mode "+mode);
                for(int i=0;i<10;i++)router.Csv.Step();int count=router.Csv.ConsumedRows;double time=router.Csv.Timeline.Time;epoch=router.Epoch;
                menu.ShowMenu();Need(menu.StartStudio()&&router.Epoch==epoch&&router.Csv.Mode==mode&&router.Csv.ConsumedRows==count&&router.Csv.Timeline.Time==time,"menu preserves "+mode+" observations");
            }
            Need(ui.SelectStartupInput(0,out _),"Manual for independent output trace");
            var seed=(float[])router.Applied.Clone();var requested=new[]{105f,75f,95f,100f,.4f};
            using(var reference=new ControlStudioOutputPolicy(seed)){
                Need(reference.Submit(requested)==0&&ui.manual.LoadAtomic(requested),"identical target / independent native context");
                var expected=new float[5];var velocity=new float[4];epoch=router.Epoch;
                for(int tick=0;tick<150;tick++){
                    if(tick%10==0){menu.ShowMenu();Need(menu.StartStudio()&&router.Epoch==epoch,"trace menu roundtrip "+tick);}
                    reference.Tick(expected,velocity);router.AdvanceVirtual(.02);
                    Need(router.Applied.SequenceEqual(expected),"exact Applied trace tick "+tick);
                }
            }
            File.WriteAllText(Output+"/command_trace_result.txt","PASS: 동일 seed/target의 독립 Agent2 native context와 150개 20ms 틱 Applied가 정확히 일치. 메뉴 15회 왕복에서 epoch 변화 없음. Recorded/XYZ A/XYZ B의 관측 수·시간·mode 유지.\n");
            Need(router.robot.transform.position==root,"robot root unchanged");
            Need(router.HardwareTxCount==0&&router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"Hardware TX=0");
            File.WriteAllText(Output+"/editor_result.txt","PASS: 동기식 Editor 초기화·전체 선택 API·상태 유지·Manual 출력 trace·CSV 3모드 시험.\n실제 Play Mode 결과는 play_result.txt에 분리 기록. Player 직접 조작은 미검증.\n");
        }catch(Exception e){File.WriteAllText(Output+"/editor_result.txt","FAIL\n"+e);throw;}
    }
    static void Capture(string name,int width=1600,int height=900)
    {
        var cam=ui.orbit.GetComponent<Camera>();Call(ui.orbit,"LateUpdate");Call(menu,"LateUpdate");Call(ui,"Update");Call(router.GetComponent<ControlStudioPresentationUI>(),"LateUpdate");router.GetComponent<RobotVisualProfiles>().Sync();
        var oldTarget=cam.targetTexture;var oldRect=cam.rect;var active=RenderTexture.active;var oldProjection=cam.projectionMatrix;int oldMask=cam.cullingMask;
        var final=RenderTexture.GetTemporary(width,height,24);var uiRender=RenderTexture.GetTemporary(width,height,24,RenderTextureFormat.ARGB32);
        var texture=new Texture2D(width,height,TextureFormat.RGB24,false);GameObject uiCameraObject=null;
        var canvases=new[]{ui.RuntimeCanvas.GetComponent<Canvas>(),menu.MenuCanvas.GetComponent<Canvas>()};
        var oldModes=canvases.Select(c=>c.renderMode).ToArray();var oldCameras=canvases.Select(c=>c.worldCamera).ToArray();
        var scalers=canvases.Select(c=>c.GetComponent<CanvasScaler>()).ToArray();var oldScales=canvases.Select(c=>c.scaleFactor).ToArray();var scalerEnabled=scalers.Select(s=>s.enabled).ToArray();
        try{
            cam.targetTexture=final;cam.rect=new Rect(0,0,1,1);cam.cullingMask&=~(1<<5);
            // Off-center projection reproduces the startup right-hand robot viewport in a full-size capture.
            if(menu.Visible){var projection=Matrix4x4.Perspective(cam.fieldOfView,(float)width/height,cam.nearClipPlane,cam.farClipPlane);projection.m02=-.34f;projection.m00*=menu.PreviewScale;projection.m11*=menu.PreviewScale;cam.projectionMatrix=projection;}
            else{var projection=Matrix4x4.Perspective(cam.fieldOfView,(width*oldRect.width)/(height*oldRect.height),cam.nearClipPlane,cam.farClipPlane);projection.m00*=oldRect.width;projection.m11*=oldRect.height;projection.m02=-(2*oldRect.x+oldRect.width-1);projection.m12=-(2*oldRect.y+oldRect.height-1);cam.projectionMatrix=projection;}
            foreach(var c in canvases)c.enabled=false;cam.Render();
            uiCameraObject=new GameObject("Capture UI camera",typeof(Camera));var uiCam=uiCameraObject.GetComponent<Camera>();uiCam.cullingMask=1<<5;uiCam.nearClipPlane=.1f;uiCam.farClipPlane=50;uiCam.targetTexture=uiRender;uiCam.clearFlags=CameraClearFlags.SolidColor;uiCam.backgroundColor=Color.clear;
            uiCam.GetUniversalAdditionalCameraData().renderPostProcessing=false;
            foreach(var c in canvases){c.enabled=true;c.renderMode=RenderMode.ScreenSpaceCamera;c.worldCamera=uiCam;c.planeDistance=1;SetLayer(c.transform,5);}
            for(int i=0;i<canvases.Length;i++){scalers[i].enabled=false;canvases[i].scaleFactor=Mathf.Min(width/1600f,height/900f);}
            foreach(var c in canvases)foreach(var text in c.GetComponentsInChildren<Text>(true))text.SetAllDirty();
            Canvas.ForceUpdateCanvases();uiCam.Render();RenderTexture.active=final;GL.PushMatrix();GL.LoadPixelMatrix(0,width,height,0);Graphics.DrawTexture(new Rect(0,0,width,height),uiRender);GL.PopMatrix();texture.ReadPixels(new Rect(0,0,width,height),0,0);texture.Apply();File.WriteAllBytes(Output+"/"+name,texture.EncodeToPNG());
        }finally{
            for(int i=0;i<canvases.Length;i++){canvases[i].renderMode=oldModes[i];canvases[i].worldCamera=oldCameras[i];canvases[i].scaleFactor=oldScales[i];scalers[i].enabled=scalerEnabled[i];}
            cam.targetTexture=oldTarget;cam.rect=oldRect;cam.projectionMatrix=oldProjection;cam.cullingMask=oldMask;RenderTexture.active=active;
            if(uiCameraObject!=null)UnityEngine.Object.DestroyImmediate(uiCameraObject);UnityEngine.Object.DestroyImmediate(texture);RenderTexture.ReleaseTemporary(uiRender);RenderTexture.ReleaseTemporary(final);
        }
    }
    static void SetLayer(Transform t,int layer){t.gameObject.layer=layer;foreach(Transform child in t)SetLayer(child,layer);}
    public static void Build()
    {
        Directory.CreateDirectory(Output);string exe=Main+"/Builds/SingleArmControlStartupUiRework/SingleArmControl.exe";Directory.CreateDirectory(Path.GetDirectoryName(exe));
        var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName=exe,target=BuildTarget.StandaloneWindows64});
        File.WriteAllText(Output+"/build_result.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings+" output="+exe);
        if(report.summary.result!=BuildResult.Succeeded)throw new Exception("Startup build failed");
    }
}
