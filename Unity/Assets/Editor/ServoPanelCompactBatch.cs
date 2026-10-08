using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Reflection;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.LowLevel;

// Isolated Editor Play validation. UI events stay inside Unity; no OS input or serial connection.
public static class ServoPanelCompactBatch
{
    static readonly string Main=ControlStudioBatchPaths.Root;
    static readonly string Output=Main+"/Validation/ServoPanelCompact";
    const string Active="ServoPanelCompactPlay";
    static int stage;static double next;static SingleArmCommandRouter router;static ControlStudioRuntimeUI ui;
    static ControlStudioStartupMenu menu;static ControlStudioPresentationUI view;static RobotControlMockPanel mock;
    static float[] applied;static int epoch;static string visuals;
    static readonly List<string> checks=new List<string>();
    static void Need(bool ok,string note){if(!ok)throw new Exception(note);checks.Add("PASS: "+note);}
    static void Call(object o,string name)=>o.GetType().GetMethod(name,BindingFlags.Instance|BindingFlags.NonPublic).Invoke(o,null);
    public static void Run()
    {
        if(!ControlStudioBatchPaths.Allowed)throw new Exception("Isolated validation project only");
        Directory.CreateDirectory(Output);EditorSceneManager.OpenScene("Assets/Scenes/Demo_07_SingleArmControl.unity");
        SessionState.SetBool(Active,true);SessionState.SetString(Active+"time",DateTime.UtcNow.ToString("O"));EditorApplication.isPlaying=true;
    }
    [InitializeOnLoadMethod]static void Hook(){EditorApplication.update+=Tick;}
    static void BindCapture()
    {
        ControlStudioStartupMenuBatch.ValidationOutput=Output;
        foreach(var pair in new[]{("router",(object)router),("ui",(object)ui),("menu",(object)menu)})typeof(ControlStudioStartupMenuBatch).GetField(pair.Item1,BindingFlags.Static|BindingFlags.NonPublic).SetValue(null,pair.Item2);
    }
    static void Capture(string name,int width=1920,int height=1080)
    {
        mock.RefreshVisibility();Canvas.ForceUpdateCanvases();
        typeof(ControlStudioStartupMenuBatch).GetMethod("Capture",BindingFlags.Static|BindingFlags.NonPublic).Invoke(null,new object[]{name,width,height});
    }
    static GameObject Find(string name,bool button=false)=>ui.RuntimeCanvas.GetComponentsInChildren<Transform>(true).Concat(menu.MenuCanvas.GetComponentsInChildren<Transform>(true)).Single(t=>t.name==name&&(!button||t.GetComponent<Button>()!=null)).gameObject;
    static PointerEventData Hit(GameObject target,Vector2 point)
    {
        Canvas.ForceUpdateCanvases();var data=new PointerEventData(EventSystem.current){position=point,button=PointerEventData.InputButton.Left};
        var hits=new List<RaycastResult>();EventSystem.current.RaycastAll(data,hits);
        Need(hits.Count>0&&(hits[0].gameObject==target||hits[0].gameObject.transform.IsChildOf(target.transform)),"UI raycast: "+target.name+" @"+point+" hits="+string.Join(",",hits.Take(3).Select(h=>h.gameObject.name)));
        data.pointerCurrentRaycast=data.pointerPressRaycast=hits[0];return data;
    }
    static void Click(string name)
    {
        Canvas.ForceUpdateCanvases();mock.RefreshVisibility();
        var target=Find(name,true);var b=target.GetComponent<Button>();Need(b!=null&&b.IsInteractable()&&target.activeInHierarchy,"clickable: "+name);
        var rect=(RectTransform)target.transform;var point=RectTransformUtility.WorldToScreenPoint(null,rect.TransformPoint(rect.rect.center));
        var data=Hit(target,point);ExecuteEvents.ExecuteHierarchy(target,data,ExecuteEvents.pointerDownHandler);ExecuteEvents.ExecuteHierarchy(target,data,ExecuteEvents.pointerUpHandler);ExecuteEvents.ExecuteHierarchy(target,data,ExecuteEvents.pointerClickHandler);mock.RefreshView();
    }
    static void Drag(Slider slider,float value)
    {
        var area=(RectTransform)slider.handleRect.parent;var p=area.TransformPoint(new Vector3(Mathf.Lerp(area.rect.xMin,area.rect.xMax,value/100),area.rect.center.y,0));
        var data=Hit(slider.gameObject,RectTransformUtility.WorldToScreenPoint(null,p));
        ExecuteEvents.ExecuteHierarchy(data.pointerCurrentRaycast.gameObject,data,ExecuteEvents.initializePotentialDrag);
        ExecuteEvents.ExecuteHierarchy(data.pointerCurrentRaycast.gameObject,data,ExecuteEvents.pointerDownHandler);
        ExecuteEvents.Execute(slider.gameObject,data,ExecuteEvents.dragHandler);ExecuteEvents.Execute(slider.gameObject,data,ExecuteEvents.pointerUpHandler);
        Need(Mathf.Abs(slider.value-Mathf.Lerp(slider.minValue,slider.maxValue,value/100))<1.1f,"slider pointer input "+value);
    }
    static void Tick()
    {
        if(!SessionState.GetBool(Active,false))return;
        try {
            if(DateTime.UtcNow-DateTime.Parse(SessionState.GetString(Active+"time",""))>TimeSpan.FromMinutes(4))throw new Exception("Play timeout");
            if(!EditorApplication.isPlaying||EditorApplication.isCompiling||EditorApplication.timeSinceStartup<next)return;
            if(stage==0){
                router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();if(router==null||!router.Ready)return;
                ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();mock=router.GetComponent<RobotControlMockPanel>();if(mock==null||mock.PanelRoot==null)return;
                BindCapture();Click("START");router.SetPaused(true);
            } else if(stage==1){
                Need(!ui.ServoDetailsExpanded&&!ui.Sliders[0].gameObject.activeInHierarchy,"default collapsed / all servo controls hidden");
                Need(view.CameraDesignWidth==1600,"collapsed full-width 3D viewport");
                applied=(float[])ui.manual.Values.Clone();epoch=router.Epoch;
                Capture("01_servo_collapsed.png");Click("Servo details toggle");
            } else if(stage==2){
                Need(ui.ServoDetailsExpanded&&ui.Sliders.All(s=>s.gameObject.activeInHierarchy),"expanded five sliders");
                Need(view.CameraDesignWidth==1140,"compact expanded viewport");
                Need(ui.manual.Values.SequenceEqual(applied)&&router.Epoch==epoch,"expand preserves commands and epoch");
                Capture("02_servo_expanded.png");
                // Real slider drag away from the initial value through its existing listener.
                var slider=ui.Sliders[0];Drag(slider,60);Need(Mathf.Abs(ui.manual.Values[0]-Mathf.Lerp(slider.minValue,slider.maxValue,.6f))<.01f,"existing Manual slider callback");
                ui.Inputs[1].onEndEdit.Invoke("82");Need(ui.manual.Values[1]==82,"existing numeric edit callback");
                applied=(float[])ui.manual.Values.Clone();Click("Servo details toggle");
            } else if(stage==3){
                Need(ui.manual.Values.SequenceEqual(applied),"collapse retains edited values");Click("Servo details toggle");
                Need(ui.manual.Values.SequenceEqual(applied)&&ui.Inputs[1].text=="82","reopen retains edited values");
                Capture("03_expanded_retained_values.png");Click("Preview toggle");Capture("04_expanded_preview.png");
                Capture("05_compact_1280x720.png",1280,720);Click("HIDE CONTROLS");
            } else if(stage==4){
                Need(!Find("Manual panel").activeInHierarchy&&!mock.PanelRoot.activeInHierarchy,"Controls Hide hides both panels");
                Click("HIDE CONTROLS");Need(ui.ServoDetailsExpanded,"Controls Show preserves expansion");Click("PRESENTATION");
            } else if(stage==5){
                Need(!ui.RuntimeCanvas.gameObject.activeInHierarchy,"Presentation UI hidden");Capture("06_presentation.png");view.SetPresentationMode(false);
                Need(ui.manual.Values.SequenceEqual(applied)&&router.Epoch==epoch,"view toggles preserve commands and epoch");
                Need(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"Hardware TX=0");Finish(true);return;
            }
            stage++;next=EditorApplication.timeSinceStartup+.75;
        }catch(Exception e){checks.Add("FAIL: "+e);Finish(false);}
    }
    static void Finish(bool pass){SessionState.SetBool(Active,false);File.WriteAllLines(Output+"/editor_play_result.txt",checks);EditorApplication.Exit(pass?0:2);}
}
