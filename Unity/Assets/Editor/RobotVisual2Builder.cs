using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

// Copies the actual scene meshes and materials. The extra plates below are Tool covers, never replacement arms.
public static class RobotVisual2Builder
{
    const string Folder="Assets/Resources/VisualProfiles";
    const string HumanoidScene="Assets/Scenes/Demo_01_HumanoidArm.unity";
    static Material steel,dark,light,blue,amber;

    public static void BuildAssets()
    {
        var source=UnityEngine.Object.FindFirstObjectByType<Demo06RealisticVisualAdapter>();
        if(source==null||!source.IsConfigured)throw new Exception("Demo_07 G51 visual source is unavailable");
        steel=Required<Material>("Assets/Validation/Demo06RealisticVisuals/MAT_MetalSilver.mat");
        dark=Required<Material>("Assets/Validation/Demo06RealisticVisuals/MAT_RobotMatteBlack.mat");
        light=Required<Material>("Assets/Materials/G51Generated/G51_Silver_v3.mat");
        blue=Required<Material>("Assets/Resources/VisualProfiles/SprayHoseBlue.mat");
        amber=Required<Material>("Assets/Resources/VisualProfiles/IndustrialAmber.mat");
        Directory.CreateDirectory(Folder);
        BuildDual(source);
        var preview=EditorSceneManager.OpenPreviewScene(HumanoidScene);
        try{
            var system=preview.GetRootGameObjects().Single(x=>x.name=="DualRobotSystem");
            BuildHumanoid(source,system);
        }finally{EditorSceneManager.ClosePreviewScene(preview);}
        AssetDatabase.SaveAssets();AssetDatabase.Refresh();
    }

    static T Required<T>(string path) where T:UnityEngine.Object
    {var asset=AssetDatabase.LoadAssetAtPath<T>(path);if(asset==null)throw new Exception("Missing source asset: "+path);return asset;}

    static GameObject Root(string name,Demo06RealisticVisualAdapter source)
    {
        var root=new GameObject(name);
        root.transform.SetPositionAndRotation(source.transform.position,source.transform.rotation);
        root.transform.localScale=source.transform.lossyScale;
        root.AddComponent<RobotVisualProfileRig>();return root;
    }

    static GameObject CopyArm(GameObject source,Transform parent,bool keepRightGripper)
    {
        var clone=UnityEngine.Object.Instantiate(source);
        if(clone.scene!=SceneManager.GetActiveScene())SceneManager.MoveGameObjectToScene(clone,SceneManager.GetActiveScene());
        clone.transform.SetParent(parent,true);
        var right=clone.transform.Find("RobotArm_R");
        foreach(var behaviour in clone.GetComponentsInChildren<MonoBehaviour>(true)){
            if(keepRightGripper&&behaviour is G51GripperVisual&&right!=null&&behaviour.transform.IsChildOf(right))continue;
            UnityEngine.Object.DestroyImmediate(behaviour);
        }
        foreach(var collider in clone.GetComponentsInChildren<Collider>(true))UnityEngine.Object.DestroyImmediate(collider);
        return clone;
    }

    static Transform Node(string name,Transform parent)
    {var t=new GameObject(name).transform;t.SetParent(parent,false);return t;}

    static Transform Box(string name,Transform parent,Vector3 position,Vector3 size,Material material)
    {
        var t=GameObject.CreatePrimitive(PrimitiveType.Cube).transform;t.name=name;t.SetParent(parent,false);
        t.localPosition=position;t.localScale=size;
        UnityEngine.Object.DestroyImmediate(t.GetComponent<Collider>());
        t.GetComponent<Renderer>().sharedMaterial=material;return t;
    }

    static Transform WorldBox(string name,Transform parent,Vector3 position,Vector3 size,Material material)
    {var t=Box(name,parent,Vector3.zero,size,material);t.position=position;t.rotation=Quaternion.identity;return t;}

    static string IndexPath(Transform ancestor,Transform child)
    {
        var indexes=new List<int>();
        while(child!=ancestor){if(child==null)throw new Exception("Source transform is outside adapter");indexes.Add(child.GetSiblingIndex());child=child.parent;}
        indexes.Reverse();return string.Join("/",indexes);
    }

