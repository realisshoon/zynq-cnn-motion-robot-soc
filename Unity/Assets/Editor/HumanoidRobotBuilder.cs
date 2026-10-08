using System;
using System.IO;
using System.Linq;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

// Demo_01 arm geometry is copied into a new, independent humanoid prefab.
public static class HumanoidRobotBuilder
{
    const string Folder="Assets/Resources/VisualProfiles/HumanoidRobot";
    const string Prefab="Assets/Resources/VisualProfiles/HumanoidRobot.prefab";
    const string SourceScene="Assets/Scenes/Demo_01_HumanoidArm.unity";
    static Material white,graphite,steel,cyan;

    static Material Material(string name,Color color,float metallic,float smoothness)
    {
        string path=Folder+"/"+name+".mat";
        var value=AssetDatabase.LoadAssetAtPath<Material>(path);
        if(value!=null)return value;
        var shader=Shader.Find("Universal Render Pipeline/Lit") ?? Shader.Find("Standard");
        value=new Material(shader){name=name,color=color};
        if(value.HasProperty("_BaseColor"))value.SetColor("_BaseColor",color);
        if(value.HasProperty("_Metallic"))value.SetFloat("_Metallic",metallic);
        if(value.HasProperty("_Smoothness"))value.SetFloat("_Smoothness",smoothness);
        AssetDatabase.CreateAsset(value,path);return value;
    }
    static Transform Node(string name,Transform parent,Vector3 localPosition)
    {
        var t=new GameObject(name).transform;t.SetParent(parent,false);t.localPosition=localPosition;return t;
    }
    static Transform Part(string name,PrimitiveType shape,Transform parent,Vector3 localPosition,Vector3 scale,Material material)
    {
        var t=GameObject.CreatePrimitive(shape).transform;t.name=name;t.SetParent(parent,false);
        t.localPosition=localPosition;t.localScale=scale;
        UnityEngine.Object.DestroyImmediate(t.GetComponent<Collider>());
        t.GetComponent<Renderer>().sharedMaterial=material;return t;
    }
    static Transform Bar(string name,Transform parent,Vector3 a,Vector3 b,float radius,Material material)
    {
        var t=Part(name,PrimitiveType.Cylinder,parent,Vector3.zero,Vector3.one,material);
        t.position=(a+b)*.5f;t.rotation=Quaternion.FromToRotation(Vector3.up,(b-a).normalized);
        float length=(b-a).magnitude;
        float parentScale=Mathf.Max(.0001f,parent.lossyScale.x);
        t.localScale=new Vector3(radius/parentScale,length*.5f/parentScale,radius/parentScale);
        return t;
    }

