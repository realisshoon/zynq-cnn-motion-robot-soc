using System.Globalization;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    public sealed class ManualServoSource : MonoBehaviour
    {
        public SingleArmCommandRouter router;
        public float[] Values {get;private set;}=SingleArmCommandRouter.Home;
        public string[] Errors {get;}=new string[5];
        public void AdoptCurrent(){Values=(float[])router.Approved.Clone();for(int i=0;i<5;i++)Errors[i]="";}
        public bool SetText(int axis,string text)
        {
            if(router.Source!="MANUAL"){router.Reject("MANUAL does not own input");return false;}
            if(!float.TryParse(text,NumberStyles.Float,CultureInfo.InvariantCulture,out float value)||float.IsNaN(value)||float.IsInfinity(value))
            {Errors[axis]="Enter a finite number";router.Reject("M"+axis+" invalid number");return false;}
            return Set(axis,value);
        }
        public bool Set(int axis,float value)
        {
            var q=(float[])Values.Clone();q[axis]=value;
            if(!router.Submit("MANUAL",router.Epoch,q)){Errors[axis]=router.Status;return false;}
            Values=q;for(int i=0;i<5;i++)Errors[i]="";return true;
        }
        public bool LoadAtomic(float[] q)
        {
            if(!router.ValidatePreset(q,out string reason)){router.Reject(reason);return false;}
            if(!router.Submit("MANUAL",router.Epoch,q))return false;
            Values=(float[])q.Clone();for(int i=0;i<5;i++)Errors[i]="";return true;
        }
        public bool Home()=>LoadAtomic(SingleArmCommandRouter.Home);
    }
}
