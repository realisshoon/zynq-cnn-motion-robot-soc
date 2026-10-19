using System;
using System.IO;
using System.Linq;
using System.Reflection;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

// Explicit isolated Editor validation; no Player, physical COM or user input automation.
public static class ControlStudioSimulationLightingBatch
{
    static readonly string Main=ControlStudioBatchPaths.Root;
    static readonly string Output=Main+"/Validation/VisualQualityRecovery";
    const string PlayKey="BrightSimulationStudioLightingPlay";
    static SingleArmCommandRouter router;
    static ControlStudioRuntimeUI ui;
    static ControlStudioStartupMenu menu;
    static ControlStudioPresentationUI view;
    static RobotVisualProfiles profiles;
    static void Call(object instance,string name)=>instance.GetType().GetMethod(name,BindingFlags.Instance|BindingFlags.NonPublic).Invoke(instance,null);
    static void Need(bool ok,string label){if(!ok)throw new Exception(label);Debug.Log("LIGHTING PASS: "+label);}
    static void Guard(){if(!ControlStudioBatchPaths.Allowed)throw new Exception("Isolated batch project required");Directory.CreateDirectory(Output);}
    static void Bind()
    {
        ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();profiles=router.GetComponent<RobotVisualProfiles>();
        ControlStudioStartupMenuBatch.ValidationOutput=Output;
        foreach(var pair in new[]{("router",(object)router),("ui",(object)ui),("menu",(object)menu)})typeof(ControlStudioStartupMenuBatch).GetField(pair.Item1,BindingFlags.Static|BindingFlags.NonPublic).SetValue(null,pair.Item2);
    }
    static void Capture(string name)=>typeof(ControlStudioStartupMenuBatch).GetMethod("Capture",BindingFlags.Static|BindingFlags.NonPublic).Invoke(null,new object[]{name,1920,1080});
    static string RuntimeVisualState()
    {
        var visualRoot=profiles.Selected==RobotVisualProfileId.HumanoidRobot?profiles.Humanoid.transform:profiles.Adapter.controller.transform.root;
        return string.Join("\n",visualRoot.GetComponentsInChildren<MeshRenderer>(true).Where(r=>r.enabled&&r.gameObject.activeInHierarchy).Select(r=>{
            var mesh=r.GetComponent<MeshFilter>()?.sharedMesh;
            return r.name+" | mesh="+(mesh==null?"NONE":mesh.name+"#"+mesh.GetInstanceID()+" vertices="+mesh.vertexCount+" source="+AssetDatabase.GetAssetPath(mesh))+" | shadow="+r.shadowCastingMode+"/receive="+r.receiveShadows+" | layers="+r.renderingLayerMask+" | materials="+string.Join(";",r.sharedMaterials.Select(m=>m==null?"NONE":m.name+"#"+m.GetInstanceID()+" shader="+m.shader.name+" color="+m.color+" metal="+(m.HasProperty("_Metallic")?m.GetFloat("_Metallic"):0)+" smooth="+(m.HasProperty("_Smoothness")?m.GetFloat("_Smoothness"):0)+" source="+AssetDatabase.GetAssetPath(m)));
        }).OrderBy(x=>x));
    }
    static string RuntimeLightingState()=>RenderSettings.defaultReflectionMode+"/"+RenderSettings.customReflectionTexture.GetInstanceID()+"/"+RenderSettings.reflectionIntensity+"/"+RenderSettings.ambientMode+"/"+RenderSettings.ambientSkyColor+"\n"+
        string.Join("\n",profiles.Simulation.Root.GetComponentsInChildren<Light>(true).Select(l=>l.name+"/"+l.enabled+"/"+l.gameObject.activeInHierarchy+"/"+l.intensity+"/"+l.color+"/"+l.shadows+"/"+(uint)l.GetUniversalAdditionalLightData().renderingLayers).OrderBy(x=>x));
    public static void AuditVisualSources()
    {
        Guard();EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();Call(router.robot,"Awake");Call(router,"Start");
        ui=router.GetComponent<ControlStudioRuntimeUI>();Call(ui.orbit,"Start");Call(ui,"Start");Bind();
        AuditCurrentSources();
    }
    static void AuditCurrentSources()
    {
        string folder=Main+"/Validation/VisualQualityRecovery";Directory.CreateDirectory(folder);
        foreach(var id in new[]{RobotVisualProfileId.G51,RobotVisualProfileId.HumanoidRobot}){
            profiles.Select(id);profiles.Sync();menu.ShowMenu();CheckStudio();string baseline=RuntimeVisualState(),illumination=RuntimeLightingState();File.WriteAllText(folder+"/"+id+"_startup_renderers.txt",baseline);
            Need(menu.StartStudio(),"audit START");profiles.Select(id);profiles.Sync();Need(RuntimeVisualState()==baseline,"renderer / mesh / material instance parity Main "+id);
            view.SetPresentationMode(true);profiles.Sync();Need(RuntimeVisualState()==baseline,"renderer / mesh / material instance parity Presentation "+id);view.SetPresentationMode(false);
            CheckStudio();Need(RuntimeLightingState()==illumination,"studio illumination / reflection parity Main-Presentation "+id);
            foreach(int input in new[]{0,1,2}){Need(ui.SelectStartupInput(input,out _),"audit input "+input);profiles.Sync();Need(RuntimeVisualState()==baseline,"renderer / mesh / material parity input "+input+" / "+id);CheckStudio();Need(RuntimeLightingState()==illumination,"studio illumination / reflection parity input "+input+" / "+id);}
        }
        var lines=UnityEngine.Object.FindObjectsByType<Light>(FindObjectsInactive.Include,FindObjectsSortMode.None).Select(l=>l.name+" active="+l.gameObject.activeInHierarchy+" enabled="+l.enabled+" type="+l.type+" intensity="+l.intensity+" color="+l.color+" shadow="+l.shadows+" layers="+(uint)l.GetUniversalAdditionalLightData().renderingLayers);
        File.WriteAllLines(folder+"/runtime_lights.txt",lines);
        File.WriteAllText(folder+"/source_audit_result.txt","PASS: 실제 runtime Mesh/Material/Renderer instance가 Startup/Main/Presentation 및 Manual/CSV/UART source 전환에서 동일. G51 realistic adapter와 G51IndustrialVisual을 계속 사용하며 Humanoid는 기존 Resources/VisualProfiles/HumanoidRobot prefab이다. geometry/material fallback으로 되돌아가는 현상은 관찰되지 않음. lighting/reflection baseline 변경은 별도 확인 대상. Hardware TX="+router.HardwareTxCount);
    }
    static void CheckStudio()
    {
        view.Background.Apply();profiles.Sync();
        var lights=UnityEngine.Object.FindObjectsByType<Light>(FindObjectsInactive.Exclude,FindObjectsSortMode.None).Where(l=>l.enabled).ToArray();
        Need(lights.Length==8&&lights.All(l=>l.transform.IsChildOf(profiles.Simulation.Root)),"eight common studio/product lights / no stacked legacy lights");
        Need(profiles.Simulation.Root.Find("SimulationLighting").gameObject.activeInHierarchy,"global rig independent of selected profile/UI");
        var key=lights.Single(l=>l.name.StartsWith("Studio Key"));Need(key.shadows==LightShadows.Soft&&key.shadowStrength<1,"soft key shadows");
        var pool=lights.Single(l=>l.name.StartsWith("Studio FloorFill"));
        Need((uint)pool.GetUniversalAdditionalLightData().renderingLayers==2&&profiles.Simulation.Root.Find("GroundPlane").GetComponent<Renderer>().renderingLayerMask==2,"URP ground-only pool layers");
        Need(lights.Where(l=>l.type==LightType.Directional).All(l=>(uint)l.GetUniversalAdditionalLightData().renderingLayers==3),"global directions illuminate robot and ground");
        Need(RenderSettings.defaultReflectionMode==DefaultReflectionMode.Custom&&RenderSettings.customReflectionTexture!=null,"custom HDR studio reflection");
        Need(profiles.Simulation.Root.GetComponentsInChildren<ReflectionProbe>().Length==1,"one reflection probe");
        Need(!profiles.Environment.Root.gameObject.activeInHierarchy,"workcell hidden");
        Need(profiles.Simulation.Root.Find("SimulationBackdrop").childCount==1,"wide backdrop without close room corner");
        var root=profiles.Selected==RobotVisualProfileId.HumanoidRobot?profiles.Humanoid.transform:
            profiles.Selected==RobotVisualProfileId.G51?profiles.Adapter.controller.transform.root:profiles.Current.transform;
        var active=root.GetComponentsInChildren<MeshRenderer>(true).Where(r=>r.enabled&&r.gameObject.activeInHierarchy).ToArray();
        Need(active.All(r=>r.GetComponent<MeshFilter>()?.sharedMesh?.name!="Cube"&&r.GetComponent<MeshFilter>()?.sharedMesh?.name!="Cylinder"),"zero raw Cube/Cylinder in active robot visual");
        Need(active.SelectMany(r=>r.sharedMaterials).All(m=>m==null||!m.name.StartsWith("MAT_Robot")),"zero legacy flat robot materials");
        var cylinder=active.Select(r=>r.GetComponent<MeshFilter>()?.sharedMesh).FirstOrDefault(m=>m!=null&&m.name.StartsWith("Machined cylinder"));
        if(cylinder!=null){Need((cylinder.bounds.size-new Vector3(1,2,1)).sqrMagnitude<.000001f,"legacy cylinder bounds preserved");Need(cylinder.normals[cylinder.vertexCount-1].y>.99f&&cylinder.normals[cylinder.vertexCount-2].y<-.99f,"outward cylinder caps");}
    }
    static Bounds RenderBounds(Transform root)
    {
        var renderers=root.GetComponentsInChildren<MeshRenderer>(true).Where(r=>r.enabled&&r.gameObject.activeInHierarchy).ToArray();Need(renderers.Length>0,"detail renderers");
        Bounds bounds=renderers[0].bounds;foreach(var renderer in renderers.Skip(1))bounds.Encapsulate(renderer.bounds);return bounds;
    }
    static void Detail(string name,Vector3 center,float distance)
    {
        var orbit=ui.orbit;Vector3 target=orbit.Target;float before=orbit.Distance;
        typeof(ControlStudioOrbitCamera).GetProperty("Target").SetValue(orbit,center);
        typeof(ControlStudioOrbitCamera).GetProperty("Distance").SetValue(orbit,distance);
        Call(orbit,"PositionCamera");CheckStudio();Capture(name);
        typeof(ControlStudioOrbitCamera).GetProperty("Target").SetValue(orbit,target);
        typeof(ControlStudioOrbitCamera).GetProperty("Distance").SetValue(orbit,before);Call(orbit,"PositionCamera");
    }
    static void Captures()
    {
        CheckStudio();Capture("01_g51_startup_three_quarter.png");Need(menu.StartStudio(),"G51 START");ui.orbit.SetPresentationCamera();CheckStudio();Capture("02_g51_main_three_quarter.png");
        view.SetPresentationMode(true);CheckStudio();Capture("03_g51_presentation.png");
        var hand=profiles.Adapter.m3.visual.Find("M3GripperMountPoint_Visual/GripperVisualMount_Visual");var grip=RenderBounds(hand);
        Detail("04_g51_wrist_gripper.png",grip.center,Mathf.Max(.24f,grip.extents.magnitude*3.4f));
        var robot=RenderBounds(profiles.Adapter.controller.transform.root);
        Detail("05_g51_base_shaft.png",robot.center-Vector3.up*robot.size.y*.22f,.42f);
        view.SetPresentationMode(false);menu.ShowMenu();menu.Cycle(0,1);CheckStudio();Capture("06_humanoid_startup_three_quarter.png");
        Need(menu.StartStudio(),"Humanoid START");ui.orbit.SetPresentationCamera();CheckStudio();Capture("07_humanoid_main_three_quarter.png");
        view.SetPresentationMode(true);CheckStudio();Capture("08_humanoid_presentation.png");
        var arm=profiles.Humanoid.GetComponentsInChildren<Transform>(true).First(t=>t.name=="RightUpperArm");
        var elbow=profiles.Humanoid.GetComponentsInChildren<Transform>(true).First(t=>t.name=="RightElbowPitch");
        Detail("09_humanoid_shoulder_elbow.png",(arm.position+elbow.position)*.5f,.95f);
        var torso=profiles.Humanoid.GetComponentsInChildren<Transform>(true).First(t=>t.name=="painted curved front chest shell");
        Detail("10_humanoid_torso_branding.png",torso.GetComponent<Renderer>().bounds.center,1.1f);view.SetPresentationMode(false);
    }
    public static void Validate()
    {
        Guard();try{
            EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();Call(router.robot,"Awake");Call(router,"Start");Need(router.Ready,"native Agent2 ready");
            ui=router.GetComponent<ControlStudioRuntimeUI>();Call(ui.orbit,"Start");Call(ui,"Start");Bind();
            int epoch=router.Epoch;var applied=(float[])router.Applied.Clone();var robotMatrix=router.robot.transform.localToWorldMatrix;
            CheckStudio();Captures();Need(router.Epoch==epoch&&router.Applied.SequenceEqual(applied)&&router.robot.transform.localToWorldMatrix==robotMatrix,"view changes preserve Robot / epoch / commands");
            foreach(bool visible in new[]{false,true}){view.SetControlsVisible(visible);CheckStudio();}
            profiles.Select(RobotVisualProfileId.MechanicalDualTable);CheckStudio();profiles.Select(RobotVisualProfileId.G51);CheckStudio();
            ui.orbit.ResetView();CheckStudio();ui.orbit.SetPresentationCamera();
            var shell=profiles.Humanoid.GetComponentsInChildren<Renderer>(true).SelectMany(r=>r.sharedMaterials).First(m=>m!=null&&m.name=="HumanoidWhiteShell");Need(Mathf.Abs(shell.color.r-.88f)<.001f,"known-good shell PBR restored / background does not override robot material");
            foreach(var csv in new[]{"cnn_agent1_result.csv","mediapipe_agent1_result.csv"})foreach(var mode in new[]{CsvInputMode.RecordedHumanAngles,CsvInputMode.XyzStoredBodyAuxGripper,CsvInputMode.XyzStoredBodyHoldGripper}){
                Need(router.Csv.Load(Path.Combine(Application.streamingAssetsPath,"ControlStudioSamples/"+csv),mode),"load "+csv+" "+mode);
                for(int i=0;i<10;i++)router.Csv.Step();int count=router.Csv.ConsumedRows;double time=router.Csv.Timeline.Time;epoch=router.Epoch;
                menu.ShowMenu();Need(menu.StartStudio()&&router.Epoch==epoch&&router.Csv.ConsumedRows==count&&router.Csv.Timeline.Time==time,"CSV mode navigation preserved");
            }
            Need(ui.SelectStartupInput(0,out _),"Manual");var seed=(float[])router.Applied.Clone();var target=new[]{105f,75f,95f,100f,.4f};
            using(var reference=new ControlStudioOutputPolicy(seed)){
                Need(reference.Submit(target)==0&&ui.manual.LoadAtomic(target),"independent native target");var expected=new float[5];var velocity=new float[4];
                for(int i=0;i<150;i++){if(i%30==0){profiles.Select(i%60==0?RobotVisualProfileId.HumanoidRobot:RobotVisualProfileId.G51);CheckStudio();}reference.Tick(expected,velocity);router.AdvanceVirtual(.02);Need(router.Applied.SequenceEqual(expected),"exact command trace "+i);}
            }
            Need(router.HardwareTxCount==0&&router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"Hardware TX=0");
            File.WriteAllText(Output+"/editor_result.txt","PASS: 10개 1920×1080 Editor 렌더 / 공통 studio+product 8 lights / soft shadow·custom reflection / primitive Cube/Cylinder=0·cap winding·bounds 확인 / Robot·epoch 보존 / 두 CSV×3모드 10행·메뉴 상태 회귀 / 독립 Agent2 150틱 Applied 정확히 일치 / Hardware TX=0. 전체 CSV 재검증·실제 Player는 수행하지 않음.\n");
            AuditCurrentSources();
        }catch(Exception e){File.WriteAllText(Output+"/editor_result.txt","FAIL\n"+e);throw;}
    }
    public static void RenderCaptures()
    {
        Guard();EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();Call(router.robot,"Awake");Call(router,"Start");
        ui=router.GetComponent<ControlStudioRuntimeUI>();Call(ui.orbit,"Start");Call(ui,"Start");Bind();CheckStudio();Captures();
    }
    static int stage;static double next;
    public static void RunPlay(){Guard();EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");SessionState.SetBool(PlayKey,true);SessionState.SetString(PlayKey+"time",DateTime.UtcNow.ToString("O"));EditorApplication.isPlaying=true;}
    [InitializeOnLoadMethod]static void Register()
    {
        EditorApplication.update+=()=>{
            if(!SessionState.GetBool(PlayKey,false))return;
            if(DateTime.UtcNow-DateTime.Parse(SessionState.GetString(PlayKey+"time",DateTime.UtcNow.ToString("O")))>TimeSpan.FromMinutes(3)){Finish("FAIL: timeout",2);return;}
            if(!EditorApplication.isPlaying||EditorApplication.isCompiling||EditorApplication.timeSinceStartup<next)return;
            try{
                if(stage==0){router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();if(router==null||router.GetComponent<ControlStudioStartupMenu>()==null)return;Bind();Need(router.Ready,"Play ready");CheckStudio();menu.Cycle(0,1);}
                else if(stage==1){CheckStudio();Need(menu.StartStudio(),"Play START");}
                else if(stage==2){CheckStudio();view.SetPresentationMode(true);Capture("11_play_humanoid.png");}
                else if(stage==3){CheckStudio();view.SetPresentationMode(false);menu.ShowMenu();menu.Cycle(0,1);Need(menu.StartStudio(),"Play G51 START");}
                else if(stage==4){CheckStudio();view.SetPresentationMode(true);Capture("12_play_g51.png");Need(router.HardwareTxCount==0,"Play TX=0");Finish("PASS: 분리된 Editor Play Mode Startup→Humanoid→Presentation→MENU→G51 / 광원 중복 없음 / Hardware TX=0. Player 실행 없음.",0);return;}
                stage++;next=EditorApplication.timeSinceStartup+.5;
            }catch(Exception e){Finish("FAIL\n"+e,2);}
        };
    }
    static void Finish(string message,int code){SessionState.SetBool(PlayKey,false);File.WriteAllText(Output+"/play_result.txt",message);EditorApplication.Exit(code);}
    public static void Build()
    {
        Guard();string path=Main+"/Builds/SingleArmControlVisualQualityRecovery/SingleArmControl.exe";
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions{scenes=new[]{"Assets/Scenes/Demo_07_SingleArmControl.unity"},locationPathName=path,target=BuildTarget.StandaloneWindows64});
        File.WriteAllText(Output+"/build_result.txt",report.summary.result+" errors="+report.summary.totalErrors+" warnings="+report.summary.totalWarnings+" output="+path);
        Need(report.summary.result==UnityEditor.Build.Reporting.BuildResult.Succeeded,"Windows build");
    }
}
