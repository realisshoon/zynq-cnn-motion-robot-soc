using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using Object = UnityEngine.Object;

public static class DualRobotDemoBuilder
{
    public const string ScenePath = "Assets/Scenes/DualRobotDemo.unity";
    [MenuItem("Tools/Human Motion/Dual Robot Demo/Build or Rebuild %#F9")]
    public static void Build()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode) return;
        if (SceneManager.GetActiveScene().isDirty)
            throw new InvalidOperationException("현재 Scene을 저장한 뒤 Build를 실행하세요. 미저장 작업을 덮어쓰지 않습니다.");
        var sourceScene = EditorSceneManager.OpenPreviewScene("Assets/Scenes/Main.unity");
        try
        {
            var source = sourceScene.GetRootGameObjects().SelectMany(r=>r.GetComponentsInChildren<RobotArmController>(true)).Single();
            if (source.gripperVisual == null || !source.GetComponentsInChildren<Transform>(true).Any(t=>t.name.StartsWith("__G51V2_")))
                throw new InvalidOperationException("Main Scene에 저장된 G51 v2 / linkage gripper가 필요합니다.");
            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            var system = new GameObject("DualRobotSystem");
            var left = Clone(source, system.transform, "RobotArm_L");
            var right = Clone(source, system.transform, "RobotArm_R");
            foreach (var rx in left.GetComponentsInChildren<UdpJointCommandReceiver>(true)) Object.DestroyImmediate(rx);
            var receivers = right.GetComponentsInChildren<UdpJointCommandReceiver>(true);
            foreach (var rx in receivers) Object.DestroyImmediate(rx);
            var receiver = right.gameObject.AddComponent<UdpJointCommandReceiver>();
            receiver.controller = right; receiver.listenAddress = "127.0.0.1"; receiver.port = 5005; receiver.enabled = false;
            ValidateReferences(left); ValidateReferences(right);

            var demoObject = new GameObject("DualArmDemoController");
            demoObject.transform.SetParent(system.transform, false);
            var demo = demoObject.AddComponent<DualArmDemoController>();
            demo.leftArm = left; demo.rightArm = right; demo.rightReceiver = receiver;
            demo.leftIdle = Pose(65,100,80,120,90);
            demo.leftPick = Pose(65,130,60,145,90);
            demo.leftHandoff = Pose(90,140,70,150,90);
            demo.rightIdle = Pose(90,100,80,120,180);
            demo.rightHandoff = Pose(90,140,70,150,180);
            demo.rightAway = Pose(125,110,80,130,180);
            const float mountHeight = 0.78f;
            // 내부 pivot은 그대로 두고 root만 회전: local +Y가 아래, +Z가 중앙을 향한다.
            left.transform.rotation = Quaternion.Euler(0,90,180);
            right.transform.rotation = Quaternion.Euler(0,-90,180);
            left.transform.position = right.transform.position = Vector3.up * mountHeight;

            // 현재 visual의 실제 접촉 pad 간격에서 시연용 구의 크기와 hold opening 산출.
            // CAD 실측 치수나 물리적인 파지 성공을 의미하지 않는다.
            Apply(left, demo.leftHandoff);
            float gapClosed = Gap(left, 0), gapOpen = Gap(left, 1);
            if (gapOpen <= gapClosed) throw new InvalidOperationException("Gripper open/close pad 간격을 확인하세요.");
            float diameter = Mathf.Clamp(0.04f, gapClosed + (gapOpen-gapClosed)*0.2f, gapClosed + (gapOpen-gapClosed)*0.8f);
            float low=0, high=1;
            for (int i=0;i<20;i++) { float mid=(low+high)*0.5f; if (Gap(left,mid)<diameter) low=mid; else high=mid; }
            demo.holdOpening = (low+high)*0.5f;
            Gap(left,demo.holdOpening); Gap(right,demo.holdOpening);
            Transform lg = GrabPoint(left,"GrabPoint_L"), rg = GrabPoint(right,"GrabPoint_R");
            Apply(left, WithGrip(demo.leftHandoff,demo.holdOpening));
            Apply(right, WithGrip(demo.rightHandoff,demo.holdOpening));
            float handoffHeight = lg.position.y;
            Vector3 center = new Vector3(0,handoffHeight,0);
            left.transform.position += center-lg.position;
            right.transform.position += center-rg.position;
            var handoff = new GameObject("HandoffPosition").transform;
            handoff.SetParent(system.transform,false); handoff.position=center;

            var transfer = system.AddComponent<BallTransferController>();
            transfer.leftGrab=lg; transfer.rightGrab=rg; transfer.ballRadius=diameter*0.5f;
            var ball=Primitive("RedBall", PrimitiveType.Sphere,system.transform,Vector3.zero,Vector3.one*diameter,
                Material("BallRed", new Color(0.85f,0.025f,0.025f),0.25f));
            transfer.ball=ball.transform; demo.transfer=transfer;
            Apply(left,WithGrip(demo.leftPick,demo.holdOpening));
            transfer.resetPosition=lg.position;
            var ui = demoObject.AddComponent<DualArmStatusUI>(); ui.demo=demo;
            demo.ResetDemo();

            var frame=new GameObject("CenterFrame").transform; frame.SetParent(system.transform,false);
            var metal=Material("FrameGraphite",new Color(0.045f,0.05f,0.06f),0.15f);
            var aluminum=Material("FrameAluminum",new Color(0.5f,0.55f,0.6f),0.65f);
            float width = Mathf.Abs(right.transform.position.x-left.transform.position.x)+0.18f;
            const float frameDepth = 0.20f;
            float beamHeight = mountHeight+0.015f;
            Primitive("Base",PrimitiveType.Cube,frame,new Vector3(0,0.025f,frameDepth),new Vector3(0.34f,0.05f,0.32f),metal);
            var column=Primitive("VerticalColumn",PrimitiveType.Cube,frame,new Vector3(0,beamHeight*0.5f,frameDepth),new Vector3(0.065f,beamHeight,0.065f),aluminum);
            var beam=Primitive("HorizontalBeam",PrimitiveType.Cube,frame,new Vector3(0,beamHeight,frameDepth),new Vector3(width,0.055f,0.065f),aluminum);
            // 단순 T-slot 선으로 aluminium extrusion을 표현한다. 관절 geometry와 무관한 visual이다.
            foreach(float sign in new[]{-1f,1f})
            {
                Primitive("ColumnSlot",PrimitiveType.Cube,frame,new Vector3(sign*0.018f,beamHeight*0.5f,frameDepth-0.033f),new Vector3(0.004f,beamHeight-0.03f,0.0015f),metal);
                Primitive("BeamSlot",PrimitiveType.Cube,frame,new Vector3(0,beamHeight+sign*0.014f,frameDepth-0.033f),new Vector3(width-0.01f,0.003f,0.0015f),metal);
            }
            Mount(frame,"LeftMount",left.transform.position,frameDepth,metal);
            Mount(frame,"RightMount",right.transform.position,frameDepth,metal);
            var camera = new GameObject("Main Camera").AddComponent<Camera>(); camera.tag="MainCamera";
            camera.transform.position = new Vector3(0.10f,0.55f,-1.65f)*Mathf.Max(1,width/0.9f);
            camera.transform.LookAt(new Vector3(0,0.43f,0.04f)); camera.fieldOfView=36; camera.nearClipPlane=0.01f; camera.farClipPlane=30;
            camera.clearFlags=CameraClearFlags.SolidColor; camera.backgroundColor=new Color(0.035f,0.05f,0.075f);
            camera.gameObject.AddComponent<AudioListener>();
            Light("Key",new Vector3(35,-35,0),2.2f,Color.white);
            Light("Fill",new Vector3(55,145,0),1.2f,new Color(0.65f,0.8f,1));
            PointLight("FrontSoftbox",new Vector3(0,0.8f,-0.65f),2f);
            PointLight("LeftSoftbox",new Vector3(-0.7f,0.5f,-0.1f),1.5f);
            PointLight("RimSoftbox",new Vector3(0.4f,0.7f,0.6f),2f);
            RenderSettings.ambientMode=UnityEngine.Rendering.AmbientMode.Flat;
            RenderSettings.ambientLight=new Color(0.45f,0.48f,0.55f);
            EditorSceneManager.MarkSceneDirty(scene);
            EditorSceneManager.SaveScene(scene,ScenePath);
            Selection.activeGameObject=demoObject;
            Debug.Log($"Dual Robot 준비 완료. ball radius={transfer.ballRadius:F4}m, hold={demo.holdOpening:F3}, pad gap={gapClosed:F4}..{gapOpen:F4}m. Main/G51 원본 보존.");
        }
        finally { EditorSceneManager.ClosePreviewScene(sourceScene); }
    }

    private static RobotArmController Clone(RobotArmController source,Transform parent,string name)
    {
        var go=Object.Instantiate(source.gameObject,parent); go.name=name;
        var controller=go.GetComponent<RobotArmController>(); controller.inputMode=RobotArmController.InputMode.Manual;
        return controller;
    }
    public static void ValidateReferences(RobotArmController c)
    {
        foreach(var component in c.GetComponentsInChildren<MonoBehaviour>(true))
        {
            if(component==null) throw new InvalidOperationException("Missing script");
            var so=new SerializedObject(component); var property=so.GetIterator();
            while(property.Next(true))
            {
                if(property.propertyType!=SerializedPropertyType.ObjectReference) continue;
                var reference=property.objectReferenceValue as Component;
                if(reference!=null && reference.transform!=c.transform && !reference.transform.IsChildOf(c.transform))
                    throw new InvalidOperationException(c.name+" 외부 reference: "+property.propertyPath);
            }
        }
        foreach(var t in c.GetComponentsInChildren<Transform>(true))
            if(t.localScale.x<=0 || t.localScale.y<=0 || t.localScale.z<=0) throw new InvalidOperationException("비양수 scale: "+t.name);
    }
    private static JointCommandData Pose(float b,float s,float e,float p,float r) => new JointCommandData
        {valid=true,base_deg=b,shoulder_deg=s,elbow_deg=e,wrist_pitch_deg=p,wrist_roll_deg=r,gripper_norm=1};
    private static JointCommandData WithGrip(JointCommandData c,float grip) {c.gripper_norm=grip;return c;}
    private static void Apply(RobotArmController c,JointCommandData pose) {c.testCommand=pose;c.ApplyCommand(pose);}
    private static Transform Pad(RobotArmController c,string suffix) => c.gripperVisual.transform.Find("Finger_"+suffix+"/FingerPad_"+suffix);
    private static float Gap(RobotArmController c,float grip)
    {
        c.gripperVisual.Apply(grip); var l=Pad(c,"L"); var r=Pad(c,"R");
        return Vector3.Distance(l.position,r.position)-(l.lossyScale.x+r.lossyScale.x)*0.5f;
    }
    private static Transform GrabPoint(RobotArmController c,string name)
    {
        var point=new GameObject(name).transform; point.SetParent(c.gripperVisual.transform,false);
        point.position=(Pad(c,"L").position+Pad(c,"R").position)*0.5f; return point;
    }
    private static GameObject Primitive(string name,PrimitiveType type,Transform parent,Vector3 pos,Vector3 scale,Material material)
    {
        var go=GameObject.CreatePrimitive(type);go.name=name;go.transform.SetParent(parent,false);
        go.transform.localPosition=pos;go.transform.localScale=scale;go.GetComponent<Renderer>().sharedMaterial=material;
        Object.DestroyImmediate(go.GetComponent<Collider>());return go;
    }
    private static Material Material(string name,Color color,float metallic)
    {
        Directory.CreateDirectory("Assets/Materials/DualRobot");
        string path="Assets/Materials/DualRobot/"+name+".mat";
        var mat=AssetDatabase.LoadAssetAtPath<Material>(path);
        if(mat==null){mat=new Material(Shader.Find("Universal Render Pipeline/Lit"));AssetDatabase.CreateAsset(mat,path);}
        mat.SetColor("_BaseColor",color);mat.SetFloat("_Metallic",metallic);mat.SetFloat("_Smoothness",0.5f);
        EditorUtility.SetDirty(mat);AssetDatabase.SaveAssets();return mat;
    }
    private static void Light(string name,Vector3 angles,float intensity,Color color)
    {var light=new GameObject(name).AddComponent<Light>();light.type=LightType.Directional;light.transform.eulerAngles=angles;light.intensity=intensity;light.color=color;light.shadows=LightShadows.None;}
    private static void PointLight(string name,Vector3 position,float intensity)
    {var light=new GameObject(name).AddComponent<Light>();light.type=LightType.Point;light.transform.position=position;light.intensity=intensity;light.range=3;light.shadows=LightShadows.None;}
    private static void Mount(Transform frame,string name,Vector3 rootPosition,float depth,Material material)
    {
        var mount=new GameObject(name).transform;mount.SetParent(frame,false);mount.position=rootPosition;
        Primitive("BasePlate",PrimitiveType.Cube,mount,new Vector3(0,-0.037f,0),new Vector3(0.105f,0.014f,0.10f),material);
        Primitive("RearPlate",PrimitiveType.Cube,mount,new Vector3(0,-0.008f,depth),new Vector3(0.09f,0.07f,0.015f),material);
        Primitive("Cantilever",PrimitiveType.Cube,mount,new Vector3(0,-0.021f,depth*0.5f),new Vector3(0.065f,0.018f,depth),material);
    }
}
