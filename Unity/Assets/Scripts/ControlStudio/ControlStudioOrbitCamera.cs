using System.Collections.Generic;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioOrbitCamera : MonoBehaviour
    {
        public SingleArmCommandRouter router;
        public Shader axisShader;
        public int selectedAxis;
        public Vector3 Target {get;private set;}
        public float Distance {get;private set;}
        Vector3 homeTarget;float homeDistance,homeFieldOfView,yaw=-35,pitch=18;
        bool presentation;
        bool orbit,pan;
        readonly List<RaycastResult> hits=new List<RaycastResult>();
        LineRenderer axis;
        void Start()
        {
            bool first=true;Bounds b=new Bounds();
            foreach(var r in router.robot.GetComponentsInChildren<Renderer>())if(r.enabled){if(first){b=r.bounds;first=false;}else b.Encapsulate(r.bounds);}
            homeTarget=first?router.robot.transform.position:b.center;
            homeDistance=Mathf.Max(.5f,b.extents.magnitude*3.2f);
            homeFieldOfView=GetComponent<Camera>().fieldOfView;
            ResetView();
            var g=new GameObject("Selected joint axis (display only)");axis=g.AddComponent<LineRenderer>();
            axis.material=new Material(axisShader);axis.material.color=new Color(.2f,1,.6f);
            axis.startWidth=axis.endWidth=homeDistance*.002f;axis.positionCount=2;
        }
        public void ResetView(){Target=homeTarget;Distance=homeDistance;yaw=-35;pitch=18;if(homeFieldOfView>0)GetComponent<Camera>().fieldOfView=homeFieldOfView;PositionCamera();}
        public void SetPresentationMode(bool enabled)
        {
            if(presentation==enabled)return;
            // Presentation is UI visibility only; explicit framing tools stay separate.
            presentation=enabled;
            if(axis!=null)axis.enabled=!enabled;
        }
        public void SetPresentationCamera()
        {
            var visual=router.GetComponent<RobotVisualProfiles>();
            if(visual!=null&&visual.Selected==RobotVisualProfileId.HumanoidRobot&&visual.Humanoid!=null)
            {SetHumanoidPresentationCamera(visual.Humanoid.transform);return;}
            var tool=router.GetComponent<Tool1Runtime>();bool first=true;Bounds bounds=new Bounds();
            Transform visualRoot=visual!=null&&visual.Selected!=RobotVisualProfileId.G51&&visual.Current!=null
                ?visual.Current.transform:router.robot.transform.root;
            foreach(var renderer in visualRoot.GetComponentsInChildren<Renderer>(true))
                if(renderer.enabled&&renderer.gameObject.activeInHierarchy){if(first){bounds=renderer.bounds;first=false;}else bounds.Encapsulate(renderer.bounds);}
            if((visual==null||!visual.SoloRobotView)&&tool?.Surface!=null&&tool.Surface.gameObject.activeInHierarchy){var surface=tool.Surface.GetComponent<Renderer>();if(surface!=null){if(first){bounds=surface.bounds;first=false;}else bounds.Encapsulate(surface.bounds);}}
            if(first)return;
            Target=bounds.center;Vector3 forward=visual!=null&&visual.Selected!=RobotVisualProfileId.G51
                ?visualRoot.forward:tool?.Visual?.toolTip!=null?tool.Visual.toolTip.forward:router.robot.transform.forward;
            Vector3 side=Vector3.Cross(Vector3.up,forward).normalized;if(side.sqrMagnitude<.01f)side=Vector3.right;
            Vector3 offset=-forward+side*.48f+Vector3.up*.18f;
            bool solo=visual!=null&&visual.SoloRobotView;
            float fit=visual!=null&&visual.Selected!=RobotVisualProfileId.G51?2.15f:solo?1.75f:1.65f;
            Distance=Mathf.Max(bounds.extents.magnitude*fit,solo?.65f:.55f);
            GetComponent<Camera>().fieldOfView=solo?43f:48f;
            Vector3 view=offset.normalized;Vector3 direction=-view;
            yaw=Mathf.Atan2(direction.x,direction.z)*Mathf.Rad2Deg;
            pitch=-Mathf.Asin(Mathf.Clamp(direction.y,-1,1))*Mathf.Rad2Deg;
            PositionCamera();
        }
        public void SetHumanoidPresentationCamera(Transform root)
        {
            if(root==null)return;
            bool first=true;Bounds bounds=new Bounds();
            foreach(var renderer in root.GetComponentsInChildren<Renderer>(true))
                if(renderer.enabled&&renderer.gameObject.activeInHierarchy)
                {if(first){bounds=renderer.bounds;first=false;}else bounds.Encapsulate(renderer.bounds);}
            if(first)return;
            Target=bounds.center+Vector3.up*.05f;
            Distance=Mathf.Max(2.2f,bounds.extents.magnitude*2.45f);
            yaw=-24f;pitch=9f;PositionCamera();
        }
        public void SetG51PickPresentationCamera(Transform robotRoot,Transform pickupCube)
        {
            if(robotRoot==null)return;
            bool first=true;Bounds bounds=new Bounds();
            foreach(var renderer in robotRoot.GetComponentsInChildren<MeshRenderer>(true))
                if(renderer.enabled&&renderer.gameObject.activeInHierarchy)
                {if(first){bounds=renderer.bounds;first=false;}else bounds.Encapsulate(renderer.bounds);}
            if(pickupCube!=null)
            {
                var renderer=pickupCube.GetComponent<Renderer>();
                if(renderer!=null){if(first){bounds=renderer.bounds;first=false;}else bounds.Encapsulate(renderer.bounds);}
            }
            if(first)return;
            Target=bounds.center;
            Distance=Mathf.Max(.80f,bounds.extents.magnitude*1.4f);
            yaw=-40f;pitch=16f;
            var cameraView=GetComponent<Camera>();cameraView.clearFlags=CameraClearFlags.SolidColor;
            cameraView.backgroundColor=new Color(.018f,.023f,.032f);cameraView.fieldOfView=38f;
            PositionCamera();
        }
        bool OverUI(Vector2 pos)
        {
            if(EventSystem.current==null)return false;
            hits.Clear();EventSystem.current.RaycastAll(new PointerEventData(EventSystem.current){position=pos},hits);return hits.Count>0;
        }
        void LateUpdate()
        {
            var presentationUi=router.GetComponent<ControlStudioPresentationUI>();
            float fit=Mathf.Min(Screen.width/1600f,Screen.height/900f);
            float left=(Screen.width-1600*fit)*.5f,bottom=(Screen.height-900*fit)*.5f;
            float width=presentationUi!=null?presentationUi.CameraDesignWidth:960;
            // Render the complete background; frame the robot inside the space left by the UI.
            var viewCamera=GetComponent<Camera>();
            viewCamera.rect=new Rect(left/Screen.width,bottom/Screen.height,1600*fit/Screen.width,804*fit/Screen.height);
            var projection=Matrix4x4.Perspective(viewCamera.fieldOfView,1600f/804,viewCamera.nearClipPlane,viewCamera.farClipPlane);
            projection.m02=1-width/1600f;viewCamera.projectionMatrix=projection;
            var m=Mouse.current;
            if(m!=null)
            {
                bool over=OverUI(m.position.ReadValue());
                if(m.rightButton.wasPressedThisFrame)orbit=!over;
                if(m.middleButton.wasPressedThisFrame)pan=!over;
                if(!m.rightButton.isPressed)orbit=false;if(!m.middleButton.isPressed)pan=false;
                var d=m.delta.ReadValue();
                if(orbit){yaw+=d.x*.25f;pitch=Mathf.Clamp(pitch-d.y*.25f,-70,75);}
                if(pan)Target+=(-transform.right*d.x-transform.up*d.y)*Distance*.001f;
                if(!over)Distance=Mathf.Clamp(Distance*Mathf.Exp(-m.scroll.ReadValue().y*.08f),homeDistance*.25f,homeDistance*4);
                PositionCamera();
            }
            if(axis!=null)
            {
                var visual=router.GetComponent<RobotVisualProfiles>();
                axis.enabled=!presentation&&(visual==null||!visual.SoloRobotView);
                var robot=router.robot;var pivots=new[]{robot.elbowRoll,robot.elbowPitch,robot.wristPitch,robot.wristRoll,robot.gripperVisual.transform};
                var pivot=pivots[Mathf.Clamp(selectedAxis,0,4)];var dir=(selectedAxis==1||selectedAxis==2)?pivot.right:pivot.up;
                axis.SetPosition(0,pivot.position-dir*homeDistance*.035f);axis.SetPosition(1,pivot.position+dir*homeDistance*.035f);
            }
        }
        void PositionCamera(){transform.position=Target+Quaternion.Euler(pitch,yaw,0)*Vector3.back*Distance;transform.LookAt(Target);}
        void OnDestroy(){if(axis!=null){Destroy(axis.material);Destroy(axis.gameObject);}}
    }
}
