using UnityEngine;

/// <summary>
/// G51 / RoboCon 2-finger gear-linkage gripper visual.
///
/// Source intent:
/// - The real gripper has two meshed gears and side jaws connected by links.
/// - gripper_norm remains 0=close, 1=open.
/// - This component is VISUAL ONLY. It does not replace the robot-side calibration/safety logic.
///
/// The geometry below is a photo-proportion model, not a CAD/physical-dimension model.
/// </summary>
[ExecuteAlways]
[DisallowMultipleComponent]
public sealed class G51GripperVisual : MonoBehaviour
{
    [Header("Fixed jaw pivots")]
    public Transform leftJawPivot;
    public Transform rightJawPivot;

    [Header("Jaw link anchors")]
    public Transform leftJawLinkAnchor;
    public Transform rightJawLinkAnchor;

    [Header("Gear pivots / crank pins")]
    public Transform leftGear;
    public Transform rightGear;
    public Transform leftDrivePin;
    public Transform rightDrivePin;

    [Header("Dynamic connecting rods")]
    public Transform leftLinkBar;
    public Transform rightLinkBar;

    [Header("Visual solver frame (optional)")]
    [Tooltip("Use when the visible linkage is mounted away from this control Transform.")]
    public Transform visualRoot;

    [Header("Motion - visual placeholder; tune against real hardware")]
    [Tooltip("gripper_norm=0 at the visual model. Not a measured servo angle.")]
    public float closedDriveDeg = -28f;

    [Tooltip("gripper_norm=1 at the visual model. Not a measured servo angle.")]
    public float openDriveDeg = 12f;

    [Header("Link visual")]
    [Min(0.005f)] public float linkThickness = 0.045f;
    [Min(0.005f)] public float linkDepth = 0.055f;

    [SerializeField] private Quaternion leftGearRest = Quaternion.identity;
    [SerializeField] private Quaternion rightGearRest = Quaternion.identity;
    [SerializeField] private float leftLinkLength;
    [SerializeField] private float rightLinkLength;
    [SerializeField] private float leftBranchSign = 1f;
    [SerializeField] private float rightBranchSign = -1f;
    [SerializeField, Range(0f, 1f)] private float previewOpen = 0.5f;

    private Transform SolverFrame => visualRoot != null ? visualRoot : transform;

    /// <summary>
    /// Call once after Builder creates the neutral linkage geometry.
    /// The current geometry becomes the solver's 0-degree reference.
    /// </summary>
    public void CaptureRest()
    {
        if (leftGear != null) leftGearRest = leftGear.localRotation;
        if (rightGear != null) rightGearRest = rightGear.localRotation;

        leftLinkLength = LocalDistance(leftDrivePin, leftJawLinkAnchor);
        rightLinkLength = LocalDistance(rightDrivePin, rightJawLinkAnchor);

        leftBranchSign = CaptureBranch(leftJawPivot, leftDrivePin, leftJawLinkAnchor);
        rightBranchSign = CaptureBranch(rightJawPivot, rightDrivePin, rightJawLinkAnchor);

        // Degenerate sign should never happen with the Builder geometry, but keep a stable fallback.
        if (Mathf.Abs(leftBranchSign) < 0.5f) leftBranchSign = 1f;
        if (Mathf.Abs(rightBranchSign) < 0.5f) rightBranchSign = -1f;
    }

    public void Apply(float gripperNorm)
    {
        float t = Mathf.Clamp01(gripperNorm);
        previewOpen = t;
        float drive = Mathf.Lerp(closedDriveDeg, openDriveDeg, t);

        // Two meshed gears rotate in opposite directions.
        if (leftGear != null)
            leftGear.localRotation = leftGearRest * Quaternion.AngleAxis(+drive, Vector3.forward);
        if (rightGear != null)
            rightGear.localRotation = rightGearRest * Quaternion.AngleAxis(-drive, Vector3.forward);

        // The real gripper is linkage-driven. Do not directly map gripper_norm to a linear X slide.
        // Solve the two-circle 4-bar geometry after the crank pins move with the gears.
        SolveJaw(leftJawPivot, leftJawLinkAnchor, leftDrivePin, leftLinkLength, leftBranchSign);
        SolveJaw(rightJawPivot, rightJawLinkAnchor, rightDrivePin, rightLinkLength, rightBranchSign);

        UpdateLink(leftLinkBar, leftDrivePin, leftJawLinkAnchor);
        UpdateLink(rightLinkBar, rightDrivePin, rightJawLinkAnchor);
    }

