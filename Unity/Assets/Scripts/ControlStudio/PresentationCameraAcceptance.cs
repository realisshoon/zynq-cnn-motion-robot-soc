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
    public sealed class PresentationCameraAcceptance : MonoBehaviour
    {
        readonly List<string> log=new List<string>();string output;
        ControlStudioRuntimeUI ui;ControlStudioStartupMenu menu;ControlStudioPresentationUI view;RobotControlMockPanel mock;
        SingleArmCommandRouter router;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void OptionalRun()
        {
            var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--presentation-camera-verify");
            if(Application.isEditor||i<0||i+1>=args.Length)return;
            var test=new GameObject("Explicit Presentation Camera verification").AddComponent<PresentationCameraAcceptance>();test.output=args[i+1];
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
        sealed class CameraState
        {
            public Vector3 position,target;public Quaternion rotation;public float fov,distance;public Rect rect;public Matrix4x4 projection;
            public CameraState(ControlStudioOrbitCamera orbit){var c=orbit.GetComponent<Camera>();position=c.transform.position;rotation=c.transform.rotation;fov=c.fieldOfView;rect=c.rect;projection=c.projectionMatrix;target=orbit.Target;distance=orbit.Distance;}
        }
        void Verify(CameraState before,string stage)
        {
            var after=new CameraState(ui.orbit);
            Check(Vector3.Distance(before.position,after.position)<.00001f,stage+" position unchanged");
            Check(Quaternion.Angle(before.rotation,after.rotation)<.001f,stage+" rotation unchanged");
            Check(Mathf.Abs(before.fov-after.fov)<.0001f,stage+" FOV unchanged");
            Check(before.rect==after.rect,stage+" viewport rect unchanged");
            Check(before.projection==after.projection,stage+" projection unchanged");
            Check(before.target==after.target&&before.distance==after.distance,stage+" orbit target and zoom unchanged");
        }
        void ArbitraryOrbit(int family)
        {
            // Test-only fixture for a non-default user orbit, pan, zoom and FOV.
            var flags=System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic;
            var type=typeof(ControlStudioOrbitCamera);var orbit=ui.orbit;
            type.GetField("yaw",flags).SetValue(orbit,family==0?-48f:-18f);
            type.GetField("pitch",flags).SetValue(orbit,family==0?21f:13f);
            type.GetProperty("Distance").SetValue(orbit,orbit.Distance*1.08f);
            orbit.GetComponent<Camera>().fieldOfView=46f;
            type.GetMethod("PositionCamera",flags).Invoke(orbit,null);
        }
        IEnumerator Run()
        {
            yield return new WaitForSecondsRealtime(2);
            router=FindFirstObjectByType<SingleArmCommandRouter>();Check(router!=null&&router.Ready,"native output ready");
            ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();mock=router.GetComponent<RobotControlMockPanel>();
            foreach(int width in new[]{1920,1280}){
                Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(1);
                for(int family=0;family<2;family++){
                    string prefix=width+"x"+(width*9/16)+(family==0?"_G51":"_Humanoid");
                    if(!menu.Visible){Click("MENU");yield return new WaitForSecondsRealtime(.2f);Click("ROBOT next");yield return new WaitForSecondsRealtime(.3f);}
                    Click("START");yield return new WaitForSecondsRealtime(.5f);router.SetPaused(true);
                    Check((router.GetComponent<RobotVisualProfiles>().Selected==RobotVisualProfileId.G51)==(family==0),"selected "+prefix);
                    ArbitraryOrbit(family);yield return new WaitForSecondsRealtime(.5f);
                    if(!ui.ServoDetailsExpanded){Click("Servo details toggle");yield return new WaitForSecondsRealtime(.2f);}
                    if(!mock.PreviewOpen){Click("Preview toggle");yield return new WaitForSecondsRealtime(.2f);}
                    var state=new CameraState(ui.orbit);var commands=(float[])ui.manual.Values.Clone();int epoch=router.Epoch;
                    var joints=router.robot.GetComponentsInChildren<Transform>(true);var positions=joints.Select(t=>t.localPosition).ToArray();var rotations=joints.Select(t=>t.localRotation).ToArray();
                    log.Add("CAMERA "+prefix+" position="+state.position.ToString("F6")+" rotation="+state.rotation.ToString("F6")+" FOV="+state.fov+" rect="+state.rect.ToString("F6"));
                    yield return Capture(prefix+"_01_ui_on");
                    Click("PRESENTATION");yield return new WaitForSecondsRealtime(.4f);
                    Check(!ui.RuntimeCanvas.gameObject.activeInHierarchy&&!mock.PreviewRect.gameObject.activeInHierarchy,"all UI and preview hidden");
                    Verify(state,prefix+" Presentation ON");
                    Check(view.Background.Selected==ControlStudioBackgroundView.BackgroundKind.SimulationGrid,"Grid retained in Presentation");
                    yield return Capture(prefix+"_02_presentation");
                    view.SetPresentationMode(false);yield return new WaitForSecondsRealtime(.4f);
                    Verify(state,prefix+" Presentation OFF");
                    Check(ui.ServoDetailsExpanded&&mock.PreviewOpen&&mock.PanelRoot.activeInHierarchy,"prior UI and preview state restored");
                    yield return Capture(prefix+"_03_ui_restored");
                    Check(ui.manual.Values.SequenceEqual(commands)&&router.Epoch==epoch,"commands and epoch preserved");
                    Check(joints.Select((t,i)=>Vector3.Distance(t.localPosition,positions[i])<.00001f&&Quaternion.Angle(t.localRotation,rotations[i])<.001f).All(v=>v),"logical rig pose unchanged");
                    Click("Background menu toggle");yield return new WaitForSecondsRealtime(.2f);
                    Check(!view.Background.FactoryAvailable&&!Find("FACTORY",true).GetComponent<Button>().interactable,"unavailable Factory disabled / no false success");
                    Check(Find("Factory availability reason").GetComponent<Text>().text.Contains("HDRP"),"incompatible Factory guidance visible");
                    Check(!view.Background.Select(ControlStudioBackgroundView.BackgroundKind.Factory),"missing Factory request rejected");
                    yield return new WaitForSecondsRealtime(.2f);Verify(state,"Factory rejected");
                    Check(view.Background.Selected==ControlStudioBackgroundView.BackgroundKind.SimulationGrid,"Grid retained after rejected Factory request");
                    yield return Capture(prefix+"_04_factory_unavailable");Click("SIMULATION GRID");yield return new WaitForSecondsRealtime(.2f);Verify(state,"Grid reselect");
                    Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);var hidden=new CameraState(ui.orbit);
                    Click("PRESENTATION");yield return new WaitForSecondsRealtime(.2f);Verify(hidden,"hidden-controls Presentation ON");
                    view.SetPresentationMode(false);yield return new WaitForSecondsRealtime(.2f);Verify(hidden,"hidden-controls Presentation OFF");
                    Check(!view.ControlsVisible&&mock.PreviewOpen,"hidden control state restored");
                    Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);
                    Click("Servo details toggle");yield return new WaitForSecondsRealtime(.2f);
                    Check(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"Hardware TX=0");
                }
            }
            log.Add("NOT VERIFIED: actual Factory rendering/switching; cached package is HDRP-only, no URP prefab imported.");
            log.Add("INPUT: test-only arbitrary orbit fixture; EventSystem button clicks; OS mouse drag not tested.");
            Screen.SetResolution(1920,1080,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(1);
        }
    }
}
