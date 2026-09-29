using UnityEngine;

public class XyzTelemetrySkeletonVisualizer : MonoBehaviour
{
    [Header("Source")]
    [SerializeField] private XyzTelemetryUdpReceiver receiver;

    [Header("Placement")]
    [SerializeField] private Vector3 worldOffset = new Vector3(0f, 1.4f, 0f);
    [SerializeField] private float poseScale = 0.5f;
    [SerializeField] private bool centerOnShoulders = true;

    [Header("Axis")]
    [SerializeField] private bool flipX = false;
    [SerializeField] private bool flipY = false;
    [SerializeField] private bool flipZ = false;

    [Header("Visual")]
    [SerializeField] private float jointRadius = 0.035f;
    [SerializeField] private float boneRadius = 0.015f;

    private Transform visualRoot;

    private Transform leftShoulder;
    private Transform leftElbow;
    private Transform leftWrist;

    private Transform rightShoulder;
    private Transform rightElbow;
    private Transform rightWrist;

    private Transform leftUpperArm;
    private Transform leftForearm;

    private Transform rightUpperArm;
    private Transform rightForearm;

    private Transform shoulderLine;

    private void Reset()
    {
        receiver = GetComponent<XyzTelemetryUdpReceiver>();
    }

    private void Awake()
    {
        if (receiver == null)
            receiver = GetComponent<XyzTelemetryUdpReceiver>();

        BuildVisuals();
    }

    private void Update()
    {
        if (receiver == null)
            return;

        bool anyValid =
            receiver.LeftValid ||
            receiver.RightValid;

        if (!anyValid)
        {
            if (visualRoot != null)
                visualRoot.gameObject.SetActive(false);

            return;
        }

        visualRoot.gameObject.SetActive(true);

        Vector3 ls = ConvertPoint(receiver.LeftShoulder);
        Vector3 le = ConvertPoint(receiver.LeftElbow);
        Vector3 lw = ConvertPoint(receiver.LeftWrist);

        Vector3 rs = ConvertPoint(receiver.RightShoulder);
        Vector3 re = ConvertPoint(receiver.RightElbow);
        Vector3 rw = ConvertPoint(receiver.RightWrist);

        Vector3 origin = Vector3.zero;

        if (centerOnShoulders &&
            receiver.LeftValid &&
            receiver.RightValid)
        {
            origin = (ls + rs) * 0.5f;
        }

        ls = (ls - origin) * poseScale + worldOffset;
        le = (le - origin) * poseScale + worldOffset;
        lw = (lw - origin) * poseScale + worldOffset;

        rs = (rs - origin) * poseScale + worldOffset;
        re = (re - origin) * poseScale + worldOffset;
        rw = (rw - origin) * poseScale + worldOffset;

        if (receiver.LeftValid)
        {
            leftShoulder.position = ls;
            leftElbow.position = le;
            leftWrist.position = lw;

            UpdateBone(leftUpperArm, ls, le);
            UpdateBone(leftForearm, le, lw);

            leftShoulder.gameObject.SetActive(true);
            leftElbow.gameObject.SetActive(true);
            leftWrist.gameObject.SetActive(true);
            leftUpperArm.gameObject.SetActive(true);
            leftForearm.gameObject.SetActive(true);
        }
        else
        {
            SetLeftActive(false);
        }

        if (receiver.RightValid)
        {
            rightShoulder.position = rs;
            rightElbow.position = re;
            rightWrist.position = rw;

            UpdateBone(rightUpperArm, rs, re);
            UpdateBone(rightForearm, re, rw);

            rightShoulder.gameObject.SetActive(true);
            rightElbow.gameObject.SetActive(true);
            rightWrist.gameObject.SetActive(true);
            rightUpperArm.gameObject.SetActive(true);
            rightForearm.gameObject.SetActive(true);
        }
        else
        {
            SetRightActive(false);
        }

        if (receiver.LeftValid && receiver.RightValid)
        {
            UpdateBone(shoulderLine, ls, rs);
            shoulderLine.gameObject.SetActive(true);
        }
        else
        {
            shoulderLine.gameObject.SetActive(false);
        }
    }

    private Vector3 ConvertPoint(Vector3 p)
    {
        if (flipX) p.x = -p.x;
        if (flipY) p.y = -p.y;
        if (flipZ) p.z = -p.z;

        return p;
    }

    private void BuildVisuals()
    {
        GameObject root = new GameObject("XYZ_Skeleton_Visual");
        visualRoot = root.transform;
        visualRoot.SetParent(transform, false);

        leftShoulder = CreateJoint("LeftShoulder");
        leftElbow = CreateJoint("LeftElbow");
        leftWrist = CreateJoint("LeftWrist");

        rightShoulder = CreateJoint("RightShoulder");
        rightElbow = CreateJoint("RightElbow");
        rightWrist = CreateJoint("RightWrist");

        leftUpperArm = CreateBone("LeftUpperArm");
        leftForearm = CreateBone("LeftForearm");

        rightUpperArm = CreateBone("RightUpperArm");
        rightForearm = CreateBone("RightForearm");

        shoulderLine = CreateBone("ShoulderLine");
    }

    private Transform CreateJoint(string name)
    {
        GameObject go = GameObject.CreatePrimitive(
            PrimitiveType.Sphere
        );

        go.name = name;
        go.transform.SetParent(visualRoot, false);

        go.transform.localScale =
            Vector3.one * jointRadius * 2f;

        Collider c = go.GetComponent<Collider>();
        if (c != null)
            Destroy(c);

        return go.transform;
    }

    private Transform CreateBone(string name)
    {
        GameObject go = GameObject.CreatePrimitive(
            PrimitiveType.Cylinder
        );

        go.name = name;
        go.transform.SetParent(visualRoot, false);

        Collider c = go.GetComponent<Collider>();
        if (c != null)
            Destroy(c);

        return go.transform;
    }

    private void UpdateBone(
        Transform bone,
        Vector3 a,
        Vector3 b)
    {
        Vector3 direction = b - a;
        float length = direction.magnitude;

        if (length < 0.0001f)
        {
            bone.gameObject.SetActive(false);
            return;
        }

        bone.gameObject.SetActive(true);

        bone.position = (a + b) * 0.5f;

        bone.rotation = Quaternion.FromToRotation(
            Vector3.up,
            direction.normalized
        );

        // Unity Cylinder 기본 높이 = 2
        bone.localScale = new Vector3(
            boneRadius,
            length * 0.5f,
            boneRadius
        );
    }

    private void SetLeftActive(bool value)
    {
        leftShoulder.gameObject.SetActive(value);
        leftElbow.gameObject.SetActive(value);
        leftWrist.gameObject.SetActive(value);

        leftUpperArm.gameObject.SetActive(value);
        leftForearm.gameObject.SetActive(value);
    }

    private void SetRightActive(bool value)
    {
        rightShoulder.gameObject.SetActive(value);
        rightElbow.gameObject.SetActive(value);
        rightWrist.gameObject.SetActive(value);

        rightUpperArm.gameObject.SetActive(value);
        rightForearm.gameObject.SetActive(value);
    }
}
