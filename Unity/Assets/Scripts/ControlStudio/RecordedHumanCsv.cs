using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

namespace HumanMotion.ControlStudio
{
    public sealed class RecordedHumanRow
    {
        public uint FrameId,TargetFrameId;
        public double Time;
        public float[] Human;
        public Pose3DFrame Pose;
        // target, major, finger, observable, hand, wrist, gripper_hold, body
        public int[] Flags;
        public IReadOnlyDictionary<string,string> Fields;
    }
    public sealed class RecordedHumanCsv
    {
        public static readonly string[] AngleNames={"elbow_roll_deg","elbow_pitch_deg","wrist_pitch_deg","wrist_roll_deg","gripper_norm"};
        public static readonly string[] FlagNames={"target_valid","major_fresh","finger_fresh","elbow_roll_observable","hand_fresh","wrist_valid","gripper_hold","body_frame_valid"};
        public IReadOnlyList<RecordedHumanRow> Rows {get;private set;}
        public string Path {get;private set;}
        public string Sha256 {get;private set;}
        public int Columns {get;private set;}
        public double Duration=>Rows[Rows.Count-1].Time;
        static readonly CultureInfo CI=CultureInfo.InvariantCulture;
        public static RecordedHumanCsv Load(string path,CsvInputMode mode=CsvInputMode.RecordedHumanAngles)
        {
            var bytes=File.ReadAllBytes(path);
            var result=Parse(new UTF8Encoding(false,true).GetString(bytes),mode);
            result.Path=System.IO.Path.GetFullPath(path);
            using(var hash=SHA256.Create())result.Sha256=BitConverter.ToString(hash.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();
            return result;
        }
        public static RecordedHumanCsv Parse(string text,CsvInputMode mode=CsvInputMode.RecordedHumanAngles)
        {
            var records=ReadRecords(text.TrimStart('\ufeff'));
            if(records.Count<2)throw new FormatException("Header and data rows required");
            var header=records[0];var map=new Dictionary<string,int>(StringComparer.Ordinal);
            for(int i=0;i<header.Length;i++)if(string.IsNullOrWhiteSpace(header[i])||map.ContainsKey(header[i]))throw new FormatException("Empty/duplicate header");else map.Add(header[i],i);
            bool xyz=mode!=CsvInputMode.RecordedHumanAngles;
            var xyzNames=new List<string>();foreach(string point in new[]{"shoulder_l","shoulder_r","elbow","wrist","finger1","finger2"})foreach(string axis in new[]{"_x3d","_y3d","_z"})xyzNames.Add(point+axis);
            var basisNames=new List<string>();foreach(string a in new[]{"x","y","z"})foreach(string b in new[]{"x","y","z"})basisNames.Add("body_"+a+"_"+b);
            var needed=xyz?xyzNames.Concat(basisNames).Concat(new[]{"active_arm","finger_branch_selected_valid","finger_branch_ever_selected"}).Concat(mode==CsvInputMode.XyzStoredBodyAuxGripper?new[]{"gripper_norm"}:new string[0]):AngleNames;
            foreach(string n in needed.Concat(FlagNames).Concat(new[]{"frame_id","time_sec","target_frame_id"}))if(!map.ContainsKey(n))throw new FormatException("Missing column: "+n);
            var rows=new List<RecordedHumanRow>();double previous=-1;
            for(int i=1;i<records.Count;i++)
            {
                var cells=records[i];if(cells.Length!=header.Length)throw new FormatException("Row "+i+": column count mismatch");
                var fields=header.Select((h,j)=>new{h,v=cells[j]}).ToDictionary(x=>x.h,x=>x.v,StringComparer.Ordinal);
                double Number(string n){if(!double.TryParse(fields[n],NumberStyles.Float,CI,out double v)||double.IsNaN(v)||double.IsInfinity(v))throw new FormatException("Row "+i+": nonfinite/invalid "+n);return v;}
                uint Id(string n){if(!uint.TryParse(fields[n],NumberStyles.None,CI,out uint v))throw new FormatException("Row "+i+": invalid "+n);return v;}
                // Validate all known numeric observation metadata; unknown extra columns remain untouched.
                foreach(string n in header){if(xyz&&(n.StartsWith("raw_")||AngleNames.Contains(n)&&n!="gripper_norm"||mode==CsvInputMode.XyzStoredBodyHoldGripper&&n=="gripper_norm"))continue;if(n.EndsWith("_x3d")||n.EndsWith("_y3d")||n.EndsWith("_z")||n.StartsWith("body_")||n.StartsWith("raw_")||n.StartsWith("gripper_")||n.StartsWith("finger_branch_")||n=="active_arm"||n=="update_ret")Number(n);}
                double time=Number("time_sec");if(time<0||time<=previous)throw new FormatException("Row "+i+": time must strictly increase (no sorting)");previous=time;
                var flags=FlagNames.Select(n=>{double v=Number(n);if(v!=0&&v!=1)throw new FormatException("Row "+i+": flag must be 0/1: "+n);return (int)v;}).ToArray();
                float Float(string n){double v=Number(n);if(v>float.MaxValue||v< -float.MaxValue)throw new FormatException("Row "+i+": float overflow "+n);return (float)v;}
                var angles=xyz?null:AngleNames.Select(Float).ToArray();Pose3DFrame pose=null;
                if(xyz){
                    if(Number("active_arm")!=1)throw new FormatException("XYZ CSV profile requires RIGHT arm");
                    var sf=new int[10];Array.Copy(flags,sf,8);int j=8;
                    foreach(string n in new[]{"finger_branch_selected_valid","finger_branch_ever_selected"}){double v=Number(n);if(v!=0&&v!=1)throw new FormatException("Invalid branch flag");sf[j++]=(int)v;}
                    pose=new Pose3DFrame{Xyz=xyzNames.Select(Float).ToArray(),BodyBasis=basisNames.Select(Float).ToArray(),SourceFlags=sf,AuxiliaryGripper=mode==CsvInputMode.XyzStoredBodyAuxGripper?Float("gripper_norm"):0};
                    if(pose.AuxiliaryGripper<0||pose.AuxiliaryGripper>1)throw new FormatException("Auxiliary gripper outside 0..1");
                }
                rows.Add(new RecordedHumanRow{FrameId=Id("frame_id"),TargetFrameId=Id("target_frame_id"),Time=time,Human=angles,Pose=pose,Flags=flags,Fields=fields});
            }
            return new RecordedHumanCsv{Rows=rows.AsReadOnly(),Columns=header.Length,Path="memory",Sha256=""};
        }
        static List<string[]> ReadRecords(string text)
        {
            var rows=new List<string[]>();var row=new List<string>();var field=new StringBuilder();bool quoted=false,closed=false;
            for(int i=0;i<text.Length;i++)
            {
                char c=text[i];
                if(quoted){if(c=='"'){if(i+1<text.Length&&text[i+1]=='"'){field.Append('"');i++;}else{quoted=false;closed=true;}}else field.Append(c);continue;}
                if(c=='"'){if(field.Length>0||closed)throw new FormatException("Malformed CSV quote");quoted=true;continue;}
                if(c==','||c=='\r'||c=='\n')
                {
                    row.Add(field.ToString());field.Clear();closed=false;
                    if(c!=','){rows.Add(row.ToArray());row.Clear();if(c=='\r'&&i+1<text.Length&&text[i+1]=='\n')i++;}
                    continue;
                }
                if(closed)throw new FormatException("Unexpected characters after quoted field");field.Append(c);
            }
            if(quoted)throw new FormatException("Unclosed CSV quote");
            if(field.Length>0||row.Count>0||closed){row.Add(field.ToString());rows.Add(row.ToArray());}
            return rows;
        }
    }
}
