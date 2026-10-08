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
    public sealed class HumanoidShoulderAcceptance : MonoBehaviour
    {
        public string output;public bool Complete {get;private set;}public bool Passed {get;private set;}
        public System.Action<string> EditorCapture;readonly List<string> log=new List<string>();
        SingleArmCommandRouter router;ControlStudioRuntimeUI ui;ControlStudioStartupMenu menu;RobotControlMockPanel mock;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]static void RunExplicitly()
        {
            var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--shoulder-verify");
            if(Application.isEditor||i<0||i+1>=args.Length)return;
            new GameObject("Explicit handoff Player verification").AddComponent<HumanoidShoulderAcceptance>().output=args[i+1];
        }
        void Check(bool ok,string note){log.Add((ok?"PASS ":"FAIL ")+note);if(!ok)throw new Exception(note);}
        void Save()=>File.WriteAllLines(Path.Combine(output,"shoulder_result.txt"),log);
        IEnumerator Start()
        {
            Directory.CreateDirectory(output);var sequence=Run();
            while(true){bool more;object step=null;try{more=sequence.MoveNext();if(more)step=sequence.Current;}catch(Exception e){log.Add("FAIL "+e);Complete=true;Save();yield break;}if(!more)break;yield return step;}
            Passed=true;Complete=true;log.Add("RESULT=PASS / "+(Application.isEditor?"Editor Play":"Windows Player")+" / EventSystem input / TX=0");Save();
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
            yield return new WaitForSecondsRealtime(.35f);
            if(EditorCapture!=null)EditorCapture(name+".png");
            else {yield return new WaitForEndOfFrame();var image=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,name+".png"),image.EncodeToPNG());Destroy(image);}
            Save();
        }
        void HardwareOff()=>Check(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0&&!router.GetComponent<ControlStudioUartOutput>().Connected&&!router.GetComponent<UartPose3DSource>().Connected,"Hardware TX=0; RX/TX disconnected");
        void Axis(HumanoidVisualRig.Arm arm,float pitch,float roll)
        {
            Check(Quaternion.Angle(arm.shoulderPitch.localRotation,arm.rest.shoulderPitch*Quaternion.AngleAxis(pitch,Vector3.right))<.1f,"actual shoulder local X "+arm.shoulderPitch.name+" observed="+arm.shoulderPitch.localRotation+" expectedDelta="+pitch);
            Check(Quaternion.Angle(arm.shoulderRoll.localRotation,arm.rest.shoulderRoll*Quaternion.AngleAxis(roll,Vector3.forward))<.1f,"actual shoulder local Z "+arm.shoulderRoll.name+" observed="+arm.shoulderRoll.localRotation+" expectedDelta="+roll);
            Check(arm.Configured,"arm descendants / ToolMount / gripper hierarchy intact");
        }
        IEnumerator WaitShoulders(HumanoidVisualRig rig,float rp,float rr,float lp,float lr)
        {
            float deadline=Time.realtimeSinceStartup+20;int frames=0;
            while(Time.realtimeSinceStartup<deadline){
                bool Near(HumanoidVisualRig.Arm a,float p,float r)=>Quaternion.Angle(a.shoulderPitch.localRotation,a.rest.shoulderPitch*Quaternion.AngleAxis(p,Vector3.right))<.1f&&Quaternion.Angle(a.shoulderRoll.localRotation,a.rest.shoulderRoll*Quaternion.AngleAxis(r,Vector3.forward))<.1f;
                if(Near(rig.right,rp,rr)&&Near(rig.left,lp,lr))break;
                frames++;yield return null;
            }
            log.Add("INFO shoulder settling frames="+frames+" paused="+router.Paused+" focus="+Application.isFocused);
        }
        IEnumerator Run()
        {
            yield return new WaitForSecondsRealtime(2);router=FindFirstObjectByType<SingleArmCommandRouter>();
            Check(router!=null&&router.Ready,"native ready");ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();mock=router.GetComponent<RobotControlMockPanel>();
            var view=router.GetComponent<ControlStudioPresentationUI>();var visuals=router.GetComponent<RobotVisualProfiles>();var panel=router.GetComponent<HumanoidServoPreviewPanel>();
            if(!Application.isEditor){Screen.SetResolution(1920,1080,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.7f);}
            Click("START");yield return new WaitForSecondsRealtime(.2f);router.SetPaused(true);router.enabled=false;
            Check(!ui.ServoDetailsExpanded,"G51 default Servo collapsed");var original=(float[])router.Applied.Clone();int epoch=router.Epoch;
            Click("Robot BOTH");yield return new WaitForSecondsRealtime(.2f);Check(!mock.StateFor(0).Pwm&&!mock.StateFor(1).Pwm,"BOTH selection sends no command");
            Click("Robot 0 RIGHT");Click("Advanced toggle");yield return new WaitForSecondsRealtime(.2f);Click("PWM enable");
            Click("Robot BOTH");Click("Follow start");Check(mock.StateFor(0).Follow&&!mock.StateFor(1).Follow,"BOTH partial: R accepted / L PWM OFF denied");
            Check(mock.Results[0]==RobotControlMockPanel.MockResult.Accepted&&mock.Results[1]==RobotControlMockPanel.MockResult.Denied&&mock.ResultMessage.Contains("PARTIAL"),"independent per-side result");
            yield return Capture("01_both_partial_mock");Click("Follow stop");Click("PWM disable");Click("Advanced toggle");
            Check(original.SequenceEqual(router.Applied)&&router.Epoch==epoch,"mock target changes preserve control trace / epoch");
            Click("Servo details toggle");yield return new WaitForSecondsRealtime(.3f);Check(ui.Sliders.All(s=>s.gameObject.activeInHierarchy),"G51 M0-M4 preserved even BOTH mock target");
            Drag(ui.Sliders[2],110);yield return new WaitForSecondsRealtime(.2f);
            using(var reference=new ControlStudioOutputPolicy(original)){
                Check(reference.Submit(ui.manual.Values)==0,"independent native target accepted");router.SetPaused(false);var expected=new float[5];var v=new float[4];
                for(int i=0;i<160;i++){reference.Tick(expected,v);router.AdvanceVirtual(.02);Check(router.Applied.SequenceEqual(expected),"G51 Agent2 trace "+i);}
            }
            yield return Capture("02_g51_existing_servo");Click("Servo details toggle");Click("MENU");yield return new WaitForSecondsRealtime(.2f);Click("ROBOT next");yield return new WaitForSecondsRealtime(.2f);Click("START");yield return new WaitForSecondsRealtime(.3f);
            Check(visuals.Selected==RobotVisualProfileId.HumanoidRobot&&panel.Active,"Humanoid preview UI selected");
            ui.manual.LoadAtomic(new[]{90f,90f,90f,90f,1f});for(int i=0;i<160;i++)router.AdvanceVirtual(.02);visuals.Sync();
            var rig=visuals.Humanoid;var transforms=rig.GetComponentsInChildren<Transform>(true);var parents=transforms.ToDictionary(t=>t,t=>t.parent);var locals=transforms.ToDictionary(t=>t,t=>t.localPosition);
            var fixedJoints=new[]{rig.right.shoulderRoot,rig.left.shoulderRoot,rig.right.shoulderPitch,rig.left.shoulderPitch};var pivots=fixedJoints.Select(t=>t.position).ToArray();
            Click("Servo details toggle");yield return new WaitForSecondsRealtime(.2f);Check(!panel.ShoulderOpen&&!panel.ArmOpen,"Humanoid independent sections default compact");yield return Capture("03_humanoid_compact");
            Click("Shoulder section toggle");yield return new WaitForSecondsRealtime(.2f);Click("Robot 0 RIGHT");yield return new WaitForSecondsRealtime(.2f);
            Drag(panel.ShoulderSliders[0],10);Drag(panel.ShoulderSliders[1],-8);yield return WaitShoulders(rig,10,-8,0,0);Axis(rig.right,10,-8);Axis(rig.left,0,0);yield return Capture("04_right_shoulder");
            Click("Robot 1 LEFT");yield return new WaitForSecondsRealtime(.2f);Drag(panel.ShoulderSliders[0],5);Drag(panel.ShoulderSliders[1],8);yield return WaitShoulders(rig,10,-8,5,8);Axis(rig.left,5,8);Axis(rig.right,10,-8);yield return Capture("05_left_shoulder");
            Click("Robot BOTH");yield return new WaitForSecondsRealtime(.2f);Check(Mathf.Abs(visuals.Preview.Shoulder(0,0)-10)<.1&&Mathf.Abs(visuals.Preview.Shoulder(1,0)-5)<.1,"BOTH selection preserves distinct shoulder values");
            Drag(panel.ShoulderSliders[0],7);Drag(panel.ShoulderSliders[1],0);yield return WaitShoulders(rig,7,0,7,0);Axis(rig.right,7,0);Axis(rig.left,7,0);
            Click("Arm joints section toggle");yield return new WaitForSecondsRealtime(.3f);Drag(panel.ArmSliders[0],94);Drag(panel.ArmSliders[1],110);Drag(panel.ArmSliders[2],80);Drag(panel.ArmSliders[3],100);Drag(panel.ArmSliders[4],.3f);
            for(int i=0;i<180;i++)router.AdvanceVirtual(.02);yield return new WaitForSecondsRealtime(1.2f);visuals.Sync();
            Check(Mathf.Abs(router.Applied[1]-110)<.1&&Mathf.Abs(visuals.Preview.LeftValue(1)-110)<.1,"BOTH explicit arm edit: R Agent2 / L preview");
            Check(Quaternion.Angle(rig.right.elbowPitch.localRotation,rig.right.rest.elbowPitch*Quaternion.AngleAxis(20,Vector3.right))<.1,"right approved elbow follows existing adapter");
            Check(Quaternion.Angle(rig.left.elbowPitch.localRotation,rig.left.rest.elbowPitch*Quaternion.AngleAxis(20,Vector3.right))<.1,"left preview elbow");
            Check(transforms.All(t=>t.parent==parents[t]),"no reparent");
            Check(fixedJoints.Select((t,i)=>Vector3.Distance(t.position,pivots[i])).All(d=>d<.00001f),"shoulder pivots stay fixed");
            Check(transforms.Where(t=>!t.IsChildOf(rig.left.gripper)&&!t.IsChildOf(rig.right.gripper)).All(t=>t.localPosition==locals[t]),"all non-gripper local positions unchanged");
            Check(Vector3.Distance(rig.left.toolMount.position,rig.right.toolMount.position)>.15f,"tested working pose tool separation > .15 scene units");
            var rightQ=(float[])ui.manual.Values.Clone();var leftQ=Enumerable.Range(0,5).Select(visuals.Preview.LeftValue).ToArray();
            Click("Robot 1 LEFT");yield return new WaitForSecondsRealtime(.2f);Drag(panel.ArmSliders[1],100);yield return new WaitForSecondsRealtime(.2f);Check(ui.manual.Values.SequenceEqual(rightQ),"LEFT edits do not write right source");
            Click("Robot BOTH");yield return new WaitForSecondsRealtime(.2f);Check(Mathf.Abs(visuals.Preview.LeftValue(1)-100)<.1&&Mathf.Abs(ui.manual.Values[1]-110)<.1,"BOTH selection does not unify arm values");
            Click("Preview toggle");yield return new WaitForSecondsRealtime(.3f);
            var bounds=new Vector3[4];mock.PreviewRect.GetWorldCorners(bounds);var servo=ui.RuntimeCanvas.GetComponentsInChildren<Transform>().Single(t=>t.name=="Manual panel") as RectTransform;var sb=new Vector3[4];servo.GetWorldCorners(sb);
            Check(bounds[2].x<=sb[0].x&&sb[0].y>=0,"preview and expanded Servo do not overlap / no bottom clipping");yield return Capture("06_both_arms_camera_1920");
            if(!Application.isEditor){Screen.SetResolution(1280,720,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.8f);}
            yield return Capture("07_both_arms_camera_1280");
            mock.PreviewRect.GetWorldCorners(bounds);servo.GetWorldCorners(sb);Check(bounds[2].x<=sb[0].x&&sb[0].y>=0,"1280 camera / Servo layout fits");
            Click("Shoulder section toggle");Click("Arm joints section toggle");yield return new WaitForSecondsRealtime(.2f);Click("Shoulder section toggle");Click("Arm joints section toggle");yield return new WaitForSecondsRealtime(.2f);
            Check(ui.manual.Values.SequenceEqual(rightQ)&&Mathf.Abs(visuals.Preview.LeftValue(1)-100)<.1,"collapse retains independent values");
            router.SetPaused(true);var held=rig.left.shoulderPitch.localRotation;panel.Edit(0,2);yield return new WaitForSecondsRealtime(.4f);Check(rig.left.shoulderPitch.localRotation==held,"Pause holds preview pose and stores target");
            Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);Check(!servo.gameObject.activeInHierarchy&&!mock.PanelRoot.activeInHierarchy,"Controls Hide hides both panels");yield return Capture("08_controls_hidden");
            Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);var cam=ui.orbit.GetComponent<Camera>();var cp=cam.transform.position;var cr=cam.transform.rotation;float fov=cam.fieldOfView;
            Click("PRESENTATION");yield return new WaitForSecondsRealtime(.2f);Check(!ui.RuntimeCanvas.gameObject.activeInHierarchy&&cam.transform.position==cp&&cam.transform.rotation==cr&&cam.fieldOfView==fov,"Presentation hides UI without reframing");yield return Capture("09_presentation");view.SetPresentationMode(false);
            HardwareOff();Check(router.Epoch==epoch,"shoulder UI preserves source epoch");
            var audit=new List<string>();foreach(var arm in new[]{rig.right,rig.left})foreach(var t in new[]{arm.shoulderRoot,arm.shoulderYaw,arm.shoulderPitch,arm.shoulderRoll})audit.Add(t.name+" parent="+t.parent.name+" localPosition="+t.localPosition.ToString("F6")+" localRotation="+t.localRotation.ToString("F6"));
            File.WriteAllLines(Path.Combine(output,"actual_shoulder_transforms.txt"),audit);
            router.enabled=true;
        }
    }
}
