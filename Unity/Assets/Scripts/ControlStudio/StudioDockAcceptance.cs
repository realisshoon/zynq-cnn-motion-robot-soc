using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.LowLevel;

namespace HumanMotion.ControlStudio
{
    // Explicit offline UI verification. No serial opening or board commands.
    public sealed class StudioDockAcceptance : MonoBehaviour
    {
        readonly List<string> log=new List<string>();string output;bool liveObs,previousBackground,portsOnly,controlOnly;
        Keyboard verificationKeyboard;InputSettings.BackgroundBehavior previousInputBackground;
        SingleArmCommandRouter router;ControlStudioRuntimeUI ui;ControlStudioStartupMenu menu;ControlStudioPresentationUI view;
        ControlStudioDockLayout dock;RobotControlMockPanel preview;ControlStudioVideoCapture capture;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void OptionalRun()
        {
            var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"--studio-dock-verify");
            if(i<0)i=Array.IndexOf(args,"--camera-size-verify");
            if(i<0)i=Array.IndexOf(args,"--ports-ui-verify");
            if(i<0)i=Array.IndexOf(args,"--control-ui-verify");
            if(i<0||i+1>=args.Length)return;
            var test=new GameObject("Explicit Studio Dock verification").AddComponent<StudioDockAcceptance>();
            test.output=args[i+1];test.liveObs=Array.IndexOf(args,"--studio-live-obs")>=0||Array.IndexOf(args,"--camera-size-live-obs")>=0;
            test.portsOnly=Array.IndexOf(args,"--ports-ui-verify")>=0;
            test.controlOnly=Array.IndexOf(args,"--control-ui-verify")>=0;
        }
        void Check(bool ok,string note){log.Add((ok?"PASS: ":"FAIL: ")+note);if(!ok)throw new InvalidOperationException(note);}
        void Save()=>File.WriteAllLines(Path.Combine(output,"studio_ui_result.txt"),log);
        IEnumerator Start()
        {
            previousBackground=Application.runInBackground;Application.runInBackground=true;
            previousInputBackground=InputSystem.settings.backgroundBehavior;
            InputSystem.settings.backgroundBehavior=InputSettings.BackgroundBehavior.IgnoreFocus;
            verificationKeyboard=InputSystem.AddDevice<Keyboard>("Studio verification keyboard");verificationKeyboard.MakeCurrent();
            Directory.CreateDirectory(output);log.Add((Application.isEditor?"Unity Editor":"Windows Player")+" / EventSystem pointer verification / no physical UART");
            log.Add("Explicit fixture runInBackground="+Application.runInBackground);
            log.Add("Unity="+Application.unityVersion+" exe="+Environment.GetCommandLineArgs()[0]);
            var run=controlOnly?RunControl():portsOnly?RunPorts():Run();while(true){bool next;object step=null;try{next=run.MoveNext();if(next)step=run.Current;}catch(Exception e){log.Add("FAIL: "+e);Save();ScreenCapture.CaptureScreenshot(Path.Combine(output,"failure.png"));yield break;}if(!next)break;yield return step;}
            log.Add("RESULT=PASS");Save();Destroy(gameObject);
        }
        void OnDestroy(){if(verificationKeyboard!=null)InputSystem.RemoveDevice(verificationKeyboard);InputSystem.settings.backgroundBehavior=previousInputBackground;Application.runInBackground=previousBackground;}
        Transform Find(string name)=>ui.RuntimeCanvas.GetComponentsInChildren<Transform>(true).Concat(menu.MenuCanvas.GetComponentsInChildren<Transform>(true)).Concat(dock.OverlayRoot.GetComponentsInChildren<Transform>(true)).Concat(dock.DetailRoot.GetComponentsInChildren<Transform>(true)).First(t=>t.name==name);
        void Click(string name)
        {
            Canvas.ForceUpdateCanvases();var t=Find(name);var button=t.GetComponent<Button>();Check(button!=null&&button.IsInteractable()&&t.gameObject.activeInHierarchy,"clickable "+name);
            var r=(RectTransform)t;var point=RectTransformUtility.WorldToScreenPoint(null,r.TransformPoint(r.rect.center));
            var data=new PointerEventData(EventSystem.current){position=point,button=PointerEventData.InputButton.Left};var hits=new List<RaycastResult>();EventSystem.current.RaycastAll(data,hits);
            Check(hits.Count>0&&(hits[0].gameObject==t.gameObject||hits[0].gameObject.transform.IsChildOf(t)),"actual pointer hit "+name);
            ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerDownHandler);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerUpHandler);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerClickHandler);
        }
        Rect Bounds(RectTransform rect){var p=new Vector3[4];rect.GetWorldCorners(p);var lo=RectTransformUtility.WorldToScreenPoint(null,p[0]);var hi=RectTransformUtility.WorldToScreenPoint(null,p[2]);return Rect.MinMaxRect(lo.x,lo.y,hi.x,hi.y);}
        IEnumerator Capture(string name){yield return new WaitForSecondsRealtime(.25f);yield return new WaitForEndOfFrame();var image=ScreenCapture.CaptureScreenshotAsTexture();File.WriteAllBytes(Path.Combine(output,name+".png"),image.EncodeToPNG());Destroy(image);log.Add("CAPTURE "+name+" "+Screen.width+"x"+Screen.height);Save();}
        void VideoGeometry(string note)
        {
            var viewport=(RectTransform)Find("Live capture viewport");var image=Find("Live capture preview").GetComponent<RawImage>();Rect rect=Bounds(viewport);
            var s=capture.Session;float a=s.Rotation*Mathf.Deg2Rad,c=Mathf.Abs(Mathf.Cos(a)),sn=Mathf.Abs(Mathf.Sin(a));float aspect=(s.Width*c+s.Height*sn)/(s.Width*sn+s.Height*c);
            Check(preview.PreviewVisible&&image.texture!=null&&image.gameObject.activeInHierarchy,note+" real texture displayed");
            Check(Mathf.Abs(rect.width/rect.height-aspect)<.001f,note+" source aspect preserved");
            var p=new Vector3[4];image.rectTransform.GetWorldCorners(p);var points=p.Select(v=>RectTransformUtility.WorldToScreenPoint(null,v)).ToArray();
            Check(Mathf.Abs(points.Max(v=>v.x)-points.Min(v=>v.x)-rect.width)<.1f&&Mathf.Abs(points.Max(v=>v.y)-points.Min(v=>v.y)-rect.height)<.1f,note+" RawImage fills exact video area / no added bars");
            Check(Mathf.Abs(image.uvRect.width)==1&&Mathf.Abs(image.uvRect.height)==1&&image.GetComponentInParent<RectMask2D>()==null,note+" full stereo UV / no crop");
            Check(rect.xMin>=-.1f&&rect.yMin>=-.1f&&rect.xMax<=Screen.width+.1f&&rect.yMax<=Screen.height+.1f,note+" inside screen");
        }
        void ClearRobot(Rect overlay,string note)
        {
            var adapter=router.GetComponent<RobotVisualProfiles>().Adapter;int count=0;
            foreach(var renderer in new[]{adapter.m0.visual,adapter.m1.visual,adapter.m2.visual,adapter.m3.visual}.SelectMany(t=>t.GetComponentsInChildren<Renderer>()).Distinct().Where(r=>r.enabled&&r.gameObject.activeInHierarchy)){
                var b=renderer.bounds;var p=Enumerable.Range(0,8).Select(i=>Camera.main.WorldToScreenPoint(b.center+Vector3.Scale(b.extents,new Vector3((i&1)==0?-1:1,(i&2)==0?-1:1,(i&4)==0?-1:1)))).ToArray();
                if(p.Any(v=>v.z<=0))continue;var rect=Rect.MinMaxRect(p.Min(v=>v.x),p.Min(v=>v.y),p.Max(v=>v.x),p.Max(v=>v.y));
                Check(!rect.Overlaps(overlay),note+" clear of "+renderer.name);count++;
            }
            Check(count>0,note+" G51 visual renderers checked");
        }
        void NoTx(string note){var boards=router.GetComponent<BoardUartSource>();Check(!boards.Right.Connected&&!boards.Left.Connected&&router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,note+" physical UART disconnected / TX=0");}
        void BasicButtons()
        {
            var viewport=Bounds(dock.InspectorScroll.viewport);
            foreach(var name in new[]{"Control target 0","Control target 1","Control target 2","Board control E","Board control A","Board control S","Board control X","Board control O","Board control H"}){
                var t=(RectTransform)Find(name);var b=Bounds(t);
                Check(t.gameObject.activeInHierarchy&&b.xMin>=viewport.xMin-.1f&&b.xMax<=viewport.xMax+.1f&&b.yMin>=viewport.yMin-.1f&&b.yMax<=viewport.yMax+.1f,"basic button visible without clipping "+name);
            }
        }
        IEnumerable<Button> DetailButtons()=>Find("UART / Hardware panel").GetComponentsInChildren<Button>().Where(b=>b.gameObject.activeInHierarchy);
        void DetailGeometry(string note)
        {
            var viewport=Bounds(dock.DetailScroll.viewport);Check(!dock.DetailScroll.horizontal,note+" vertical scrolling only / no horizontal clipping workaround");
            foreach(var graphic in Find("UART / Hardware panel").GetComponentsInChildren<Graphic>().Where(g=>g.gameObject.activeInHierarchy)){
                var rect=Bounds(graphic.rectTransform);Check(rect.xMin>=viewport.xMin-.1f&&rect.xMax<=viewport.xMax+.1f,note+" horizontal bounds "+graphic.name);
            }
        }
        void LastButtonReachable()
        {
            var button=DetailButtons().OrderBy(b=>Bounds((RectTransform)b.transform).yMin).First();
            var scroll=dock.DetailScroll;float max=Mathf.Max(0,scroll.content.rect.height-scroll.viewport.rect.height);
            float y=-scroll.content.InverseTransformPoint(((RectTransform)button.transform).TransformPoint(((RectTransform)button.transform).rect.center)).y;
            scroll.verticalNormalizedPosition=max>0?1-Mathf.Clamp(y-scroll.viewport.rect.height/2,0,max)/max:1;Canvas.ForceUpdateCanvases();
            var rect=Bounds((RectTransform)button.transform);var viewport=Bounds(scroll.viewport);
            Check(rect.yMin>=viewport.yMin-.1f&&rect.yMax<=viewport.yMax+.1f,"last detail button accessible after vertical scroll: "+button.name);
        }
        void ScrollTo(Transform t)
        {
            var scroll=dock.DetailScroll;float max=Mathf.Max(0,scroll.content.rect.height-scroll.viewport.rect.height);
            float y=-scroll.content.InverseTransformPoint(((RectTransform)t).TransformPoint(((RectTransform)t).rect.center)).y;
            scroll.verticalNormalizedPosition=max>0?1-Mathf.Clamp(y-scroll.viewport.rect.height/2,0,max)/max:1;Canvas.ForceUpdateCanvases();
        }
        void Pointer(Transform t)
        {
            Canvas.ForceUpdateCanvases();var r=(RectTransform)t;var point=RectTransformUtility.WorldToScreenPoint(null,r.TransformPoint(r.rect.center));
            var data=new PointerEventData(EventSystem.current){position=point,button=PointerEventData.InputButton.Left};var hits=new List<RaycastResult>();EventSystem.current.RaycastAll(data,hits);
            Check(hits.Count>0&&(hits[0].gameObject==t.gameObject||hits[0].gameObject.transform.IsChildOf(t)),"actual selectable pointer hit "+t.name);
            ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerDownHandler);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerUpHandler);ExecuteEvents.ExecuteHierarchy(t.gameObject,data,ExecuteEvents.pointerClickHandler);
        }
        IEnumerator SelectPortOption(string name,int index,string screenshot=null)
        {
            var t=Find(name);ScrollTo(t);yield return new WaitForSecondsRealtime(.1f);Pointer(t);yield return new WaitForSecondsRealtime(.15f);
            var dropdown=t.GetComponent<Dropdown>();var items=t.GetComponentsInChildren<Toggle>().Where(v=>v.gameObject.activeInHierarchy).ToArray();
            Check(items.Length==dropdown.options.Count,"real dropdown popup options "+name);Check(index<items.Length,"dropdown target option exists");
            if(screenshot!=null)yield return Capture(screenshot);
            Pointer(items[index].transform);yield return new WaitForSecondsRealtime(.3f);Check(dropdown.value==index,"dropdown selection "+name+" = "+dropdown.options[index].text);
        }
        void ControlButtonsVisible(string note)
        {
            var viewport=Bounds(dock.DetailScroll.viewport);
            foreach(var name in new[]{"Control target 0","Control target 1","Control target 2","Board control E","Board control X","Board control A","Board control S","Board control O","Board control H"}){
                var t=(RectTransform)Find(name);var b=Bounds(t);Check(t.gameObject.activeInHierarchy&&b.xMin>=viewport.xMin-.1f&&b.xMax<=viewport.xMax+.1f&&b.yMin>=viewport.yMin-.1f&&b.yMax<=viewport.yMax+.1f,note+" fully visible "+name);
            }
        }
        Button VisualButton(Transform parent,string name,float x,bool enabled,bool selected)
        {
            var r=(RectTransform)new GameObject(name,typeof(RectTransform),typeof(Image),typeof(Button)).transform;r.SetParent(parent,false);
            r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);r.anchoredPosition=new Vector2(x,-46);r.sizeDelta=new Vector2(180,38);
            var b=r.GetComponent<Button>();b.transition=Selectable.Transition.None;b.targetGraphic=r.GetComponent<Image>();b.interactable=enabled;
            var label=new GameObject("Visual fixture label",typeof(RectTransform),typeof(Text)).GetComponent<Text>();label.transform.SetParent(r,false);label.font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");label.fontSize=14;label.text=name;label.alignment=TextAnchor.MiddleCenter;label.raycastTarget=false;
            label.rectTransform.anchorMin=Vector2.zero;label.rectTransform.anchorMax=Vector2.one;label.rectTransform.offsetMin=label.rectTransform.offsetMax=Vector2.zero;
            ControlStudioBoardControlPanel.StableButtonVisual(b,selected);return b;
        }
        IEnumerator StableColors(string prefix)
        {
            // Standalone visual fixtures have NO listeners/gateways/serial owner; do not arm live UI buttons.
            var canvas=new GameObject("Explicit Control color fixture",typeof(RectTransform),typeof(Canvas),typeof(GraphicRaycaster));canvas.GetComponent<Canvas>().renderMode=RenderMode.ScreenSpaceOverlay;canvas.GetComponent<Canvas>().sortingOrder=32000;
            var r=(RectTransform)new GameObject("Visual-only fixture backdrop",typeof(RectTransform),typeof(Image)).transform;r.SetParent(canvas.transform,false);r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,0);r.anchoredPosition=new Vector2(20,20);r.sizeDelta=new Vector2(590,108);r.GetComponent<Image>().color=new Color(.1f,.12f,.14f);
            var label=new GameObject("Fixture provenance",typeof(RectTransform),typeof(Text)).GetComponent<Text>();label.transform.SetParent(r,false);label.rectTransform.anchorMin=label.rectTransform.anchorMax=label.rectTransform.pivot=new Vector2(0,1);label.rectTransform.anchoredPosition=new Vector2(12,-10);label.rectTransform.sizeDelta=new Vector2(564,26);label.font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");label.fontSize=14;label.text="VISUAL STATE FIXTURE / NO BOARD / NO TX";label.color=Color.white;label.raycastTarget=false;
            var samples=new[]{VisualButton(r,"Enabled / fixed",12,true,false),VisualButton(r,"Disabled / fixed",204,false,false),VisualButton(r,"Selected / fixed",396,true,true)};
            Canvas.ForceUpdateCanvases();yield return null;var actual=Enumerable.Range(0,3).Select(i=>Find("Control target "+i).GetComponent<Button>()).Concat("EASXOH".Select(k=>Find("Board control "+k).GetComponent<Button>())).Concat(new[]{Find("Advanced board control").GetComponent<Button>()}).ToArray();
            var all=actual.Concat(samples).ToArray();var colors=all.Select(b=>b.GetComponent<Image>().color).ToArray();var tints=all.Select(b=>b.targetGraphic.canvasRenderer.GetColor()).ToArray();
            Check(all.All(b=>b.transition==Selectable.Transition.None),"all Control/fixture buttons have no color tween or animation transition");
            Check(samples[0].interactable&&!samples[1].interactable&&samples[2].interactable&&samples[0].GetComponent<Image>().color!=samples[1].GetComponent<Image>().color&&samples[0].GetComponent<Image>().color!=samples[2].GetComponent<Image>().color,"Enabled/Disabled/Selected distinct fixed visual fixture colors; no hardware readiness claim");
            yield return Capture(prefix+"06_fixed_colors_t0_visual_fixture");
            var data=new PointerEventData(EventSystem.current){button=PointerEventData.InputButton.Left};ExecuteEvents.Execute(samples[0].gameObject,data,ExecuteEvents.pointerEnterHandler);EventSystem.current.SetSelectedGameObject(samples[0].gameObject);ExecuteEvents.Execute(samples[0].gameObject,data,ExecuteEvents.pointerDownHandler);
            bool same=true;double deadline=Time.realtimeSinceStartupAsDouble+4;while(Time.realtimeSinceStartupAsDouble<deadline){for(int i=0;i<all.Length;i++){bool unchanged=all[i].GetComponent<Image>().color==colors[i]&&all[i].targetGraphic.canvasRenderer.GetColor()==tints[i];if(same&&!unchanged)log.Add("COLOR CHANGE: "+all[i].name+" image="+all[i].GetComponent<Image>().color+" renderer="+all[i].targetGraphic.canvasRenderer.GetColor()+" initial image="+colors[i]+" renderer="+tints[i]);same&=unchanged;}yield return null;}
            ExecuteEvents.Execute(samples[0].gameObject,data,ExecuteEvents.pointerUpHandler);ExecuteEvents.Execute(samples[0].gameObject,data,ExecuteEvents.pointerExitHandler);EventSystem.current.SetSelectedGameObject(null);
            Check(same,"4-second every-frame Enabled/Disabled/Selected color stability including hover/select/press; visual fixture only for Enabled motion color");yield return Capture(prefix+"07_fixed_colors_t4_visual_fixture");
            samples[0].interactable=false;ControlStudioBoardControlPanel.StableButtonVisual(samples[0]);Check(samples[0].GetComponent<Image>().color==samples[1].GetComponent<Image>().color,"actual Interactable change updates to fixed Disabled color");
            NoTx("color stability fixture");Destroy(canvas);yield return null;
        }
        IEnumerator RunControl()
        {
            yield return new WaitForSecondsRealtime(2);router=FindFirstObjectByType<SingleArmCommandRouter>();Check(router!=null&&router.Ready,"existing controller ready / no COM");
            ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();dock=router.GetComponent<ControlStudioDockLayout>();
            Check(menu.Visible,"unchanged Startup displayed");yield return Capture("00_startup_unchanged");Click("START");yield return new WaitForSecondsRealtime(.3f);Click("MENU");yield return new WaitForSecondsRealtime(.1f);while(menu.Choice(1)!=2)menu.Cycle(1,1);Click("START");yield return new WaitForSecondsRealtime(.3f);
            Check(router.Source=="BOARD","original START UART input reaches Control Studio");router.SetPaused(true);var panel=router.GetComponent<ControlStudioBoardControlPanel>();var boards=router.GetComponent<BoardUartSource>();
            foreach(int width in new[]{1920,1280}){
                Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.7f);string prefix=width+"x"+width*9/16+"_control_";
                Check(Screen.width==width,"actual Player resolution "+width);dock.Select(ControlStudioDockLayout.Workspace.Control);yield return new WaitForSecondsRealtime(.3f);dock.DetailScroll.verticalNormalizedPosition=1;Canvas.ForceUpdateCanvases();
                ControlButtonsVisible("basic Control");DetailGeometry("Control initial");Check(!panel.AdvancedOpen&&!Find("Control semantics").gameObject.activeInHierarchy,"advanced response hidden by default; Gripper stays basic");
                Check(Find("Control physical unknown").GetComponent<Text>().text.Contains("UNKNOWN"),"servo connection/power/position never invented");
                foreach(char key in "EASXOH")Check(!Find("Board control "+key).GetComponent<Button>().interactable,"unconnected board cannot offer motion "+key);
                Check(!Find("Right Control summary").GetComponent<Text>().text.ToUpperInvariant().Contains("READY")&&Find("Right Control summary").GetComponent<Text>().text.Contains("PWM UNKNOWN"),"unknown hardware state is not READY / PWM ON");
                foreach(int target in new[]{1,2,0}){Click("Control target "+target);yield return new WaitForSecondsRealtime(.2f);Check(panel.SelectedTarget==target,"original target selection "+target);NoTx("target selection");}
                string[] expected={"PWM 활성화 (E)","추종 시작 (A)","추종 명령 중지 (S)","PWM 비활성화 (X)","그리퍼 열기 (O)","그리퍼 자동 제어 (H)"};for(int i=0;i<6;i++)Check(Find("Board control "+"EASXOH"[i]).GetComponentInChildren<Text>().text==expected[i],"command caption maps original key "+"EASXOH"[i]);
                Check(((RectTransform)Find("Board control X")).anchoredPosition.y==((RectTransform)Find("Board control E")).anchoredPosition.y&&((RectTransform)Find("Board control A")).anchoredPosition.y>((RectTransform)Find("Board control O")).anchoredPosition.y,"PWM / Follow / Gripper grouped in required order");
                yield return Capture(prefix+"01_basic_unknown_all_buttons");ScrollTo(Find("Control physical unknown"));yield return Capture(prefix+"02_unknown_status");
                ScrollTo(Find("Advanced board control"));Click("Advanced board control");yield return new WaitForSecondsRealtime(.3f);Check(panel.AdvancedOpen,"advanced Control explicitly opened");DetailGeometry("Control advanced");LastButtonReachable();yield return Capture(prefix+"03_advanced");
                ScrollTo(Find("Advanced board control"));Click("Advanced board control");yield return new WaitForSecondsRealtime(.2f);dock.DetailScroll.verticalNormalizedPosition=1;yield return new WaitForSecondsRealtime(.15f);ControlButtonsVisible("collapsed advanced");
                var fixedColors=StableColors(prefix);while(fixedColors.MoveNext())yield return fixedColors.Current;
                // Reuse existing RX-only fixture loader, never create a live COM transport.
                dock.Select(ControlStudioDockLayout.Workspace.Ports);yield return new WaitForSecondsRealtime(.2f);var ports=router.GetComponent<ControlStudioUartPanel>().PortsView;
                Check(!ports.AdvancedExpanded,"Ports default fold state preserved");DetailGeometry("Ports regression");yield return Capture(prefix+"04_ports_preserved");ScrollTo(Find("Advanced communication diagnostics"));Click("Advanced communication diagnostics");yield return new WaitForSecondsRealtime(.2f);ScrollTo(Find("Right Load fixture"));Click("Right Load fixture");yield return new WaitForSecondsRealtime(.2f);
                Check(boards.IsFixture(BoardSide.Right)&&!boards.Left.Connected,"RX-only RIGHT Fixture / LEFT independent OFF");
                boards.Right.AcceptLine("[ST] async_test=1 pwm=1 result=status mode=LIVE;",BoardUartTransport.Now);
                dock.Select(ControlStudioDockLayout.Workspace.Control);yield return new WaitForSecondsRealtime(.3f);Click("Control target 2");yield return new WaitForSecondsRealtime(.2f);
                Check(Find("Right Control summary").GetComponent<Text>().text.Contains("FIXTURE / RX ONLY")&&Find("Right Control summary").GetComponent<Text>().text.Contains("PWM ON (보고값)")&&Find("Left Control summary").GetComponent<Text>().text.Contains("PWM UNKNOWN"),"BOTH shows independent software PWM report vs unknown peer, not servo readiness");
                foreach(char key in "EASXOH")Check(!Find("Board control "+key).GetComponent<Button>().interactable,"RX-only Fixture and unavailable peer refuse motion "+key);
                ScrollTo(Find("Right Control summary"));yield return Capture(prefix+"05_both_fixture_status_not_hardware");
                boards.Right.AcceptLine("[REC_STATE] schema=99",BoardUartTransport.Now);yield return new WaitForSecondsRealtime(.3f);Check(Find("Right Control summary").GetComponent<Text>().text.Contains("통신/파싱 오류")&&Find("Right Control summary").GetComponent<Text>().text.Contains("PWM UNKNOWN"),"invalid software state displays parser fault / unknown output, never READY");
                // UI fixture disconnect only: no serial transport exists.
                dock.Select(ControlStudioDockLayout.Workspace.Ports);yield return new WaitForSecondsRealtime(.2f);ScrollTo(Find("Right Disconnect"));Click("Right Disconnect");yield return new WaitForSecondsRealtime(.2f);ScrollTo(Find("Advanced communication diagnostics"));Click("Advanced communication diagnostics");yield return new WaitForSecondsRealtime(.2f);
                dock.Select(ControlStudioDockLayout.Workspace.Control);yield return new WaitForSecondsRealtime(.2f);ScrollTo(Find("Control target 0"));Click("Control target 0");yield return new WaitForSecondsRealtime(.2f);NoTx("Control + Ports cycle");
            }
            log.Add("NOT VERIFIED: real COM Open/Close, physical servo readiness, power/position feedback, hardware safety, real command acceptance/motion. No COM or servo test performed.");
            log.Add("Enabled motion color was tested using listener-free visual fixtures; actual unconnected and RX-only fixture motion buttons stayed disabled.");
            NoTx("final Control verification");
        }
        IEnumerator RunPorts()
        {
            yield return new WaitForSecondsRealtime(2);router=FindFirstObjectByType<SingleArmCommandRouter>();Check(router!=null&&router.Ready,"existing controller ready / no COM");
            ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();dock=router.GetComponent<ControlStudioDockLayout>();
            Check(menu.Visible,"unchanged Startup Menu displayed");yield return Capture("00_startup_unchanged");Click("START");yield return new WaitForSecondsRealtime(.3f);
            Click("MENU");yield return new WaitForSecondsRealtime(.1f);while(menu.Choice(1)!=2)menu.Cycle(1,1);Click("START");yield return new WaitForSecondsRealtime(.3f);
            Check(router.Source=="BOARD","Startup UART selection arrives unchanged");router.SetPaused(true);
            var boards=router.GetComponent<BoardUartSource>();var ports=router.GetComponent<ControlStudioUartPanel>().PortsView;
            string[] remembered={PlayerPrefs.GetString("ControlStudio.Ports.Right",""),PlayerPrefs.GetString("ControlStudio.Ports.Left","")};
            string[] states={
                ControlStudioPortsView.ConnectionState(false,false,"DISCONNECTED",false,double.NaN),
                ControlStudioPortsView.ConnectionState(true,false,"CONNECTING",false,double.NaN),
                ControlStudioPortsView.ConnectionState(true,false,"COM / awaiting REC_STATE",true,double.NaN),
                ControlStudioPortsView.ConnectionState(true,false,"COM / awaiting REC_STATE",true,.01),
                ControlStudioPortsView.ConnectionState(true,false,"COM / awaiting REC_STATE",true,2),
                ControlStudioPortsView.ConnectionState(false,false,"OperationCanceledException",false,double.NaN),
                ControlStudioPortsView.ConnectionState(true,true,"FIXTURE",true,.01)};
            Check(states.SequenceEqual(new[]{"미연결","연결 중","연결됨 / 수신 대기","정상 수신 중","수신 지연 / STALE","연결 오류","FIXTURE · 오프라인 테스트"}),"seven UI states from synthetic evidence / not physical connection verification");
            foreach(int width in new[]{1920,1280}){
                Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.7f);string prefix=width+"x"+width*9/16+"_ports_";
                Check(Screen.width==width,"actual Player resolution "+width);dock.Select(ControlStudioDockLayout.Workspace.Ports);dock.DetailScroll.verticalNormalizedPosition=1;yield return new WaitForSecondsRealtime(.3f);
                Check(!ports.AdvancedExpanded&&!Find("Right fixture").gameObject.activeInHierarchy,"advanced diagnostics / Fixture hidden by default");
                Check(Find("Right COM").GetComponent<Dropdown>()!=null&&Find("Left COM").GetComponent<Dropdown>()!=null,"independent COM dropdowns");
                Check(Find("Right baud").GetComponent<Dropdown>().value==0&&Find("Left baud").GetComponent<Dropdown>().value==0,"921600 default unchanged");
                Click("Refresh COM ports");yield return new WaitForSecondsRealtime(.25f);log.Add("ACTUAL WINDOWS ENUMERATION: "+string.Join(",",ports.AvailablePorts));NoTx("real Windows port scan");
                Check(Find("Right connection status").GetComponent<Text>().text=="미연결"&&Find("Left connection status").GetComponent<Text>().text=="미연결","actual disconnected states / not success");
                DetailGeometry("Ports basic");yield return Capture(prefix+"01_basic_actual_com");ScrollTo(Find("Left Disconnect"));yield return Capture(prefix+"02_basic_left");
                // Enumeration fixture only. No valid isolated Connect is ever invoked.
                ports.SetPorts(new[]{"COM910","COM900","COM901"});yield return new WaitForSecondsRealtime(.3f);Check(ports.AvailablePorts.SequenceEqual(new[]{"COM900","COM901","COM910"}),"port enumeration fixture natural sorting");
                yield return SelectPortOption("Right COM",0,prefix+"03_dropdown_enumeration_fixture");ScrollTo(Find("Right Connect"));Click("Right Connect");yield return new WaitForSecondsRealtime(.3f);
                Check(Find("Right connection guidance").GetComponent<Text>().text=="연결할 COM 포트를 선택하세요.","unselected Connect UI validation / no serial opening");NoTx("unselected Connect");
                yield return SelectPortOption("Right COM",1);yield return SelectPortOption("Left COM",1);Check(ports.SelectedPort(0)==ports.SelectedPort(1),"duplicate selection fixture precondition");
                ScrollTo(Find("Right Connect"));Click("Right Connect");yield return new WaitForSecondsRealtime(.3f);Check(Find("Right connection guidance").GetComponent<Text>().text.Contains("동일 COM"),"duplicate Connect blocked before existing backend / no serial opening");NoTx("duplicate Connect");yield return Capture(prefix+"04_duplicate_guard_fixture");
                yield return SelectPortOption("Left COM",2);Check(ports.SelectedPort(0)!=ports.SelectedPort(1),"independent board selections");
                yield return SelectPortOption("Right baud",1);Check(Find("Left baud").GetComponent<Dropdown>().value==0,"RIGHT baud selection leaves LEFT unchanged");yield return SelectPortOption("Right baud",0);
                ScrollTo(Find("Advanced communication diagnostics"));Click("Advanced communication diagnostics");yield return new WaitForSecondsRealtime(.3f);Check(ports.AdvancedExpanded,"advanced diagnostics explicitly opened");
                DetailGeometry("Ports advanced");ScrollTo(Find("Right boundary"));yield return Capture(prefix+"05_advanced_queries");
                foreach(char key in "VTt"){
                    ScrollTo(Find("Right query "+key));Click("Right query "+key);yield return new WaitForSecondsRealtime(.35f);Check(boards.TxBytes(BoardSide.Right)==0&&!boards.Right.Connected,"explicit query "+key+" refused without real connection; TX=0");
                }
                Check(!boards.CommandBoundaryConfirmed(BoardSide.Right),"boundary confirmation never automatic");
                foreach(var side in new[]{BoardSide.Right,BoardSide.Left}){
                    ScrollTo(Find(side+" Load fixture"));Click(side+" Load fixture");yield return new WaitForSecondsRealtime(.3f);
                    Check(boards.IsFixture(side)&&boards.Session(side).Connected,"existing offline Fixture loaded "+side);
                    Check(Find(side+" connection status").GetComponent<Text>().text.Contains("FIXTURE"),"fixture state separate from live COM "+side);
                }
                ScrollTo(Find("Left UART state"));yield return Capture(prefix+"06_fixture_not_hardware");DetailGeometry("Ports long RX text");LastButtonReachable();yield return Capture(prefix+"07_advanced_bottom");
                ScrollTo(Find("Right Disconnect"));Click("Right Disconnect");yield return new WaitForSecondsRealtime(.3f);Check(!boards.Right.Connected&&boards.Left.Connected&&boards.IsFixture(BoardSide.Left),"RIGHT fixture Disconnect keeps LEFT independent");
                Check(Find("Right connection summary").GetComponent<Text>().text.StartsWith("FIXTURE RX",StringComparison.Ordinal),"held historical RX retains fixture provenance after Disconnect");
                ScrollTo(Find("Left Disconnect"));Click("Left Disconnect");yield return new WaitForSecondsRealtime(.3f);NoTx("both offline Fixture Disconnect buttons");
                ScrollTo(Find("Advanced communication diagnostics"));Click("Advanced communication diagnostics");yield return new WaitForSecondsRealtime(.3f);Check(!ports.AdvancedExpanded&&!Find("Right fixture").gameObject.activeInHierarchy,"advanced diagnostics collapsed again");
                ScrollTo(Find("Refresh COM ports"));Click("Refresh COM ports");yield return new WaitForSecondsRealtime(.2f);Check(ports.SelectedPort(0)=="COM900"&&ports.SelectedPort(1)=="COM901","refresh preserves selections even when not enumerated");NoTx("refresh / selections / fixture UI");
                Click("Close detailed controls");yield return new WaitForSecondsRealtime(.2f);Check(!dock.DetailOpen,"Ports close returns to existing Dock");
            }
            for(int i=0;i<2;i++)PlayerPrefs.SetString("ControlStudio.Ports."+(i==0?"Right":"Left"),remembered[i]);
            log.Add("NOT VERIFIED: actual COM Open/Close, hardware RX, physical connection errors / no permission to connect during UI verification.");
            log.Add("FIXTURE ONLY: independent connect/disconnect sessions, duplicate UI guard, synthetic connection state classification.");
            log.Add("Commands retain V/T/t through original BoardUartSource.Query; no E/A/S/X sent.");NoTx("final Ports verification");
        }
        IEnumerator Run()
        {
            yield return new WaitForSecondsRealtime(2);router=FindFirstObjectByType<SingleArmCommandRouter>();Check(router!=null&&router.Ready,"existing native/controller ready");
            ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();dock=router.GetComponent<ControlStudioDockLayout>();preview=router.GetComponent<RobotControlMockPanel>();capture=router.GetComponent<ControlStudioVideoCapture>();
            Check(menu.Visible&&!dock.Dock.gameObject.activeInHierarchy,"original initial setup visible; redesign hidden before START");
            view.SetPresentationMode(true);Check(!view.PresentationMode,"Presentation unavailable during initial setup");
            yield return Capture("00_initial_setup_unchanged");Click("START");yield return new WaitForSecondsRealtime(.5f);
            Check(!menu.Visible&&dock.Dock.gameObject.activeInHierarchy&&router.Source=="MANUAL","START enters redesigned Studio with original Manual / G51 selection");
            File.WriteAllLines(Path.Combine(output,"runtime_hierarchy.txt"),ui.RuntimeCanvas.GetComponentsInChildren<Transform>(true).Select(t=>t.name+" / "+t.gameObject.activeSelf));
            if(liveObs){capture.RefreshDevices();Check(capture.Session.Devices.Contains("OBS Virtual Camera"),"OBS device enumerated");Check(capture.StartCapture("OBS Virtual Camera",1920,540,30),"explicit OBS test owner started");double deadline=Time.realtimeSinceStartupAsDouble+12;while(!capture.HasFrame&&Time.realtimeSinceStartupAsDouble<deadline)yield return null;Check(capture.HasFrame,"actual OBS frames received");log.Add("OBS ACTUAL "+capture.Session.Width+"x"+capture.Session.Height);}
            else InstallFake(true);
            if(!preview.PreviewOpen)Click("Preview toggle");router.SetPaused(true);
            Click("MENU");yield return new WaitForSecondsRealtime(.1f);while(menu.Choice(1)!=2)menu.Cycle(1,1);Click("START");yield return new WaitForSecondsRealtime(.3f);
            foreach(int width in new[]{1920,1280}){
                int height=width*9/16;string prefix=width+"x"+height+"_";Screen.SetResolution(width,height,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(1);
                Check(Screen.width==width&&Screen.height==height,"actual resolution "+prefix);view.SetControlsVisible(true);dock.CloseDetails();yield return new WaitForSecondsRealtime(.25f);
                var camera=Camera.main;var pos=camera.transform.position;var rot=camera.transform.rotation;float fov=camera.fieldOfView;int epoch=router.Epoch;var command=(float[])router.Requested.Clone();var session=capture.Session;
                Check(Mathf.Abs(Bounds((RectTransform)Find("Header")).height-48*width/1600f)<.1f,"thin Toolbar / 57.6px at 1920");
                Rect dockBounds=Bounds(dock.Dock);Check(dockBounds.width>=320&&dockBounds.width<=440&&dockBounds.xMin>=width*.70f-.1f&&dockBounds.xMax<=width+.1f&&dockBounds.yMin>=-.1f,"28 percent dock inside screen");
                VideoGeometry("dock video");ClearRobot(dockBounds,"open Dock");yield return Capture(prefix+"01_panel_open");
                Check(!Find("Hardware tab 0").gameObject.activeInHierarchy,"no duplicate UART tabs in basic Dock");
                foreach(var pair in new[]{new[]{(int)ControlStudioDockLayout.Workspace.Ports,0},new[]{(int)ControlStudioDockLayout.Workspace.Control,1},new[]{(int)ControlStudioDockLayout.Workspace.Settings,3},new[]{(int)ControlStudioDockLayout.Workspace.Records,2},new[]{(int)ControlStudioDockLayout.Workspace.Capture,4}}){
                    dock.Select((ControlStudioDockLayout.Workspace)pair[0]);yield return new WaitForSecondsRealtime(.2f);
                    Check(dock.DetailOpen&&router.GetComponent<ControlStudioUartPanel>().SelectedPage==pair[1]&&Find("UART / Hardware panel").gameObject.activeInHierarchy,"existing detail workspace "+(ControlStudioDockLayout.Workspace)pair[0]);
                    DetailGeometry("detail "+(ControlStudioDockLayout.Workspace)pair[0]);NoTx("detail selection");
                    yield return Capture(prefix+"02_details_"+(ControlStudioDockLayout.Workspace)pair[0]+"_top");
                    dock.DetailScroll.verticalNormalizedPosition=0;yield return new WaitForSecondsRealtime(.2f);LastButtonReachable();
                    if(pair[1]==0)yield return Capture(prefix+"02_details_Ports_bottom");
                }
                foreach(var workspace in new[]{ControlStudioDockLayout.Workspace.Source,ControlStudioDockLayout.Workspace.Tool,ControlStudioDockLayout.Workspace.Status}){
                    Click("Details tab "+workspace);yield return new WaitForSecondsRealtime(.15f);Check(dock.Selected==workspace,"existing extra workspace accessible "+workspace);NoTx("extra workspace");yield return Capture(prefix+"02_details_"+workspace);
                }
                Click("Close detailed controls");yield return new WaitForSecondsRealtime(.2f);Check(!dock.DetailOpen,"details close restores compact Dock");BasicButtons();
                Click("HIDE CONTROLS");yield return new WaitForSecondsRealtime(.2f);Check(!dock.Dock.gameObject.activeInHierarchy&&Find("Expand right dock").gameObject.activeInHierarchy,"closed Dock has edge expand control");
                for(int size=1;size<=4;size++){
                    Click("Camera size menu toggle");yield return new WaitForSecondsRealtime(.1f);Click("Camera size "+size);yield return new WaitForSecondsRealtime(.2f);
                    float expected=size==1?300*width/1600f:size==2?375*width/1600f:size==3?620:width==1920?800:480;
                    var video=Bounds((RectTransform)Find("Live capture viewport"));Check(Mathf.Abs(video.width-expected)<.1f,"Size "+size+" original width "+expected);VideoGeometry("Size "+size);
                    if(size==3||size==4)yield return Capture(prefix+"03_closed_size"+size);
                }
                yield return Capture(prefix+"04_panel_closed");Click("Expand right dock");yield return new WaitForSecondsRealtime(.2f);Check(view.ControlsVisible&&preview.HiddenPreviewSize==4&&ReferenceEquals(capture.Session,session),"open restores size and original capture owner");
                Click("PRESENTATION");yield return new WaitForSecondsRealtime(.2f);Check(!ui.RuntimeCanvas.gameObject.activeInHierarchy&&!dock.Dock.gameObject.activeInHierarchy&&preview.PreviewVisible,"Presentation hides controls and retains live camera overlay");VideoGeometry("Presentation");ClearRobot(Bounds(preview.PreviewRect),"Presentation overlay");yield return Capture(prefix+"05_presentation_live");
                view.SetPresentationMode(false);yield return new WaitForSecondsRealtime(.2f);Check(view.ControlsVisible&&preview.HiddenPreviewSize==4&&preview.PreviewOpen,"Presentation return restores open / size / camera preference");
                Click("HIDE CONTROLS");view.SetPresentationMode(true);yield return new WaitForSecondsRealtime(.2f);view.SetPresentationMode(false);yield return new WaitForSecondsRealtime(.2f);Check(!view.ControlsVisible,"Presentation retains closed Dock state");
                Check(Vector3.Distance(pos,camera.transform.position)<.0001f&&Quaternion.Angle(rot,camera.transform.rotation)<.01f&&Mathf.Abs(fov-camera.fieldOfView)<.001f,"camera position / rotation / FOV unchanged");
                Check(epoch==router.Epoch&&command.SequenceEqual(router.Requested)&&ReferenceEquals(session,capture.Session),"layout / Presentation preserve commands, epoch and stream owner");
                view.SetControlsVisible(true);NoTx("UI cycle");
                view.SetPresentationMode(true);yield return null;
                verificationKeyboard.MakeCurrent();InputSystem.QueueStateEvent(verificationKeyboard,new KeyboardState(Key.Escape));yield return new WaitForSecondsRealtime(.1f);
                InputSystem.QueueStateEvent(verificationKeyboard,new KeyboardState());yield return new WaitForSecondsRealtime(.1f);
                Check(!view.PresentationMode&&!menu.Visible&&view.ControlsVisible,"ESC returns to Studio without opening initial setup");
                Click("Background menu toggle");yield return null;Check(Find("FACTORY").GetComponent<Button>().interactable==view.Background.FactoryAvailable,"Factory availability remains honest");
                if(view.Background.FactoryAvailable){Click("FACTORY");Check(view.Background.Selected==ControlStudioBackgroundView.BackgroundKind.Factory,"existing Factory selection");Click("Background menu toggle");yield return null;}
                else log.Add("NOT TESTED Factory visual: existing compatible Factory prefab unavailable");
                Click("SIMULATION GRID");Check(view.Background.Selected==ControlStudioBackgroundView.BackgroundKind.SimulationGrid,"existing Studio / Grid selection retained");
            }
            // Camera disconnect/recovery and black frames use a named software backend only.
            var backend=InstallFake(true);yield return new WaitForSecondsRealtime(.3f);Check(capture.HasFrame,"fixture receives a valid frame");
            foreach(int width in new[]{1920,1280}){
                Screen.SetResolution(width,width*9/16,FullScreenMode.Windowed);yield return new WaitForSecondsRealtime(.5f);string prefix=width+"x"+width*9/16+"_fixture_";
                view.SetPresentationMode(true);backend.Current.UpdateFrames=false;yield return new WaitForSecondsRealtime(.3f);Check(preview.PreviewVisible,"short frame jitter does not flicker preview");
                yield return new WaitForSecondsRealtime(2);Check(!capture.HasFrame&&!preview.PreviewVisible&&backend.Creates==1&&backend.Current.Disposals==0,"stale hides camera after grace / no device restart");yield return Capture(prefix+"06_presentation_no_frames");
                backend.Current.UpdateFrames=true;yield return new WaitForSecondsRealtime(.3f);Check(capture.HasFrame&&preview.PreviewVisible,"same-stream recovery automatically displays camera");yield return Capture(prefix+"07_camera_recovered");
                backend.Current.Black();yield return new WaitForSecondsRealtime(.3f);Check(capture.HasFrame&&preview.PreviewVisible,"black frame / no person remains a valid stream");yield return Capture(prefix+"08_black_frame_receiving");
                backend.Devices=Array.Empty<string>();yield return new WaitForSecondsRealtime(2.2f);Check(!capture.Session.DeviceAvailable&&!preview.PreviewVisible&&backend.Current.Disposals==0,"device unavailable hides preview without destroying stream");
                backend.Devices=new[]{"Stereo UI fixture"};yield return new WaitForSecondsRealtime(2.2f);Check(capture.Session.DeviceAvailable&&preview.PreviewVisible&&backend.Creates==1,"device / frame recovery retains owner");
                view.SetPresentationMode(false);yield return new WaitForSecondsRealtime(.15f);float cameraHeight=dock.CameraSectionHeight;Click("Preview toggle");yield return new WaitForSecondsRealtime(.15f);Check(dock.CameraSectionHeight<cameraHeight,"Camera OFF collapses unused height");yield return Capture(prefix+"10_camera_off_compact_dock");view.SetPresentationMode(true);yield return new WaitForSecondsRealtime(.2f);Check(!preview.PreviewVisible&&capture.HasFrame,"Camera OFF hides receiving stream without stopping it");yield return Capture(prefix+"09_presentation_camera_off");
                view.SetPresentationMode(false);yield return new WaitForSecondsRealtime(.15f);Click("Preview toggle");yield return new WaitForSecondsRealtime(.15f);
            }
            // Real virtual joint application through the existing Manual -> Native -> Controller -> Visual path.
            view.SetControlsVisible(true);dock.CloseDetails();Click("MENU");yield return new WaitForSecondsRealtime(.1f);while(menu.Choice(1)!=0)menu.Cycle(1,1);Click("START");yield return new WaitForSecondsRealtime(.2f);router.SetPaused(false);var adapter=router.GetComponent<RobotVisualProfiles>().Adapter;var rest=adapter.m0.visual.localRotation;
            ui.manual.Set(0,105);ui.manual.Set(1,85);ui.manual.Set(2,110);ui.manual.Set(3,105);ui.manual.Set(4,.7f);yield return new WaitForSecondsRealtime(3);
            Check(router.HasApplied&&Quaternion.Angle(rest,adapter.m0.visual.localRotation)>.1f,"G51 existing virtual joint Transform changes through original controller");NoTx("virtual joint fixture");ui.manual.Home();router.SetPaused(true);
            // The initial setup controller is reused unchanged for all six Robot/Input combinations.
            foreach(int robot in new[]{0,1})foreach(int input in new[]{0,1,2}){
                Click("MENU");yield return new WaitForSecondsRealtime(.1f);Check(menu.Visible&&!dock.Dock.gameObject.activeInHierarchy,"original setup reopens without redesigned Dock");
                while(menu.Choice(0)!=robot)menu.Cycle(0,1);while(menu.Choice(1)!=input)menu.Cycle(1,1);
                yield return new WaitForSecondsRealtime(.15f);Click("START");yield return new WaitForSecondsRealtime(.4f);
                Check(!menu.Visible,"START accepted Robot "+robot+" Input "+input);
                Check(router.Source==(input==0?"MANUAL":input==1?"CSV":"BOARD"),"selected Input arrives at Control Studio "+input);
                var visuals=router.GetComponent<RobotVisualProfiles>();Check(visuals.Selected==(robot==0?RobotVisualProfileId.G51:RobotVisualProfileId.HumanoidRobot),"selected existing Robot arrives at Control Studio "+robot);
                Check(robot==0?visuals.Adapter.gameObject.activeInHierarchy:visuals.Humanoid.gameObject.activeInHierarchy,"existing selected model active");yield return Capture("setup_robot"+robot+"_input"+input);NoTx("setup combination");
                if(input==0){dock.Select(ControlStudioDockLayout.Workspace.Robot);yield return new WaitForSecondsRealtime(.2f);Check(Find("Manual panel").gameObject.activeInHierarchy,"existing Servo details accessible");yield return Capture("setup_robot"+robot+"_servo_details");dock.CloseDetails();yield return new WaitForSecondsRealtime(.1f);}
                if(input==1){dock.Select(ControlStudioDockLayout.Workspace.Source);yield return new WaitForSecondsRealtime(.2f);Check(Find("CSV recorded human controls").gameObject.activeInHierarchy,"existing CSV source controls accessible");yield return Capture("setup_robot"+robot+"_csv_details");dock.CloseDetails();yield return new WaitForSecondsRealtime(.1f);}
            }
        }
        FakeBackend InstallFake(bool frames)
        {
            capture.StopCapture();capture.Session.Dispose();var backend=new FakeBackend();var session=new VideoCaptureSession(backend);session.RefreshDevices();session.Start("Stereo UI fixture",1920,540,30,Time.realtimeSinceStartupAsDouble);backend.Current.UpdateFrames=frames;
            typeof(ControlStudioVideoCapture).GetField("session",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic).SetValue(capture,session);return backend;
        }
        sealed class FakeBackend:IVideoCaptureBackend
        {public string[] Devices={"Stereo UI fixture"};public FakeStream Current;public int Creates;public string[] Enumerate()=>Devices;public IVideoCaptureStream Create(string device,int width,int height,int fps){Creates++;return Current=new FakeStream();}}
        sealed class FakeStream:IVideoCaptureStream
        {
            readonly Texture2D texture;public bool UpdateFrames;public int Disposals;public object Texture=>texture;public bool IsPlaying{get;private set;}public bool DidUpdate=>IsPlaying&&UpdateFrames;
            public int Width=>1920;public int Height=>540;public int Rotation=>0;public bool VerticallyMirrored=>false;
            public FakeStream(){texture=new Texture2D(1920,540,TextureFormat.RGB24,false);var p=new Color[1920*540];for(int i=0;i<p.Length;i++)p[i]=i%1920<960?new Color(.28f,.43f,.55f):new Color(.39f,.51f,.62f);texture.SetPixels(p);texture.Apply();}
            public void Black(){texture.SetPixels(new Color[1920*540]);texture.Apply();}public void Play(){IsPlaying=true;}public void Dispose(){IsPlaying=false;Disposals++;UnityEngine.Object.Destroy(texture);}
        }
    }
}

