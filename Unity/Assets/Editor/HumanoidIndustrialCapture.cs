using System;
using System.Collections.Generic;
using System.IO;
using HumanMotion.ControlStudio;
using UnityEditor;
using UnityEngine;

// Editor-only visual inspection. Temporary objects are never saved into Demo_07.
public static class HumanoidIndustrialCapture
{
    const string Folder="Validation/HumanoidIndustrial";
    static Camera view;static RenderTexture render;static Texture2D pixels;

    [MenuItem("Tools/Humanoid/Capture Nine Views")]
    public static void Run()
    {
        Directory.CreateDirectory(Folder);
        var prefab=AssetDatabase.LoadAssetAtPath<HumanoidVisualRig>(
            "Assets/Resources/VisualProfiles/HumanoidRobot.prefab");
        if(prefab==null||!prefab.IsConfigured)throw new Exception("Humanoid visual prefab is not configured");
        HumanoidVisualRig rig=null;GameObject cameraObject=null;GameObject lightRoot=null;
        var otherRenderers=new List<Renderer>();
        var otherLights=new List<Light>();
        Color oldAmbient=RenderSettings.ambientLight;
        try
        {
            rig=UnityEngine.Object.Instantiate(prefab);rig.name="TEMP Humanoid Visual Capture";
            foreach(var candidate in UnityEngine.Object.FindObjectsByType<Renderer>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                if(candidate.enabled && !candidate.transform.IsChildOf(rig.transform))
                {
                    candidate.enabled=false;
                    otherRenderers.Add(candidate);
                }
            }
            foreach(var candidate in UnityEngine.Object.FindObjectsByType<Light>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                if(candidate.enabled)
                {
                    candidate.enabled=false;
                    otherLights.Add(candidate);
                }
            }
            rig.ResetToRest();rig.SetTool(ToolKind.Gripper);
            if(Mathf.Abs(rig.left.wristPitch.position.x+rig.right.wristPitch.position.x)>.01f ||
               Quaternion.Angle(rig.left.toolMount.rotation,rig.right.toolMount.rotation)>1f)
                throw new Exception("Neutral wrists or gripper orientations are not mirrored");
            if(rig.right.elbowPitch.position.z>=rig.right.shoulderYaw.position.z-.04f ||
               rig.left.elbowPitch.position.z>=rig.left.shoulderYaw.position.z-.04f)
                throw new Exception("Neutral upper arms do not face the front work envelope");
            lightRoot=new GameObject("TEMP Humanoid Studio Lighting");
            MakeLight(lightRoot.transform,"key",new Color(1f,.94f,.86f),.58f,new Vector3(35,-25,0));
            MakeLight(lightRoot.transform,"fill",new Color(.70f,.83f,1f),.40f,new Vector3(40,135,0));
            MakeLight(lightRoot.transform,"rim",new Color(.72f,.88f,1f),.35f,new Vector3(15,190,0));
            RenderSettings.ambientLight=new Color(.38f,.42f,.47f);
            cameraObject=new GameObject("TEMP Humanoid Capture Camera");view=cameraObject.AddComponent<Camera>();
            view.clearFlags=CameraClearFlags.SolidColor;view.backgroundColor=new Color(.035f,.047f,.062f);
            view.fieldOfView=38f;view.nearClipPlane=.01f;view.farClipPlane=100f;
            render=new RenderTexture(1600,1200,24){antiAliasing=4};render.Create();view.targetTexture=render;
            pixels=new Texture2D(1600,1200,TextureFormat.RGB24,false);
            Vector3 aim=new Vector3(0,1.17f,-.12f);
            Snap("01_front",new Vector3(0,.38f,-3.75f),aim);
            Snap("02_left_side",new Vector3(-3.0f,.34f,-.1f),aim);
            Snap("03_right_side",new Vector3(3.0f,.34f,-.1f),aim);
            Snap("04_three_quarter",new Vector3(1.50f,.55f,-3.45f),aim);
            Snap("05_shoulder",rig.right.shoulderYaw.position+new Vector3(.52f,.29f,-1.15f),rig.right.shoulderYaw.position);
            Snap("06_elbow",rig.right.elbowPitch.position+new Vector3(.50f,.26f,-1.05f),rig.right.elbowPitch.position);
            Snap("07_wrist_tool",rig.right.wristPitch.position+new Vector3(.48f,.26f,-1.08f),rig.right.wristPitch.position);
            Snap("08_torso_brand",rig.torso.position+new Vector3(0,.05f,-1.35f),rig.torso.position+new Vector3(0,-.08f,-.17f));
            var rest=rig.right.rest.elbowPitch;
            var target=new HumanoidArmJointTarget{elbowPitch=30,elbowRoll=15,wristPitch=-18,wristRoll=22,
                gripper=.25f,valid=true,fresh=true};
            if(!rig.ApplyArmTarget(HumanoidArmSide.Right,target))throw new Exception("Right arm work target failed");
            if(Quaternion.Angle(rest,rig.right.elbowPitch.localRotation)<20)throw new Exception("Elbow did not move");
            Snap("09_combined_working",new Vector3(1.50f,.55f,-3.45f),aim);
            rig.ResetToRest();
            var first=rig.right.gripperVisual.leftJawPivot.localRotation;
            rig.ApplyArmTarget(HumanoidArmSide.Right,new HumanoidArmJointTarget{gripper=0,valid=true,fresh=true});
            if(Quaternion.Angle(first,rig.right.gripperVisual.leftJawPivot.localRotation)<1f)
                throw new Exception("Gripper does not close");
            const string materials="Assets/Resources/VisualProfiles/HumanoidRobot/";
            int[] counts={CountMaterial(rig,AssetDatabase.LoadAssetAtPath<Material>(materials+"HumanoidWhiteShell.mat")),
                CountMaterial(rig,AssetDatabase.LoadAssetAtPath<Material>(materials+"HumanoidGraphite.mat")),
                CountMaterial(rig,AssetDatabase.LoadAssetAtPath<Material>(materials+"HumanoidDarkPolymer.mat")),
                CountMaterial(rig,AssetDatabase.LoadAssetAtPath<Material>(materials+"HumanoidBrushedSteel.mat")),
                CountMaterial(rig,AssetDatabase.LoadAssetAtPath<Material>(materials+"HumanoidStainlessShaft.mat"))};
            if(Array.Exists(counts,count=>count==0))throw new Exception("Material separation missing");
            File.WriteAllText(Folder+"/validation.txt",
                $"PASS views=9 rig={rig.IsConfigured} material_counts={string.Join(",",counts)} " +
                $"left_shoulder={rig.left.shoulderYaw.position} left_elbow={rig.left.elbowPitch.position} left_wrist={rig.left.wristPitch.position} left_tool={rig.left.toolMount.position} " +
                $"right_shoulder={rig.right.shoulderYaw.position} right_elbow={rig.right.elbowPitch.position} " +
                $"right_wrist={rig.right.wristPitch.position} right_tool={rig.right.toolMount.position} hardware_tx=0\n");
            Debug.Log("HUMANOID INDUSTRIAL CAPTURE PASS");
        }
        finally
        {
            foreach(var candidate in otherRenderers)if(candidate!=null)candidate.enabled=true;
            foreach(var candidate in otherLights)if(candidate!=null)candidate.enabled=true;
            RenderSettings.ambientLight=oldAmbient;
            if(render!=null){render.Release();UnityEngine.Object.DestroyImmediate(render);}
            if(pixels!=null)UnityEngine.Object.DestroyImmediate(pixels);
            if(cameraObject!=null)UnityEngine.Object.DestroyImmediate(cameraObject);
            if(lightRoot!=null)UnityEngine.Object.DestroyImmediate(lightRoot);
            if(rig!=null)UnityEngine.Object.DestroyImmediate(rig.gameObject);
        }
    }
    static int CountMaterial(HumanoidVisualRig rig,Material mat)
    {
        int count=0;foreach(var r in rig.GetComponentsInChildren<Renderer>(true))
            if(r.sharedMaterial==mat)count++;
        return count;
    }
    static void MakeLight(Transform parent,string name,Color color,float intensity,Vector3 rotation)
    {
        var go=new GameObject(name);go.transform.SetParent(parent,false);
        go.transform.rotation=Quaternion.Euler(rotation);
        var light=go.AddComponent<Light>();light.type=LightType.Directional;
        light.color=color;light.intensity=intensity;light.shadows=LightShadows.Soft;
    }
    static void Snap(string name,Vector3 position,Vector3 target)
    {
        view.transform.position=position;view.transform.LookAt(target,Vector3.up);
        view.Render();var prior=RenderTexture.active;RenderTexture.active=render;
        pixels.ReadPixels(new Rect(0,0,1600,1200),0,0);pixels.Apply(false,false);
        File.WriteAllBytes(Folder+"/"+name+".png",pixels.EncodeToPNG());
        RenderTexture.active=prior;
    }
}
