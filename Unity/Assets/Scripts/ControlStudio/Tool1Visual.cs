using UnityEngine;
namespace HumanMotion.ControlStudio
{
    public sealed class Tool1Visual : MonoBehaviour
    {
        public ToolKind kind;public Transform toolTip;public ParticleSystem spray;
        public Tool1WeldingEffect welding;
        public Tool1NailingEffect nailing;
        public bool Contact {get;private set;}
        float emission;
        public void Stop(){if(welding!=null)welding.Stop();if(nailing!=null)nailing.Stop();if(spray!=null)spray.Stop(true,ParticleSystemStopBehavior.StopEmittingAndClear);emission=0;Contact=false;}
        public void Advance(ToolCommand command,float seconds,Color color,float radius,float intensity)
        {
            if(!command.Run||command.Tool!=kind){Stop();return;}
            if(kind==ToolKind.Welding){if(welding!=null){welding.Advance(true,seconds,intensity,radius);Contact=welding.ArcOn;}return;}
            if(kind==ToolKind.Nailing){if(nailing!=null){nailing.Advance(true,seconds,intensity,radius);Contact=nailing.ImpactOn;}return;}
            if(kind!=ToolKind.Spray){Stop();return;}
            seconds=Mathf.Clamp(seconds,0,.5f);if(seconds==0)return;
            if(spray!=null){var main=spray.main;main.startColor=color;emission+=seconds*90;int n=Mathf.FloorToInt(emission);emission-=n;spray.Emit(n);spray.Simulate(seconds,false,false,false);spray.Pause();}
            if(Physics.Raycast(toolTip.position,toolTip.forward,out var hit,.65f,~0,QueryTriggerInteraction.Ignore)){
                var surface=hit.collider.GetComponent<Tool1PaintableSurface>();if(surface!=null)surface.Paint(hit.textureCoord,color,radius,seconds);
            }
        }
    }
}
