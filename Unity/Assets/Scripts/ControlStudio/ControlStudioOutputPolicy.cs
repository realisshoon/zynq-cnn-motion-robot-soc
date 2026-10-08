using System;
using System.Runtime.InteropServices;

namespace HumanMotion.ControlStudio
{
    // Native servo-domain policy. Never falls back to a C# motion/calibration formula.
    public sealed class ControlStudioOutputPolicy : IDisposable
    {
        const string Library = "control_studio_v2";
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int studio_abi_version();
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern void studio_limits([Out] float[] lo,[Out] float[] hi);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr studio_create(float[] home);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern void studio_destroy(IntPtr handle);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int studio_validate(float[] q);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int studio_submit(IntPtr handle,float[] q);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int studio_tick(IntPtr handle,[Out] float[] q,[Out] float[] velocity);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int studio_submit_human(IntPtr handle,float[] human,uint targetFrame,int[] flags,[Out] float[] wrapped,[Out] float[] candidate,[Out] float[] approved,[Out] int[] info);
        public readonly float[] Min=new float[5], Max=new float[5];
        IntPtr handle;
        public ControlStudioOutputPolicy(float[] home)
        {
            if(studio_abi_version()!=2)throw new InvalidOperationException("NATIVE_ABI_MISMATCH (required 2)");
            studio_limits(Min,Max);
            if (home == null || home.Length != 5) throw new ArgumentException("Home requires five values");
            handle=studio_create(home);if(handle==IntPtr.Zero)throw new InvalidOperationException("C rejected Home");
        }
        public int Validate(float[] q)=>q==null||q.Length!=5?-1:studio_validate(q);
        public int Submit(float[] q)=>q==null||q.Length!=5?-1:studio_submit(handle,q);
        public int Tick(float[] q,float[] velocity)=>studio_tick(handle,q,velocity);
        public int SubmitHuman(RecordedHumanRow row,float[] wrapped,float[] candidate,float[] approved,int[] info)
            =>studio_submit_human(handle,row.Human,row.TargetFrameId,row.Flags,wrapped,candidate,approved,info);
        public void Dispose(){if(handle!=IntPtr.Zero){studio_destroy(handle);handle=IntPtr.Zero;}}
        public static string Reason(int rc)=>rc==0?"APPROVED":rc==-1?"INVALID / NONFINITE":rc==-2?"OUT OF RANGE":rc>=100?"C SAFETY HOLD flags="+(rc-100):"NATIVE ERROR "+rc;
    }
}
