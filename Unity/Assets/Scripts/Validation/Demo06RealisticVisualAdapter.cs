using System;
using UnityEngine;

// Visual-only. Input, command calibration, validity and HOLD remain upstream.
[DisallowMultipleComponent]
[DefaultExecutionOrder(1200)]
public sealed class Demo06RealisticVisualAdapter : MonoBehaviour
{
    [Serializable]
    public sealed class RotationBinding
    {
        public Transform logical;
        public Transform visual;
        public Quaternion logicalRest = Quaternion.identity;
        public Quaternion visualRest = Quaternion.identity;
        public Quaternion axisBasis = Quaternion.identity;

        public void Apply()
        {
            Quaternion delta = Quaternion.Inverse(logicalRest) * logical.localRotation;
            visual.localRotation = visualRest * axisBasis * delta * Quaternion.Inverse(axisBasis);
        }
    }

    public ForearmArmController controller;
    public RotationBinding m0, m1, m2, m3;

    public bool IsConfigured => controller != null && controller.IsConfigured &&
        Valid(m0) && Valid(m1) && Valid(m2) && Valid(m3);

    private bool Valid(RotationBinding binding) => binding != null && binding.logical != null &&
        binding.visual != null && binding.visual.IsChildOf(transform) &&
        !binding.logical.IsChildOf(transform);

    private void LateUpdate() { SyncVisuals(); }

    // Also used by the editor-only acceptance check; never writes logical pivots.
    public void SyncVisuals()
    {
        if (!IsConfigured) return;
        m0.Apply(); m1.Apply(); m2.Apply(); m3.Apply();
    }
}
