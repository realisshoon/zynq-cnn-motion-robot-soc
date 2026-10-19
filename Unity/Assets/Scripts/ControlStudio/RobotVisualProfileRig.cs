using System;
using UnityEngine;
namespace HumanMotion.ControlStudio
{
    public sealed class RobotVisualProfileRig : MonoBehaviour
    {
        [Serializable] public sealed class Joint
        {
            public string sourcePath;public Transform visual;
            public Vector3 localOffset;public Quaternion restRotation=Quaternion.identity,logicalRest=Quaternion.identity,axisBasis=Quaternion.identity;
        }
        [Serializable] public sealed class Linkage
        {public string sourcePath,sourceIndexPath;public Transform visual;[NonSerialized] public Transform source;}
        [Serializable] public sealed class ToolShell
        {public ToolKind tool;public string displayName;public Transform[] roots;}
        public Vector3 previewMirrorAxis=Vector3.right;
        public Joint[] joints;public Linkage[] linkage;public Transform toolMount,rightArm,leftArmPreview,hand,leftHandlerGrip,leftHandlerHand;
        public Transform[] leftFeederJoints;
        public G51GripperVisual sourceGripperVisual;
        public G51GripperVisual leftHandlerGripperVisual;
        public int sourceRightRendererCount,sourceLeftRendererCount;
        public ToolShell[] toolShells;
        Demo06RealisticVisualAdapter.RotationBinding[] bindings;
        public void Bind(Demo06RealisticVisualAdapter adapter)
        {
            var original=new[]{adapter.m0,adapter.m1,adapter.m2,adapter.m3};bindings=new Demo06RealisticVisualAdapter.RotationBinding[4];
            for(int i=0;i<4;i++){var j=joints[i];j.visual.localPosition=j.localOffset;bindings[i]=new Demo06RealisticVisualAdapter.RotationBinding{logical=original[i].logical,visual=j.visual,logicalRest=j.logicalRest,visualRest=j.restRotation,axisBasis=j.axisBasis};}
            foreach(var link in linkage)link.source=string.IsNullOrEmpty(link.sourceIndexPath)?adapter.transform.Find(link.sourcePath):ByIndex(adapter.transform,link.sourceIndexPath);
        }
        static Transform ByIndex(Transform root,string path)
        {
            foreach(var part in path.Split('/')){
                if(!int.TryParse(part,out int index)||index<0||index>=root.childCount)return null;
                root=root.GetChild(index);
            }
            return root;
        }
        public bool Supports(ToolKind tool)
        {return toolShells!=null&&Array.Exists(toolShells,x=>x!=null&&x.tool==tool&&x.roots!=null&&x.roots.Length>0);}
        public string ShellName(ToolKind tool)
        {var shell=toolShells==null?null:Array.Find(toolShells,x=>x!=null&&x.tool==tool);return shell==null?"UNAVAILABLE":shell.displayName;}
        public void Sync(bool showHand,ToolKind tool,float gripperNorm)
        {
            if(bindings==null)return;
            foreach(var b in bindings)b.Apply();
            foreach(var l in linkage)if(l.source!=null){l.visual.localPosition=l.source.localPosition;l.visual.localRotation=l.source.localRotation;l.visual.localScale=l.source.localScale;}
            hand.gameObject.SetActive(showHand);
            if(showHand&&sourceGripperVisual!=null)sourceGripperVisual.Apply(gripperNorm);
            if(toolShells!=null)foreach(var shell in toolShells)if(shell!=null&&shell.roots!=null)
                foreach(var root in shell.roots)if(root!=null)root.gameObject.SetActive(shell.tool==tool);
        }
    }
}
