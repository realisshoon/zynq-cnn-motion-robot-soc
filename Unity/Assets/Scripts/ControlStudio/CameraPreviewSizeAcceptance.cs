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
        SingleArmCommandRouter router;ControlStudioVideoCapture capture;bool liveObs;
        // Historical layout checks; StudioDockAcceptance now owns the explicit runtime flags.
        static void OptionalRun()
        {
            var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--camera-size-verify");
            if(Application.isEditor||i<0||i+1>=args.Length)return;
            var test=new GameObject("Explicit camera size verification").AddComponent<CameraPreviewSizeAcceptance>();test.output=args[i+1];
            test.liveObs=Array.IndexOf(args,"--camera-size-live-obs")>=0;
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
            capture=router.GetComponent<ControlStudioVideoCapture>();
            if(liveObs){
                capture.RefreshDevices();Check(capture.Session.Devices.Contains("OBS Virtual Camera"),"explicit OBS Virtual Camera exists");
                Check(capture.StartCapture("OBS Virtual Camera",1280,720,30),"explicit OBS preview start / no UART operation");
                double deadline=Time.realtimeSinceStartupAsDouble+12;
                while(!capture.HasFrame&&Time.realtimeSinceStartupAsDouble<deadline)yield return null;
                Check(capture.HasFrame,"actual OBS webcam frames received");
                log.Add("OBS actual format="+capture.Session.Width+"x"+capture.Session.Height+" rotation="+capture.Session.Rotation);
            }
            foreach(int width in new[]{1920,1280}){
                int height=width*9/16;string prefix=width+"x"+height+"_";
                Screen.SetResolution(width,height,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(1);
                Check(Screen.width==width&&Screen.height==height,"actual resolution "+prefix);
                if(menu.Visible){
                    Check(menu.MenuCanvas.gameObject.activeInHierarchy,"Startup Menu visible before START");
                    yield return Capture(prefix+"00_startup");
                    Click("START");yield return new WaitForSecondsRealtime(.5f);
                }
                router.SetPaused(true);yield return new WaitForSecondsRealtime(.5f);
                var values=(float[])ui.manual.Values.Clone();int epoch=router.Epoch;
                var camera=Camera.main;Check(camera!=null,"robot camera exists");
                var cameraPosition=camera.transform.position;var cameraRotation=camera.transform.rotation;
                var cameraProjection=camera.projectionMatrix;
                if(!mock.PreviewOpen){Click("Preview toggle");yield return new WaitForSecondsRealtime(.2f);}
                Check(view.ControlsVisible&&!ui.ServoDetailsExpanded,"SHOW controls / Servo collapsed");
                Check(router.robot!=null&&router.robot.gameObject.activeInHierarchy,"G51 controller/model active");
                Check(!Find("Camera size menu toggle").gameObject.activeInHierarchy,"SHOW hides size selector");
                Rect original=Bounds(mock.PreviewRect);
                float fit=width/1600f;
                float heading=liveObs?24:0;
                float aspect=liveObs?DisplayAspect():16f/9;
                Check(Mathf.Abs(original.width-272*fit)<.1f&&Mathf.Abs(original.height-(272/aspect+heading)*fit)<.1f,"regular preview width / frame aspect / separate title");
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
                    float maximum=Mathf.Min(620,Mathf.Min(width-32,(height-108*fit-16-heading*fit)*aspect));
                    float expected=Mathf.Min(size==1?300*fit:size==2?375*fit:maximum,maximum);
                    Check(mock.HiddenPreviewSize==size&&Mathf.Abs(current.width-expected)<.1f,"size "+size+" distinct expected width "+expected);
                    Check(Mathf.Abs(current.width/(current.height-heading*fit)-aspect)<.001f,"size "+size+" actual source aspect");
                    if(liveObs)CheckLiveFrame(current,fit,"size "+size);
                    Check(current.xMin>=0&&current.xMax<=width&&current.yMin>=0&&current.yMax<=height-96*fit,"size "+size+" within viewport below header");
                    if(size>1)Check(Mathf.Abs(current.xMin-previous.xMin)<.05f&&Mathf.Abs(current.yMax-previous.yMax)<.05f,"fixed top-left across size changes");
                    previous=current;log.Add("GEOMETRY "+prefix+"size"+size+"="+current.ToString("F2"));
                    yield return Capture(prefix+"0"+(size+1)+"_hide_size"+size);
                }
                Rect size3=previous;
                Click("Camera size menu toggle");yield return new WaitForSecondsRealtime(.15f);
                Click("Camera size 4");yield return new WaitForSecondsRealtime(.2f);
                Rect size4=Bounds(mock.PreviewRect);
                float jumbo=Mathf.Min(width==1920?800:480,Mathf.Min(width-32,(height-96*fit-16-heading*fit)*aspect));
                Check(mock.HiddenPreviewSize==4&&Mathf.Abs(size4.width-jumbo)<.1f,"Size 4 responsive 800 / 480px width with aspect height clamp");
                Check(Mathf.Abs(size4.xMax-size3.xMax)<.1f&&Mathf.Abs(size4.yMax-(height-96*fit))<.1f,"Size 4 right aligned immediately below toolbar");
                Check(size4.xMin>=0&&size4.xMax<=width&&size4.yMin>=0&&size4.yMax<=height-96*fit+.1f,"Size 4 within screen below Header Toolbar");
                if(liveObs)CheckLiveFrame(size4,fit,"Size 4");
                CheckRobotClearance(camera,size4);
                log.Add("GEOMETRY "+prefix+"size4="+size4.ToString("F2"));
                yield return Capture(prefix+"04b_hide_size4");
                for(int size=1;size<=3;size++){
                    Click("Camera size menu toggle");yield return new WaitForSecondsRealtime(.1f);Click("Camera size "+size);yield return new WaitForSecondsRealtime(.2f);
                    var restored=Bounds(mock.PreviewRect);
                    Check(Mathf.Abs(restored.width-(size==1?300*fit:size==2?375*fit:size3.width))<.1f,"Size 4 return to Size "+size+" preserves existing width");
                    Check(Mathf.Abs(restored.xMin-size3.xMin)<.1f&&Mathf.Abs(restored.yMax-size3.yMax)<.1f,"Size 4 return to Size "+size+" preserves existing origin");
                }
                SameRect(size3,Bounds(mock.PreviewRect),"Size 4 to Size 3 restores exact geometry");
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
                // Size 4 selection survives SHOW/HIDE and Presentation without
                // changing the normal preview or any robot/servo panel positions.
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);
                Click("Camera size menu toggle");yield return null;Click("Camera size 4");yield return new WaitForSecondsRealtime(.2f);
                SameRect(size4,Bounds(mock.PreviewRect),"Size 4 selected again");
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);
                SameRect(original,Bounds(mock.PreviewRect),"SHOW retains original small preview with Size 4 selected");
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);
                Check(mock.HiddenPreviewSize==4,"Size 4 selection survives SHOW/HIDE");
                SameRect(size4,Bounds(mock.PreviewRect),"HIDE restores Size 4 geometry");
                Click("PRESENTATION");yield return new WaitForSecondsRealtime(.2f);
                Check(!ui.RuntimeCanvas.gameObject.activeInHierarchy,"Presentation hides Size 4 with existing UI");
                view.SetPresentationMode(false);yield return new WaitForSecondsRealtime(.2f);
                SameRect(size4,Bounds(mock.PreviewRect),"Presentation return restores Size 4");
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);
                Check(Vector3.Distance(camera.transform.position,cameraPosition)<.0001f&&Quaternion.Angle(camera.transform.rotation,cameraRotation)<.01f,"same Robot Camera transform for comparisons");
                Check(camera.projectionMatrix==cameraProjection,"same Robot Camera projection restored");
                Check(ui.manual.Values.SequenceEqual(values)&&router.Epoch==epoch,"requested commands and epoch unchanged");
                Click("Servo details toggle");yield return new WaitForSecondsRealtime(.2f);
                Check(!Overlap((RectTransform)Find("Manual panel"),mock.PreviewRect),"original SHOW layout avoids Servo overlap");
                Click("Servo details toggle");yield return new WaitForSecondsRealtime(.2f);
                SameRect(original,Bounds(mock.PreviewRect),"regular placement after Servo close unchanged");
                Check(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0&&!router.GetComponent<ControlStudioUartOutput>().Connected,"Hardware TX=0 / disconnected");
                var board=router.GetComponent<BoardUartSource>();
                Check(board==null||(!board.Right.Connected&&!board.Left.Connected),"RIGHT/LEFT UART remain disconnected");
            }
            if(liveObs)capture.StopCapture();
            // Exercise non-16:9 frames without opening any additional camera device.
            var sessionField=typeof(ControlStudioVideoCapture).GetField("session",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic);
            capture.Session.Dispose();
            view.SetControlsVisible(false);
            foreach(var format in new[]{new[]{640,480,0},new[]{1280,360,0},new[]{640,480,90}}){
                var fixture=new FrameBackend(format[0],format[1],format[2]);
                var session=new VideoCaptureSession(fixture);session.RefreshDevices();
                session.Start("Stereo aspect fixture",format[0],format[1],30,Time.realtimeSinceStartupAsDouble);
                sessionField.SetValue(capture,session);yield return new WaitForSecondsRealtime(.2f);
                if(mock.HiddenPreviewSize!=4){Click("Camera size menu toggle");yield return null;Click("Camera size 4");yield return new WaitForSecondsRealtime(.2f);}
                Rect panel=Bounds(mock.PreviewRect);
                CheckLiveFrame(panel,Screen.width/1600f,"fixture "+string.Join("x",format),true);
                Check(panel.xMin>=0&&panel.xMax<=Screen.width&&panel.yMin>=0&&panel.yMax<=Screen.height,"fixture aspect fits screen including portrait");
                yield return Capture("1280x720_08_aspect_"+string.Join("x",format));
                session.Dispose();
            }
            Check(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"aspect fixtures UART TX=0");
            Screen.SetResolution(1920,1080,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(1);
        }
        float DisplayAspect()
        {
            var frame=capture.Session;float angle=frame.Rotation*Mathf.Deg2Rad;
            float c=Mathf.Abs(Mathf.Cos(angle)),s=Mathf.Abs(Mathf.Sin(angle));
            return (frame.Width*c+frame.Height*s)/(frame.Width*s+frame.Height*c);
        }
        void CheckRobotClearance(Camera camera,Rect preview)
        {
            var adapter=router.GetComponent<RobotVisualProfiles>().Adapter;
            Check(adapter!=null&&adapter.IsConfigured,"G51 existing visual adapter configured for clearance check");
            // Logical pivots have no mesh; inspect the adapter's real visual hierarchy,
            // including the gripper beneath M3, without changing any transform.
            var roots=new[]{adapter.m0.visual,adapter.m1.visual,adapter.m2.visual,adapter.m3.visual};
            var renderers=roots.Where(t=>t!=null).SelectMany(t=>t.GetComponentsInChildren<Renderer>()).Distinct().Where(r=>r.enabled&&r.gameObject.activeInHierarchy).ToArray();
            Check(renderers.Length>0,"G51 joint and gripper renderers available for clearance check");
            foreach(var renderer in renderers){
                var bounds=renderer.bounds;var points=new List<Vector3>();
                for(int i=0;i<8;i++)points.Add(camera.WorldToScreenPoint(bounds.center+Vector3.Scale(bounds.extents,new Vector3((i&1)==0?-1:1,(i&2)==0?-1:1,(i&4)==0?-1:1))));
                if(points.Any(p=>p.z<=0))continue;
                Rect projected=Rect.MinMaxRect(points.Min(p=>p.x),points.Min(p=>p.y),points.Max(p=>p.x),points.Max(p=>p.y));
                Check(!preview.Overlaps(projected),"Size 4 clear of G51 joint/gripper renderer "+renderer.name);
                log.Add("ROBOT SCREEN "+renderer.name+"="+projected.ToString("F2"));
            }
        }
        void CheckLiveFrame(Rect panel,float scale,string note,bool fixture=false)
        {
            Check(capture.HasFrame,note+" frame remains live");
            var viewport=(RectTransform)Find("Live capture viewport");var image=Find("Live capture preview").GetComponent<RawImage>();
            Rect frame=Bounds(viewport);var label=Bounds((RectTransform)Find("Capture preview source"));
            Check((fixture?image.texture is Texture2D:image.texture is WebCamTexture)&&image.gameObject.activeInHierarchy,note+" frame texture shown by RawImage");
            Check(Mathf.Abs(frame.width-panel.width)<.1f&&Mathf.Abs(frame.height-panel.width/DisplayAspect())<.1f,note+" actual video enlarged with parent / no letterbox");
            var corners=new Vector3[4];image.rectTransform.GetWorldCorners(corners);
            var points=corners.Select(p=>RectTransformUtility.WorldToScreenPoint(null,p)).ToArray();
            Rect displayed=Rect.MinMaxRect(points.Min(p=>p.x),points.Min(p=>p.y),points.Max(p=>p.x),points.Max(p=>p.y));
            SameRect(frame,displayed,note+" rotated RawImage fills exact video viewport");
            Check(label.yMin>=frame.yMax&&Mathf.Abs(label.yMin-frame.yMax-4*scale)<.1f,note+" title above image / 4 design px gap");
            Check(Mathf.Abs(image.uvRect.width)==1&&Mathf.Abs(image.uvRect.height)==1,note+" full UV includes both stereo halves / no crop");
            Check(image.GetComponentInParent<Mask>()==null&&image.GetComponentInParent<RectMask2D>()==null,note+" no crop mask");
        }
        sealed class FrameBackend : IVideoCaptureBackend
        {
            readonly int width,height,rotation;
            public FrameBackend(int w,int h,int angle){width=w;height=h;rotation=angle;}
            public string[] Enumerate()=>new[]{"Stereo aspect fixture"};
            public IVideoCaptureStream Create(string device,int w,int h,int fps)=>new FrameStream(width,height,rotation);
        }
        sealed class FrameStream : IVideoCaptureStream
        {
            readonly Texture2D texture;
            public object Texture=>texture;public bool IsPlaying{get;private set;}public bool DidUpdate=>IsPlaying;
            public int Width=>texture.width;public int Height=>texture.height;public int Rotation{get;}public bool VerticallyMirrored=>false;
            public FrameStream(int w,int h,int angle){
                Rotation=angle;texture=new Texture2D(w,h,TextureFormat.RGB24,false);var pixels=new Color[w*h];
                for(int y=0;y<h;y++)for(int x=0;x<w;x++)pixels[y*w+x]=x<w/2?new Color(.15f,.65f,.85f):new Color(.85f,.45f,.15f);
                texture.SetPixels(pixels);texture.Apply();
            }
            public void Play()=>IsPlaying=true;
            public void Dispose(){IsPlaying=false;UnityEngine.Object.Destroy(texture);}
        }
    }
}
