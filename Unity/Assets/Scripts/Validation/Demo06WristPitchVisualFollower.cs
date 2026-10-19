using UnityEngine;

// Demo_06 only: copies the logical wrist-pitch rotation delta to visual geometry.
[DisallowMultipleComponent]
[DefaultExecutionOrder(1100)]
public sealed class Demo06WristPitchVisualFollower : MonoBehaviour
{
    public Transform logicalWristPitch;
    public Transform wristPitchVisualPivot;

    private Quaternion logicalRestRotation;
    private Quaternion visualRestRotation;
    private bool configured;

    private void Awake()
    {
        if (logicalWristPitch == null || wristPitchVisualPivot == null ||
            logicalWristPitch == wristPitchVisualPivot ||
            logicalWristPitch.parent != wristPitchVisualPivot.parent)
            return;

        logicalRestRotation = logicalWristPitch.localRotation;
        visualRestRotation = wristPitchVisualPivot.localRotation;
        configured = true;
    }

    private void LateUpdate()
    {
        if (!configured || logicalWristPitch == null || wristPitchVisualPivot == null)
            return;

        Quaternion logicalDelta =
            Quaternion.Inverse(logicalRestRotation) * logicalWristPitch.localRotation;
        wristPitchVisualPivot.localRotation = visualRestRotation * logicalDelta;
    }
}
