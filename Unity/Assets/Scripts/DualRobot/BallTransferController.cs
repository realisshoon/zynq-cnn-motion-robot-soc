using UnityEngine;

public sealed class BallTransferController : MonoBehaviour
{
    public enum Ownership { None, HeldByLeft, HeldByRight }
    public Transform ball, leftGrab, rightGrab;
    [Min(0.001f)] public float ballRadius = 0.02f;
    [Min(0.001f)] public float captureTolerance = 0.012f;
    public Vector3 resetPosition;
    public Ownership Owner { get; private set; }
    public string LastError { get; private set; } = "";
    public float LastTransferJump { get; private set; }

    public void ResetBall()
    {
        ball.SetParent(transform, true);
        ball.position = resetPosition;
        ball.rotation = Quaternion.identity;
        ball.localScale = Vector3.one * (2f * ballRadius);
        Owner = Ownership.None;
        LastError = "";
        LastTransferJump = 0;
        if (ball.TryGetComponent<Rigidbody>(out var body))
        { body.isKinematic = true; body.useGravity = false; }
    }

    public bool GrabLeft()
    {
        if (Owner != Ownership.None) return Fail("공의 소유권이 이미 지정되어 있습니다.");
        if (Vector3.Distance(ball.position, leftGrab.position) > captureTolerance)
            return Fail("왼팔 GrabPoint가 공에서 너무 멉니다. Pick pose를 확인하세요.");
        ball.SetParent(leftGrab, true);
        Owner = Ownership.HeldByLeft;
        return true;
    }

    public bool TransferToRight()
    {
        if (Owner != Ownership.HeldByLeft) return Fail("왼팔이 공을 소유해야 전달할 수 있습니다.");
        if (Vector3.Distance(ball.position, rightGrab.position) > captureTolerance)
            return Fail("오른팔 GrabPoint가 전달 위치에 도착하지 않았습니다.");
        Vector3 before = ball.position;
        ball.SetParent(rightGrab, true); // world pose/scale 보존: handoff 순간 snap 금지.
        LastTransferJump = Vector3.Distance(before, ball.position);
        Owner = Ownership.HeldByRight;
        return true;
    }

    private bool Fail(string reason) { LastError = reason; return false; }
}
