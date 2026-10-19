using UnityEngine;

public sealed class WorldPositionProbe : MonoBehaviour
{
    public Transform target;

    private void Update()
    {
        if (target == null)
            return;

        Vector3 p = target.position;

        Debug.Log(
            $"WORLD POS | X={p.x:F3} Y={p.y:F3} Z={p.z:F3}"
        );
    }
}
