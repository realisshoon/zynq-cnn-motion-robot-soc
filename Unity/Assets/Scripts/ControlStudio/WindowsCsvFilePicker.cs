using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace HumanMotion.ControlStudio
{
    public static class WindowsCsvFilePicker
    {
        [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
        sealed class OpenFileName
        {
            public int size;public IntPtr owner,instance;
            public string filter="CSV files (*.csv)\0*.csv\0\0";public IntPtr customFilter;
            public int maxCustomFilter,index=1;public IntPtr file;public int maxFile;
            public IntPtr fileTitle;public int maxFileTitle;public string initialDirectory,title="CSV 파일 불러오기";
            public int flags=0x00001000|0x00000800|0x00000008|0x00080000|0x02000000;
            public short fileOffset,extensionOffset;public string defaultExtension="csv";
            public IntPtr customData,hook,template,reserved;public int reservedValue,flagsEx;
        }
        [DllImport("comdlg32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool GetOpenFileNameW([In,Out]OpenFileName data);
        [DllImport("comdlg32.dll")]static extern uint CommDlgExtendedError();
        public static Task<string> Pick()
        {
            var completion=new TaskCompletionSource<string>();IntPtr owner=Process.GetCurrentProcess().MainWindowHandle;
            var thread=new Thread(()=>{IntPtr buffer=Marshal.AllocHGlobal(65536);try{
                Marshal.WriteInt16(buffer,0);var data=new OpenFileName{owner=owner,file=buffer,maxFile=32768};data.size=Marshal.SizeOf(data);
                if(GetOpenFileNameW(data))completion.SetResult(Marshal.PtrToStringUni(buffer));
                else{uint error=CommDlgExtendedError();if(error==0)completion.SetResult(null);else completion.SetException(new InvalidOperationException("Windows file dialog error 0x"+error.ToString("X")));}
            }catch(Exception e){completion.SetException(e);}finally{Marshal.FreeHGlobal(buffer);}}){IsBackground=true};
            thread.SetApartmentState(ApartmentState.STA);thread.Start();return completion.Task;
        }
    }
}
