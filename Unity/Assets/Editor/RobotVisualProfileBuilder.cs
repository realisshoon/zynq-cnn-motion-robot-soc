using System.Collections.Generic;
using System.Linq;
using System.IO;
using UnityEngine;
using UnityEditor;
using HumanMotion.ControlStudio;
public static class RobotVisualProfileBuilder
{
    const string Folder="Assets/Resources/VisualProfiles";
    static Material Shell,Dark,Metal,Accent,IndustrialArmor,IndustrialBlue;
    static Material Mat(string name,Color color,float metallic)
    {string path=Folder+"/"+name+".mat";var m=AssetDatabase.LoadAssetAtPath<Material>(path);if(m!=null)return m;m=new Material(Shader.Find("Universal Render Pipeline/Lit"));m.color=color;m.SetFloat("_Metallic",metallic);m.SetFloat("_Smoothness",.5f);AssetDatabase.CreateAsset(m,path);return m;}
    static Transform Node(string name,Transform parent)
    {var t=new GameObject(name).transform;t.SetParent(parent,false);return t;}
    static Transform Shape(string name,Transform parent,PrimitiveType type,Vector3 pos,Vector3 scale,Material mat)
    {var g=GameObject.CreatePrimitive(type);g.name=name;g.transform.SetParent(parent,false);g.transform.localPosition=pos;g.transform.localScale=scale;Object.DestroyImmediate(g.GetComponent<Collider>());g.GetComponent<Renderer>().sharedMaterial=mat;return g.transform;}
    static Transform WorldShape(string name,Transform parent,PrimitiveType type,Vector3 pos,Vector3 scale,Material mat)
    {var t=Shape(name,parent,type,Vector3.zero,scale,mat);t.position=pos;t.rotation=Quaternion.identity;return t;}
    static void Bone(string name,Transform parent,Vector3 a,Vector3 b,float width,Material mat)
    {var t=WorldShape(name,parent,PrimitiveType.Capsule,(a+b)*.5f,new Vector3(width,Vector3.Distance(a,b)*.5f,width),mat);t.rotation=Quaternion.FromToRotation(Vector3.up,b-a);}
    public static void BuildAssets()
    {
        Directory.CreateDirectory(Folder);AssetDatabase.Refresh();Shell=Mat("Polymer",new Color(.77f,.83f,.88f),.2f);Dark=Mat("JointGraphite",new Color(.055f,.085f,.12f),.4f);Metal=Mat("Titanium",new Color(.36f,.48f,.55f),.8f);Accent=Mat("SignalTeal",new Color(.07f,.7f,.77f),.45f);
        IndustrialArmor=Mat("IndustrialAmber",new Color(.93f,.43f,.08f),.45f);IndustrialBlue=Mat("SprayHoseBlue",new Color(.07f,.35f,.75f),.25f);
        var adapter=Object.FindFirstObjectByType<Demo06RealisticVisualAdapter>();if(adapter==null)throw new System.Exception("Demo_07 adapter required");
        // Retain the completed STEP 4 assets; expose one Industrial and one upper-body Humanoid family.
        foreach(var id in new[]{RobotVisualProfileId.Industrial,RobotVisualProfileId.Humanoid}){
            string path=Folder+"/"+id+".prefab";BuildRig(adapter,id,path);
            string profilePath=Folder+"/"+id+".asset";if(File.Exists(profilePath))continue;var def=ScriptableObject.CreateInstance<RobotVisualProfileDefinition>();def.id=id;def.displayName=id==RobotVisualProfileId.Industrial?"Industrial":"Humanoid (right active / left static preview)";def.leftArmPreview=id==RobotVisualProfileId.Humanoid;def.prefab=AssetDatabase.LoadAssetAtPath<GameObject>(path).GetComponent<RobotVisualProfileRig>();AssetDatabase.CreateAsset(def,profilePath);
        }
        if(!File.Exists(Folder+"/G51.asset")){var def=ScriptableObject.CreateInstance<RobotVisualProfileDefinition>();def.id=RobotVisualProfileId.G51;def.displayName="G51 (existing scene visual root)";AssetDatabase.CreateAsset(def,Folder+"/G51.asset");}
        AssetDatabase.SaveAssets();AssetDatabase.Refresh();
    }
    static void BuildRig(Demo06RealisticVisualAdapter src,RobotVisualProfileId id,string path)
    {
        bool industrial=id==RobotVisualProfileId.Industrial;Material armor=industrial?IndustrialArmor:Shell;
        var root=new GameObject(id.ToString());root.transform.SetPositionAndRotation(src.transform.position,src.transform.rotation);root.transform.localScale=src.transform.lossyScale;
        var rig=root.AddComponent<RobotVisualProfileRig>();var map=new Dictionary<Transform,Transform>{{src.transform,root.transform}};
        var originals=new[]{src.m0,src.m1,src.m2,src.m3};Transform hand=null;
        System.Func<Transform,Transform> clone=null;clone=t=>{
            if(map.TryGetValue(t,out var found))return found;var parent=clone(t.parent);if(hand!=null&&parent==map[src.m3.visual])parent=hand;
            var result=Node(t.name,parent);result.localPosition=t.localPosition;result.localRotation=t.localRotation;result.localScale=t.localScale;map[t]=result;return result;
        };
        rig.joints=new RobotVisualProfileRig.Joint[4];
        for(int i=0;i<4;i++){var b=originals[i];var v=clone(b.visual);v.localRotation=b.visualRest;rig.joints[i]=new RobotVisualProfileRig.Joint{sourcePath=AnimationUtility.CalculateTransformPath(b.visual,src.transform),visual=v,localOffset=v.localPosition,restRotation=b.visualRest,logicalRest=b.logicalRest,axisBasis=b.axisBasis};}
        rig.rightArm=map[src.m0.visual];rig.rightArm.name="RightArm_M0";
        var m1=map[src.m1.visual];var m2=map[src.m2.visual];var m3=map[src.m3.visual];
        Shape("Elbow housing",m1,PrimitiveType.Sphere,Vector3.zero,Vector3.one*.07f,Dark);
        Shape(industrial?"Industrial forearm casing":"Forearm polymer shell",m1,PrimitiveType.Capsule,new Vector3(0,.065f,0),new Vector3(.061f,.057f,.055f),armor);
        Shape("Forearm dorsal inset",m1,PrimitiveType.Cube,new Vector3(0,.070f,-.026f),new Vector3(.03f,.071f,.007f),industrial?Metal:Accent);
        Shape("Wrist pitch hinge",m2,PrimitiveType.Sphere,Vector3.zero,Vector3.one*.046f,Metal);
        Shape("Wrist roll cuff",m3,PrimitiveType.Cylinder,new Vector3(0,.012f,0),new Vector3(.039f,.018f,.039f),Dark);
        rig.toolMount=Node("RightToolMountBinding",m3);var connector=src.m3.visual.Find("M3GripperMountPoint_Visual");rig.toolMount.SetPositionAndRotation(connector.position,Quaternion.LookRotation(src.m3.visual.up,src.m3.visual.forward));
        hand=Node("RightHand_M4_visual_only",m3);rig.hand=hand;
        var palm=Node("Palm shell",hand);palm.SetPositionAndRotation(rig.toolMount.position,rig.toolMount.rotation);Shape("Polymer palm",palm,PrimitiveType.Cube,new Vector3(0,0,.03f),new Vector3(.054f,.035f,.064f),armor);
        var linkage=src.controller.gripperVisual;
        foreach(var jaw in new[]{linkage.leftJawPivot,linkage.rightJawPivot}){
            var pivot=clone(jaw);Shape("Articulated pincer",pivot,PrimitiveType.Cube,new Vector3(0,.22f,0),new Vector3(.105f,.4f,.11f),armor);Shape("Tactile finger pad",pivot,PrimitiveType.Cube,new Vector3(0,.41f,.055f),new Vector3(.11f,.12f,.04f),Dark);
        }
        rig.linkage=map.Where(p=>p.Key!=src.transform&&!originals.Any(b=>b.visual==p.Key)).Select(p=>new RobotVisualProfileRig.Linkage{sourcePath=AnimationUtility.CalculateTransformPath(p.Key,src.transform),visual=p.Value}).ToArray();
        rig.toolShells=BuildToolShells(industrial,m1,m3);
        // Decorative upper arm is fixed: no anatomical shoulder/elbow DOF is invented.
        Vector3 elbow=src.m0.visual.position,shoulder=elbow+Vector3.up*.185f,center=shoulder-Vector3.right*.17f;
        if(industrial)
        {
            var stand=Node("Industrial compact base_fixed",root.transform);
            WorldShape("Angular mounting plate",stand,PrimitiveType.Cube,elbow-Vector3.up*.07f,new Vector3(.17f,.025f,.16f),Dark);
            WorldShape("Rounded drive pedestal",stand,PrimitiveType.Cylinder,elbow-Vector3.up*.029f,new Vector3(.064f,.037f,.064f),IndustrialArmor);
            WorldShape("Pivot casing",stand,PrimitiveType.Sphere,elbow,Vector3.one*.078f,Metal);
            WorldShape("Safety stripe",stand,PrimitiveType.Cube,elbow-Vector3.up*.072f+Vector3.forward*.082f,new Vector3(.12f,.008f,.003f),IndustrialArmor);
        }
        else
        {
        var torso=Node("Torso_fixed_presentation",root.transform);
        WorldShape("Chest graphite core",torso,PrimitiveType.Cube,center-Vector3.up*.05f,new Vector3(.20f,.14f,.10f),Dark);
        WorldShape("Chest polymer armor",torso,PrimitiveType.Sphere,center-Vector3.up*.025f+Vector3.forward*.02f,new Vector3(.27f,.19f,.13f),Shell);
        WorldShape("Chest status panel",torso,PrimitiveType.Cube,center+Vector3.forward*.084f,new Vector3(.086f,.034f,.008f),Accent);
        WorldShape("Waist shell",torso,PrimitiveType.Cube,center-Vector3.up*.18f,new Vector3(.15f,.062f,.09f),Metal);
        WorldShape("Right shoulder housing",torso,PrimitiveType.Sphere,shoulder,Vector3.one*.083f,Shell);
        Bone("Right fixed upper arm",torso,shoulder-Vector3.up*.032f,elbow+Vector3.up*.027f,.065f,Shell);
        Bone("Right upper arm inset",torso,shoulder-Vector3.up*.05f+Vector3.forward*.027f,elbow+Vector3.up*.045f+Vector3.forward*.027f,.016f,Accent);
        var neck=center+Vector3.up*.125f;
        WorldShape("Neck support",torso,PrimitiveType.Cylinder,neck-Vector3.up*.04f,new Vector3(.032f,.065f,.032f),Dark);
        WorldShape("Fixed neck collar",torso,PrimitiveType.Cylinder,neck,new Vector3(.041f,.018f,.041f),Metal);
        WorldShape("Simple robotic head",torso,PrimitiveType.Sphere,neck+Vector3.up*.075f,new Vector3(.12f,.13f,.105f),Shell);
        WorldShape("Head visor",torso,PrimitiveType.Cube,neck+Vector3.up*.08f+Vector3.forward*.055f,new Vector3(.087f,.024f,.011f),Accent);
        if(id==RobotVisualProfileId.Humanoid){
            var left=Node("LeftArm_STATIC_PREVIEW_future_independent_binding",root.transform);rig.leftArmPreview=left;
            Vector3 ls=center+Vector3.Reflect(shoulder-center,rig.previewMirrorAxis.normalized),le=ls-Vector3.up*.185f,lw=le+Vector3.forward*.115f+Vector3.up*.035f;
            WorldShape("Left shoulder housing",left,PrimitiveType.Sphere,ls,Vector3.one*.083f,Shell);Bone("Left upper arm",left,ls-Vector3.up*.032f,le+Vector3.up*.027f,.065f,Shell);
            WorldShape("Left elbow joint",left,PrimitiveType.Sphere,le,Vector3.one*.07f,Dark);Bone("Left forearm",left,le,lw,.061f,Shell);WorldShape("Left wrist",left,PrimitiveType.Sphere,lw,Vector3.one*.045f,Metal);
            var lm=Node("LeftToolMount_PREVIEW_ONLY",left);lm.position=lw;lm.rotation=Quaternion.LookRotation(lw-le);Shape("Left palm",lm,PrimitiveType.Cube,new Vector3(0,0,.033f),new Vector3(.055f,.035f,.053f),Shell);
            foreach(float sign in new[]{-1f,1f})Shape("Left static pincer",lm,PrimitiveType.Cube,new Vector3(sign*.025f,0,.075f),new Vector3(.016f,.019f,.05f),Shell);
        }
        }
        root.transform.SetPositionAndRotation(Vector3.zero,Quaternion.identity);root.transform.localScale=Vector3.one;PrefabUtility.SaveAsPrefabAsset(root,path);Object.DestroyImmediate(root);
    }
    static void Tube(string name,Transform parent,Vector3 a,Vector3 b,float width,Material material)
    {var tube=Shape(name,parent,PrimitiveType.Cylinder,(a+b)*.5f,new Vector3(width,Vector3.Distance(a,b)*.5f,width),material);tube.localRotation=Quaternion.FromToRotation(Vector3.up,b-a);}
    static RobotVisualProfileRig.ToolShell[] BuildToolShells(bool industrial,Transform forearm,Transform wrist)
    {
        var shells=new RobotVisualProfileRig.ToolShell[3];var kinds=new[]{ToolKind.Gripper,ToolKind.Spray,ToolKind.Welding};
        for(int i=0;i<kinds.Length;i++)
        {
            var body=Node(kinds[i]+"_ForearmShell",forearm);var cuff=Node(kinds[i]+"_WristCover",wrist);
            if(kinds[i]==ToolKind.Gripper)
            {
                Shape(industrial?"Compact angular service shroud":"Finger drive sleeve",body,PrimitiveType.Cube,new Vector3(0,.061f,0),new Vector3(.097f,.072f,.068f),industrial?IndustrialArmor:Metal);
                Shape("Pincer drive cover",cuff,PrimitiveType.Cube,new Vector3(0,.023f,0),new Vector3(.082f,.033f,.067f),industrial?Dark:Shell);
            }
            else if(kinds[i]==ToolKind.Spray)
            {
                Shape("Smooth spray shroud",body,PrimitiveType.Capsule,new Vector3(0,.067f,0),new Vector3(.072f,.052f,.067f),industrial?IndustrialBlue:Shell);
                Tube("External blue hose A",body,new Vector3(.050f,.008f,-.025f),new Vector3(.058f,.064f,-.043f),.009f,IndustrialBlue);
                Tube("External blue hose B",body,new Vector3(.058f,.064f,-.043f),new Vector3(.029f,.109f,-.035f),.009f,IndustrialBlue);
                Shape("Spray nozzle guard",cuff,PrimitiveType.Cylinder,new Vector3(0,.012f,0),new Vector3(.045f,.018f,.045f),industrial?Metal:Accent);
            }
            else
            {
                Shape("Welding heat shield",body,PrimitiveType.Cube,new Vector3(0,.068f,-.029f),new Vector3(.094f,.087f,.035f),Dark);
                Shape("Replaceable protective cover",body,PrimitiveType.Cube,new Vector3(0,.057f,-.047f),new Vector3(.078f,.063f,.009f),industrial?IndustrialArmor:Metal);
                Tube("Torch cable A",body,new Vector3(.051f,.003f,.005f),new Vector3(.063f,.06f,-.021f),.012f,Dark);
                Tube("Torch cable B",body,new Vector3(.063f,.06f,-.021f),new Vector3(.03f,.12f,-.019f),.012f,Dark);
                Shape("Wrist thermal ring",cuff,PrimitiveType.Cylinder,new Vector3(0,.012f,0),new Vector3(.048f,.017f,.048f),industrial?IndustrialArmor:Metal);
            }
            shells[i]=new RobotVisualProfileRig.ToolShell{tool=kinds[i],displayName=industrial?new[]{"Compact jaw housing","Smooth spray shell / hose","Heat shield / torch cable"}[i]:new[]{"Robotic hand sleeve","Humanoid spray cuff / hose","Humanoid thermal guard / cable"}[i],roots=new[]{body,cuff}};
        }
        return shells;
    }
}
