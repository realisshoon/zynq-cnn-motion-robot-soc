using System;
using System.Runtime.InteropServices;
namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioXyzPolicy : IDisposable
    {
        const string Library="control_studio_xyz";
        [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] static extern int xyz_abi_version();
        [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] static extern IntPtr xyz_create(uint epoch,float grip);
        [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] static extern void xyz_destroy(IntPtr h);
        [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] static extern int xyz_step(IntPtr h,uint epoch,uint frame,double time,float[] xyz,float[] basis,int[] flags,float grip,int mode,float compatibilitySpan,[Out] float[] human,[Out] int[] info);
        IntPtr handle;
        readonly uint epoch;readonly int mode;
        public ControlStudioXyzPolicy(int epoch,float appliedGripper,CsvInputMode mode)
        {
            if(xyz_abi_version()!=1)throw new InvalidOperationException("XYZ_ABI_MISMATCH (required 1)");
            this.epoch=(uint)epoch;this.mode=mode==CsvInputMode.XyzStoredBodyHoldGripper?1:0;
            handle=xyz_create(this.epoch,appliedGripper);if(handle==IntPtr.Zero)throw new InvalidOperationException("XYZ context rejected seed");
        }
        public XyzSolveResult Solve(RecordedHumanRow row,int requestEpoch,float compatibilitySpan=1f)
        {
            if(handle==IntPtr.Zero)throw new ObjectDisposedException(nameof(ControlStudioXyzPolicy));
            var p=row.Pose;if(p==null)throw new ArgumentException("Processed XYZ required");
            var q=new float[5];var info=new int[7];
            // Positive guard-only compatibility argument; NOT a measured shoulder pixel span.
            int rc=xyz_step(handle,(uint)requestEpoch,row.FrameId,row.Time,p.Xyz,p.BodyBasis,p.SourceFlags,p.AuxiliaryGripper,mode,compatibilitySpan,q,info);
            if(rc!=0)throw new InvalidOperationException("XYZ ROW REJECTED code="+rc+" (epoch/time/finite/basis contract)");
            var flags=(int[])row.Flags.Clone();flags[0]=info[0];flags[5]=info[1];flags[4]=info[2];flags[3]=info[3];
            return new XyzSolveResult{SolverInfo=info,Target=new RecordedHumanRow{FrameId=row.FrameId,TargetFrameId=unchecked((uint)info[6]),Time=row.Time,Flags=flags,Human=q}};
        }
        public void Dispose(){if(handle!=IntPtr.Zero){xyz_destroy(handle);handle=IntPtr.Zero;}}
    }
}
