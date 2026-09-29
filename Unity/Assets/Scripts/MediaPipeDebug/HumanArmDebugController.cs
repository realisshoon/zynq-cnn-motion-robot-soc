using UnityEngine;

namespace HumanMotion.MediaPipeDebug
{
    public sealed class HumanArmDebugController : MonoBehaviour
    {
        [Tooltip("IPoseSource를 구현한 컴포넌트")]
        public MonoBehaviour poseSource;
        public MediaPipeCoordinateMapping coordinateMapping = new MediaPipeCoordinateMapping();
        [Range(0, 1)] public float confidenceThreshold = .5f;
        public float jointRadius = .022f, linkWidth = .016f;
        public PoseFrame AppliedPose { get; private set; }
        public Transform[] Joints { get; private set; }
        public LineRenderer[] Links { get; private set; }
        public int LowConfidenceCount { get; private set; }
        public int MissingPositionCount { get; private set; }
        private IPoseSource source;
        private Material material;
        private readonly Color leftColor = new Color(.23f, .78f, 1f);
        private readonly Color rightColor = new Color(1f, .43f, .25f);
        private MaterialPropertyBlock properties;
        private static readonly string[] Names = { "Shoulder", "Elbow", "Wrist" };

        private void Awake()
        {
            properties = new MaterialPropertyBlock();
            Joints = new Transform[6]; Links = new LineRenderer[4];
            var shader = Shader.Find("Universal Render Pipeline/Unlit") ?? Shader.Find("Unlit/Color");
            material = new Material(shader);
            for (int arm = 0; arm < 2; ++arm)
            {
                var root = new GameObject(arm == 0 ? "LeftArm" : "RightArm").transform;
                root.SetParent(transform, false);
                for (int j = 0; j < 3; ++j)
                {
                    var sphere = GameObject.CreatePrimitive(PrimitiveType.Sphere);
                    sphere.name = Names[j]; sphere.layer = 30;
                    sphere.transform.SetParent(root, false);
                    sphere.transform.localScale = Vector3.one * (jointRadius * 2);
                    Destroy(sphere.GetComponent<Collider>());
                    sphere.GetComponent<Renderer>().sharedMaterial = material;
                    sphere.SetActive(false);
                    Joints[arm * 3 + j] = sphere.transform;
                }
                for (int link = 0; link < 2; ++link)
                {
                    var go = new GameObject(link == 0 ? "UpperArm" : "Forearm");
                    go.layer = 30; go.transform.SetParent(root, false);
                    var line = go.AddComponent<LineRenderer>();
                    line.sharedMaterial = material; line.useWorldSpace = true; line.positionCount = 2;
                    line.startWidth = line.endWidth = linkWidth; line.numCapVertices = 6;
                    line.enabled = false;
                    Links[arm * 2 + link] = line;
                }
            }
        }

        private void Start()
        {
            source = poseSource as IPoseSource;
            if (source == null) { Debug.LogError("IPoseSource 연결 필요", this); return; }
            source.FrameChanged += Apply;
            if (source.Current != null) Apply(source.Current);
        }

        public void Apply(PoseFrame frame)
        {
            AppliedPose = frame;
            LowConfidenceCount = 0;
            MissingPositionCount = 0;
            for (int i = 0; i < 6; ++i)
            {
                bool finite = frame != null && frame.landmarks[i].HasPosition;
                Joints[i].gameObject.SetActive(finite);
                if (!finite) { ++MissingPositionCount; continue; }
                var point = frame.landmarks[i];
                // valid/visibility는 색상에만 반영한다. 좌표는 RAW 그대로 사용한다.
                Joints[i].position = coordinateMapping.MediaPipeToUnity(point.position);
                bool low = Low(point);
                if (low) ++LowConfidenceCount;
                Paint(Joints[i].GetComponent<Renderer>(), low ? Color.yellow : i < 3 ? leftColor : rightColor);
            }
            for (int i = 0; i < 4; ++i)
            {
                int a = (i / 2) * 3 + i % 2, b = a + 1;
                bool finite = frame != null && frame.landmarks[a].HasPosition && frame.landmarks[b].HasPosition;
                Links[i].enabled = finite;
                if (!finite) continue;
                Links[i].SetPosition(0, Joints[a].position); Links[i].SetPosition(1, Joints[b].position);
                Paint(Links[i], Low(frame.landmarks[a]) || Low(frame.landmarks[b]) ? Color.yellow : i < 2 ? leftColor : rightColor);
            }
        }

        public bool Low(PoseLandmark p) => !PoseLandmark.Finite(p.visibility) || !PoseLandmark.Finite(p.presence) ||
            p.visibility < confidenceThreshold || p.presence < confidenceThreshold;
        private void Paint(Renderer renderer, Color color)
        {
            properties.SetColor("_BaseColor", color); properties.SetColor("_Color", color);
            renderer.SetPropertyBlock(properties);
        }
        private void LateUpdate() { if (AppliedPose != null) Apply(AppliedPose); }
        private void OnDestroy()
        { if (source != null) source.FrameChanged -= Apply; if (material != null) Destroy(material); }
    }
}
