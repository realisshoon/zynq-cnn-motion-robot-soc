using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;

namespace HumanMotion.MediaPipeDebug
{
    public sealed class UdpMediaPipePoseSource : MonoBehaviour, IPoseSource
    {
        [Serializable] public sealed class Joint
        {
            public float[] xyz;
            public float visibility = -1, presence = -1;
            public PoseLandmark Convert() => new PoseLandmark {
                position = xyz.Length == 3 ? new Vector3(xyz[0], xyz[1], xyz[2]) : Vector3.one * float.NaN,
                visibility = visibility, presence = presence };
            public bool WellFormed => xyz != null && (xyz.Length == 0 || (xyz.Length == 3 &&
                PoseLandmark.Finite(xyz[0]) && PoseLandmark.Finite(xyz[1]) && PoseLandmark.Finite(xyz[2]))) &&
                PoseLandmark.Finite(visibility) && PoseLandmark.Finite(presence);
        }
        [Serializable] public sealed class Packet
        {
            public int version;
            public string session_id;
            public long frame_id = -1;
            public double timestamp, source_fps;
            public bool valid;
            public Joint left_shoulder, left_elbow, left_wrist, right_shoulder, right_elbow, right_wrist;
            public string preview_jpeg;
            public Joint[] Joints => new[] { left_shoulder, left_elbow, left_wrist, right_shoulder, right_elbow, right_wrist };
        }
        private struct Received { public byte[] bytes; public double at; }
        public string bindAddress = "127.0.0.1";
        public int port = 5055;
        [Min(.1f)] public float timeoutSeconds = .75f;
        public PoseFrame Current { get; private set; }
        public event Action<PoseFrame> FrameChanged;
        public Texture2D Preview { get; private set; }
        public bool HasPreview { get; private set; }
        public string Error { get; private set; } = "";
        public string SessionId { get; private set; }
        public long LastFrameId { get; private set; } = -1;
        public long AcceptedPackets { get; private set; }
        public long RejectedPackets { get; private set; }
        public double ReceiveFps { get; private set; }
        public double PacketAgeMs => lastReceipt <= 0 ? double.PositiveInfinity : (Now - lastReceipt) * 1000;
        public double CaptureAgeMs => lastTimestamp <= 0 ? double.PositiveInfinity : (UnixNow - lastTimestamp) * 1000;
        public bool Connected => lastReceipt > 0 && PacketAgeMs <= timeoutSeconds * 1000 && string.IsNullOrEmpty(threadError);
        private static double Now => (double)System.Diagnostics.Stopwatch.GetTimestamp() / System.Diagnostics.Stopwatch.Frequency;
        private static double UnixNow => (DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
        private readonly object gate = new object();
        private readonly Queue<Received> pending = new Queue<Received>();
        private readonly HashSet<string> retiredSessions = new HashSet<string>();
        private UdpClient client;
        private Thread worker;
        private volatile bool running;
        private volatile string threadError;
        private double lastReceipt, lastTimestamp, fpsStart;
        private int fpsCount;

        private void OnEnable()
        {
            Current = null; SessionId = null; LastFrameId = -1;
            lastReceipt = lastTimestamp = 0; AcceptedPackets = RejectedPackets = 0;
            ReceiveFps = 0; fpsCount = 0; fpsStart = Now; Error = ""; threadError = null;
            retiredSessions.Clear();
            Application.runInBackground = true;
            try
            {
                client = new UdpClient(AddressFamily.InterNetwork);
                client.Client.ExclusiveAddressUse = true;
                client.Client.Bind(new IPEndPoint(IPAddress.Parse(bindAddress), port));
                client.Client.ReceiveBufferSize = 1024 * 1024;
                running = true;
                worker = new Thread(ReceiveLoop) { IsBackground = true, Name = "MediaPipe RAW UDP" };
                worker.Start();
            }
            catch (Exception e) { Error = "UDP bind failed: " + e.Message; client?.Close(); client = null; }
        }

        private void ReceiveLoop()
        {
            var endpoint = new IPEndPoint(IPAddress.Any, 0);
            try
            {
                while (running)
                {
                    var bytes = client.Receive(ref endpoint);
                    if (bytes.Length > 60000) continue;
                    lock (gate)
                    {
                        // 입력 과다 시 오래된 packet을 버려 지연 누적을 막는다. 좌표는 수정하지 않는다.
                        if (pending.Count >= 32) pending.Dequeue();
                        pending.Enqueue(new Received { bytes = bytes, at = Now });
                    }
                }
            }
            catch (Exception e) { if (running) threadError = "UDP receive failed: " + e.Message; }
        }

        public static Packet Parse(byte[] bytes)
        {
            var p = JsonUtility.FromJson<Packet>(Encoding.UTF8.GetString(bytes));
            if (p == null || p.version != 1 || string.IsNullOrEmpty(p.session_id) || p.frame_id < 0 ||
                double.IsNaN(p.timestamp) || double.IsInfinity(p.timestamp) || p.timestamp <= 0 ||
                double.IsNaN(p.source_fps) || double.IsInfinity(p.source_fps) || p.source_fps < 0)
                throw new FormatException("Invalid UDP header");
            foreach (var joint in p.Joints)
                if (joint == null || !joint.WellFormed) throw new FormatException("Invalid UDP joint");
            return p;
        }

        private void Update()
        {
            if (threadError != null) Error = threadError;
            Received[] batch;
            lock (gate) { batch = pending.ToArray(); pending.Clear(); }
            Packet newest = null;
            foreach (var received in batch)
            {
                try
                {
                    var p = Parse(received.bytes);
                    if (Now - received.at > timeoutSeconds || UnixNow - p.timestamp > timeoutSeconds ||
                        p.timestamp > UnixNow + 1 || retiredSessions.Contains(p.session_id) ||
                        (p.session_id == SessionId && p.frame_id <= LastFrameId))
                    { ++RejectedPackets; continue; }
                    if (SessionId != null && p.session_id != SessionId)
                    {
                        if (p.timestamp < lastTimestamp) { ++RejectedPackets; continue; }
                        retiredSessions.Add(SessionId);
                    }
                    SessionId = p.session_id; LastFrameId = p.frame_id;
                    lastReceipt = received.at; lastTimestamp = p.timestamp;
                    ++AcceptedPackets; ++fpsCount; newest = p;
                }
                catch (Exception) { ++RejectedPackets; }
            }
            if (Now - fpsStart >= 1)
            { ReceiveFps = fpsCount / (Now - fpsStart); fpsStart = Now; fpsCount = 0; }
            if (newest != null)
            {
                var pose = new PoseFrame { frameId = newest.frame_id, timeSec = newest.timestamp,
                    sourceFps = newest.source_fps, valid = newest.valid };
                var joints = newest.Joints;
                for (int i = 0; i < 6; ++i) pose.landmarks[i] = joints[i].Convert();
                HasPreview = false;
                if (!string.IsNullOrEmpty(newest.preview_jpeg))
                {
                    try
                    {
                        if (Preview == null) Preview = new Texture2D(2, 2, TextureFormat.RGB24, false);
                        HasPreview = Preview.LoadImage(System.Convert.FromBase64String(newest.preview_jpeg));
                    }
                    catch (Exception) { HasPreview = false; }
                }
                Current = pose; FrameChanged?.Invoke(pose);
            }
            if (!Connected && Current != null)
            { Current = null; HasPreview = false; FrameChanged?.Invoke(null); }
        }

        private void OnDisable()
        {
            running = false; client?.Close(); worker?.Join(1000); worker = null; client = null;
            lock (gate) pending.Clear();
            Current = null; HasPreview = false; lastReceipt = 0; FrameChanged?.Invoke(null);
        }
        private void OnDestroy() { if (Preview != null) Destroy(Preview); }
    }
}
