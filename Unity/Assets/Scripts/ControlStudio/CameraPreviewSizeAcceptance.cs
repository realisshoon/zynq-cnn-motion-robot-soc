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
    public sealed class CameraPreviewSizeAcceptance : MonoBehaviour
    {
        readonly List<string> log=new List<string>();string output;
        ControlStudioRuntimeUI ui;ControlStudioStartupMenu menu;ControlStudioPresentationUI view;RobotControlMockPanel mock;
        SingleArmCommandRouter router;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void OptionalRun()
        {
            var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--camera-size-verify");
            if(Application.isEditor||i<0||i+1>=args.Length)return;
            var test=new GameObject("Explicit camera size verification").AddComponent<CameraPreviewSizeAcceptance>();test.output=args[i+1];
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
        Rect Bounds(RectTransform rect)
        {
            var corners=new Vector3[4];rect.GetWorldCorners(corners);
            var lo=RectTransformUtility.WorldToScreenPoint(null,corners[0]);var hi=RectTransformUtility.WorldToScreenPoint(null,corners[2]);
            return Rect.MinMaxRect(lo.x,lo.y,hi.x,hi.y);
        }
        void SameRect(Rect a,Rect b,string note)=>Check(Vector2.Distance(a.position,b.position)<.05f&&Vector2.Distance(a.size,b.size)<.05f,note);
        IEnumerator Run()
        {
            yield return new WaitForSecondsRealtime(2);
            router=FindFirstObjectByType<SingleArmCommandRouter>();Check(router!=null&&router.Ready,"native output ready");
            ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();mock=router.GetComponent<RobotControlMockPanel>();
            foreach(int width in new[]{1920,1280}){
                int height=width*9/16;string prefix=width+"x"+height+"_";
                Screen.SetResolution(width,height,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(1);
                Check(Screen.width==width&&Screen.height==height,"actual resolution "+prefix);
                if(menu.Visible){Click("START");yield return new WaitForSecondsRealtime(.5f);}
                router.SetPaused(true);yield return new WaitForSecondsRealtime(.5f);
                var values=(float[])ui.manual.Values.Clone();int epoch=router.Epoch;
                var camera=Camera.main;Check(camera!=null,"robot camera exists");
                var cameraPosition=camera.transform.position;var cameraRotation=camera.transform.rotation;
                var cameraProjection=camera.projectionMatrix;
                if(!mock.PreviewOpen){Click("Preview toggle");yield return new WaitForSecondsRealtime(.2f);}
                Check(view.ControlsVisible&&!ui.ServoDetailsExpanded,"SHOW controls / Servo collapsed");
                Check(!Find("Camera size menu toggle").gameObject.activeInHierarchy,"SHOW hides size selector");
                Rect original=Bounds(mock.PreviewRect);
                float fit=width/1600f;
                Check(Mathf.Abs(original.width-272*fit)<.1f&&Mathf.Abs(original.height-153*fit)<.1f,"unchanged regular preview dimensions");
                Check(Mathf.Abs(original.x-1304*fit)<.1f&&Mathf.Abs(original.yMax-(height-108*fit))<.1f,"unchanged regular preview position");
                log.Add("GEOMETRY "+prefix+"original="+original.ToString("F2"));
                yield return Capture(prefix+"01_show_original");
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);
                Check(!mock.PanelRoot.activeInHierarchy&&!Find("Manual panel").gameObject.activeInHierarchy,"control panels hidden");
                Check(mock.PreviewOpen&&mock.PreviewRect.gameObject.activeInHierarchy,"Camera ON survives Hide");
                Rect previous=new Rect();
                for(int size=1;size<=3;size++){
                    Click("Camera size menu toggle");yield return new WaitForSecondsRealtime(.15f);
                    Click("Camera size "+size);yield return new WaitForSecondsRealtime(.2f);
                    Rect current=Bounds(mock.PreviewRect);
                    float expected=new[]{300,375,450}[size-1]*fit;
                    Check(mock.HiddenPreviewSize==size&&Mathf.Abs(current.width-expected)<.1f,"size "+size+" distinct expected width "+expected);
                    Check(Mathf.Abs(current.width/current.height-16f/9)<.001f,"size "+size+" 16:9");
                    Check(current.xMin>=0&&current.xMax<=width&&current.yMin>=0&&current.yMax<=height-96*fit,"size "+size+" within viewport below header");
                    if(size>1)Check(Mathf.Abs(current.xMin-previous.xMin)<.05f&&Mathf.Abs(current.yMax-previous.yMax)<.05f,"fixed top-left across size changes");
                    previous=current;log.Add("GEOMETRY "+prefix+"size"+size+"="+current.ToString("F2"));
                    yield return Capture(prefix+"0"+(size+1)+"_hide_size"+size);
                }
                Click("Preview toggle");yield return new WaitForSecondsRealtime(.2f);
                Check(!mock.PreviewOpen&&!mock.PreviewRect.gameObject.activeInHierarchy,"Camera OFF available while hidden");
                yield return Capture(prefix+"05_hide_camera_off");
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);
                Check(!mock.PreviewOpen,"SHOW preserves Camera OFF");
                Click("Preview toggle");yield return new WaitForSecondsRealtime(.2f);
                SameRect(original,Bounds(mock.PreviewRect),"SHOW restores original position and size");
                yield return Capture(prefix+"06_show_restored");
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);
                Check(mock.HiddenPreviewSize==3&&mock.PreviewOpen,"previous size and ON restored on Hide");
                SameRect(previous,Bounds(mock.PreviewRect),"restored Size 3 geometry");
                Click("Camera size menu toggle");yield return new WaitForSecondsRealtime(.2f);
                yield return Capture(prefix+"07_size_menu");
                Click("PRESENTATION");yield return new WaitForSecondsRealtime(.2f);
                Check(!ui.RuntimeCanvas.gameObject.activeInHierarchy&&!mock.PreviewRect.gameObject.activeInHierarchy,"Presentation still hides ALL UI including preview");
                view.SetPresentationMode(false);yield return new WaitForSecondsRealtime(.2f);
                Check(!view.ControlsVisible&&mock.PreviewOpen,"Presentation return preserves hidden controls and Camera ON");
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);
                Check(!Find("Camera size choices").gameObject.activeInHierarchy,"size popover closed on SHOW");
                Check(Vector3.Distance(camera.transform.position,cameraPosition)<.0001f&&Quaternion.Angle(camera.transform.rotation,cameraRotation)<.01f,"same Robot Camera transform for comparisons");
                Check(camera.projectionMatrix==cameraProjection,"same Robot Camera projection restored");
                Check(ui.manual.Values.SequenceEqual(values)&&router.Epoch==epoch,"requested commands and epoch unchanged");
                Click("Servo details toggle");yield return new WaitForSecondsRealtime(.2f);
                Check(!Overlap((RectTransform)Find("Manual panel"),mock.PreviewRect),"original SHOW layout avoids Servo overlap");
                Click("Servo details toggle");yield return new WaitForSecondsRealtime(.2f);
                SameRect(original,Bounds(mock.PreviewRect),"regular placement after Servo close unchanged");
                Check(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0&&!router.GetComponent<ControlStudioUartOutput>().Connected,"Hardware TX=0 / disconnected");
            }
            Screen.SetResolution(1920,1080,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(1);
        }
    }
}