    private void Update()
    {
        // Keep rods visually attached if the hierarchy is edited in Edit Mode.
        UpdateLink(leftLinkBar, leftDrivePin, leftJawLinkAnchor);
        UpdateLink(rightLinkBar, rightDrivePin, rightJawLinkAnchor);
    }

    private float LocalDistance(Transform a, Transform b)
    {
        if (a == null || b == null) return 0f;
        Vector3 la = SolverFrame.InverseTransformPoint(a.position);
        Vector3 lb = SolverFrame.InverseTransformPoint(b.position);
        return Vector2.Distance(new Vector2(la.x, la.y), new Vector2(lb.x, lb.y));
    }

    private float CaptureBranch(Transform jaw, Transform pin, Transform anchor)
    {
        if (jaw == null || pin == null || anchor == null) return 0f;

        Vector3 b3 = SolverFrame.InverseTransformPoint(jaw.position);
        Vector3 p3 = SolverFrame.InverseTransformPoint(pin.position);
        Vector3 q3 = SolverFrame.InverseTransformPoint(anchor.position);
        Vector2 d = new Vector2(p3.x - b3.x, p3.y - b3.y);
        Vector2 q = new Vector2(q3.x - b3.x, q3.y - b3.y);
        float cross = d.x * q.y - d.y * q.x;
        return Mathf.Sign(cross);
    }

    private bool SolveJaw(
        Transform jaw,
        Transform jawAnchor,
        Transform drivePin,
        float linkLength,
        float branchSign)
    {
        if (jaw == null || jawAnchor == null || drivePin == null || linkLength <= 0.0001f)
            return false;

        Vector3 b3 = SolverFrame.InverseTransformPoint(jaw.position);
        Vector3 p3 = SolverFrame.InverseTransformPoint(drivePin.position);
        Vector2 b = new Vector2(b3.x, b3.y);
        Vector2 p = new Vector2(p3.x, p3.y);
        Vector2 bp = p - b;
        float d = bp.magnitude;

        Vector2 anchorLocal = new Vector2(jawAnchor.localPosition.x, jawAnchor.localPosition.y);
        float jawRadius = anchorLocal.magnitude;
        if (d <= 0.0001f || jawRadius <= 0.0001f) return false;

        // Circle intersection feasibility: Q is jaw anchor, |B-Q|=jawRadius, |P-Q|=linkLength.
        if (d > jawRadius + linkLength || d < Mathf.Abs(jawRadius - linkLength))
            return false;

        float a = (jawRadius * jawRadius - linkLength * linkLength + d * d) / (2f * d);
        float h2 = jawRadius * jawRadius - a * a;
        if (h2 < -0.00001f) return false;
        float h = Mathf.Sqrt(Mathf.Max(0f, h2));

        Vector2 u = bp / d;
        Vector2 mid = b + a * u;
        Vector2 perp = new Vector2(-u.y, u.x);
        Vector2 q1 = mid + h * perp;
        Vector2 q2 = mid - h * perp;

        Vector2 q = BranchMatches(b, p, q1, branchSign) ? q1 : q2;

        float targetAngle = Mathf.Atan2(q.y - b.y, q.x - b.x) * Mathf.Rad2Deg;
        float localAnchorAngle = Mathf.Atan2(anchorLocal.y, anchorLocal.x) * Mathf.Rad2Deg;
        float jawRotation = targetAngle - localAnchorAngle;

        jaw.localRotation = Quaternion.AngleAxis(jawRotation, Vector3.forward);
        return true;
    }

    private static bool BranchMatches(Vector2 b, Vector2 p, Vector2 q, float sign)
    {
        Vector2 d = p - b;
        Vector2 r = q - b;
        float cross = d.x * r.y - d.y * r.x;
        return Mathf.Sign(cross) == Mathf.Sign(sign);
    }

    private void UpdateLink(Transform bar, Transform a, Transform b)
    {
        if (bar == null || a == null || b == null) return;

        Vector3 delta = b.position - a.position;
        float length = delta.magnitude;
        if (length < 0.0001f) return;

        bar.position = (a.position + b.position) * 0.5f;
        bar.rotation = Quaternion.FromToRotation(Vector3.up, delta.normalized);
        float localLength = visualRoot != null && bar.parent != null
            ? Vector3.Distance(
                bar.parent.InverseTransformPoint(a.position),
                bar.parent.InverseTransformPoint(b.position))
            : length;
        bar.localScale = new Vector3(linkThickness, localLength, linkDepth);
    }
}
