using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // View-only policy: never submits commands or changes source/solver state.
    [DefaultExecutionOrder(3500)]
    public sealed class ControlStudioBackgroundView : MonoBehaviour
    {
        public enum BackgroundKind { SimulationGrid, Factory }
        public BackgroundKind Selected { get; private set; }
        public bool FactoryAvailable => factory != null;
        public string FactoryStatus => FactoryAvailable ? "Factory" : "No compatible Factory prefab.\nUnity Factory requires HDRP; this app uses URP.";
        public Transform Root { get; private set; }
        RobotVisualProfiles profiles;
        GameObject factory;

        public void Initialize(RobotVisualProfiles owner)
        {
            profiles=owner;
            Root=new GameObject("BackgroundRoot").transform;
            var grid=new GameObject("SimulationGrid").transform;grid.SetParent(Root,false);
            if(profiles.Simulation!=null)profiles.Simulation.Root.SetParent(grid,true);
            // Optional explicitly curated, visual-only environment. No asset download/import.
            var prefab=Resources.Load<GameObject>("Backgrounds/FactoryBackground");
            if(prefab!=null){factory=Instantiate(prefab,Root,false);factory.name="FactoryBackground";factory.SetActive(false);}
            Select(BackgroundKind.SimulationGrid);
        }
        public bool Select(BackgroundKind kind)
        {
            if(kind==BackgroundKind.Factory&&!FactoryAvailable)return false;
            Selected=kind;Apply();return true;
        }
        public void Apply()
        {
            if(profiles==null)return;
            profiles.SetEnvironmentMode(EnvironmentViewMode.RobotOnly);
            // Explicit roots own every work surface, legacy tray and cell prop.
            if(profiles.Environment?.Root!=null)profiles.Environment.Root.gameObject.SetActive(false);
            if(profiles.GripperCell?.CellRoot!=null)profiles.GripperCell.CellRoot.gameObject.SetActive(false);
            if(profiles.Simulation!=null){profiles.Simulation.Root.parent.gameObject.SetActive(Selected==BackgroundKind.SimulationGrid);profiles.Simulation.ApplyBackground(true);}
            if(factory!=null)factory.SetActive(Selected==BackgroundKind.Factory);
        }
        void LateUpdate()=>Apply();
        void OnDestroy(){if(Root!=null)Destroy(Root.gameObject);}
    }
}
