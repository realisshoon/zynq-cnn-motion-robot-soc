using System;
using System.Runtime.InteropServices;

namespace HumanMotion.ControlStudio
{
    // Native servo-domain policy. Never falls back to a C# motion/calibration formula.
    public sealed class ControlStudioOutputPolicy : IDisposable
    {
        static class Right
        {
        const string Library = "control_studio_right";
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_abi_version();
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern void studio_limits([Out] float[] lo,[Out] float[] hi);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern IntPtr studio_create(float[] home);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern void studio_destroy(IntPtr handle);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_validate(float[] q);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_submit(IntPtr handle,float[] q);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_tick(IntPtr handle,[Out] float[] q,[Out] float[] velocity);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_submit_human(IntPtr handle,float[] human,uint targetFrame,int[] flags,[Out] float[] wrapped,[Out] float[] candidate,[Out] float[] approved,[Out] int[] info);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_role();
        }
        static class Left
        {
        const string Library = "control_studio_left";
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_abi_version();
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern void studio_limits([Out] float[] lo,[Out] float[] hi);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern IntPtr studio_create(float[] home);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern void studio_destroy(IntPtr handle);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_validate(float[] q);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_submit(IntPtr handle,float[] q);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_tick(IntPtr handle,[Out] float[] q,[Out] float[] velocity);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_submit_human(IntPtr handle,float[] human,uint targetFrame,int[] flags,[Out] float[] wrapped,[Out] float[] candidate,[Out] float[] approved,[Out] int[] info);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] public static extern int studio_role();
        }
        readonly BoardSide side;
        public readonly float[] Min=new float[5], Max=new float[5];
        IntPtr handle;
        public ControlStudioOutputPolicy(float[] home, BoardSide side=BoardSide.Right)
        {
            if(side!=BoardSide.Right&&side!=BoardSide.Left)throw new ArgumentOutOfRangeException(nameof(side));
            RequireLength(home,5,nameof(home));
            this.side=side;
            if((side==BoardSide.Right?Right.studio_abi_version():Left.studio_abi_version())!=3)throw new InvalidOperationException("NATIVE_ABI_MISMATCH (required 3)");
            if((side==BoardSide.Right?Right.studio_role():Left.studio_role())!=(side==BoardSide.Right?2:1))throw new InvalidOperationException("NATIVE_ROLE_MISMATCH");
            if(side==BoardSide.Right)Right.studio_limits(Min,Max);else Left.studio_limits(Min,Max);
            handle=side==BoardSide.Right?Right.studio_create(home):Left.studio_create(home);if(handle==IntPtr.Zero)throw new InvalidOperationException("C rejected Home");
        }
        public int Validate(float[] q)=>q==null||q.Length!=5?-1:(side==BoardSide.Right?Right.studio_validate(q):Left.studio_validate(q));
        public int Submit(float[] q)
        {
            RequireHandle();
            return q==null||q.Length!=5?-1:(side==BoardSide.Right?Right.studio_submit(handle,q):Left.studio_submit(handle,q));
        }
        public int Tick(float[] q,float[] velocity)
        {
            RequireHandle();RequireLength(q,5,nameof(q));RequireLength(velocity,4,nameof(velocity));
            return side==BoardSide.Right?Right.studio_tick(handle,q,velocity):Left.studio_tick(handle,q,velocity);
        }
        public int SubmitHuman(RecordedHumanRow row,float[] wrapped,float[] candidate,float[] approved,int[] info)
        {
            RequireHandle();
            if(row==null)throw new ArgumentNullException(nameof(row));
            RequireLength(row.Human,5,"row.Human");RequireLength(row.Flags,8,"row.Flags");
            RequireLength(wrapped,5,nameof(wrapped));RequireLength(candidate,5,nameof(candidate));
            RequireLength(approved,5,nameof(approved));RequireLength(info,4,nameof(info));
            var flags=new int[9];Array.Copy(row.Flags,flags,8);flags[8]=row.GripperValid?1:0;
            return side==BoardSide.Right?Right.studio_submit_human(handle,row.Human,row.TargetFrameId,flags,wrapped,candidate,approved,info):Left.studio_submit_human(handle,row.Human,row.TargetFrameId,flags,wrapped,candidate,approved,info);
        }
        void RequireHandle(){if(handle==IntPtr.Zero)throw new ObjectDisposedException(nameof(ControlStudioOutputPolicy));}
        static void RequireLength(Array values,int length,string name)
        {
            if(values==null)throw new ArgumentNullException(name);
            if(values.Length!=length)throw new ArgumentException("Requires "+length+" values",name);
        }
        public void Dispose(){if(handle!=IntPtr.Zero){if(side==BoardSide.Right)Right.studio_destroy(handle);else Left.studio_destroy(handle);handle=IntPtr.Zero;}}
        public static string Reason(int rc)=>rc==0?"APPROVED":rc==-1?"INVALID / NONFINITE":rc==-2?"OUT OF RANGE":rc>=100?"C SAFETY HOLD flags="+(rc-100):"NATIVE ERROR "+rc;
    }
}