    static Transform ByIndex(Transform root,string path)
    {foreach(var part in path.Split('/'))root=root.GetChild(int.Parse(part));return root;}

    static Transform CopyOf(Demo06RealisticVisualAdapter source,Transform clone,Transform original)
    {return ByIndex(clone,IndexPath(source.transform,original));}

    static Transform G51Hand(Demo06RealisticVisualAdapter source,Transform clone)
    {
        var hand=source.m3.visual.Find("M3GripperMountPoint_Visual/GripperVisualMount_Visual");
        if(hand==null)throw new Exception("Verified G51 gripper source unavailable");
        return CopyOf(source,clone,hand);
    }

    static G51GripperVisual CopyG51Grip(Demo06RealisticVisualAdapter source,Transform clone,Transform hand)
    {
        var originalHand=source.m3.visual.Find("M3GripperMountPoint_Visual/GripperVisualMount_Visual");
        var original=source.transform.root.GetComponentsInChildren<G51GripperVisual>(true)
            .FirstOrDefault(x=>x.leftJawPivot!=null&&x.leftJawPivot.IsChildOf(originalHand));
        if(original==null)throw new Exception("Source G51 linkage controller for the verified hand is unavailable");
        var visual=hand.gameObject.AddComponent<G51GripperVisual>();
        EditorUtility.CopySerialized(original,visual);
        visual.leftJawPivot=CopyOf(source,clone,original.leftJawPivot);
        visual.rightJawPivot=CopyOf(source,clone,original.rightJawPivot);
        visual.leftJawLinkAnchor=CopyOf(source,clone,original.leftJawLinkAnchor);
        visual.rightJawLinkAnchor=CopyOf(source,clone,original.rightJawLinkAnchor);
        visual.leftGear=CopyOf(source,clone,original.leftGear);
        visual.rightGear=CopyOf(source,clone,original.rightGear);
        visual.leftDrivePin=CopyOf(source,clone,original.leftDrivePin);
        visual.rightDrivePin=CopyOf(source,clone,original.rightDrivePin);
        visual.leftLinkBar=CopyOf(source,clone,original.leftLinkBar);
        visual.rightLinkBar=CopyOf(source,clone,original.rightLinkBar);
        visual.visualRoot=original.visualRoot==null?null:CopyOf(source,clone,original.visualRoot);
        return visual;
    }

    static Transform G51Mount(Demo06RealisticVisualAdapter source,Transform clone,string name)
    {
        var m3=CopyOf(source,clone,source.m3.visual);
        var connector=source.m3.visual.Find("M3GripperMountPoint_Visual");
        if(connector==null)throw new Exception("Verified G51 ToolMount connector unavailable");
        var mount=Node(name,m3);
        var relative=source.m3.visual.InverseTransformPoint(connector.position);
        mount.localPosition=relative;
        mount.rotation=Quaternion.LookRotation(m3.up,m3.forward);
        return mount;
    }

    static void BindG51(RobotVisualProfileRig rig,Demo06RealisticVisualAdapter source,Transform right)
    {
        var original=new[]{source.m0,source.m1,source.m2,source.m3};
        rig.joints=new RobotVisualProfileRig.Joint[4];var jointSources=new HashSet<Transform>();
        for(int i=0;i<4;i++){
            var s=original[i];var visual=CopyOf(source,right,s.visual);jointSources.Add(s.visual);
            rig.joints[i]=new RobotVisualProfileRig.Joint{
                sourcePath=IndexPath(source.transform,s.visual),visual=visual,localOffset=visual.localPosition,
                restRotation=s.visualRest,logicalRest=s.logicalRest,axisBasis=s.axisBasis};
        }
        var links=new List<RobotVisualProfileRig.Linkage>();
        foreach(var part in source.GetComponentsInChildren<Transform>(true)){
            if(part==source.transform||jointSources.Contains(part))continue;
            string path=IndexPath(source.transform,part);
            links.Add(new RobotVisualProfileRig.Linkage{sourceIndexPath=path,visual=ByIndex(right,path)});
        }
        rig.linkage=links.ToArray();
    }

