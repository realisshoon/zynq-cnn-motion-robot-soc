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
public static class RobotControlGuiMvpBatch
{
    static readonly string Main=ControlStudioBatchPaths.Root;
    static readonly string Output=Main+"/Validation/RobotControlGuiMvp";
    const string Active="RobotControlGuiMvpPlay";
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
        Need(Mathf.Abs(slider.value-value)<1.1f,"slider pointer input "+value);
    }
    static void KeyPress(Key key)
    {
        var settings=InputSystem.settings;var oldBackground=settings.backgroundBehavior;var oldEditor=settings.editorInputBehaviorInPlayMode;
        settings.backgroundBehavior=InputSettings.BackgroundBehavior.IgnoreFocus;
        settings.editorInputBehaviorInPlayMode=InputSettings.EditorInputBehaviorInPlayMode.AllDeviceInputAlwaysGoesToGameView;
        var keyboard=InputSystem.AddDevice<Keyboard>();
        try{keyboard.MakeCurrent();InputSystem.QueueStateEvent(keyboard,new KeyboardState(key));InputSystem.Update();Need(keyboard[key].isPressed,"synthetic keyboard delivered "+key);Call(mock,"Update");InputSystem.QueueStateEvent(keyboard,new KeyboardState());InputSystem.Update();}
        finally{InputSystem.RemoveDevice(keyboard);settings.backgroundBehavior=oldBackground;settings.editorInputBehaviorInPlayMode=oldEditor;}
    }
    static string VisualSignature()
    {
        var profiles=router.GetComponent<RobotVisualProfiles>();var root=profiles.Selected==RobotVisualProfileId.HumanoidRobot?profiles.Humanoid.transform:profiles.Adapter.controller.transform.root;
        return string.Join("\n",root.GetComponentsInChildren<Renderer>(true).Select(r=>r.name+"/"+r.transform.localToWorldMatrix.ToString("F6")+"/"+r.enabled+"/"+string.Join(",",r.sharedMaterials.Select(m=>m==null?"null":m.GetInstanceID().ToString()))))+
            string.Join(";",profiles.Simulation.Root.GetComponentsInChildren<Light>(true).Select(l=>l.name+"/"+l.intensity+"/"+l.enabled));
    }
    static void Preserved(){Need(router.Epoch==epoch&&router.Applied.SequenceEqual(applied),"mock leaves epoch / Applied unchanged");Need(VisualSignature()==visuals,"mock leaves rig / materials / lights unchanged");Need(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0&&!router.GetComponent<ControlStudioUartOutput>().Connected&&!router.GetComponent<UartPose3DSource>().Connected,"real UART disconnected / Hardware TX=0");}
    static void Tick()
    {
        if(!SessionState.GetBool(Active,false))return;
        try{
            if(DateTime.UtcNow-DateTime.Parse(SessionState.GetString(Active+"time",""))>TimeSpan.FromMinutes(4))throw new Exception("Play timeout");
            if(!EditorApplication.isPlaying||EditorApplication.isCompiling||EditorApplication.timeSinceStartup<next)return;
            if(stage==0){
                router=UnityEngine.Object.FindFirstObjectByType<SingleArmCommandRouter>();if(router==null||!router.Ready)return;
                ui=router.GetComponent<ControlStudioRuntimeUI>();menu=router.GetComponent<ControlStudioStartupMenu>();view=router.GetComponent<ControlStudioPresentationUI>();mock=router.GetComponent<RobotControlMockPanel>();if(mock==null||mock.PanelRoot==null)return;
                BindCapture();Need(menu.Visible&&!mock.PanelRoot.activeInHierarchy,"Startup remains independent");Click("START");router.SetPaused(true);
            }
            else if(stage==1){
                Need(mock.SelectedRobot==0&&!mock.State.Pwm&&!mock.State.Follow,"default Robot 0 / PWM OFF / FOLLOW OFF");Capture("01_main_robot0.png");
                applied=(float[])router.Applied.Clone();epoch=router.Epoch;visuals=VisualSignature();
                KeyPress(Key.A);Need(!mock.State.Follow,"A rejected while PWM OFF");KeyPress(Key.E);Need(mock.State.Pwm,"E PWM ON");KeyPress(Key.A);Need(mock.State.Follow,"A FOLLOW ON");KeyPress(Key.S);Need(!mock.State.Follow&&mock.State.Pwm,"S preserves PWM");KeyPress(Key.X);Need(!mock.State.Pwm&&!mock.State.Follow,"X clears both");
                // The current UI starts collapsed: focus must target a visible input field.
                Click("Servo details toggle");Call(ui,"Update");Call(view,"LateUpdate");Canvas.ForceUpdateCanvases();
                Need(ui.Inputs[0].gameObject.activeInHierarchy,"servo field visible after layout update");
                EventSystem.current.SetSelectedGameObject(ui.Inputs[0].gameObject);
                Need(EventSystem.current.currentSelectedGameObject==ui.Inputs[0].gameObject,"servo field selected");
                // Keyboard delivery is tested above; check focus guard without replacing the input device.
                mock.HandleShortcut(Key.E);Need(!mock.State.Pwm,"editing a servo number does not trigger mock hotkey");EventSystem.current.SetSelectedGameObject(null);Click("Servo details toggle");Preserved();Click("Advanced toggle");
            }
            else if(stage==2){
                Need(mock.AdvancedOpen,"Advanced opened");Need(mock.State.Red==40&&mock.State.Green==30&&mock.State.Blue==50,"initial margin 40 / 30 / 50");
                Click("PWM enable");Click("Follow start");Need(mock.State.Follow,"button FOLLOW ON");Click("Follow stop");Need(mock.State.Pwm&&!mock.State.Follow,"button stop preserves PWM");Click("Refresh mock status");Capture("02_advanced.png");
            }
            else if(stage==3){Drag(mock.MarginSliders[0],64);Drag(mock.MarginSliders[1],22);Drag(mock.MarginSliders[2],78);Need(mock.State.AppliedRed==40,"draft margin not applied early");Click("Apply mock margins");Need(mock.State.AppliedRed==64&&mock.State.AppliedGreen==22&&mock.State.AppliedBlue==78,"Mock Apply only");Capture("03_rgb_adjusted.png");Preserved();Click("Advanced toggle");Click("Preview toggle");}
            else if(stage==4){Need(mock.PreviewOpen,"Preview ON");var r=(RectTransform)Find("Placeholder / no capture").transform;Need(Math.Abs(r.rect.width/r.rect.height-16f/9)<.001,"Preview 16:9");Capture("04_camera_preview_on.png");Click("Preview toggle");}
            else if(stage==5){Need(!mock.PreviewOpen,"Preview OFF");Capture("05_camera_preview_off.png");Click("Robot 1 LEFT");Need(!mock.State.Pwm&&!mock.State.Follow&&mock.State.Red==40,"Robot 1 independent default");KeyPress(Key.E);Click("Follow start");Capture("05b_robot1_mock_follow.png");Click("Robot 0 RIGHT");Need(mock.State.Pwm&&!mock.State.Follow&&mock.State.AppliedRed==64,"Robot 0 retained");Preserved();Click("MENU");}
            else if(stage==6){Click("ROBOT next");Need(menu.Visible&&!mock.PanelRoot.activeInHierarchy,"Humanoid startup has no mock overlay");Capture("06_humanoid_selection.png");Click("START");}
            else if(stage==7){Need(router.GetComponent<RobotVisualProfiles>().Selected==RobotVisualProfileId.HumanoidRobot,"Humanoid preserved");Capture("06b_humanoid_main.png");Capture("09_narrow_1280x720.png",1280,720);Click("HIDE CONTROLS");}
            else if(stage==8){mock.RefreshVisibility();Need(!mock.PanelRoot.activeInHierarchy,"Controls Hidden hides mock / preview");KeyPress(Key.X);Need(mock.State.Pwm,"Hidden hotkeys ignored");Capture("07_controls_hidden.png");Click("HIDE CONTROLS");Click("PRESENTATION");}
            else if(stage==9){Need(view.PresentationMode&&!ui.RuntimeCanvas.gameObject.activeInHierarchy&&!mock.CanInteract,"Presentation has no controls");Capture("08_presentation.png");view.SetPresentationMode(false);}
            else if(stage==10){
                router.SetPaused(false);var seed=(float[])router.Applied.Clone();var target=new[]{105f,75f,95f,100f,.4f};
                using(var reference=new ControlStudioOutputPolicy(seed)){
                    Need(reference.Submit(target)==0&&ui.manual.LoadAtomic(target),"existing Manual target accepted");var expected=new float[5];var velocity=new float[4];
                    for(int tick=0;tick<150;tick++){mock.SelectRobot(tick%2);mock.HandleShortcut(tick%4==0?Key.E:tick%4==1?Key.A:tick%4==2?Key.S:Key.X);reference.Tick(expected,velocity);router.AdvanceVirtual(.02);Need(router.Applied.SequenceEqual(expected),"Agent2 / router trace "+tick);}
                }
                Need(router.GetComponent<ControlStudioUartOutput>().HardwareTxCount==0,"final Hardware TX=0");Finish(true);return;
            }
            stage++;next=EditorApplication.timeSinceStartup+.75;
        }catch(Exception e){checks.Add("FAIL: "+e);Finish(false);}
    }
    static void Finish(bool pass){SessionState.SetBool(Active,false);File.WriteAllLines(Output+"/editor_play_result.txt",checks);EditorApplication.Exit(pass?0:2);}
}
