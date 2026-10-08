using System.Linq;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Layout only. Existing buttons retain their listeners and command ownership.
    [DefaultExecutionOrder(5000)]
    public sealed class ControlStudioHeaderLayout : MonoBehaviour
    {
        ControlStudioRuntimeUI studio;ControlStudioPresentationUI view;
        GameObject backgroundMenu;Button servo,cameraButton;Text backgroundLabel;
        public static void Attach(ControlStudioRuntimeUI owner,Transform design)
        {
            var layout=owner.gameObject.AddComponent<ControlStudioHeaderLayout>();
            layout.Initialize(owner,design);
        }
        static RectTransform Place(Transform t,Transform parent,float x,float y,float w,float h)
        {
            var r=(RectTransform)t;r.SetParent(parent,false);r.anchorMin=r.anchorMax=r.pivot=new Vector2(0,1);
            r.anchoredPosition=new Vector2(x,-y);r.sizeDelta=new Vector2(w,h);r.localScale=Vector3.one;return r;
        }
        void Initialize(ControlStudioRuntimeUI owner,Transform design)
        {
            studio=owner;view=GetComponent<ControlStudioPresentationUI>();
            Transform Find(string name)=>design.GetComponentsInChildren<Transform>(true).First(t=>t.name==name);
            void Move(string name,float x,float y,float w,float h)
            {
                var t=Find(name);Place(t,design,x,y,w,h);
                var button=t.GetComponent<Button>();if(button==null)return;
                var label=t.GetComponentInChildren<Text>();var r=label.rectTransform;
                r.anchorMin=Vector2.zero;r.anchorMax=Vector2.one;r.offsetMin=new Vector2(4,0);r.offsetMax=new Vector2(-4,0);
                label.alignment=TextAnchor.MiddleCenter;label.fontSize=16;
            }
            Move("Title",24,8,820,32);Find("Title").GetComponent<Text>().fontSize=24;
            Move("MENU",870,10,110,32);Move("Hardware",1030,8,546,34);
            Move("Reset View",24,54,140,32);Move("Pause virtual",174,54,160,32);
            Move("HIDE CONTROLS",344,54,170,32);Move("PRESENTATION",524,54,150,32);
            Move("Preview toggle",950,54,210,32);Move("Servo details toggle",1170,54,190,32);
            servo=Find("Servo details toggle").GetComponent<Button>();cameraButton=Find("Preview toggle").GetComponent<Button>();

            var trigger=new GameObject("Background menu toggle",typeof(RectTransform),typeof(Image),typeof(Button));
            Place(trigger.transform,design,684,54,256,32);trigger.GetComponent<Image>().color=new Color(.14f,.23f,.30f);
            var label=new GameObject("Label",typeof(RectTransform),typeof(Text));Place(label.transform,trigger.transform,4,0,248,32);
            backgroundLabel=label.GetComponent<Text>();backgroundLabel.font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");backgroundLabel.fontSize=16;
            backgroundLabel.color=Color.white;backgroundLabel.alignment=TextAnchor.MiddleCenter;backgroundLabel.raycastTarget=false;
            backgroundMenu=new GameObject("Background dropdown",typeof(RectTransform),typeof(Image));
            Place(backgroundMenu.transform,design,684,96,256,142);backgroundMenu.GetComponent<Image>().color=new Color(.035f,.055f,.075f,.99f);
            var grid=Find("SIMULATION GRID");var factory=Find("FACTORY");
            Place(grid,backgroundMenu.transform,8,8,240,32);Place(factory,backgroundMenu.transform,8,50,240,32);
            grid.GetComponent<Button>().onClick.AddListener(()=>backgroundMenu.SetActive(false));
            factory.GetComponent<Button>().onClick.AddListener(()=>backgroundMenu.SetActive(false));
            factory.GetComponent<Button>().interactable=view.Background.FactoryAvailable;
            if(!view.Background.FactoryAvailable)factory.GetComponentInChildren<Text>().text="FACTORY / unavailable";
            var reason=new GameObject("Factory availability reason",typeof(RectTransform),typeof(Text));
            Place(reason.transform,backgroundMenu.transform,8,86,240,52);
            var reasonText=reason.GetComponent<Text>();reasonText.font=backgroundLabel.font;reasonText.fontSize=13;
            reasonText.color=new Color(.75f,.82f,.87f);reasonText.alignment=TextAnchor.MiddleLeft;reasonText.raycastTarget=false;
            reasonText.text=view.Background.FactoryAvailable?"Visual-only environment ready":view.Background.FactoryStatus;
            trigger.GetComponent<Button>().onClick.AddListener(()=>backgroundMenu.SetActive(!backgroundMenu.activeSelf));
            backgroundMenu.SetActive(false);Find("Presentation control strip").gameObject.SetActive(false);
            Find("View controls").gameObject.SetActive(false);
            LateUpdate();
        }
        void LateUpdate()
        {
            if(studio==null)return;
            bool menu=GetComponent<ControlStudioStartupMenu>()?.Visible==true;
            servo.gameObject.SetActive(view.ControlsVisible&&studio.router.Source=="MANUAL");
            cameraButton.gameObject.SetActive(!menu&&!view.PresentationMode);
            if(menu||view.PresentationMode||!view.ControlsVisible)backgroundMenu.SetActive(false);
            backgroundLabel.text="Background: "+(view.Background.Selected==ControlStudioBackgroundView.BackgroundKind.SimulationGrid?"Grid":"Factory")+"  ▾";
        }
    }
}