    static Transform Child(Transform parent,string path)
    {var result=parent.Find(path);if(result==null)throw new Exception("Source arm child missing: "+path);return result;}

    static void Save(GameObject root,RobotVisualProfileId id,string sourcePath)
    {
        string prefabPath=Folder+"/"+id+".prefab";
        root.transform.SetPositionAndRotation(Vector3.zero,Quaternion.identity);root.transform.localScale=Vector3.one;
        PrefabUtility.SaveAsPrefabAsset(root,prefabPath);
        UnityEngine.Object.DestroyImmediate(root);
        string definitionPath=Folder+"/"+id+".asset";
        var definition=AssetDatabase.LoadAssetAtPath<RobotVisualProfileDefinition>(definitionPath);
        if(definition==null){definition=ScriptableObject.CreateInstance<RobotVisualProfileDefinition>();AssetDatabase.CreateAsset(definition,definitionPath);}
        definition.id=id;definition.displayName=id==RobotVisualProfileId.MechanicalDualTable?"G51 Source Dual Table":"Demo_01 Source Humanoid";
        definition.sourceAssetPath=sourcePath;definition.rightArmLive=true;definition.leftArmPreview=true;
        definition.prefab=Required<GameObject>(prefabPath).GetComponent<RobotVisualProfileRig>();EditorUtility.SetDirty(definition);
    }

    static void BuildDual(Demo06RealisticVisualAdapter source)
    {
        var root=Root("MechanicalDualTable",source);var rig=root.GetComponent<RobotVisualProfileRig>();
        var right=CopyArm(source.gameObject,root.transform,true);right.name="RightArm_ACTIVE_Demo07G51Source";
        right.transform.localPosition=Vector3.zero;right.transform.localRotation=Quaternion.identity;right.transform.localScale=Vector3.one;
        var left=CopyArm(source.gameObject,root.transform,true);left.name="LeftArm_SCRIPTED_HANDLER_Demo07G51Source";
        left.transform.localPosition=Vector3.right*.55f;left.transform.localRotation=Quaternion.identity;left.transform.localScale=Vector3.one;
        rig.rightArm=right.transform;rig.leftArmPreview=left.transform;BindG51(rig,source,right.transform);
        rig.sourceRightRendererCount=right.GetComponentsInChildren<Renderer>(true).Length;
        rig.sourceLeftRendererCount=left.GetComponentsInChildren<Renderer>(true).Length;
        rig.hand=G51Hand(source,right.transform);var leftHand=G51Hand(source,left.transform);rig.leftHandlerHand=leftHand;
        rig.sourceGripperVisual=CopyG51Grip(source,right.transform,rig.hand);
        rig.leftHandlerGripperVisual=CopyG51Grip(source,left.transform,leftHand);
        rig.toolMount=G51Mount(source,right.transform,"RightVisualToolMount");
        var leftMount=G51Mount(source,left.transform,"LeftHandlerGrip_VISUAL_ONLY");rig.leftHandlerGrip=leftMount;
        rig.leftFeederJoints=new[]{CopyOf(source,left.transform,source.m0.visual),CopyOf(source,left.transform,source.m1.visual),CopyOf(source,left.transform,source.m2.visual),CopyOf(source,left.transform,source.m3.visual)};
        var rightForearm=CopyOf(source,right.transform,source.m1.visual);var rightWrist=CopyOf(source,right.transform,source.m3.visual);
        var leftForearm=CopyOf(source,left.transform,source.m1.visual);var leftWrist=CopyOf(source,left.transform,source.m3.visual);
        rig.toolShells=Shells(rightForearm,rightWrist,leftForearm,leftWrist,leftMount,leftHand,true);
        var cell=Node("G51 source dual table / fixed frame",root.transform);
        var a=source.m0.visual.position;var b=a+root.transform.right*.55f;var center=(a+b)*.5f;
        WorldBox("Shared metal workplate",cell,center+Vector3.forward*.13f-Vector3.up*.12f,new Vector3(.68f,.018f,.28f),steel);
        WorldBox("Front frame rail",cell,center+Vector3.forward*.26f-Vector3.up*.20f,new Vector3(.74f,.025f,.025f),dark);
        WorldBox("Rear frame rail",cell,center-Vector3.forward*.02f-Vector3.up*.20f,new Vector3(.74f,.025f,.025f),dark);
        Save(root,RobotVisualProfileId.MechanicalDualTable,"Assets/Scenes/Demo_07_SingleArmControl.unity : Demo06RealisticVisualRoot");
    }

