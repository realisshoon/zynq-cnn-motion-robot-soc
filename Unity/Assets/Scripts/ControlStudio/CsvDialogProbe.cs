using System;
using System.Collections;
using System.IO;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Opt-in native dialog/UI observation only; no UART open or send.
    public sealed class CsvDialogProbe : MonoBehaviour
    {
        string path;SingleArmCommandRouter router;
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]static void Optional(){var a=Environment.GetCommandLineArgs();int i=Array.IndexOf(a,"--csv-dialog-probe");if(i>=0&&i+1<a.Length)new GameObject("CSV native dialog probe").AddComponent<CsvDialogProbe>().path=Path.GetFullPath(a[i+1]);}
        IEnumerator Start(){Directory.CreateDirectory(path);Application.runInBackground=true;yield return new WaitForSecondsRealtime(2);router=FindFirstObjectByType<SingleArmCommandRouter>();var menu=router.GetComponent<ControlStudioStartupMenu>();menu.Cycle(1,1-menu.Choice(1));menu.StartStudio();router.GetComponent<ControlStudioDockLayout>().CloseDetails();
            while(true){var csv=router.Csv;var b=router.GetComponent<BoardUartSource>();File.WriteAllText(Path.Combine(path,"dialog_state.txt"),"pending="+csv.PendingFile?.Path+"\nconfirm="+csv.NeedsConfirmation+"\nfile="+csv.File?.Path+"\nrole="+csv.Role+"\nsource="+router.Source+"\npaused="+router.Paused+"\ntime="+csv.Timeline?.Time+"\nrows="+csv.Data?.Rows.Count+"\nmessage="+csv.Message+"\napplied="+string.Join(",",router.Applied)+"\nphysicalTX="+(b.TxBytes(BoardSide.Right)+b.TxBytes(BoardSide.Left)+router.GetComponent<ControlStudioUartOutput>().HardwareTxCount)+"\nCOM="+b.Right.Connected+","+b.Left.Connected);yield return new WaitForSecondsRealtime(1);}
        }
    }
}
