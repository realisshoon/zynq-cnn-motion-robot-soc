using System.IO;
using UnityEngine;
using UnityEditor;
using HumanMotion.ControlStudio;
public static class Tool1AssetBuilder
{
    const string Folder="Assets/Resources/Tool1";
    static Material Mat(string name,Color color,bool unlit=false)
    {
        string path=Folder+"/"+name+".mat";var found=AssetDatabase.LoadAssetAtPath<Material>(path);if(found!=null)return found;
        var m=new Material(Shader.Find(unlit?"Universal Render Pipeline/Unlit":"Universal Render Pipeline/Lit"));m.color=color;if(m.HasProperty("_Cull"))m.SetFloat("_Cull",0);
        if(!unlit&&m.HasProperty("_Metallic")){m.SetFloat("_Metallic",.55f);m.SetFloat("_Smoothness",.55f);}AssetDatabase.CreateAsset(m,path);return m;
    }
    static Transform Child(string name,Transform parent,Vector3 pos)
    {var t=new GameObject(name).transform;t.SetParent(parent,false);t.localPosition=pos;return t;}
    static void Shape(string name,Transform parent,PrimitiveType type,Vector3 pos,Vector3 scale,Material m,bool alongZ=false)
    {var g=GameObject.CreatePrimitive(type);g.name=name;g.transform.SetParent(parent,false);g.transform.localPosition=pos;g.transform.localScale=scale;if(alongZ)g.transform.localRotation=Quaternion.Euler(90,0,0);Object.DestroyImmediate(g.GetComponent<Collider>());g.GetComponent<Renderer>().sharedMaterial=m;}
    public static void BuildAssets()
    {
        Directory.CreateDirectory(Folder);AssetDatabase.Refresh();var metal=Mat("Metal",new Color(.66f,.72f,.78f));var dark=Mat("Dark",new Color(.055f,.075f,.09f));var blue=Mat("Blue",new Color(.05f,.37f,.8f));var gold=Mat("Gold",new Color(.98f,.61f,.12f));Mat("Panel",Color.white,true);Mat("WeldPanel",new Color(.22f,.26f,.29f),true);Mat("WeldBead",new Color(.63f,.46f,.24f),true);Mat("Fastener",new Color(.91f,.8f,.44f),true);
        foreach(var kind in new[]{ToolKind.Spray,ToolKind.Welding}){
            string path=Folder+"/"+(kind==ToolKind.Welding?"WeldingTorch":kind.ToString())+".prefab";if(File.Exists(path))continue;
            var root=new GameObject(kind.ToString());var view=root.AddComponent<Tool1Visual>();view.kind=kind;
            var visual=Child("ToolVisual",root.transform,Vector3.zero);view.toolTip=Child("ToolTip",root.transform,new Vector3(0,0,kind==ToolKind.Spray?.145f:.24f));var effect=Child("ToolEffect",root.transform,view.toolTip.localPosition);
            Shape("Fixed socket collar",visual,PrimitiveType.Cylinder,new Vector3(0,0,.012f),new Vector3(.045f,.012f,.045f),dark,true);
            if(kind==ToolKind.Spray){
                Shape("Spray body",visual,PrimitiveType.Cylinder,new Vector3(0,0,.06f),new Vector3(.043f,.038f,.043f),blue,true);
                Shape("Paint reservoir",visual,PrimitiveType.Sphere,new Vector3(0,-.027f,.06f),new Vector3(.035f,.048f,.06f),metal);
                Shape("Nozzle collar",visual,PrimitiveType.Cylinder,new Vector3(0,0,.105f),new Vector3(.025f,.009f,.025f),dark,true);
                Shape("Nozzle",visual,PrimitiveType.Cylinder,new Vector3(0,0,.13f),new Vector3(.012f,.015f,.012f),metal,true);
                var ps=effect.gameObject.AddComponent<ParticleSystem>();var main=ps.main;main.playOnAwake=false;main.maxParticles=256;main.startLifetime=.35f;main.startSpeed=.6f;main.startSize=.006f;main.simulationSpace=ParticleSystemSimulationSpace.World;
                var emission=ps.emission;emission.enabled=false;var shape=ps.shape;shape.shapeType=ParticleSystemShapeType.Cone;shape.angle=10;shape.radius=.003f;
                ps.GetComponent<ParticleSystemRenderer>().sharedMaterial=Mat("Mist",new Color(.5f,.8f,1),true);ps.Stop(true,ParticleSystemStopBehavior.StopEmittingAndClear);view.spray=ps;
            }else{
                Shape("Torch insulated body",visual,PrimitiveType.Cylinder,new Vector3(0,0,.055f),new Vector3(.037f,.036f,.037f),dark,true);
                Shape("Cable connector",visual,PrimitiveType.Cylinder,new Vector3(0,-.019f,.025f),new Vector3(.015f,.018f,.015f),metal);
                Shape("Cable boot",visual,PrimitiveType.Cylinder,new Vector3(0,-.043f,.025f),new Vector3(.011f,.013f,.011f),dark);
                var neck=Child("Bent torch neck",visual,new Vector3(0,0,.09f));neck.localRotation=Quaternion.Euler(-35,0,0);
                Shape("Copper neck",neck,PrimitiveType.Cylinder,new Vector3(0,0,.024f),new Vector3(.012f,.03f,.012f),gold,true);
                Shape("Gas nozzle",neck,PrimitiveType.Cylinder,new Vector3(0,0,.061f),new Vector3(.02f,.016f,.02f),metal,true);
                Shape("Electrode tip",neck,PrimitiveType.Cylinder,new Vector3(0,0,.081f),new Vector3(.004f,.007f,.004f),gold,true);
                view.toolTip.localPosition=neck.localPosition+neck.localRotation*new Vector3(0,0,.089f);view.toolTip.localRotation=neck.localRotation;
                var weld=effect.gameObject.AddComponent<Tool1WeldingEffect>();view.welding=weld;weld.tip=view.toolTip;
                var glow=GameObject.CreatePrimitive(PrimitiveType.Sphere);glow.name="Local arc glow";glow.transform.SetParent(effect,false);Object.DestroyImmediate(glow.GetComponent<Collider>());
                glow.GetComponent<Renderer>().sharedMaterial=Mat("Arc",new Color(.55f,.8f,1),true);weld.glow=glow.transform;glow.SetActive(false);
                var lamp=Child("Arc light",effect,Vector3.zero).gameObject.AddComponent<Light>();lamp.type=LightType.Point;lamp.color=new Color(.55f,.8f,1);lamp.range=.13f;lamp.enabled=false;weld.arcLight=lamp;
                var ps=Child("Small sparks",effect,Vector3.zero).gameObject.AddComponent<ParticleSystem>();var main=ps.main;main.playOnAwake=false;main.maxParticles=96;main.startLifetime=.18f;main.startSpeed=.22f;main.startSize=.0018f;main.startColor=new Color(1,.65f,.17f);main.simulationSpace=ParticleSystemSimulationSpace.World;
                var emission=ps.emission;emission.enabled=false;var shape=ps.shape;shape.shapeType=ParticleSystemShapeType.Cone;shape.angle=55;shape.radius=.001f;
                ps.GetComponent<ParticleSystemRenderer>().sharedMaterial=Mat("Spark",new Color(1,.7f,.2f),true);ps.Stop(true,ParticleSystemStopBehavior.StopEmittingAndClear);weld.sparks=ps;

            }
            PrefabUtility.SaveAsPrefabAsset(root,path);Object.DestroyImmediate(root);
        }
        string nailPath=Folder+"/NailingHead.prefab";
        if(!File.Exists(nailPath)){
            var root=new GameObject("Nailing");var view=root.AddComponent<Tool1Visual>();view.kind=ToolKind.Nailing;
            var visual=Child("Mechanical fastening head",root.transform,Vector3.zero);
            view.toolTip=Child("ToolTip",root.transform,new Vector3(0,0,.16f));
            Shape("Square drive housing",visual,PrimitiveType.Cube,new Vector3(0,0,.045f),new Vector3(.076f,.066f,.085f),dark);
            Shape("Replaceable service plate",visual,PrimitiveType.Cube,new Vector3(0,.035f,.045f),new Vector3(.066f,.008f,.073f),metal);
            Shape("Amber impact collar",visual,PrimitiveType.Cylinder,new Vector3(0,0,.102f),new Vector3(.032f,.014f,.032f),gold,true);
            Shape("Fastener guide",visual,PrimitiveType.Cylinder,new Vector3(0,0,.132f),new Vector3(.014f,.027f,.014f),metal,true);
            Shape("Driver tip",visual,PrimitiveType.Cylinder,new Vector3(0,0,.157f),new Vector3(.006f,.008f,.006f),dark,true);
            var effect=Child("Virtual fastening effect",root.transform,view.toolTip.localPosition);
            var nail=effect.gameObject.AddComponent<Tool1NailingEffect>();nail.tip=view.toolTip;view.nailing=nail;
            var pulse=GameObject.CreatePrimitive(PrimitiveType.Cube);pulse.name="Short virtual impact flash";pulse.transform.SetParent(effect,false);
            Object.DestroyImmediate(pulse.GetComponent<Collider>());pulse.GetComponent<Renderer>().sharedMaterial=gold;nail.pulse=pulse.transform;pulse.SetActive(false);
            PrefabUtility.SaveAsPrefabAsset(root,nailPath);Object.DestroyImmediate(root);
        }
        AssetDatabase.SaveAssets();AssetDatabase.Refresh();
    }
}
