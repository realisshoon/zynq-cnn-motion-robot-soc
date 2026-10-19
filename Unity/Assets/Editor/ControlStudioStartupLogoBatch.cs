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

// Isolated batch rendering/tests; never opens a Player or physical port.
public static class ControlStudioStartupLogoBatch
{
    static readonly string Main=ControlStudioBatchPaths.Root;
    static readonly string Output=Main+"/Validation/StartupAlignmentGlobalBackground";
    const string PlayKey="StartupAlignmentGlobalBackgroundPlay";
    static SingleArmCommandRouter router;
    static ControlStudioRuntimeUI ui;
    static ControlStudioStartupMenu menu;
    static ControlStudioPresentationUI view;
    static void Call(object instance,string name)=>instance.GetType().GetMethod(name,BindingFlags.Instance|BindingFlags.NonPublic).Invoke(instance,null);
    static void Need(bool ok,string label){if(!ok)throw new Exception(label);Debug.Log("LOGO VIEW PASS: "+label);}
    static void Guard(){if(!ControlStudioBatchPaths.Allowed)throw new Exception("Isolated batch project required");Directory.CreateDirectory(Output);}
    static void Bind()
    {
        ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();
        ControlStudioStartupMenuBatch.ValidationOutput=Output;
        foreach(var pair in new[]{("router",(object)router),("ui",(object)ui),("menu",(object)menu)})typeof(ControlStudioStartupMenuBatch).GetField(pair.Item1,BindingFlags.Static|BindingFlags.NonPublic).SetValue(null,pair.Item2);
    }
    static void Capture(string name,int width=1600,int height=900)=>typeof(ControlStudioStartupMenuBatch).GetMethod("Capture",BindingFlags.Static|BindingFlags.NonPublic).Invoke(null,new object[]{name,width,height});
    static Vector3 Center(RectTransform rect)=>rect.TransformPoint(rect.rect.center);
    static void CheckMenuLayout()
    {
        Canvas.ForceUpdateCanvases();var panel=menu.MenuCanvas.GetComponentsInChildren<RectTransform>(true).Single(t=>t.name=="Startup menu panel");float axis=Center(panel).x;
        var title=menu.MenuCanvas.GetComponentsInChildren<Text>(true).Single(t=>t.text=="움이움");Need(title.font.HasCharacter('움')&&title.font.HasCharacter('이'),"plain Korean sans-serif branding");
        var outline=panel.Find("Startup brand outline");Need(outline.childCount==5,"four thin outline edges plus text / no glow");
        Need(!menu.MenuCanvas.GetComponentsInChildren<Image>(true).Any(i=>i.sprite!=null),"no mechanical/image logo in Startup");
        foreach(var name in new[]{"Startup brand outline","ROBOT CONTROL STUDIO","ROBOT section","INPUT section","TOOL section","START"})Need(Math.Abs(Center((RectTransform)panel.Find(name)).x-axis)<.01f,"common panel center axis "+name);
        foreach(var row in new[]{"ROBOT","INPUT","TOOL"}){
            var group=panel.Find(row+" section");var options=group.Find(row+" OptionRow");Need(options.GetComponent<HorizontalLayoutGroup>()!=null,"fixed arrow/value layout "+row);
            Need(Math.Abs(Center((RectTransform)options.GetChild(1)).x-axis)<.01f,"option centered "+row);
            float left=axis-Center((RectTransform)options.GetChild(0)).x;float right=Center((RectTransform)options.GetChild(2)).x-axis;Need(Math.Abs(left-right)<.01f,"symmetric fixed arrows "+row);
        }
    }
    static void CheckView()
    {
        Call(ui,"Update");Call(router.GetComponent<Tool1RuntimeUI>(),"Update");Call(router.GetComponent<ControlStudioUartPanel>(),"Update");Call(view,"LateUpdate");view.Background.Apply();
        var profiles=router.GetComponent<RobotVisualProfiles>();profiles.Sync();
        Need(profiles.Simulation.Root.Find("SimulationBackdrop").gameObject.activeInHierarchy,"global grid / wall / stage background active");
        Need(profiles.EnvironmentMode==EnvironmentViewMode.RobotOnly,"Robot Only policy");
        Need(!profiles.Environment.Root.GetComponentsInChildren<Renderer>(true).Any(r=>r.enabled&&r.gameObject.activeInHierarchy),"zero visible workcell props / boxes / surfaces");
        Need(!router.GetComponent<Tool1Runtime>().Surface.gameObject.activeInHierarchy&&!router.GetComponent<Tool1Runtime>().WeldSurface.gameObject.activeInHierarchy&&!router.GetComponent<Tool1Runtime>().FastenSurface.gameObject.activeInHierarchy,"all explicit tool surfaces hidden");
        var children=ui.RuntimeCanvas.GetComponentsInChildren<RectTransform>(true);
        foreach(var pair in new[]{("Manual panel","MANUAL"),("CSV recorded human controls","CSV"),("UART / Hardware panel","UART")})
            Need(children.Single(t=>t.name==pair.Item1).gameObject.activeInHierarchy==(view.ControlsVisible&&router.Source==pair.Item2),"exclusive input panel "+pair.Item2);
        Need(!children.Any(t=>t.gameObject.activeInHierarchy&&(t.name.StartsWith("Robot family ")||t.name.StartsWith("Environment ")||t.name=="TOOLS / JOINTS"||t.name=="TOOL-1 sidebar"||t.name=="Dual Table Cell UI"||t.name=="Gripper Pick Place UI")),"no duplicate/workcell/developer UI");
    }
    public static void Validate()
    {
        Guard();try{
            EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();Call(router.robot,"Awake");Call(router,"Start");Need(router.Ready,"native Agent2 output ready");ui=router.GetComponent<ControlStudioRuntimeUI>();Call(ui.orbit,"Start");Call(ui,"Start");Bind();
            var profiles=router.GetComponent<RobotVisualProfiles>();var tools=router.GetComponent<Tool1Runtime>();
            Need(menu.Visible&&menu.MenuCanvas.GetComponentsInChildren<Button>().Length==7,"three startup rows and START");
            Need(!menu.MenuCanvas.GetComponentsInChildren<Text>(true).Any(t=>t.text=="ENVIRONMENT"),"no startup Environment selector");
            CheckMenuLayout();Capture("01_startup_robot_arm.png");menu.Cycle(0,1);CheckMenuLayout();Capture("02_startup_humanoid.png");Capture("03_startup_1152x648.png",1152,648);
            int epoch=router.Epoch;var applied=(float[])router.Applied.Clone();Need(menu.StartStudio(),"START");Need(router.Epoch==epoch&&router.Applied.SequenceEqual(applied),"START same source no Home/reset");CheckView();Capture("04_humanoid_grid_controls.png");
            var rootBefore=router.robot.transform.position;var humanoidBefore=profiles.Humanoid.transform.localToWorldMatrix;
            view.SetControlsVisible(false);CheckView();Call(ui.orbit,"LateUpdate");Need(ui.orbit.GetComponent<Camera>().rect.width>.99f,"Hide Controls expands viewport");Capture("05_humanoid_grid_hidden.png");
            Need(router.Epoch==epoch&&router.Applied.SequenceEqual(applied),"hide does not mutate source or Applied");view.SetControlsVisible(true);CheckView();Call(ui.orbit,"LateUpdate");Need(Math.Abs(ui.orbit.GetComponent<Camera>().rect.width-.6)<.001,"Show Controls restores viewport");
            Need(!view.Background.FactoryAvailable&&!view.Background.Select(ControlStudioBackgroundView.BackgroundKind.Factory)&&view.Background.Selected==ControlStudioBackgroundView.BackgroundKind.SimulationGrid,"Factory unavailable without fake background");
            for(int i=0;i<10;i++){view.Background.Select(ControlStudioBackgroundView.BackgroundKind.SimulationGrid);view.SetControlsVisible(i%2==0);}
            Need(router.robot.transform.position==rootBefore&&profiles.Humanoid.transform.localToWorldMatrix==humanoidBefore&&router.Epoch==epoch,"background/UI never moves Robot or changes epoch");view.SetControlsVisible(true);
            foreach(var kind in new[]{ToolKind.Spray,ToolKind.Welding,ToolKind.Nailing,ToolKind.Gripper}){Need(profiles.SelectTool(kind),"tool selection "+kind);CheckView();}
            menu.ShowMenu();Need(menu.StartStudio()&&router.Epoch==epoch,"MENU roundtrip retains state");
            menu.ShowMenu();menu.Cycle(0,1);Need(menu.StartStudio(),"Robot Arm START");ui.orbit.SetPresentationCamera();CheckView();Capture("06_robot_arm_grid_controls.png");view.SetControlsVisible(false);CheckView();Capture("07_robot_arm_grid_hidden.png");view.SetControlsVisible(true);
            foreach(var mode in new[]{CsvInputMode.RecordedHumanAngles,CsvInputMode.XyzStoredBodyAuxGripper,CsvInputMode.XyzStoredBodyHoldGripper}){
                Need(router.Csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/cnn_agent1_result.csv"),mode),"CSV "+mode);for(int i=0;i<10;i++)router.Csv.Step();CheckView();int rows=router.Csv.ConsumedRows;double time=router.Csv.Timeline.Time;epoch=router.Epoch;
                view.SetControlsVisible(false);CheckView();router.Csv.Step();Need(router.Csv.ConsumedRows>rows,"CSV processing continues with hidden controls");rows=router.Csv.ConsumedRows;time=router.Csv.Timeline.Time;
                menu.ShowMenu();Need(menu.StartStudio()&&router.Epoch==epoch&&router.Csv.ConsumedRows==rows&&router.Csv.Timeline.Time==time,"MENU preserves CSV rows/time "+mode);view.SetControlsVisible(true);
            }
            Capture("08_csv_controls.png");Need(ui.SelectStartupInput(2,out _),"UART selection");CheckView();var rx=router.GetComponent<UartPose3DSource>();Need(rx.ConnectMock(),"RX mock only");view.SetControlsVisible(false);
            rx.FeedMockBytes(System.Text.Encoding.ASCII.GetBytes(File.ReadLines(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/cnn_pose3d_v1.uart")).First()+"\n"));Call(rx,"Update");Need(rx.PacketsRx>0,"RX processing continues with controls hidden");view.SetControlsVisible(true);CheckView();Call(router.GetComponent<ControlStudioUartPanel>(),"Update");Capture("09_uart_controls.png");rx.Disconnect();
            Need(ui.SelectStartupInput(0,out _),"Manual regression");var seed=(float[])router.Applied.Clone();var target=new[]{105f,75f,95f,100f,.4f};using(var reference=new ControlStudioOutputPolicy(seed)){
                Need(reference.Submit(target)==0&&ui.manual.LoadAtomic(target),"identical native output target");var expected=new float[5];var velocity=new float[4];epoch=router.Epoch;
                for(int tick=0;tick<150;tick++){if(tick%10==0){view.SetControlsVisible(!view.ControlsVisible);menu.ShowMenu();Need(menu.StartStudio()&&router.Epoch==epoch,"navigation preserves source");}reference.Tick(expected,velocity);router.AdvanceVirtual(.02);Need(router.Applied.SequenceEqual(expected),"exact Applied trace "+tick);}
            }
            view.SetControlsVisible(true);view.SetPresentationMode(true);Need(!ui.RuntimeCanvas.gameObject.activeInHierarchy,"Presentation UI hidden");Need(!router.GetComponentsInChildren<Canvas>(true).Any(c=>c.gameObject.activeInHierarchy),"Presentation all UI hidden");Capture("10_presentation_grid.png");view.SetPresentationMode(false);CheckView();
            Need(router.HardwareTxCount==0&&router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"Hardware TX=0");
            File.WriteAllText(Output+"/editor_result.txt","PASS: plain Korean text + 4-edge outline / common panel center axis / fixed symmetric arrows / 3-row menu / global grid-wall-stage / robot-only props hidden / Manual·CSV·UART exclusive panels / Hide·Show viewport / hidden CSV and Mock RX continue / Presentation UI=0 / MENU state retained / independent Agent2 150-tick trace identical / Hardware TX=0. Factory=BLOCKED (not imported).\n");
        }catch(Exception e){File.WriteAllText(Output+"/editor_result.txt","FAIL\n"+e);throw;}
    }
    static int stage;static double nextStage;
    public static void RunPlay(){Guard();EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");SessionState.SetBool(PlayKey,true);SessionState.SetString(PlayKey+"time",DateTime.UtcNow.ToString("O"));EditorApplication.isPlaying=true;}
    [InitializeOnLoadMethod]static void Register()
    {
        EditorApplication.update+=()=>{if(!SessionState.GetBool(PlayKey,false))return;if(DateTime.UtcNow-DateTime.Parse(SessionState.GetString(PlayKey+"time",DateTime.UtcNow.ToString("O")))>TimeSpan.FromMinutes(3)){Finish("FAIL: timeout",2);return;}if(!EditorApplication.isPlaying||EditorApplication.isCompiling||EditorApplication.timeSinceStartup<nextStage)return;
            try{if(stage==0){router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();if(router==null||router.GetComponent<ControlStudioStartupMenu>()==null)return;Bind();Need(router.Ready&&menu.Visible,"Play Mode initial menu");CheckMenuLayout();Capture("11_play_startup_logo.png");menu.Cycle(0,1);}
                else if(stage==1){CheckMenuLayout();Capture("12_play_humanoid_logo.png");Need(menu.StartStudio(),"Play Mode START");}
                else if(stage==2){CheckView();view.SetControlsVisible(false);CheckView();Capture("13_play_hidden_controls.png");view.SetControlsVisible(true);menu.ShowMenu();Need(menu.StartStudio(),"Play Mode MENU / START");menu.ShowMenu();menu.Cycle(1,1);Need(menu.StartStudio(),"Play Mode CSV START");}
                else if(stage==3){CheckView();menu.ShowMenu();menu.Cycle(1,1);Need(menu.StartStudio(),"Play Mode UART START");}
                else if(stage==4){CheckView();view.SetPresentationMode(true);Need(!router.GetComponentsInChildren<Canvas>(true).Any(c=>c.gameObject.activeInHierarchy),"Play Mode Presentation all UI hidden");Capture("14_play_presentation.png");view.SetPresentationMode(false);Need(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"Play Mode TX=0");Finish("PASS: 실제 Editor Play Mode centered text/outline / fixed menu arrows / global grid-wall-stage / START·MENU / Manual·CSV·UART / controls hide-show / no workcell props / Presentation UI=0 / Hardware TX=0. Player 실행 없음.",0);return;}
                stage++;nextStage=EditorApplication.timeSinceStartup+.5;
            }catch(Exception e){Finish("FAIL\n"+e,2);}
        };
    }
    static void Finish(string result,int code){SessionState.SetBool(PlayKey,false);File.WriteAllText(Output+"/play_result.txt",result);EditorApplication.Exit(code);}
    public static void ValidateAndBuild(){Validate();Build();}
    public static void Build(){Guard();string exe=Main+"/Builds/SingleArmControlAlignedSimulation/SingleArmControl.exe";Directory.CreateDirectory(Path.GetDirectoryName(exe));var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName=exe,target=BuildTarget.StandaloneWindows64});File.WriteAllText(Output+"/build_result.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings+" output="+exe);if(report.summary.result!=BuildResult.Succeeded)throw new Exception("Aligned simulation build failed");}
}
