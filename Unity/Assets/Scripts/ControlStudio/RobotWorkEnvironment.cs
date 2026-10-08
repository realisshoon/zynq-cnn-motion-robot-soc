using System;
using System.Collections.Generic;
using UnityEngine;
namespace HumanMotion.ControlStudio
{
    // Virtual work-cell presentation. The existing TOOL-1W surface components own paint/weld marks.
    public sealed class RobotWorkEnvironment
    {
        [Serializable] public sealed class EnvironmentDefinition
        {
            public ToolKind tool;public string displayName;public Transform root;public Component effectSurface;
        }
        readonly Dictionary<ToolKind,EnvironmentDefinition> definitions=new Dictionary<ToolKind,EnvironmentDefinition>();
        readonly Dictionary<Transform,Vector3> tableOrigins=new Dictionary<Transform,Vector3>();
        readonly Tool1Runtime tool;readonly GameObject root;
        public EnvironmentDefinition Current {get;private set;}
        public string Error {get;private set;}="";
        public Transform Root=>root.transform;
        public RobotWorkEnvironment(Tool1Runtime runtime)
        {
            tool=runtime;root=new GameObject("ControlStudioEnvironmentRoot (AUTO / virtual only)");
            var polymer=Resources.Load<Material>("VisualProfiles/Polymer");var metal=Resources.Load<Material>("VisualProfiles/Titanium");
            var dark=Resources.Load<Material>("VisualProfiles/JointGraphite");var blue=Resources.Load<Material>("VisualProfiles/SignalTeal");
            if(polymer==null||metal==null||dark==null||blue==null||tool.Surface==null||tool.WeldSurface==null||tool.FastenSurface==null){Error="Work-cell presentation assets unavailable";return;}
            var grip=Group("Part tray / block workbench",ToolKind.Gripper,"PART TRAY / BLOCK BENCH",null);
            Vector3 pos=tool.Socket.position+tool.Socket.forward*.24f-Vector3.up*.11f;
            TrackTable(Part("Compact workbench top",grip.root,PrimitiveType.Cube,pos,new Vector3(.37f,.018f,.24f),metal));
            TrackTable(Part("Workbench base",grip.root,PrimitiveType.Cube,pos-Vector3.up*.095f,new Vector3(.24f,.17f,.17f),dark));
            TrackTable(Part("Parts tray",grip.root,PrimitiveType.Cube,pos+Vector3.up*.018f-Vector3.right*.10f,new Vector3(.11f,.016f,.12f),blue));
            for(int i=0;i<3;i++)TrackTable(Part("Loose block "+i,grip.root,PrimitiveType.Cube,pos+Vector3.up*.025f+Vector3.right*(.015f+i*.045f),new Vector3(.028f,.029f,.035f),polymer));
            var spray=Group("Paint booth / door panel",ToolKind.Spray,"PAINT BOOTH / DOOR PANEL",tool.Surface);
            tool.Surface.transform.SetParent(spray.root,true);
            PanelPart("Booth left rail",tool.Surface.transform,PrimitiveType.Cube,new Vector3(-.265f,0,-.007f),new Vector3(.022f,.54f,.025f),metal);
            PanelPart("Booth right rail",tool.Surface.transform,PrimitiveType.Cube,new Vector3(.265f,0,-.007f),new Vector3(.022f,.54f,.025f),metal);
            PanelPart("Booth header",tool.Surface.transform,PrimitiveType.Cube,new Vector3(0,.265f,-.007f),new Vector3(.55f,.022f,.025f),blue);
            PanelPart("Booth sill",tool.Surface.transform,PrimitiveType.Cube,new Vector3(0,-.265f,-.007f),new Vector3(.55f,.022f,.025f),dark);
            var weld=Group("Weld cell / metal jig",ToolKind.Welding,"WELD CELL / METAL JIG",tool.WeldSurface);
            tool.WeldSurface.transform.SetParent(weld.root,true);
            PanelPart("Jig left rail",tool.WeldSurface.transform,PrimitiveType.Cube,new Vector3(-.14f,0,-.008f),new Vector3(.018f,.28f,.026f),dark);
            PanelPart("Jig right rail",tool.WeldSurface.transform,PrimitiveType.Cube,new Vector3(.14f,0,-.008f),new Vector3(.018f,.28f,.026f),dark);
            PanelPart("Jig lower stop",tool.WeldSurface.transform,PrimitiveType.Cube,new Vector3(0,-.14f,-.008f),new Vector3(.30f,.018f,.026f),dark);
            PanelPart("Metal workpiece left",tool.WeldSurface.transform,PrimitiveType.Cube,new Vector3(-.066f,0,-.001f),new Vector3(.105f,.16f,.004f),metal);
            PanelPart("Metal workpiece right",tool.WeldSurface.transform,PrimitiveType.Cube,new Vector3(.066f,0,-.001f),new Vector3(.105f,.16f,.004f),metal);
            var weldBench=tool.Socket.position+tool.Socket.forward*.22f-Vector3.up*.15f;
            TrackTable(Part("Fixed welding workbench top",weld.root,PrimitiveType.Cube,weldBench,new Vector3(.40f,.022f,.27f),metal));
            TrackTable(Part("Fixed welding workbench base",weld.root,PrimitiveType.Cube,weldBench-Vector3.up*.09f,new Vector3(.27f,.16f,.18f),dark));
            var nail=Group("Nailing bench / fastening fixture",ToolKind.Nailing,"FASTENING BENCH / FIXTURE",tool.FastenSurface);
            tool.FastenSurface.transform.SetParent(nail.root,true);
            PanelPart("Fastener fixture upper rail",tool.FastenSurface.transform,PrimitiveType.Cube,new Vector3(0,.155f,-.012f),new Vector3(.34f,.018f,.028f),metal);
            PanelPart("Fastener fixture lower rail",tool.FastenSurface.transform,PrimitiveType.Cube,new Vector3(0,-.155f,-.012f),new Vector3(.34f,.018f,.028f),metal);
            PanelPart("Replaceable metal fastening plate",tool.FastenSurface.transform,PrimitiveType.Cube,new Vector3(0,0,.008f),new Vector3(.23f,.23f,.012f),dark);
            var nailBench=tool.Socket.position+tool.Socket.forward*.22f-Vector3.up*.15f;
            TrackTable(Part("Fixed nailing bench top",nail.root,PrimitiveType.Cube,nailBench,new Vector3(.38f,.022f,.27f),metal));
            TrackTable(Part("Fixed nailing bench base",nail.root,PrimitiveType.Cube,nailBench-Vector3.up*.09f,new Vector3(.26f,.16f,.18f),dark));
            Apply(ToolKind.Gripper);
        }
        void TrackTable(Transform part){tableOrigins.Add(part,part.position);}
        public void SetFamily(RobotVisualProfileId family,Vector3 activeToolPosition)
        {
            bool humanoid=family==RobotVisualProfileId.MechanicalHumanoid||family==RobotVisualProfileId.HumanoidRobot;
            tool.Surface.transform.localScale=Vector3.one*(humanoid?.22f:.48f);
            tool.WeldSurface.transform.localScale=Vector3.one*(humanoid?.14f:.24f);
            tool.FastenSurface.transform.localScale=Vector3.one*(humanoid?.16f:.28f);
            Vector3 offset=activeToolPosition-tool.Socket.position+
                (humanoid?new Vector3(.16f,0,.34f):family==RobotVisualProfileId.MechanicalDualTable?Vector3.forward*.1f:Vector3.zero);
            foreach(var part in tableOrigins)if(part.Key!=null)part.Key.position=part.Value+offset;
            Physics.SyncTransforms();
        }
        EnvironmentDefinition Group(string name,ToolKind kind,string display,Component surface)
        {var t=new GameObject(name).transform;t.SetParent(root.transform,false);var d=new EnvironmentDefinition{tool=kind,displayName=display,root=t,effectSurface=surface};definitions.Add(kind,d);return d;}
        static Transform Part(string name,Transform parent,PrimitiveType shape,Vector3 worldPosition,Vector3 worldSize,Material material)
        {var g=GameObject.CreatePrimitive(shape);g.name=name;g.transform.SetParent(parent,false);g.transform.position=worldPosition;g.transform.localScale=worldSize;var collider=g.GetComponent<Collider>();collider.enabled=false;UnityEngine.Object.Destroy(collider);g.GetComponent<Renderer>().sharedMaterial=material;return g.transform;}
        static void PanelPart(string name,Transform panel,PrimitiveType shape,Vector3 offset,Vector3 size,Material material)
        {var g=GameObject.CreatePrimitive(shape);g.name=name;g.transform.SetParent(panel,false);var s=panel.localScale;g.transform.localPosition=new Vector3(offset.x/s.x,offset.y/s.y,offset.z/s.z);g.transform.localScale=new Vector3(size.x/s.x,size.y/s.y,size.z/s.z);var collider=g.GetComponent<Collider>();collider.enabled=false;UnityEngine.Object.Destroy(collider);g.GetComponent<Renderer>().sharedMaterial=material;}
        public bool Supports(ToolKind kind)
        {return definitions.TryGetValue(kind,out var d)&&d.root!=null&&(kind==ToolKind.Gripper||kind==ToolKind.Spray&&tool.Surface!=null||kind==ToolKind.Welding&&tool.WeldSurface!=null||kind==ToolKind.Nailing&&tool.FastenSurface!=null);}
        public bool Apply(ToolKind kind)
        {
            if(!Supports(kind))return false;
            foreach(var d in definitions.Values)d.root.gameObject.SetActive(d.tool==kind);
            Current=definitions[kind];Physics.SyncTransforms();return true;
        }
        public void ShowOnly(ToolKind kind,bool visible)
        {
            foreach (var definition in definitions.Values)
                definition.root.gameObject.SetActive(visible && definition.tool == kind);
        }
        public void Destroy(){if(root!=null)UnityEngine.Object.Destroy(root);}
    }
}
