using UnityEngine;
namespace HumanMotion.ControlStudio
{
    // Existing IDs remain reserved; VISUAL-2 uses new IDs without reinterpreting saved assets.
    public enum RobotVisualProfileId { G51=0, SingleArmHumanoid=1, DualArmHumanoid=2, Industrial=3, Humanoid=4, MechanicalDualTable=5, MechanicalHumanoid=6, HumanoidRobot=7 }
    public sealed class RobotVisualProfileDefinition : ScriptableObject
    {
        public RobotVisualProfileId id;public string displayName,sourceAssetPath;
        public RobotVisualProfileRig prefab;
        public bool rightArmLive=true,leftArmPreview;
    }
}
