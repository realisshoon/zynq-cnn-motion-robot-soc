using System;
using System.IO;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioProfileStore : MonoBehaviour
    {
        [Serializable] public sealed class Preset
        {
            public int version=1;
            public string robotId=SingleArmCommandRouter.RobotId;
            public string kind="MANUAL_SERVO_PRESET";
            public float[] values;
        }
        public ManualServoSource source;
        public string Message {get;private set;}="Preset v1 / "+SingleArmCommandRouter.RobotId;
        public string FilePath=>Path.Combine(Application.persistentDataPath,"ControlStudio","manual-preset-v1.json");
        public bool Save()
        {
            try {
                if(!source.router.ValidatePreset(source.Values,out var reason))throw new InvalidDataException(reason);
                Directory.CreateDirectory(Path.GetDirectoryName(FilePath));
                string tmp=FilePath+".tmp";File.WriteAllText(tmp,JsonUtility.ToJson(new Preset{values=(float[])source.Values.Clone()},true));
                if(File.Exists(FilePath))File.Replace(tmp,FilePath,null);else File.Move(tmp,FilePath);
                Message="Saved preset v1";return true;
            }catch(Exception e){Message="SAVE REJECTED: "+e.Message;return false;}
        }
        public bool Load(){try{return LoadJson(File.ReadAllText(FilePath));}catch(Exception e){Message="LOAD REJECTED: "+e.Message;return false;}}
        public bool LoadJson(string json)
        {
            try {
                var p=JsonUtility.FromJson<Preset>(json);
                // Require identity fields explicitly: missing JSON fields must not inherit defaults.
                if(!json.Contains("\"version\"")||!json.Contains("\"robotId\"")||!json.Contains("\"kind\"")||p==null||p.version!=1||p.robotId!=SingleArmCommandRouter.RobotId||p.kind!="MANUAL_SERVO_PRESET")throw new InvalidDataException("Profile version / robot / kind mismatch");
                if(!source.LoadAtomic(p.values))throw new InvalidDataException(source.router.Status);
                Message="Loaded atomically";return true;
            }catch(Exception e){Message="LOAD REJECTED (previous target kept): "+e.Message;return false;}
        }
        public bool Restore(){bool ok=source.Home();Message=ok?"Default preset restored":source.router.Status;return ok;}
    }
}
