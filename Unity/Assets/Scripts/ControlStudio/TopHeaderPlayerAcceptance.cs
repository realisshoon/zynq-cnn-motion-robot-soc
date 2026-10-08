using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Opt-in Player verification only. No scene component or normal-startup action.
    public sealed class TopHeaderPlayerAcceptance : MonoBehaviour
    {
        readonly List<string> log=new List<string>();string output;
        ControlStudioRuntimeUI ui;ControlStudioStartupMenu menu;ControlStudioPresentationUI view;RobotControlMockPanel mock;
        SingleArmCommandRouter router;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void OptionalRun()
        {
            var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--top-header-verify");
            if(Application.isEditor||i<0||i+1>=args.Length)return;
            var test=new GameObject("Explicit top-header Player verification").AddComponent<TopHeaderPlayerAcceptance>();test.output=args[i+1];
        }
        void Check(bool ok,string message){log.Add((ok?"PASS: ":"FAIL: ")+message);if(!ok)throw new InvalidOperationException(message);}
        IEnumerator Start()
        {
            Directory.CreateDirectory(output);log.Add("Actual Windows Player / EventSystem pointer injection; no OS mouse automation");
            log.Add("Unity="+Application.unityVersion+" executable="+Environment.GetCommandLineArgs()[0]);
            var sequence=Run();
            while(true){bool next;object step=null;try{next=sequence.MoveNext();if(next)step=sequence.Current;}catch(Exception ex){log.Add("FAIL: "+ex);Save();ScreenCapture.CaptureScreenshot(Path.Combine(output,"failure.png"));yield break;}if(!next)break;yield return step;}
            log.Add("RESULT=PASS / Player remains open for manual review");Save();Destroy(gameObject);
        }
        void Save()=>File.WriteAllLines(Path.Combine(output,"player_ui_result.txt"),log);
        Transform Find(string name,bool button=false)=>ui.RuntimeCanvas.GetComponentsInChildren<Transform>(true).Concat(menu.MenuCanvas.GetComponentsInChildren<Transform>(true)).Single(t=>t.name==name&&(!button||t.GetComponent<Button>()!=null));
        void Click(string name)
        {
            Canvas.ForceUpdateCanvases();var t=Find(name,true);var button=t.GetComponent<Button>();
            Check(button.IsInteractable()&&t.gameObject.activeInHierarchy,"clickable "+name);
            var r=(RectTransform)t;var point=RectTransformUtility.WorldToScreenPoint(null,r.TransformPoint(r.rect.center));
            var data=new PointerEventData(EventSystem.current){position=point,button=PointerEventData.InputButton.Left};
            var hits=new List<RaycastResult>();EventSystem.current.RaycastAll(data,hits);
            Check(hits.Count>0&&(hits[0].gameObject==t.gameObject||hits[0].gameObject.transform.IsChildOf(t)),"Player pointer raycast "+name);
            data.pointerCurrentRaycast=data.pointerPressRaycast=hits[0];
            ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerDownHandler);
            ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerUpHandler);
            ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerClickHandler);
        }
        IEnumerator Capture(string name)
        {
            yield return new WaitForSecondsRealtime(.3f);yield return new WaitForEndOfFrame();
            var image=ScreenCapture.CaptureScreenshotAsTexture();
            File.WriteAllBytes(Path.Combine(output,name+".png"),image.EncodeToPNG());Destroy(image);
            log.Add("CAPTURE: "+name+" "+Screen.width+"x"+Screen.height);Save();
        }
        static bool Overlap(RectTransform a,RectTransform b)
        {
            var ac=new Vector3[4];var bc=new Vector3[4];a.GetWorldCorners(ac);b.GetWorldCorners(bc);
            return Rect.MinMaxRect(ac[0].x,ac[0].y,ac[2].x,ac[2].y).Overlaps(Rect.MinMaxRect(bc[0].x,bc[0].y,bc[2].x,bc[2].y));
        }
        IEnumerator Run()
        {
            yield return new WaitForSecondsRealtime(2);
            router=FindFirstObjectByType<SingleArmCommandRouter>();Check(router!=null&&router.Ready,"existing native output ready");
            ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();mock=router.GetComponent<RobotControlMockPanel>();
            foreach(int width in new[]{1920,1280}){
                int height=width*9/16;string prefix=width+"x"+height+"_";
                Screen.SetResolution(width,height,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(1);
                Check(Screen.width==width&&Screen.height==height,"actual Player resolution "+prefix);
                if(menu.Visible){yield return Capture(prefix+"00_startup");Click("START");yield return new WaitForSecondsRealtime(.5f);}
                router.SetPaused(true);var values=(float[])ui.manual.Values.Clone();int epoch=router.Epoch;
                Check(!ui.ServoDetailsExpanded&&!Find("Manual panel").gameObject.activeInHierarchy,"default Servo collapsed");
                Check(!Find("Presentation control strip").gameObject.activeInHierarchy,"bottom selector removed");
                yield return Capture(prefix+"01_servo_collapsed");
                Click("Reset View");yield return new WaitForSecondsRealtime(.15f);
                Click("Pause virtual");Check(!router.Paused,"Resume button");yield return new WaitForSecondsRealtime(.15f);
                Click("Pause virtual");Check(router.Paused,"Pause button");
                Click("Servo details toggle");yield return new WaitForSecondsRealtime(.15f);
                Check(ui.ServoDetailsExpanded&&ui.Sliders.All(s=>s.gameObject.activeInHierarchy),"Servo expands existing controls");
                yield return Capture(prefix+"02_servo_expanded");
                Click("Preview toggle");yield return new WaitForSecondsRealtime(.15f);
                Check(mock.PreviewOpen,"Camera ON");
                Check(!Overlap((RectTransform)Find("Manual panel"),(RectTransform)Find("Placeholder / no capture")),"Servo and Preview do not overlap");
                yield return Capture(prefix+"03_camera_on");
                Click("Preview toggle");yield return new WaitForSecondsRealtime(.15f);Check(!mock.PreviewOpen,"Camera OFF");
                yield return Capture(prefix+"04_camera_off");
                Click("Advanced toggle");yield return new WaitForSecondsRealtime(.15f);Check(mock.AdvancedOpen,"Advanced opens");
                yield return Capture(prefix+"05_advanced");Click("Advanced toggle");
                Click("Background menu toggle");yield return new WaitForSecondsRealtime(.15f);
                Check(!Find("FACTORY",true).GetComponent<Button>().interactable,"Factory unavailable stays disabled");
                yield return Capture(prefix+"06_background_dropdown");Click("SIMULATION GRID");
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.15f);
                Check(!Find("Manual panel").gameObject.activeInHierarchy&&!mock.PanelRoot.activeInHierarchy,"Controls Hidden");
                yield return Capture(prefix+"07_controls_hidden");Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.15f);
                Click("PRESENTATION");yield return new WaitForSecondsRealtime(.15f);Check(!ui.RuntimeCanvas.gameObject.activeInHierarchy,"Presentation hides all UI");
                yield return Capture(prefix+"08_presentation");view.SetPresentationMode(false);yield return new WaitForSecondsRealtime(.15f);
                Check(ui.manual.Values.SequenceEqual(values)&&router.Epoch==epoch,"layout actions preserve requested values and epoch");
                Click("Servo details toggle");yield return new WaitForSecondsRealtime(.15f);
                Click("MENU");yield return new WaitForSecondsRealtime(.15f);Click("ROBOT next");yield return new WaitForSecondsRealtime(.15f);
                yield return Capture(prefix+"09_humanoid_startup");Click("START");yield return new WaitForSecondsRealtime(.15f);
                Check(router.GetComponent<RobotVisualProfiles>().Selected==RobotVisualProfileId.HumanoidRobot,"Humanoid selected");
                yield return Capture(prefix+"10_humanoid_main");
                Check(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0&&!router.GetComponent<ControlStudioUartOutput>().Connected,"Hardware TX=0 / disconnected");
                if(width==1920){Click("MENU");yield return new WaitForSecondsRealtime(.15f);Click("ROBOT next");yield return new WaitForSecondsRealtime(.15f);}
            }
            Screen.SetResolution(1920,1080,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(1);
            yield return Capture("final_review_humanoid");
        }
    }
}