    static void BuildHumanoid(Demo06RealisticVisualAdapter source,GameObject demo01)
    {
        var root=Root("MechanicalHumanoid",source);var rig=root.GetComponent<RobotVisualProfileRig>();
        var group=CopyArm(demo01,root.transform,true);group.name="Demo01_Source_DualRobotSystem";
        foreach(string obsolete in new[]{"DualArmDemoController","RedBall","HandoffPosition"}){
            var node=group.transform.Find(obsolete);if(node!=null)UnityEngine.Object.DestroyImmediate(node.gameObject);
        }
        var right=Child(group.transform,"RobotArm_R");var left=Child(group.transform,"RobotArm_L");
        var rightShoulder=Child(right,"Base_Yaw/Shoulder_Pitch");
        var rightHand=Child(right,"Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch/Wrist_Roll/Gripper");
        var leftHand=Child(left,"Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch/Wrist_Roll/Gripper");
        var connector=source.m3.visual.Find("M3GripperMountPoint_Visual");
        Vector3 targetShoulder=source.m0.visual.position+Vector3.up*.20f;
        float originalSpan=Vector3.Distance(rightShoulder.position,rightHand.position);
        if(originalSpan<.001f||connector==null)throw new Exception("Demo_01 arm / Demo_07 ToolMount cannot be aligned");
        float targetSpan=Vector3.Distance(targetShoulder,connector.position);
        // Uniform visual scale keeps every Demo_01 arm part and proportion intact.
        group.transform.localScale*=Mathf.Max(targetSpan/originalSpan,.45f);
        group.transform.position+=targetShoulder-rightShoulder.position;
        rig.rightArm=right;rig.leftArmPreview=left;rig.hand=rightHand;
        rig.sourceRightRendererCount=right.GetComponentsInChildren<Renderer>(true).Length;
        rig.sourceLeftRendererCount=left.GetComponentsInChildren<Renderer>(true).Length;
        rig.sourceGripperVisual=rightHand.GetComponent<G51GripperVisual>();
        if(rig.sourceGripperVisual==null)throw new Exception("Demo_01 right G51 gripper visual missing");
        var original=new[]{source.m0,source.m1,source.m2,source.m3};
        var mapped=new[]{Child(right,"Base_Yaw"),Child(right,"Base_Yaw/Shoulder_Pitch/Elbow_Pitch"),
            Child(right,"Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch"),Child(right,"Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch/Wrist_Roll")};
        rig.joints=new RobotVisualProfileRig.Joint[4];
        for(int i=0;i<4;i++)rig.joints[i]=new RobotVisualProfileRig.Joint{
            sourcePath="Demo_01/RobotArm_R/"+mapped[i].name,visual=mapped[i],localOffset=mapped[i].localPosition,
            restRotation=mapped[i].localRotation,logicalRest=original[i].logicalRest,axisBasis=original[i].axisBasis};
        rig.linkage=Array.Empty<RobotVisualProfileRig.Linkage>();
        var headOrientation=Quaternion.LookRotation(source.m3.visual.up,source.m3.visual.forward);
        var rightMount=Node("RightVisualToolMount",rightHand.parent);rightMount.SetPositionAndRotation(rightHand.position,headOrientation);
        var leftMount=Node("LeftToolMount_PREVIEW_ONLY",leftHand.parent);leftMount.SetPositionAndRotation(leftHand.position,headOrientation);
        rig.toolMount=rightMount;
        var rightForearm=Child(right,"Base_Yaw/Shoulder_Pitch/Elbow_Pitch");var rightWrist=Child(right,"Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch/Wrist_Roll");
        var leftForearm=Child(left,"Base_Yaw/Shoulder_Pitch/Elbow_Pitch");var leftWrist=Child(left,"Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch/Wrist_Roll");
        rig.toolShells=Shells(rightForearm,rightWrist,leftForearm,leftWrist,leftMount,leftHand,false);
        // Preserve Demo_01's CenterFrame and retain only a mechanical chest/shoulder frame.
        var frame=Child(group.transform,"CenterFrame");var leftShoulder=Child(left,"Base_Yaw/Shoulder_Pitch");
        var mid=(rightShoulder.position+leftShoulder.position)*.5f;
        WorldBox("Added chest service plate / Demo01 frame",frame,mid-Vector3.up*.10f+Vector3.forward*.018f,new Vector3(.43f,.30f,.055f),steel);
        WorldBox("Added chest central recess",frame,mid-Vector3.up*.08f+Vector3.forward*.036f,new Vector3(.16f,.12f,.013f),dark);
        Save(root,RobotVisualProfileId.MechanicalHumanoid,"Assets/Scenes/Demo_01_HumanoidArm.unity : DualRobotSystem/RobotArm_L + RobotArm_R + CenterFrame");
    }

