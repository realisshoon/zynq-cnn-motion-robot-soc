using UnityEngine;
namespace HumanMotion.ControlStudio
{
    // Visual fastening only; no force, penetration, material removal or physical nail.
    public sealed class Tool1NailingEffect : MonoBehaviour
    {
        public Transform tip,pulse;
        public bool RequestOn {get;private set;}
        public bool ImpactOn {get;private set;}
        public string SurfaceStatus {get;private set;}="NO_WORK_SURFACE";
        public const float MaxDistance=.06f,MinDistance=.003f;
        float elapsed;
        public void Stop()
        {RequestOn=false;ImpactOn=false;SurfaceStatus="NO_WORK_SURFACE";elapsed=0;if(pulse!=null)pulse.gameObject.SetActive(false);}
        public void Advance(bool request,float seconds,float intensity,float size)
        {
            RequestOn=request;ImpactOn=false;if(pulse!=null)pulse.gameObject.SetActive(false);
            if(!request){Stop();return;}
            if(!Physics.Raycast(tip.position,tip.forward,out var hit,MaxDistance,~0,QueryTriggerInteraction.Ignore)
                ||hit.distance<MinDistance||Vector3.Dot(-tip.forward,hit.normal)<.7071068f){SurfaceStatus="NO_WORK_SURFACE";elapsed=0;return;}
            var surface=hit.collider.GetComponent<Tool1FastenableSurface>();
            if(surface==null){SurfaceStatus="NO_WORK_SURFACE";elapsed=0;return;}
            SurfaceStatus="VALID_SURFACE";elapsed+=Mathf.Clamp(seconds,0,.5f);
            if(elapsed<.1f)return;
            elapsed=0;
            if(!surface.TryMark(hit.point,hit.normal,size))return;
            ImpactOn=true;
            if(pulse!=null){pulse.position=hit.point+hit.normal*.002f;pulse.localScale=Vector3.one*Mathf.Lerp(.004f,.011f,Mathf.Clamp01(intensity));pulse.gameObject.SetActive(true);}
        }
        void OnDisable(){Stop();}
    }
}
