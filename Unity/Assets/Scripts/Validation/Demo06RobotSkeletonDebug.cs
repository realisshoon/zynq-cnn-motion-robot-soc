using UnityEngine;
using UnityEngine.Rendering;

// Demo_06 only. Draws the pivots already used by ForearmArmController.
[DisallowMultipleComponent]
[DefaultExecutionOrder(1000)]
public sealed class Demo06RobotSkeletonDebug : MonoBehaviour
{
    public ForearmArmController robot;
    public bool showDebugSkeleton = true;
    public bool showPivotAxes = true;
    public bool showWristPitchDirections = true;

    [Header("Wrist pitch hinge candidate (visual only)")]
    public Transform wristPitchPivotCapA;
    public Transform wristPitchPivotCapB;
    public bool showHingeComparison = true;

    [Header("Runtime read only")]
    [SerializeField] private bool configured;
    [SerializeField] private int segmentCount;
    [SerializeField] private bool hingeCandidateValid;
    [SerializeField] private float pivotDistance;
    [SerializeField] private float axisAngleDifferenceDeg;

    private Transform[] pivots;
    private LineRenderer[] segments;
    private LineRenderer[,] axes;
    private LineRenderer wristPitchAxis;
    private LineRenderer wristPitchDirection;
    private LineRenderer hingeCandidateAxis;
    private GameObject logicalPivotSphere;
    private GameObject hingeCandidateSphere;
    private Material lineMaterial;
    private Material logicalSphereMaterial;
    private Material hingeSphereMaterial;

    private void Awake()
    {
        if (robot == null || !robot.IsConfigured) return;
        Transform toolMount = robot.gripperVisual.transform.parent;
        if (toolMount == null || !toolMount.IsChildOf(robot.wristPitch)) return;

        // The root is a fixed anchor, not an invented Base_Yaw control pivot.
        pivots = new[] { robot.transform, robot.elbowRoll, robot.elbowPitch,
            robot.wristRoll, robot.wristPitch, toolMount, robot.gripperVisual.transform };
        segmentCount = pivots.Length - 1;

        Shader shader = Shader.Find("Universal Render Pipeline/Particles/Unlit");
        if (shader == null) shader = Shader.Find("Sprites/Default");
        if (shader == null) return;
        lineMaterial = new Material(shader) { name = "Demo06 Robot Skeleton Debug" };
        segments = new LineRenderer[segmentCount];
        for (int i = 0; i < segmentCount; i++)
            segments[i] = MakeLine("Pivot segment " + i, 0.006f,
                i == 0 ? new Color(1f, .75f, .2f) : new Color(.2f, 1f, 1f));

        axes = new LineRenderer[pivots.Length, 3];
        Color[] axisColors = { Color.red, Color.green, Color.blue };
        for (int i = 0; i < pivots.Length; i++)
            for (int axis = 0; axis < 3; axis++)
                axes[i, axis] = MakeLine("Pivot " + i + " axis " + axis,
                    0.0025f, axisColors[axis]);
        wristPitchAxis = MakeLine("WristPitch rotation axis +X", 0.006f, Color.yellow);
        wristPitchDirection = MakeLine("WristPitch rotating direction +Y", 0.006f, Color.green);
        hingeCandidateValid = wristPitchPivotCapA != null && wristPitchPivotCapB != null &&
            wristPitchPivotCapA.parent == wristPitchPivotCapB.parent &&
            wristPitchPivotCapA.parent.IsChildOf(robot.wristRoll) &&
            wristPitchPivotCapA != wristPitchPivotCapB;
        if (hingeCandidateValid)
        {
            hingeCandidateAxis = MakeLine("Visual hinge candidate axis", 0.006f, Color.magenta);
            logicalPivotSphere = MakeSphere("LOGICAL M2", Color.yellow, out logicalSphereMaterial);
            hingeCandidateSphere = MakeSphere("PHYSICAL HINGE", Color.magenta, out hingeSphereMaterial);
        }
        configured = true;
    }

