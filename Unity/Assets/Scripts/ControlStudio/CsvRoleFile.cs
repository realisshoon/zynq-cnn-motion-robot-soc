using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

namespace HumanMotion.ControlStudio
{
    // Role-less streams remain unassigned until the user explicitly confirms a target.
    public sealed class CsvRoleFile
    {
        public const int MaxBytes=8*1024*1024;
        public readonly Dictionary<BoardSide,RecordedHumanCsv> Streams=new Dictionary<BoardSide,RecordedHumanCsv>();
        public RecordedHumanCsv Unassigned {get;private set;}
        public bool CompatibilityInverse {get;private set;}
        bool decoded;
        public string Path,Schema,Sha256;
        public static CsvRoleFile Load(string path,CsvInputMode mode)
        {
            path=System.IO.Path.GetFullPath(path);
            byte[] bytes;
            using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
                if(stream.Length==0||stream.Length>MaxBytes)throw new FormatException("CSV must be 1 byte..8 MiB");
                bytes=new byte[(int)stream.Length];int n=0;
                while(n<bytes.Length){int read=stream.Read(bytes,n,bytes.Length-n);if(read==0)throw new IOException("File changed while reading");n+=read;}
            }
            string text=new UTF8Encoding(false,true).GetString(bytes).TrimStart('\ufeff');
            var records=RecordedHumanCsv.ReadRecords(text);if(records.Count<2)throw new FormatException("Header and rows required");
            var header=records[0];if(header.Distinct().Count()!=header.Length)throw new FormatException("Duplicate header");
            bool final=header.Contains("M0");int roleIndex=Array.IndexOf(header,"board_role");
            int kindIndex=Array.IndexOf(header,"data_kind");
            bool inverse=kindIndex>=0&&records.Skip(1).Any(r=>r.Length>kindIndex&&r[kindIndex]=="SERVO_INVERSE_COMPATIBILITY");
            if(inverse&&(final||mode!=CsvInputMode.RecordedHumanAngles||roleIndex>=0||records.Skip(1).Any(r=>r.Length<=kindIndex||r[kindIndex]!="SERVO_INVERSE_COMPATIBILITY")))throw new FormatException("Inverse compatibility requires a homogeneous unassigned human-angle stream");
            if(inverse)foreach(int axis in Enumerable.Range(0,5))if(!header.Contains("source_servo_M"+axis))throw new FormatException("Inverse compatibility requires all source_servo_M0~M4 cross-check columns");
            var result=new CsvRoleFile{Path=path,Schema=final?"Final M0~M4 / deg / normalized gripper":mode.ToString()};
            result.CompatibilityInverse=inverse;
            if(inverse)result.Schema="RIGHT inverse compatibility → final servo targets";
            using(var sha=SHA256.Create())result.Sha256=BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();
            var groups=new Dictionary<BoardSide,List<string[]>>();
            foreach(var row in records.Skip(1)) {
                if(row.Length!=header.Length)throw new FormatException("Column count mismatch");
                BoardSide role=BoardSide.Right;
                if(roleIndex>=0){if(row[roleIndex]=="RIGHT")role=BoardSide.Right;else if(row[roleIndex]=="LEFT")role=BoardSide.Left;else throw new FormatException("board_role must be RIGHT or LEFT, never BOTH");}
                else if(final)throw new FormatException("Final commands require board_role, angle_unit, gripper_unit, calibration_id");
                if(mode!=CsvInputMode.RecordedHumanAngles&&role==BoardSide.Left&&!final)throw new FormatException("Existing XYZ Native supports RIGHT only; LEFT XYZ unavailable");
                if(!groups.ContainsKey(role))groups[role]=new List<string[]>();groups[role].Add(row);
            }
            foreach(var pair in groups) {
                if(!final){string Encode(string[] row)=>string.Join(",",row.Select(v=>"\""+v.Replace("\"","\"\"")+"\""));
                    var parsed=RecordedHumanCsv.Parse(Encode(header)+"\n"+string.Join("\n",pair.Value.Select(Encode)),mode);
                    foreach(var r in parsed.Rows)if(r.Human!=null&&(r.Human[4]<0||r.Human[4]>1))throw new FormatException("Human gripper_norm outside 0..1");
                    var clip=RecordedHumanCsv.FromRows(parsed.Rows.ToList(),path,result.Sha256,header.Length);
                    if(roleIndex<0)result.Unassigned=clip;else result.Streams[pair.Key]=clip;
                } else {
                    int Index(string n){int i=Array.IndexOf(header,n);if(i<0)throw new FormatException("Missing column: "+n);return i;}
                    int time=Index("time_sec"),frame=Index("frame_id"),unit=Index("angle_unit"),grip=Index("gripper_unit"),cal=Index("calibration_id");
                    var axes=Enumerable.Range(0,5).Select(i=>Index("M"+i)).ToArray();var rows=new List<RecordedHumanRow>();double previous=-1;uint last=0;
                    foreach(var row in pair.Value){
                        string profile=pair.Key==BoardSide.Right?"control_studio_right_abi3":"control_studio_left_abi3";
                        if(row[unit]!="deg"||row[grip]!="normalized"||row[cal]!=profile)throw new FormatException("Final CSV requires deg, normalized and "+profile+" (virtual logical profile, not physical calibration proof)");
                        double Number(int i){if(!double.TryParse(row[i],NumberStyles.Float,CultureInfo.InvariantCulture,out var v)||double.IsNaN(v)||double.IsInfinity(v))throw new FormatException("Invalid/nonfinite "+header[i]);return v;}
                        double t=Number(time);if(t<0||t<=previous)throw new FormatException("Per-role time must strictly increase");previous=t;
                        if(!uint.TryParse(row[frame],out var id)||rows.Count>0&&id<=last)throw new FormatException("Per-role frame_id must increase");last=id;
                        var q=axes.Select(i=>(float)Number(i)).ToArray();float[] lo={10,20,10,10,0},hi={170,180,160,170,1};
                        for(int i=0;i<5;i++)if(q[i]<lo[i]||q[i]>hi[i])throw new FormatException("M"+i+" outside logical range");
                        rows.Add(new RecordedHumanRow{Time=t,FrameId=id,TargetFrameId=id,Servo=q});
                    }
                    result.Streams[pair.Key]=RecordedHumanCsv.FromRows(rows,path,result.Sha256,header.Length);
                }
            }
            return result;
        }

