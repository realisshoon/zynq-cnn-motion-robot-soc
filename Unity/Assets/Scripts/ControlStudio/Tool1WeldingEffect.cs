using UnityEngine;
namespace HumanMotion.ControlStudio
{
    public sealed class Tool1WeldingEffect : MonoBehaviour
    {
        public Transform tip,glow;public Light arcLight;public ParticleSystem sparks;
        public bool RequestOn {get;private set;} public bool ArcOn {get;private set;}
        public string SurfaceStatus {get;private set;}="NO_WORK_SURFACE";
        Tool1WeldableSurface previous;Vector3 lastPoint;int generation;float emission;
        // Virtual scene-unit limits, not electrical/thermal welding parameters.
        public const float MaxDistance=.06f,MinDistance=.003f,MaxSegment=.03f,Spacing=.001f;
        public void Stop(){RequestOn=false;SurfaceStatus="NO_WORK_SURFACE";StopEffects();}
        void StopEffects(){ArcOn=false;previous=null;emission=0;if(glow!=null)glow.gameObject.SetActive(false);if(arcLight!=null)arcLight.enabled=false;if(sparks!=null)sparks.Stop(true,ParticleSystemStopBehavior.StopEmittingAndClear);}
        public void Advance(bool request,float seconds,float intensity,float width)
        {
            RequestOn=request;
            if(!request){Stop();return;}
            if(!Physics.Raycast(tip.position,tip.forward,out var hit,MaxDistance,~0,QueryTriggerInteraction.Ignore)||hit.distance<MinDistance||Vector3.Dot(-tip.forward,hit.normal)<.7071068f){SurfaceStatus="NO_WORK_SURFACE";StopEffects();return;}
            var surface=hit.collider.GetComponent<Tool1WeldableSurface>();
            if(surface==null){SurfaceStatus="NO_WORK_SURFACE";StopEffects();return;}
            SurfaceStatus="VALID_SURFACE";ArcOn=true;intensity=Mathf.Clamp01(intensity);width=Mathf.Clamp(width,.001f,.012f);
            Vector3 point=hit.point+hit.normal*.002f;
            if(glow!=null){glow.position=point;glow.localScale=Vector3.one*Mathf.Lerp(.004f,.009f,intensity);glow.gameObject.SetActive(true);}
            if(arcLight!=null){arcLight.transform.position=point;arcLight.intensity=Mathf.Lerp(.1f,.65f,intensity);arcLight.enabled=true;}
            seconds=Mathf.Clamp(seconds,0,.5f);if(seconds<=0)return;
            bool start=previous!=surface||generation!=surface.Generation||Vector3.Distance(lastPoint,hit.point)>MaxSegment;
            if(start||Vector3.Distance(lastPoint,hit.point)>=Spacing){surface.Mark(start?hit.point:lastPoint,hit.point,hit.normal,width,start);lastPoint=hit.point;previous=surface;generation=surface.Generation;}
            if(sparks!=null){sparks.transform.position=point;sparks.transform.rotation=Quaternion.LookRotation(hit.normal);emission+=seconds*Mathf.Lerp(10,60,intensity);int count=Mathf.FloorToInt(emission);emission-=count;sparks.Emit(count);sparks.Simulate(seconds,false,false,false);sparks.Pause();}
        }
        void OnDisable(){Stop();}
    }
}