    static Mesh TorsoMesh()
    {
        // Small chamfered tapered shell; the original mechanical arms are separate children.
        float[] px={-.34f,.34f,.38f,.38f,.30f,-.30f,-.38f,-.38f};
        float[] py={.29f,.29f,.23f,-.18f,-.31f,-.31f,-.18f,.23f};
        var vertices=new Vector3[18];vertices[0]=new Vector3(0,0,-.16f);
        vertices[9]=new Vector3(0,0,.08f);
        for(int i=0;i<8;i++){vertices[i+1]=new Vector3(px[i],py[i],-.16f);vertices[i+10]=new Vector3(px[i]*.87f,py[i]*.93f,.08f);}
        var triangles=new int[8*12];int n=0;
        for(int i=0;i<8;i++)
        {
            int next=(i+1)%8;
            triangles[n++]=0;triangles[n++]=i+1;triangles[n++]=next+1;
            triangles[n++]=9;triangles[n++]=next+10;triangles[n++]=i+10;
            triangles[n++]=i+1;triangles[n++]=next+10;triangles[n++]=next+1;
            triangles[n++]=i+1;triangles[n++]=i+10;triangles[n++]=next+10;
        }
        var mesh=new Mesh{name="Humanoid compact tapered torso shell"};mesh.vertices=vertices;mesh.triangles=triangles;mesh.RecalculateNormals();mesh.RecalculateBounds();return mesh;
    }
    static void BuildBody(HumanoidVisualRig rig)
    {
        var body=Node("BodyRoot",rig.transform,Vector3.zero);rig.bodyRoot=body;
        HumanoidVisualGeometry.Box("rear extruded aluminum lift rail",body,new Vector3(0,.89f,.24f),new Vector3(.115f,1.68f,.11f),HumanoidVisualGeometry.Aluminum);
        HumanoidVisualGeometry.Box("rail black center channel",body,new Vector3(0,.89f,.178f),new Vector3(.06f,1.62f,.012f),HumanoidVisualGeometry.Rubber);
        for(int sign=-1;sign<=1;sign+=2)
        {
            HumanoidVisualGeometry.Box("rail side guide",body,new Vector3(sign*.075f,.89f,.24f),new Vector3(.024f,1.64f,.13f),HumanoidVisualGeometry.Steel);
            for(int k=0;k<7;k++)
                HumanoidVisualGeometry.Cylinder("rail fastener",body,new Vector3(sign*.075f,.31f+k*.19f,.165f),
                    new Vector3(.013f,.006f,.013f),HumanoidVisualGeometry.Fastener).localRotation=Quaternion.Euler(90,0,0);
        }
        var torso=Node("Torso",body,new Vector3(0,1.29f,0));rig.torso=torso;
        HumanoidVisualGeometry.Box("black structural torso frame",torso,new Vector3(0,0,.065f),
            new Vector3(.64f,.54f,.30f),HumanoidVisualGeometry.Black);
        HumanoidVisualGeometry.Part("painted curved front chest shell",HumanoidVisualGeometry.TorsoMesh,
            torso,Vector3.zero,Vector3.one,HumanoidVisualGeometry.White);
        for(int sign=-1;sign<=1;sign+=2)
        {
            HumanoidVisualGeometry.Box("split side access panel",torso,new Vector3(sign*.34f,-.035f,.026f),
                new Vector3(.052f,.46f,.23f),HumanoidVisualGeometry.Polymer);
            HumanoidVisualGeometry.Box("painted shoulder interface",body,new Vector3(sign*.39f,1.46f,-.095f),
                new Vector3(.16f,.18f,.21f),HumanoidVisualGeometry.Polymer);
            for(int k=0;k<3;k++)
                HumanoidVisualGeometry.Cylinder("torso panel fastener",torso,
                    new Vector3(sign*.34f,-.16f+k*.16f,-.058f),new Vector3(.007f,.004f,.007f),
                    HumanoidVisualGeometry.Fastener).localRotation=Quaternion.Euler(0,0,90);
        }
        HumanoidVisualGeometry.Box("chest upper seam",torso,new Vector3(0,.265f,-.115f),
            new Vector3(.48f,.013f,.035f),HumanoidVisualGeometry.Aluminum);
        BuildBrand(torso);
        var neck=Node("Neck",body,new Vector3(0,1.64f,.025f));
        HumanoidVisualGeometry.Cylinder("neck rotation collar",neck,Vector3.zero,
            new Vector3(.095f,.038f,.095f),HumanoidVisualGeometry.Black);
        HumanoidVisualGeometry.Cylinder("neck polished bearing",neck,new Vector3(0,.054f,0),
            new Vector3(.073f,.012f,.073f),HumanoidVisualGeometry.Steel);
        var head=Node("Head",neck,new Vector3(0,.17f,-.035f));rig.head=head;
        HumanoidVisualGeometry.Box("white rounded sensor shell",head,Vector3.zero,
            new Vector3(.275f,.185f,.215f),HumanoidVisualGeometry.White);
        HumanoidVisualGeometry.Box("sensor face bezel",head,new Vector3(0,-.003f,-.110f),
            new Vector3(.229f,.123f,.028f),HumanoidVisualGeometry.Aluminum);
        HumanoidVisualGeometry.Box("black glass visor",head,new Vector3(0,-.003f,-.127f),
            new Vector3(.214f,.108f,.019f),HumanoidVisualGeometry.Visor);
        for(int sign=-1;sign<=1;sign+=2)
        {
            HumanoidVisualGeometry.Box("head side status light",head,new Vector3(sign*.133f,.020f,-.025f),
                new Vector3(.006f,.008f,.073f),HumanoidVisualGeometry.Cyan);
            HumanoidVisualGeometry.Cylinder("head side joint",head,new Vector3(sign*.133f,-.045f,.035f),
                new Vector3(.027f,.008f,.027f),HumanoidVisualGeometry.Polymer).localRotation=Quaternion.Euler(0,0,90);
        }
    }