        // Main-thread preflight uses the existing decoder specified by this compatibility
        // schema, independently of the user-selected destination. Never recalibrate twice.
        public void ValidateCompatibility()
        {
            if(!CompatibilityInverse||decoded)return;
            var targets=new List<float[]>();
            using(var decoder=new ControlStudioOutputPolicy(SingleArmCommandRouter.Home,BoardSide.Right))
            foreach(var row in Unassigned.Rows){
                var wrapped=new float[5];var candidate=new float[5];var approved=new float[5];var info=new int[4];
                int rc=decoder.SubmitHuman(row,wrapped,candidate,approved,info);
                if(rc<0||(info[0]!=1&&info[0]!=2)||info[1]!=1||info[2]!=0||info[3]!=0)throw new FormatException("Inverse decoder rejected/held frame "+row.FrameId);
                for(int i=0;i<5;i++){
                    if(!float.TryParse(row.Fields["source_servo_M"+i],NumberStyles.Float,CultureInfo.InvariantCulture,out float expected)||float.IsNaN(expected)||float.IsInfinity(expected)||Math.Abs(candidate[i]-expected)>.00002f||Math.Abs(approved[i]-expected)>.00002f)throw new FormatException("Inverse/source servo mismatch at frame "+row.FrameId+" M"+i);
                }
                targets.Add((float[])approved.Clone());
            }
            for(int i=0;i<targets.Count;i++)Unassigned.Rows[i].Servo=targets[i];
            decoded=true;
        }
    }
}