    static RobotVisualProfileRig.ToolShell[] Shells(Transform rightForearm,Transform rightWrist,Transform leftForearm,Transform leftWrist,Transform leftMount,Transform leftHand,bool handler)
    {
        var kinds=new[]{ToolKind.Gripper,ToolKind.Spray,ToolKind.Welding,ToolKind.Nailing};
        var shells=new RobotVisualProfileRig.ToolShell[kinds.Length];
        for(int i=0;i<kinds.Length;i++){
            var kind=kinds[i];Material material=kind==ToolKind.Spray?blue:kind==ToolKind.Welding?dark:kind==ToolKind.Nailing?amber:steel;
            var roots=new List<Transform>();
            var pairs=handler?new[]{new[]{rightForearm,rightWrist}}:new[]{new[]{rightForearm,rightWrist},new[]{leftForearm,leftWrist}};
            foreach(var pair in pairs){
                var cover=Node(kind+"_SourceArmServiceCover",pair[0]);
                Box("Tool-family forearm accent",cover,new Vector3(0,.07f,-.045f),new Vector3(.075f,.065f,.009f),material);
                var front=Node(kind+"_SourceArmFrontGuard",pair[1]);
                Box("Tool-family front guard",front,new Vector3(0,0,.038f),new Vector3(.073f,.060f,.012f),material);
                roots.Add(cover);roots.Add(front);
            }
            if(!handler&&kind==ToolKind.Gripper)roots.Add(leftHand);
            else if(!handler){
                string toolPath="Assets/Resources/Tool1/"+(kind==ToolKind.Welding?"WeldingTorch":kind==ToolKind.Nailing?"NailingHead":kind.ToString())+".prefab";
                var prefab=Required<GameObject>(toolPath);
                var visual=Child(prefab.transform,kind==ToolKind.Nailing?"Mechanical fastening head":"ToolVisual");
                var head=UnityEngine.Object.Instantiate(visual.gameObject).transform;
                if(head.gameObject.scene!=SceneManager.GetActiveScene())SceneManager.MoveGameObjectToScene(head.gameObject,SceneManager.GetActiveScene());
                head.name=kind+"_LeftSourceToolVisual_STATIC";head.SetParent(leftMount,false);
                head.localPosition=visual.localPosition;head.localRotation=visual.localRotation;head.localScale=visual.localScale;
                roots.Add(head);
            }
            shells[i]=new RobotVisualProfileRig.ToolShell{
                tool=kind,displayName=handler?kind+" / RIGHT PROCESS / LEFT HANDLER GRIPPER":kind+" / source arm / matched left-right head",roots=roots.ToArray()};
        }
        return shells;
    }
}
