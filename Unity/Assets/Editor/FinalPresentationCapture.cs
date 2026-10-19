using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Reflection;
using System.Diagnostics;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.Rendering.Universal;
using Object=UnityEngine.Object;
using Debug=UnityEngine.Debug;

public static class FinalPresentationCapture
{
    static readonly string Main=ControlStudioBatchPaths.Root;
    static readonly string Output=Main+"/Validation/FinalPresentation";
    const string SceneFolder="Assets/Scenes/FinalPresentation";
    static void Need(bool pass,string message){if(!pass)throw new Exception(message);}
    static void Call(object obj,string method)=>obj.GetType().GetMethod(method,BindingFlags.Instance|BindingFlags.NonPublic).Invoke(obj,null);
    static void Guard(){Need(ControlStudioBatchPaths.Allowed,"Isolated Editor only");Directory.CreateDirectory(Output);}
    public static void Prepare()
    {
        Guard();Directory.CreateDirectory(SceneFolder);
        foreach(bool human in new[]{false,true})
        {
            var source=EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
            GameObject robot;
            if(human)robot=Object.Instantiate(Resources.Load<HumanoidVisualRig>("VisualProfiles/HumanoidRobot")).gameObject;
            else{
                var controller=Object.FindFirstObjectByType<ForearmArmController>();Need(controller!=null,"Source controller");
                robot=Object.Instantiate(controller.transform.root.gameObject);robot.name="G51 Presentation / original rig copy";
                foreach(var behaviour in robot.GetComponentsInChildren<MonoBehaviour>(true))
                    if(!(behaviour is ForearmArmController)&&!(behaviour is Demo06RealisticVisualAdapter)&&!(behaviour is G51GripperVisual))Object.DestroyImmediate(behaviour);
            }
            var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Additive);
            SceneManager.MoveGameObjectToScene(robot,scene);EditorSceneManager.CloseScene(source,true);SceneManager.SetActiveScene(scene);
            var cameraObject=new GameObject("Presentation Camera",typeof(Camera));cameraObject.tag="MainCamera";
            var camera=cameraObject.GetComponent<Camera>();camera.nearClipPlane=.01f;camera.farClipPlane=100;camera.fieldOfView=38;camera.allowHDR=true;
            var extra=camera.GetUniversalAdditionalCameraData();extra.renderPostProcessing=true;extra.antialiasing=AntialiasingMode.SubpixelMorphologicalAntiAliasing;extra.antialiasingQuality=AntialiasingQuality.High;
            var director=new GameObject("Scripted Presentation / NO HARDWARE").AddComponent<FinalPresentationDirector>();director.presentationCamera=camera;
            if(human)director.humanoid=robot.GetComponent<HumanoidVisualRig>();
            else{director.g51=robot.GetComponentInChildren<ForearmArmController>(true);director.adapter=robot.GetComponentInChildren<Demo06RealisticVisualAdapter>(true);}
            string path=SceneFolder+(human?"/Humanoid_FinalPresentation.unity":"/G51_FinalPresentation.unity");EditorSceneManager.SaveScene(scene,path);
        }
        AssetDatabase.Refresh();File.WriteAllText(Output+"/scene_prepare.txt","PASS: 별도 시연 Scene 2개 생성. 원본 Demo_07 저장 없음. scripted motion, hardware TX 경로 없음.");
    }
    static FinalPresentationDirector Load(bool human)
    {
        EditorSceneManager.OpenScene(SceneFolder+(human?"/Humanoid_FinalPresentation.unity":"/G51_FinalPresentation.unity"));
        var d=Object.FindFirstObjectByType<FinalPresentationDirector>();d.automaticPlayback=false;
        if(d.g51!=null)Call(d.g51,"Awake");d.Initialize();return d;
    }
    static Renderer[] Renderers(FinalPresentationDirector d)=>d.RobotRoot.GetComponentsInChildren<MeshRenderer>(true).Where(r=>r.enabled&&r.gameObject.activeInHierarchy).ToArray();
    static Bounds BoundsOf(Renderer[] r){Need(r.Length>0,"Visible robot");var b=r[0].bounds;foreach(var item in r.Skip(1))b.Encapsulate(item.bounds);return b;}
    static Vector3 Tip(FinalPresentationDirector d)=>d.IsHumanoid?d.humanoid.right.toolMount.position:d.adapter.m3.visual.Find("M3GripperMountPoint_Visual").position;
    static Vector3[] Corners(Bounds b)=>(from x in new[]{b.min.x,b.max.x} from y in new[]{b.min.y,b.max.y} from z in new[]{b.min.z,b.max.z} select new Vector3(x,y,z)).ToArray();
    static void Fit(FinalPresentationDirector d)
    {
        var all=Renderers(d);d.Evaluate(0);var b=BoundsOf(all);var start=Tip(d);d.Evaluate(2.4f);var end=Tip(d);
        for(int i=0;i<=330;i++){d.Evaluate(i/30f);b.Encapsulate(BoundsOf(all));}
        Vector3 offset;
        if(d.IsHumanoid)offset=new Vector3(1.4f,.40f,-3.3f).normalized;
        else{var travel=end-start;travel.y=0;travel.Normalize();offset=(-Vector3.Cross(travel,Vector3.up)+Vector3.up*.38f).normalized;}
        var cam=d.presentationCamera;cam.aspect=16f/9;Vector3 target=b.center;float distance=b.extents.magnitude;
        for(int i=0;i<200;i++){
            cam.transform.position=target+offset*distance;cam.transform.LookAt(target);
            if(Corners(b).All(p=>{var v=cam.WorldToViewportPoint(p);return v.z>.02f&&v.x>.05f&&v.x<.95f&&v.y>.05f&&v.y<.95f;}))break;
            distance*=1.025f;
        }
        d.Evaluate(0);d.UpdateStudio();
    }
    static string PathOf(Transform t)=>t.parent==null?t.name:PathOf(t.parent)+"/"+t.name;
    static void Audit(FinalPresentationDirector d,string name)
    {
        var log=new StringBuilder("SCRIPTED PRESENTATION / Unity rig-local motion / Hardware TX=0\n");
        Transform[] joints=d.IsHumanoid?new[]{d.humanoid.left.shoulderYaw,d.humanoid.left.shoulderPitch,d.humanoid.left.shoulderRoll,d.humanoid.left.elbowPitch,d.humanoid.left.elbowRoll,d.humanoid.left.wristPitch,d.humanoid.left.wristRoll,d.humanoid.right.shoulderYaw,d.humanoid.right.shoulderPitch,d.humanoid.right.shoulderRoll,d.humanoid.right.elbowPitch,d.humanoid.right.elbowRoll,d.humanoid.right.wristPitch,d.humanoid.right.wristRoll}:new[]{d.g51.elbowRoll,d.g51.elbowPitch,d.g51.wristPitch,d.g51.wristRoll};
        joints=joints.Concat(d.RobotRoot.GetComponentsInChildren<G51GripperVisual>(true).SelectMany(g=>new[]{g.leftJawPivot,g.rightJawPivot,g.leftGear,g.rightGear}).Where(t=>t!=null)).Distinct().ToArray();
        foreach(var j in joints)log.AppendLine(PathOf(j)+" localPosition="+j.localPosition+" localRotation="+j.localRotation);
        if(!d.IsHumanoid)foreach(string field in new[]{"elbowRollVisualOffsetDeg","elbowRollVisualSign","elbowPitchVisualOffsetDeg","elbowPitchVisualSign","wristPitchVisualOffsetDeg","wristPitchVisualSign"})log.AppendLine(field+"="+typeof(ForearmArmController).GetField(field,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(d.g51));
        var positions=joints.Select(j=>j.localPosition).ToArray();var parents=d.RobotRoot.GetComponentsInChildren<Transform>(true).ToDictionary(t=>t,t=>t.parent);
        var rootMatrix=d.RobotRoot.localToWorldMatrix;var renderers=Renderers(d);var csv=new StringBuilder("frame,time,tip_x,tip_y,tip_z\n");
        var previousRotations=joints.Select(j=>j.localRotation).ToArray();float maximumStep=0;
        Vector3 first=Vector3.zero,last=Vector3.zero,beforePitch=Vector3.zero,afterPitch=Vector3.zero;
        for(int i=0;i<=660;i++){
            float t=i/60f;d.Evaluate(t);var tip=Tip(d);if(i==0)first=tip;if(i==660)last=tip;if(i==192)beforePitch=tip;if(i==300)afterPitch=tip;
            Need(d.RobotRoot.localToWorldMatrix==rootMatrix,"Robot root moved");
            for(int j=0;j<joints.Length;j++)Need(joints[j].localPosition==positions[j],"Joint pivot translated");
            for(int j=0;j<joints.Length;j++){if(i>0)maximumStep=Mathf.Max(maximumStep,Quaternion.Angle(previousRotations[j],joints[j].localRotation));previousRotations[j]=joints[j].localRotation;}
            foreach(var grip in d.RobotRoot.GetComponentsInChildren<G51GripperVisual>(true)){
                if(grip.leftLinkBar!=null&&grip.leftDrivePin!=null&&grip.leftJawLinkAnchor!=null)Need(Vector3.Distance(grip.leftLinkBar.position,(grip.leftDrivePin.position+grip.leftJawLinkAnchor.position)*.5f)<.00001f,"Left rod disconnected");
                if(grip.rightLinkBar!=null&&grip.rightDrivePin!=null&&grip.rightJawLinkAnchor!=null)Need(Vector3.Distance(grip.rightLinkBar.position,(grip.rightDrivePin.position+grip.rightJawLinkAnchor.position)*.5f)<.00001f,"Right rod disconnected");
            }
            foreach(var pair in parents)Need(pair.Key.parent==pair.Value,"Hierarchy changed");
            foreach(var p in Corners(BoundsOf(renderers))){var v=d.presentationCamera.WorldToViewportPoint(p);Need(v.z>0&&v.x>.02f&&v.x<.98f&&v.y>.02f&&v.y<.98f,"Motion clipped at "+t);}
            Need(!float.IsNaN(tip.x+tip.y+tip.z),"NaN");csv.AppendLine(FormattableString.Invariant($"{i},{t:F6},{tip.x:F6},{tip.y:F6},{tip.z:F6}"));
        }
        Need(Vector3.Distance(first,last)<.00001f,"Loop pose mismatch");
        Need(maximumStep<4,"Discontinuous per-frame joint motion");
        if(!d.IsHumanoid){Need(afterPitch.y<beforePitch.y-.005f,"Pitch must lower tool: "+beforePitch+" -> "+afterPitch);d.Evaluate(0);float x0=d.presentationCamera.WorldToViewportPoint(Tip(d)).x;d.Evaluate(2.4f);Need(d.presentationCamera.WorldToViewportPoint(Tip(d)).x>x0+.03f,"Roll must sweep screen left to right");}
        log.AppendLine("PASS: 661 samples; fixed pivots/root/hierarchy; finite pose; full framing; loop closure.");log.AppendLine("Pitch start="+beforePitch+" end="+afterPitch);
        log.AppendLine("Maximum joint step at 60 fps="+maximumStep+" deg; linkage midpoint validation PASS.");
        if(!d.IsHumanoid){
            d.Evaluate(2.4f);var stopped=joints.Select(j=>j.localRotation).ToArray();
            d.Evaluate(3.1f);for(int j=0;j<joints.Length;j++)Need(Quaternion.Angle(stopped[j],joints[j].localRotation)<.01f,"Roll stop interval changed");
            for(int i=0;i<=300;i++){float t=i/60f;var c=FinalPresentationDirector.G51At(t);Need(c.gripper==1,"Gripper closed before pitch completed");if(t<=3.2f)Need(c.elbowPitch==82&&c.wristPitch==95,"Pitch started before roll stop");}
            log.AppendLine("PASS: Roll 0.6-2.4s; stop 2.4-3.2s; elbow/wrist pitch 3.2-5.0s; close 5.0-5.8s.");
        }
        File.WriteAllText(Output+"/"+name+"_hierarchy_validation.txt",log.ToString());File.WriteAllText(Output+"/"+name+"_trajectory.csv",csv.ToString());d.Evaluate(0);
    }
    static Texture2D Snap(Camera cam,int width,int height)
    {
        var rt=RenderTexture.GetTemporary(width,height,24,RenderTextureFormat.ARGB32);var old=RenderTexture.active;var target=cam.targetTexture;
        var tex=new Texture2D(width,height,TextureFormat.RGB24,false);cam.targetTexture=rt;cam.rect=new Rect(0,0,1,1);cam.aspect=(float)width/height;cam.Render();RenderTexture.active=rt;tex.ReadPixels(new Rect(0,0,width,height),0,0);tex.Apply();cam.targetTexture=target;RenderTexture.active=old;RenderTexture.ReleaseTemporary(rt);return tex;
    }
    public static void Preview()
    {
        Guard();foreach(bool human in new[]{false,true}){
            string name=human?"Humanoid":"G51";var d=Load(human);Fit(d);Audit(d,name);
            foreach(float time in new[]{0f,2.4f,3.2f,5f,6.2f,8f,10.5f}){d.Evaluate(time);var tex=Snap(d.presentationCamera,1920,1080);File.WriteAllBytes(Output+"/"+name+"_preview_"+time.ToString("F1",System.Globalization.CultureInfo.InvariantCulture)+".png",tex.EncodeToPNG());Object.DestroyImmediate(tex);}
            PersistCamera(human,d.presentationCamera);
        }
    }
    public static void AuditOnly(){Guard();foreach(bool human in new[]{false,true}){var d=Load(human);Fit(d);Audit(d,human?"Humanoid":"G51");}}
    static void PersistCamera(bool human,Camera fitted)
    {
        Vector3 position=fitted.transform.position;Quaternion rotation=fitted.transform.rotation;
        string path=SceneFolder+(human?"/Humanoid_FinalPresentation.unity":"/G51_FinalPresentation.unity");
        var clean=EditorSceneManager.OpenScene(path);var d=Object.FindFirstObjectByType<FinalPresentationDirector>();
        d.presentationCamera.transform.SetPositionAndRotation(position,rotation);d.automaticPlayback=true;
        EditorSceneManager.MarkSceneDirty(clean);EditorSceneManager.SaveScene(clean,path);
    }
    public static void RecordG51(){Record(false);}public static void RecordHumanoid(){Record(true);}
    static void Record(bool human)
    {
        Guard();string name=human?"Humanoid":"G51";var d=Load(human);Fit(d);Audit(d,name);
        const int width=3840,height=2160,fps=60;string exe=Output+"/Tooling/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe";
        var info=new ProcessStartInfo(exe,"-y -f rawvideo -pixel_format rgb24 -video_size "+width+"x"+height+" -framerate "+fps+" -i pipe:0 -vf vflip -an -c:v libx264 -preset slow -crf 16 -pix_fmt yuv420p -movflags +faststart \""+Output+"/"+name+".mp4\""){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardError=true};
        var log=new StringBuilder();using(var process=new Process{StartInfo=info}){
            process.ErrorDataReceived+=(s,e)=>{if(e.Data!=null)lock(log)log.AppendLine(e.Data);};process.Start();process.BeginErrorReadLine();
            for(int frame=0;frame<fps*FinalPresentationDirector.Duration;frame++){
                d.Evaluate(frame/(float)fps);var tex=Snap(d.presentationCamera,width,height);var data=tex.GetRawTextureData<byte>().ToArray();process.StandardInput.BaseStream.Write(data,0,data.Length);
                if(frame==330)File.WriteAllBytes(Output+"/"+name+"_UnityMaster.png",tex.EncodeToPNG());Object.DestroyImmediate(tex);
                if(frame%60==0)Debug.Log("FINAL RECORD "+name+" "+frame+"/660");
            }
            process.StandardInput.Close();process.WaitForExit();File.WriteAllText(Output+"/"+name+"_encode.log",log.ToString());Need(process.ExitCode==0,"MP4 encoder failed");
        }
        File.WriteAllText(Output+"/"+name+"_record_result.txt","PASS: Unity Camera.Render 3840x2160 / 60 fps / 660 frames / 11 seconds / H264 CRF16 / no UI. Encoder output requires full video review.");
    }
    const string PlayKey="FinalPresentationPlayValidation";
    public static void PlayG51(){StartPlay(false);} public static void PlayHumanoid(){StartPlay(true);}
    static void StartPlay(bool human)
    {
        Guard();EditorSceneManager.OpenScene(SceneFolder+(human?"/Humanoid_FinalPresentation.unity":"/G51_FinalPresentation.unity"));
        SessionState.SetBool(PlayKey,true);SessionState.SetBool(PlayKey+"human",human);SessionState.SetString(PlayKey+"start",DateTime.UtcNow.ToString("O"));EditorApplication.isPlaying=true;
    }
    static int playSamples;static bool playCapture;static Vector3 playFirst;static float playTravel;
    [InitializeOnLoadMethod] static void HookPlay(){EditorApplication.update+=CheckPlay;}
    static void CheckPlay()
    {
        if(!SessionState.GetBool(PlayKey,false))return;
        string name=SessionState.GetBool(PlayKey+"human",false)?"Humanoid":"G51";
        try{
            Need(DateTime.UtcNow-DateTime.Parse(SessionState.GetString(PlayKey+"start",""))<TimeSpan.FromMinutes(3),"Play timeout");
            if(!EditorApplication.isPlaying||EditorApplication.isCompiling)return;
            var d=Object.FindFirstObjectByType<FinalPresentationDirector>();if(d==null||Time.timeSinceLevelLoad<.2f)return;
            Need(d.automaticPlayback,"Automatic presentation playback disabled");
            Need(Object.FindFirstObjectByType<SingleArmCommandRouter>()==null,"Unexpected production writer");
            if(playSamples++==0)playFirst=Tip(d);playTravel=Mathf.Max(playTravel,Vector3.Distance(playFirst,Tip(d)));
            if(!playCapture&&Time.timeSinceLevelLoad>6){var tex=Snap(d.presentationCamera,1920,1080);File.WriteAllBytes(Output+"/"+name+"_play.png",tex.EncodeToPNG());Object.DestroyImmediate(tex);playCapture=true;}
            if(Time.timeSinceLevelLoad>11.2f){Need(playSamples>30&&playTravel>.03f,"No full Play motion");SessionState.SetBool(PlayKey,false);File.WriteAllText(Output+"/"+name+"_play_result.txt","PASS: 분리된 Unity Editor Play Mode에서 자동 시연 한 주기 완료. samples="+playSamples+", tip travel="+playTravel+". 원본 router/UART 없음; scripted presentation only.");EditorApplication.Exit(0);}
        }catch(Exception e){SessionState.SetBool(PlayKey,false);File.WriteAllText(Output+"/"+name+"_play_result.txt","FAIL: "+e);EditorApplication.Exit(2);}
    }
}
