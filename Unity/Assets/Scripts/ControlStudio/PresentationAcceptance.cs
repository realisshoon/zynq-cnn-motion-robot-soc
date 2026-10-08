using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Isolated Unity-rendered proof: actual gate, particles, paint texture and UI visibility.
    public sealed class PresentationAcceptance : MonoBehaviour
    {
        const string Out="Validation/PresentationCapture";
        readonly List<string> results=new List<string>();
        readonly List<string> metrics=new List<string>();
        SingleArmCommandRouter router;ControlStudioRuntimeUI studio;ControlStudioPresentationUI ui;
        PresentationSprayDemo demo;Tool1Runtime tool;RobotVisualProfiles profiles;ManualServoSource manual;
        public bool Complete,Passed;
        void Check(bool ok,string description){results.Add((ok?"PASS ":"FAIL ")+description);if(!ok)Complete=true;Save();if(!ok)throw new Exception(description);}
        void Save(){Directory.CreateDirectory(Out);File.WriteAllLines(Out+"/editor-results.txt",new[]{Passed?"PASS":"FAIL"}.Concat(results));File.WriteAllLines(Out+"/metrics.txt",metrics);}
        static Button FindButton(Transform root,string name)=>root.GetComponentsInChildren<Button>(true).FirstOrDefault(b=>b.name==name);
        IEnumerator Start()
        {
            yield return null;yield return null;
            router=FindFirstObjectByType<SingleArmCommandRouter>();studio=router.GetComponent<ControlStudioRuntimeUI>();
            ui=router.GetComponent<ControlStudioPresentationUI>();demo=router.GetComponent<PresentationSprayDemo>();
            tool=router.GetComponent<Tool1Runtime>();profiles=router.GetComponent<RobotVisualProfiles>();manual=router.GetComponent<ManualServoSource>();
            var canvas=studio.RuntimeCanvas;
            metrics.Add("presentation_components="+router.GetComponents<ControlStudioPresentationUI>().Length+" canvas_roots="+router.GetComponentsInChildren<Canvas>(true).Length);
            Check(canvas!=null&&ui!=null&&demo!=null,"Presentation UI and demo attached");
            var headings=new[]{"SOURCE / CSV","SERVO COMMANDS","TOOL / EFFECTS","CELL / DEBUG"};
            foreach(var heading in headings)Check(FindButton(canvas,heading)!=null,"collapsible header "+heading);
            var source=ui.SectionGroup(0);var servo=ui.SectionGroup(1);
            var toolPanel=ui.SectionGroup(2);var debug=ui.SectionGroup(3);
            Check(source!=null&&servo!=null&&toolPanel!=null&&debug!=null,
                $"four panel visibility groups present source={source!=null} servo={servo!=null} tool={toolPanel!=null} debug={debug!=null}");
            FindButton(canvas,"SOURCE / CSV").onClick.Invoke();Check(!source.activeSelf,"Source collapsed to header");
            FindButton(canvas,"SOURCE / CSV").onClick.Invoke();Check(source.activeSelf,"Source expanded and interactive");
            FindButton(canvas,"SERVO COMMANDS").onClick.Invoke();Check(!servo.activeSelf,"Servo collapsed to header");
            FindButton(canvas,"SERVO COMMANDS").onClick.Invoke();Check(servo.activeSelf,"Servo expanded and interactive");
            FindButton(canvas,"TOOL / EFFECTS").onClick.Invoke();Check(router.GetComponent<Tool1RuntimeUI>().ShowingToolPanel,"Tool expanded for next UI frame");
            FindButton(canvas,"TOOL / EFFECTS").onClick.Invoke();Check(!toolPanel.activeSelf,"Tool collapsed to header");
            FindButton(canvas,"CELL / DEBUG").onClick.Invoke();Check(!debug.activeSelf,"Cell and Debug collapsed to header");
            FindButton(canvas,"CELL / DEBUG").onClick.Invoke();Check(debug.activeSelf,"Cell and Debug restored");
            Check(router.Source=="MANUAL"&&router.Ready&&router.HardwareTxCount==0,"source and hardware ownership unchanged by UI");
            router.enabled=false;demo.AutomaticClock=false;
            Check(demo.StartDemo(),"explicit G51 Spray demo setup");
            Check(ui.PresentationMode&&!canvas.gameObject.activeInHierarchy,"Presentation hides entire runtime canvas");
            yield return null;
            var sourceCamera=FindFirstObjectByType<ControlStudioOrbitCamera>().GetComponent<Camera>();
            Check(sourceCamera.rect==new Rect(0,0,1,1),"Presentation camera full viewport");
            Check(!FindFirstObjectByType<LineRenderer>().enabled,"selected joint axis hidden");
            Check(profiles.Selected==RobotVisualProfileId.G51&&tool.Gate.Tool==ToolKind.Spray&&router.Source=="MANUAL","G51 Spray uses existing Manual owner");
            Directory.CreateDirectory(Out+"/frames");
            var camera=new GameObject("Presentation evidence camera").AddComponent<Camera>();camera.CopyFrom(sourceCamera);
            camera.rect=new Rect(0,0,1,1);camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=new Color(.018f,.025f,.035f);
            var texture=new RenderTexture(960,540,24);camera.targetTexture=texture;var image=new Texture2D(960,540,TextureFormat.RGB24,false);
            float minPitch=float.PositiveInfinity,maxPitch=float.NegativeInfinity;int maxPaint=0,maxParticles=0;
            const float dt=1f/24f;const int frameCount=240;
            for(int frame=0;frame<frameCount;frame++){
                demo.AdvanceVirtual(dt);router.AdvanceVirtual(dt);
                // Batchmode has no Game View end-of-frame callback; render after the next update.
                yield return null;
                camera.transform.SetPositionAndRotation(sourceCamera.transform.position,sourceCamera.transform.rotation);
                camera.Render();var previous=RenderTexture.active;RenderTexture.active=texture;
                image.ReadPixels(new Rect(0,0,960,540),0,0);image.Apply(false,false);
                File.WriteAllBytes(Out+"/frames/frame_"+frame.ToString("D4")+".png",image.EncodeToPNG());RenderTexture.active=previous;
                minPitch=Mathf.Min(minPitch,router.Applied[2]);maxPitch=Mathf.Max(maxPitch,router.Applied[2]);
                maxPaint=Mathf.Max(maxPaint,tool.Surface.PaintCount);
                if(tool.Visual?.spray!=null)maxParticles=Mathf.Max(maxParticles,tool.Visual.spray.particleCount);
            }
            camera.targetTexture=null;Destroy(camera.gameObject);Destroy(texture);Destroy(image);
            var paintTexture=tool.Surface.GetComponent<Renderer>().sharedMaterial.mainTexture as Texture2D;
            int colored=paintTexture==null?0:paintTexture.GetPixels32().Count(p=>p.r<245||p.g<245||p.b<245);
            float coverage=colored/(256f*256f);
            metrics.Add("frame_count="+frameCount+" fps=24 virtual_seconds="+(frameCount*dt));
            metrics.Add("ready="+demo.ReadyObserved+" running="+demo.SprayObserved+" stopped="+demo.StoppedObserved);
            metrics.Add("paint_count="+maxPaint+" coverage="+coverage.ToString("F4")+" peak_particles="+maxParticles);
            metrics.Add("applied_m2_min="+minPitch.ToString("F2")+" applied_m2_max="+maxPitch.ToString("F2"));
            Check(demo.ReadyObserved&&demo.SprayObserved&&demo.StoppedObserved,"OPEN READY / CLOSE RUNNING / OPEN STOP sequence");
            Check(maxParticles>0,"actual Spray particles rendered");
            Check(maxPaint>10&&coverage>.01f,"actual accumulated paint on panel");
            Check(maxPitch-minPitch>5f,"M2 joint moved through Manual / Router output");
            Check(router.HardwareTxCount==0,"Hardware TX=0 throughout demo");
            ui.SetPresentationMode(false);yield return null;
            Check(canvas.gameObject.activeInHierarchy&&source.activeSelf&&servo.activeSelf&&debug.activeSelf,"Presentation OFF restores runtime UI");
            Check(profiles.SelectTool(ToolKind.Gripper)&&profiles.Select(RobotVisualProfileId.G51),"legacy regression starts at G51 Gripper");
            tool.SetEnabled(false);manual.Home();router.SetPaused(false);
            Passed=Complete=true;Save();Debug.Log("PRESENTATION ACCEPTANCE PASS");
        }
    }
}