    static void BuildBrand(Transform torso)
    {
        var texture=AssetDatabase.LoadAssetAtPath<Texture2D>(Folder+"/UmiUmBrand.png");
        if(texture==null)throw new Exception("Missing Korean torso marking UmiUmBrand.png");
        string path=Folder+"/HumanoidBrand.mat";
        var material=AssetDatabase.LoadAssetAtPath<Material>(path);
        if(material==null)
        {
            material=new Material(Shader.Find("Universal Render Pipeline/Unlit")??Shader.Find("Unlit/Texture"))
                {name="HumanoidBrand"};
            AssetDatabase.CreateAsset(material,path);
        }
        if(material.HasProperty("_BaseMap"))material.SetTexture("_BaseMap",texture);
        if(material.HasProperty("_MainTex"))material.SetTexture("_MainTex",texture);
        if(material.HasProperty("_Cull"))material.SetInt("_Cull",0);
        if(material.HasProperty("_Surface"))material.SetFloat("_Surface",1f);
        if(material.HasProperty("_SrcBlend"))material.SetFloat("_SrcBlend",5f);
        if(material.HasProperty("_DstBlend"))material.SetFloat("_DstBlend",10f);
        if(material.HasProperty("_ZWrite"))material.SetFloat("_ZWrite",0f);
        material.EnableKeyword("_SURFACE_TYPE_TRANSPARENT");
        material.renderQueue=3000;
        EditorUtility.SetDirty(material);
        var quad=GameObject.CreatePrimitive(PrimitiveType.Quad);
        quad.name="움이움 torso branding";quad.transform.SetParent(torso,false);
        quad.transform.localPosition=new Vector3(0,-.19f,-.222f);
        quad.transform.localRotation=Quaternion.Euler(0,180,0);
        // The torso front faces -Z. Rotating the front-facing quad by 180 degrees
        // mirrors its UVs, so compensate on the visual quad's X scale.
        quad.transform.localScale=new Vector3(-.315f,.080f,1);
        UnityEngine.Object.DestroyImmediate(quad.GetComponent<Collider>());
        quad.GetComponent<Renderer>().sharedMaterial=material;
    }

    static Transform WrapChildren(Transform original,string name)
    {
        var children=original.Cast<Transform>().ToArray();
        var wrapper=Node(name,original,Vector3.zero);
        foreach(var child in children)child.SetParent(wrapper,true);
        return wrapper;
    }

