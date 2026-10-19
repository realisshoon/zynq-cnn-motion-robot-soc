using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Opt-in clean-clone Player check. Uses the real UI EventSystem; never connects a port.
    public sealed class HandoffReproPlayerAcceptance : MonoBehaviour
    {
        string output;readonly List<string> log=new List<string>();
        SingleArmCommandRouter router;ControlStudioRuntimeUI ui;ControlStudioStartupMenu menu;RobotControlMockPanel mock;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]static void RunExplicitly()
        {
            var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--handoff-verify");
            if(Application.isEditor||i<0||i+1>=args.Length)return;
            new GameObject("Explicit handoff Player verification").AddComponent<HandoffReproPlayerAcceptance>().output=args[i+1];
        }
        void Check(bool ok,string note){log.Add((ok?"PASS ":"FAIL ")+note);if(!ok)throw new Exception(note);}
        void Save()=>File.WriteAllLines(Path.Combine(output,"handoff_player_result.txt"),log);
        IEnumerator Start()
        {
            Directory.CreateDirectory(output);var sequence=Run();
            while(true){bool more;object step=null;try{more=sequence.MoveNext();if(more)step=sequence.Current;}catch(Exception e){log.Add("FAIL "+e);Save();ScreenCapture.CaptureScreenshot(Path.Combine(output,"failure.png"));yield break;}if(!more)break;yield return step;}
            log.Add("RESULT=PASS / actual Windows Player / EventSystem input / no physical connection");Save();Destroy(gameObject);
        }
        Transform Find(string name)=>ui.RuntimeCanvas.GetComponentsInChildren<Transform>(true).Concat(menu.MenuCanvas.GetComponentsInChildren<Transform>(true)).Single(t=>t.name==name&&t.GetComponent<Button>()!=null);
        PointerEventData Hit(GameObject target,Vector2 point)
        {
            Canvas.ForceUpdateCanvases();var data=new PointerEventData(EventSystem.current){position=point,button=PointerEventData.InputButton.Left};var hits=new List<RaycastResult>();EventSystem.current.RaycastAll(data,hits);
            Check(hits.Count>0&&(hits[0].gameObject==target||hits[0].gameObject.transform.IsChildOf(target.transform)),"pointer hit "+target.name+" at "+point+" hits="+string.Join(",",hits.Take(3).Select(h=>h.gameObject.name)));
            data.pointerCurrentRaycast=data.pointerPressRaycast=hits[0];return data;
        }
        void Click(string name)
        {
            Canvas.ForceUpdateCanvases();var t=Find(name);Check(t.gameObject.activeInHierarchy&&t.GetComponent<Button>().IsInteractable(),"clickable "+name);
            var rect=(RectTransform)t;var data=Hit(t.gameObject,RectTransformUtility.WorldToScreenPoint(null,rect.TransformPoint(rect.rect.center)));
            ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerDownHandler);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerUpHandler);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerClickHandler);
        }
        void Drag(Slider slider,float value)
        {
            Canvas.ForceUpdateCanvases();var area=(RectTransform)slider.handleRect.parent;
            var point=area.TransformPoint(new Vector3(Mathf.Lerp(area.rect.xMin,area.rect.xMax,Mathf.InverseLerp(slider.minValue,slider.maxValue,value)),area.rect.center.y,0));
            var data=Hit(slider.gameObject,RectTransformUtility.WorldToScreenPoint(null,point));
            ExecuteEvents.ExecuteHierarchy(data.pointerCurrentRaycast.gameObject,data,ExecuteEvents.initializePotentialDrag);
            ExecuteEvents.ExecuteHierarchy(data.pointerCurrentRaycast.gameObject,data,ExecuteEvents.pointerDownHandler);
            ExecuteEvents.Execute(slider.gameObject,data,ExecuteEvents.dragHandler);ExecuteEvents.Execute(slider.gameObject,data,ExecuteEvents.pointerUpHandler);
            Check(Mathf.Abs(slider.value-value)<1.1f,"slider "+slider.name+"="+slider.value);
        }
        IEnumerator Capture(string name)
        {
            yield return new WaitForSecondsRealtime(.3f);yield return new WaitForEndOfFrame();var image=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,name+".png"),image.EncodeToPNG());Destroy(image);Save();
        }
        void HardwareOff()=>Check(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0&&!router.GetComponent<ControlStudioUartOutput>().Connected&&!router.GetComponent<UartPose3DSource>().Connected,"Hardware TX=0; RX/TX disconnected");
        IEnumerator Run()
        {
            yield return new WaitForSecondsRealtime(2);
            Check(SceneManager.GetActiveScene().name=="Demo_07_SingleArmControl","default Build Settings scene is Demo07");
            router=FindFirstObjectByType<SingleArmCommandRouter>();Check(router!=null&&router.Ready,"ABI v2 native ready in Windows Player");
            ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();mock=router.GetComponent<RobotControlMockPanel>();
            Check(menu.Visible,"Startup menu");HardwareOff();Screen.SetResolution(1920,1080,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.7f);
            yield return Capture("01_startup_g51");
            Click("INPUT next");yield return new WaitForSecondsRealtime(.2f);Click("INPUT next");yield return new WaitForSecondsRealtime(.2f);
            var captions=menu.MenuCanvas.GetComponentsInChildren<Text>(true).Select(t=>t.text).ToArray();
            Check(captions.Contains("UART")&&!captions.Contains("UART RX"),"Startup INPUT uses UART label");
            yield return Capture("01a_startup_uart");Click("START");yield return Capture("01b_uart_started");
            Check(router.Source=="UART"&&!menu.Visible,"UART START selects existing source");HardwareOff();
            Click("MENU");yield return new WaitForSecondsRealtime(.2f);Click("INPUT next");yield return new WaitForSecondsRealtime(.2f);Click("START");yield return new WaitForSecondsRealtime(.2f);router.SetPaused(true);
            Check(!ui.ServoDetailsExpanded,"default Servo collapsed");yield return Capture("02_main_g51");
            Click("Servo details toggle");yield return Capture("02b_servo_expanded");Check(ui.Sliders.All(s=>s.gameObject.activeInHierarchy),"M0-M4 sliders visible");
            var seed=(float[])router.Applied.Clone();var epoch=router.Epoch;
            Drag(ui.Sliders[2],110);yield return new WaitForSecondsRealtime(.2f);Check(Mathf.Abs(ui.manual.Values[2]-110)<1.1f,"Servo UI requested M2");
            using(var reference=new ControlStudioOutputPolicy(seed)) {
                Check(reference.Submit(ui.manual.Values)==0,"Agent2 target accepted");router.enabled=false;router.SetPaused(false);var expected=new float[5];var velocity=new float[4];
                for(int i=0;i<180;i++){reference.Tick(expected,velocity);router.AdvanceVirtual(.02);Check(router.Applied.SequenceEqual(expected),"Agent2 / router trace "+i);}
                router.SetPaused(true);router.enabled=true;
            }
            yield return new WaitForSecondsRealtime(.2f);Check(Mathf.Abs(router.Applied[2]-ui.manual.Values[2])<.002,"actual ApplyCommand M2 settled");
            yield return Capture("03_servo_applied");Click("Servo details toggle");yield return new WaitForSecondsRealtime(.2f);
            Check(!mock.State.Pwm&&!mock.State.Follow,"Mock default OFF");Check(!Find("Follow start").GetComponent<Button>().IsInteractable(),"FOLLOW disabled with PWM OFF");
            Click("Advanced toggle");yield return new WaitForSecondsRealtime(.2f);Check(mock.State.Red==40&&mock.State.Green==30&&mock.State.Blue==50,"RGB default 40/30/50");
            Click("PWM enable");Click("Follow start");Check(mock.State.Pwm&&mock.State.Follow,"Mock PWM/FOLLOW ON");
            Click("Follow stop");Check(mock.State.Pwm&&!mock.State.Follow,"Mock stop preserves PWM");
            Drag(mock.MarginSliders[0],64);Drag(mock.MarginSliders[1],22);Drag(mock.MarginSliders[2],78);Click("Apply mock margins");
            Check(mock.State.AppliedRed==64&&mock.State.AppliedGreen==22&&mock.State.AppliedBlue==78,"RGB Apply Mock only");HardwareOff();yield return Capture("04_rgb_mock_applied");
            Click("Advanced toggle");Click("Preview toggle");yield return new WaitForSecondsRealtime(.2f);Check(mock.PreviewOpen,"Camera Preview Mock ON");yield return Capture("05_camera_mock_on");
            Click("Preview toggle");yield return new WaitForSecondsRealtime(.2f);Check(!mock.PreviewOpen,"Camera Preview OFF");Check(router.Epoch==epoch,"GUI actions do not reset epoch");
            Click("MENU");yield return new WaitForSecondsRealtime(.2f);Click("ROBOT next");yield return new WaitForSecondsRealtime(.2f);yield return Capture("06_startup_humanoid");Click("START");yield return new WaitForSecondsRealtime(.2f);
            Check(router.GetComponent<RobotVisualProfiles>().Selected==RobotVisualProfileId.HumanoidRobot,"Humanoid selected");yield return Capture("07_main_humanoid");
            var materials=FindObjectsByType<Renderer>(FindObjectsSortMode.None).Where(r=>r.enabled&&r.gameObject.activeInHierarchy).SelectMany(r=>r.sharedMaterials).ToArray();
            Check(materials.All(m=>m!=null&&m.shader!=null&&m.shader.isSupported&&m.shader.name!="Hidden/InternalErrorShader"),"active renderers have supported materials");
            Directory.CreateDirectory("Validation/SingleArmControlXyz");var regression=new GameObject("Handoff XYZ native regression").AddComponent<XyzStudioAcceptance>();float deadline=Time.realtimeSinceStartup+90;
            while(!regression.Complete&&Time.realtimeSinceStartup<deadline)yield return new WaitForSecondsRealtime(.2f);
            Check(regression.Complete&&regression.Passed,"both CSV x XYZ A/B, ABI1/2 native calculation, state/Manual regression");
            File.Copy("Validation/SingleArmControlXyz/editor-results.txt",Path.Combine(output,"player_xyz_result.txt"),true);Destroy(regression.gameObject);
            router.enabled=true;router.SetPaused(true);HardwareOff();yield return Capture("08_humanoid_after_native_regression");
        }
    }
}