    private GameObject MakeSphere(string markerName, Color color, out Material material)
    {
        var sphere = GameObject.CreatePrimitive(PrimitiveType.Sphere);
        sphere.name = markerName;
        sphere.transform.SetParent(transform, false);
        sphere.transform.localScale = Vector3.one * 0.06f; // 2.5x the old 0.024-unit cross.
        sphere.layer = 29;
        Collider collider = sphere.GetComponent<Collider>();
        collider.enabled = false;
        Destroy(collider);

        Shader shader = Shader.Find("Universal Render Pipeline/Unlit");
        if (shader == null) shader = Shader.Find("Unlit/Color");
        if (shader == null) shader = Shader.Find("Sprites/Default");
        if (shader == null) shader = lineMaterial.shader;
        material = new Material(shader) { name = markerName + " Debug Material" };
        if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
        if (material.HasProperty("_Color")) material.SetColor("_Color", color);
        Renderer renderer = sphere.GetComponent<Renderer>();
        renderer.sharedMaterial = material;
        renderer.shadowCastingMode = ShadowCastingMode.Off;
        renderer.receiveShadows = false;
        return sphere;
    }

    private LineRenderer MakeLine(string objectName, float width, Color color)
    {
        var go = new GameObject(objectName);
        go.transform.SetParent(transform, false);
        go.layer = 29; // Demo06Robot camera's existing robot layer.
        var line = go.AddComponent<LineRenderer>();
        line.sharedMaterial = lineMaterial;
        line.positionCount = 2;
        line.useWorldSpace = true;
        line.startWidth = line.endWidth = width;
        line.startColor = line.endColor = color;
        line.numCapVertices = 2;
        line.shadowCastingMode = ShadowCastingMode.Off;
        line.receiveShadows = false;
        return line;
    }

    private void LateUpdate()
    {
        if (!configured || pivots == null || segments == null || axes == null ||
            wristPitchAxis == null || wristPitchDirection == null) return;
        for (int i = 0; i < segments.Length; i++)
        {
            Vector3 a = pivots[i].position;
            Vector3 b = pivots[i + 1].position;
            // Co-located control pivots remain co-located; no fake link length.
            segments[i].enabled = showDebugSkeleton && (b - a).sqrMagnitude > 0.00000001f;
            segments[i].SetPosition(0, a);
            segments[i].SetPosition(1, b);
        }

        for (int i = 0; i < pivots.Length; i++)
        {
            Vector3 origin = pivots[i].position;
            Vector3[] directions = { pivots[i].right, pivots[i].up, pivots[i].forward };
            for (int axis = 0; axis < 3; axis++)
            {
                LineRenderer line = axes[i, axis];
                line.enabled = showPivotAxes &&
                    !(showHingeComparison && pivots[i] == robot.wristPitch && axis == 0);
                line.SetPosition(0, origin);
                line.SetPosition(1, origin + directions[axis] * .025f);
            }
        }

        Vector3 wristOrigin = robot.wristPitch.position;
        wristPitchAxis.enabled = showWristPitchDirections;
        wristPitchAxis.SetPosition(0, wristOrigin);
        wristPitchAxis.SetPosition(1, wristOrigin + robot.wristPitch.right * .15f);
        wristPitchDirection.enabled = showWristPitchDirections;
        wristPitchDirection.SetPosition(0, wristOrigin);
        wristPitchDirection.SetPosition(1, wristOrigin + robot.wristPitch.up * .15f);

        if (!hingeCandidateValid || hingeCandidateAxis == null ||
            logicalPivotSphere == null || hingeCandidateSphere == null) return;
        Vector3 capA = wristPitchPivotCapA.position;
        Vector3 capB = wristPitchPivotCapB.position;
        Vector3 candidateCenter = (capA + capB) * 0.5f;
        Vector3 candidateAxis = (capB - capA).normalized;
        Vector3 logicalAxis = robot.wristPitch.right;
        pivotDistance = Vector3.Distance(wristOrigin, candidateCenter);
        float directedAngle = Vector3.Angle(logicalAxis, candidateAxis);
        axisAngleDifferenceDeg = Mathf.Min(directedAngle, 180f - directedAngle);
        logicalPivotSphere.SetActive(showHingeComparison);
        logicalPivotSphere.transform.position = wristOrigin;
        hingeCandidateSphere.SetActive(showHingeComparison);
        hingeCandidateSphere.transform.position = candidateCenter;
        hingeCandidateAxis.enabled = showHingeComparison;
        hingeCandidateAxis.SetPosition(0, candidateCenter);
        hingeCandidateAxis.SetPosition(1, candidateCenter + candidateAxis * .15f);
    }

    private void OnDestroy()
    {
        if (lineMaterial != null) Destroy(lineMaterial);
        if (logicalSphereMaterial != null) Destroy(logicalSphereMaterial);
        if (hingeSphereMaterial != null) Destroy(hingeSphereMaterial);
    }
}