    static HumanoidVisualRig.Arm BuildArm(Transform sourceArm,Transform parent,string side,Scene destination)
    {
        var originalBase=sourceArm.Find("Base_Yaw");
        if(originalBase==null)throw new Exception("Demo_01 "+side+" Base_Yaw unavailable");
        var cloned=UnityEngine.Object.Instantiate(sourceArm.gameObject);
        if(cloned.scene!=destination)SceneManager.MoveGameObjectToScene(cloned,destination);
        cloned.name=side+"ArmSourceFrame_Demo01";cloned.transform.SetParent(parent,false);
        cloned.transform.localPosition=new Vector3(0,.15f,0);
        cloned.transform.localRotation=Quaternion.Euler(0,180f,0)*sourceArm.localRotation;
        cloned.transform.localScale=Vector3.one*2.30f;
        var yaw=cloned.transform.Find("Base_Yaw");
        foreach(var child in cloned.transform.Cast<Transform>().ToArray())
            if(child!=yaw)UnityEngine.Object.DestroyImmediate(child.gameObject);
        foreach(var behaviour in cloned.GetComponentsInChildren<MonoBehaviour>(true)
            .Where(item=>!(item is G51GripperVisual))
            .OrderBy(item=>item.GetType().Name=="RobotArmController"?1:0))
            UnityEngine.Object.DestroyImmediate(behaviour);
        if(cloned.GetComponentsInChildren<MonoBehaviour>(true).Any(item=>!(item is G51GripperVisual)))
            throw new Exception("Demo_01 source control component remained in visual prefab");
        foreach(var collider in cloned.GetComponentsInChildren<Collider>(true))UnityEngine.Object.DestroyImmediate(collider);
        foreach(var renderer in cloned.GetComponentsInChildren<Renderer>(true))
        {
            string name=renderer.name.ToLowerInvariant();
            Material material=name.Contains("bolt")||name.Contains("gear")?HumanoidVisualGeometry.Fastener:
                name.Contains("shaft")||name.Contains("hub")?HumanoidVisualGeometry.Steel:
                name.Contains("link")||name.Contains("bracket")?HumanoidVisualGeometry.Aluminum:
                name.Contains("servo")?HumanoidVisualGeometry.Polymer:
                name.Contains("finger")||name.Contains("jaw")?HumanoidVisualGeometry.Polymer:
                HumanoidVisualGeometry.Black;
            renderer.sharedMaterials=renderer.sharedMaterials.Select(_=>material).ToArray();
        }

        var pitch=yaw.Find("Shoulder_Pitch");var elbow=pitch?.Find("Elbow_Pitch");
        var wristPitch=elbow?.Find("Wrist_Pitch");var wristRoll=wristPitch?.Find("Wrist_Roll");
        var grip=wristRoll?.Find("Gripper");
        if(pitch==null||elbow==null||wristPitch==null||wristRoll==null||grip==null)
            throw new Exception("Demo_01 "+side+" joint hierarchy incomplete");
        yaw.name=side+"ShoulderYaw";pitch.name=side+"ShoulderPitch";
        elbow.name=side+"ElbowPitch";wristPitch.name=side+"WristPitch";
        wristRoll.name=side+"WristRoll";grip.name=side+"Gripper";

        var shoulderRoll=WrapChildren(pitch,side+"ShoulderRoll");
        var upper=WrapChildren(shoulderRoll,side+"UpperArm");
        var elbowRoll=WrapChildren(elbow,side+"ElbowRoll");
        var forearm=WrapChildren(elbowRoll,side+"Forearm");
        var tool=Node(side+"ToolMount",wristRoll,Vector3.zero);
        tool.SetPositionAndRotation(grip.position,grip.rotation);
        grip.SetParent(tool,true);

        int sign=side=="Left"?-1:1;
        // Keep the Demo_01 control pivots. Capture a visual rest pose facing the work envelope.
        // Demo_01 left wrist bind has an additional outward skew; remove it in visual rest only.
        yaw.localRotation*=Quaternion.AngleAxis(side=="Left"?115f:-90f,Vector3.up);
        pitch.localRotation*=Quaternion.AngleAxis(20f,Vector3.right);
        elbow.localRotation*=Quaternion.AngleAxis(26f,Vector3.right);
        wristPitch.localRotation*=Quaternion.AngleAxis(-10f,Vector3.right);

        Vector3 s=pitch.position,e=elbow.position,w=wristPitch.position;
        Vector3 upperDir=(e-s).normalized,foreDir=(w-e).normalized;
        HumanoidVisualGeometry.Rod(side+" structural upper arm",upper,s+upperDir*.035f,e-upperDir*.035f,
            .070f,HumanoidVisualGeometry.Black);
        HumanoidVisualGeometry.Rod(side+" upper arm painted metal sleeve",upper,Vector3.Lerp(s,e,.21f),
            Vector3.Lerp(s,e,.77f),.091f,HumanoidVisualGeometry.Black);
        HumanoidVisualGeometry.Rod(side+" upper arm raised polymer panel",upper,
            Vector3.Lerp(s,e,.30f)+Vector3.back*.067f,Vector3.Lerp(s,e,.70f)+Vector3.back*.067f,
            .023f,HumanoidVisualGeometry.Polymer);
        HumanoidVisualGeometry.Rod(side+" upper arm cable",upper,
            s+Vector3.back*.105f,e+Vector3.back*.105f,.011f,HumanoidVisualGeometry.Rubber);
        HumanoidVisualGeometry.Rod(side+" structural forearm",forearm,e+foreDir*.028f,w-foreDir*.028f,
            .061f,HumanoidVisualGeometry.Black);
        HumanoidVisualGeometry.Rod(side+" forearm anodized sleeve",forearm,
            Vector3.Lerp(e,w,.18f),Vector3.Lerp(e,w,.78f),.078f,HumanoidVisualGeometry.Black);
        HumanoidVisualGeometry.Rod(side+" forearm metal spine",forearm,
            Vector3.Lerp(e,w,.25f)+Vector3.back*.057f,Vector3.Lerp(e,w,.70f)+Vector3.back*.057f,
            .018f,HumanoidVisualGeometry.Aluminum);
        HumanoidVisualGeometry.Rod(side+" forearm cable",forearm,
            e+Vector3.back*.092f,w+Vector3.back*.092f,.009f,HumanoidVisualGeometry.Rubber);
        HumanoidVisualGeometry.Flange(side+" shoulder",yaw,s,.125f,.075f,sign);
        HumanoidVisualGeometry.Flange(side+" elbow",elbow,e,.102f,.058f,sign);
        HumanoidVisualGeometry.Flange(side+" wrist pitch",wristPitch,w,.074f,.048f,sign);
        var collar=HumanoidVisualGeometry.WorldPart(side+" wrist roll collar",HumanoidVisualGeometry.CylinderMesh,
            wristRoll,w+foreDir*.024f,new Vector3(.064f,.023f,.064f),HumanoidVisualGeometry.Aluminum);
        collar.rotation=Quaternion.FromToRotation(Vector3.up,foreDir);
        var flange=HumanoidVisualGeometry.WorldPart(side+" tool mounting flange",HumanoidVisualGeometry.CylinderMesh,
            tool,tool.position,new Vector3(.056f,.012f,.056f),HumanoidVisualGeometry.Steel);
        flange.rotation=Quaternion.FromToRotation(Vector3.up,foreDir);

        var gripper=grip.GetComponent<G51GripperVisual>();
        if(gripper==null||gripper.leftJawPivot==null||gripper.rightJawPivot==null)
            throw new Exception("Demo_01 "+side+" mechanical gripper unavailable");
        return new HumanoidVisualRig.Arm{
            shoulderRoot=parent,shoulderYaw=yaw,shoulderPitch=pitch,shoulderRoll=shoulderRoll,
            upperArm=upper,elbowPitch=elbow,elbowRoll=elbowRoll,forearm=forearm,
            wristPitch=wristPitch,wristRoll=wristRoll,toolMount=tool,gripper=grip,gripperVisual=gripper
        };
    }

    [MenuItem("Tools/Humanoid/Build Industrial Visual")]
    public static void BuildAssets()
    {
        Directory.CreateDirectory(Folder);
        HumanoidVisualGeometry.Initialize();
        white=Material("HumanoidWhiteShell",new Color(.88f,.91f,.92f),.12f,.62f);
        if(white.HasProperty("_Cull")){white.SetInt("_Cull",0);EditorUtility.SetDirty(white);}
        graphite=Material("HumanoidGraphite",new Color(.085f,.092f,.105f),.28f,.49f);
        steel=Material("HumanoidBrushedSteel",new Color(.58f,.63f,.67f),.75f,.68f);
        cyan=Material("HumanoidSensorCyan",new Color(.22f,.86f,.94f),.12f,.60f);
        Scene destination=SceneManager.GetActiveScene();
        var preview=EditorSceneManager.OpenPreviewScene(SourceScene);
        GameObject root=null;
        try
        {
            var source=preview.GetRootGameObjects().Single(x=>x.name=="DualRobotSystem").transform;
            root=new GameObject("HumanoidRobotRoot");
            if(root.scene!=destination)SceneManager.MoveGameObjectToScene(root,destination);
            var rig=root.AddComponent<HumanoidVisualRig>();
            BuildBody(rig);
            var rim=new GameObject("Humanoid presentation rim light");
            rim.transform.SetParent(root.transform,false);
            rim.transform.rotation=Quaternion.Euler(22f,175f,0);
            var rimLight=rim.AddComponent<Light>();rimLight.type=LightType.Directional;
            rimLight.color=new Color(.72f,.85f,1f);rimLight.intensity=.38f;
            rimLight.shadows=LightShadows.None;
            var left=Node("LeftShoulderRoot",root.transform,new Vector3(-.43f,1.44f,-.20f));
            var right=Node("RightShoulderRoot",root.transform,new Vector3(.43f,1.44f,-.20f));
            rig.left=BuildArm(source.Find("RobotArm_L"),left,"Left",destination);
            rig.right=BuildArm(source.Find("RobotArm_R"),right,"Right",destination);
            rig.left.toolMount.rotation=rig.right.toolMount.rotation;
            rig.CaptureRestPose();rig.ResetToRest();
            if(!rig.IsConfigured)throw new Exception("Humanoid joint hierarchy is not configured");
            PrefabUtility.SaveAsPrefabAsset(root,Prefab);
        }
        finally
        {
            if(root!=null)UnityEngine.Object.DestroyImmediate(root);
            EditorSceneManager.ClosePreviewScene(preview);
        }
        AssetDatabase.SaveAssets();AssetDatabase.Refresh();
    }
}
