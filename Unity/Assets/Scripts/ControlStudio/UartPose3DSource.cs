using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Threading;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // RX is an input source only. Serial worker enqueues bytes; Unity/main thread owns parsing and C calls.
    [DefaultExecutionOrder(-150)]
    public sealed class UartPose3DSource : MonoBehaviour
    {
        public SingleArmCommandRouter router;
        public float staleTimeoutSeconds = .5f;
        public bool Connected { get { lock (stateLock) return connected; } }
        public bool Mock { get; private set; } = true;
        public bool Active => router != null && router.Source == "UART";
        public string Status { get; private set; } = "DISCONNECTED";
        public string BodyFrameStatus { get; private set; } = "UNAVAILABLE / no packet";
        public int PacketsRx { get; private set; }
        public int ParseErrors { get; private set; }
        public int Dropped { get; private set; }
        public int Duplicates { get; private set; }
        public uint LastFrameId { get; private set; }
        public double LastTimestamp { get; private set; }
        public double PacketAgeMs => lastArrival < 0 ? double.NaN : (Time.realtimeSinceStartupAsDouble - lastArrival) * 1000;
        public XyzSolveResult LastSolve { get; private set; }
        public RecordedHumanRow LastPoseRow { get; private set; }
        public string Error { get; private set; } = "";
        public CsvInputMode Mode { get; private set; } = CsvInputMode.XyzStoredBodyAuxGripper;
        public string PortName { get; private set; } = "";
        public int BaudRate { get; private set; } = 115200;
        readonly ConcurrentQueue<byte[]> byteQueue = new ConcurrentQueue<byte[]>();
        readonly UartPoseLineFramer framer = new UartPoseLineFramer();
        readonly object stateLock = new object();
        Thread worker;
        int session, queued;
        bool connected, hasFrame;
        double lastArrival = -1, mockClock;
        byte[][] mockLines;
        double[] mockTimes;
        int mockIndex;
        ControlStudioXyzPolicy solver;

        void Awake() { if (router == null) router = GetComponent<SingleArmCommandRouter>(); }
        public bool SelectSource(CsvInputMode mode)
        {
            if (mode == CsvInputMode.RecordedHumanAngles) { Error = "UART requires XYZ mode"; return false; }
            ControlStudioXyzPolicy next = null;
            try
            {
                next = new ControlStudioXyzPolicy(router.Epoch + 1, router.Applied[4], mode);
                if (!router.ResetOwner("UART")) { next.Dispose(); return false; }
                solver?.Dispose(); solver = next; Mode = mode; ResetObservations();
                router.SetPaused(false); Status = Connected ? "CONNECTED / WAITING POSE" : "DISCONNECTED / HOLD";
                return true;
            }
            catch (Exception e) { next?.Dispose(); Error = "XYZ UNAVAILABLE: " + e.Message; Status = Error; return false; }
        }
        void ResetObservations()
        {
            while (byteQueue.TryDequeue(out _)) { } Interlocked.Exchange(ref queued, 0);
            framer.Reset(); hasFrame = false; lastArrival = -1; LastSolve = null; LastPoseRow = null;
            BodyFrameStatus = "UNAVAILABLE / no packet"; Error = ""; mockClock = 0; mockIndex = 0;
        }
        public bool ConnectMock()
        {
            Disconnect(); Mock = true; lock (stateLock) connected = true;
            PortName = "MOCK BYTE STREAM"; Status = "CONNECTED / MOCK RX";
            return !Active || SelectSource(Mode);
        }
        public bool ConnectReal(string name, int baud)
        {
            Disconnect();
            if (string.IsNullOrWhiteSpace(name) || baud < 1200 || baud > 1000000)
            { Status = "INVALID COM / BAUD"; return false; }
#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
            Mock = false; PortName = name.Trim(); BaudRate = baud;
            int version = Volatile.Read(ref session);
            worker = new Thread(() => ReceiveLoop(version, PortName, baud)) { IsBackground = true, Name = "ControlStudio UART RX" };
            worker.Start(); Status = "CONNECTING / RX ONLY";
            return !Active || SelectSource(Mode);
#else
            Status = "REAL COM RX UNSUPPORTED"; return false;
#endif
        }
        public void Disconnect()
        {
            Interlocked.Increment(ref session);
            lock (stateLock) connected = false;
            worker = null; mockLines = null; mockTimes = null; ResetObservations(); Status = "DISCONNECTED / HOLD";
        }
        public bool FeedMockBytes(byte[] bytes)
        {
            if (!Mock || !Connected || bytes == null) return false;
            Enqueue(bytes); return true;
        }
        public bool LoadMockStream(string path)
        {
            try
            {
                if (!Mock || !Connected) throw new InvalidOperationException("Connect Mock RX first");
                // The file is a Python-produced byte stream; it still enters the same byte framer.
                var lines = File.ReadAllLines(path, Encoding.ASCII);
                var data = new byte[lines.Length][]; var times = new double[lines.Length];
                for (int i = 0; i < lines.Length; i++)
                {
                    if (!UartPose3DProtocol.TryParse(lines[i], out var row, out var reason))
                        throw new FormatException("Line " + (i + 1) + ": " + reason);
                    data[i] = Encoding.ASCII.GetBytes(lines[i] + "\n"); times[i] = row.Time;
                    if (i > 0 && times[i] <= times[i - 1]) throw new FormatException("Nonmonotonic packet time");
                }
                if (Active && !SelectSource(Mode)) throw new InvalidOperationException(Error);
                mockLines = data; mockTimes = times; mockIndex = 0; mockClock = 0;
                Status = "MOCK STREAM READY / " + data.Length + " packets"; return true;
            }
            catch (Exception e) { Error = e.Message; Status = "MOCK LOAD ERROR: " + Error; return false; }
        }
        void Enqueue(byte[] bytes)
        {
            if (Interlocked.Increment(ref queued) > 64)
            { Interlocked.Decrement(ref queued); Interlocked.Increment(ref overflowCount); return; }
            byteQueue.Enqueue((byte[])bytes.Clone());
        }
        int overflowCount;
        void Update()
        {
            if (router == null) return;
            int lost = Interlocked.Exchange(ref overflowCount, 0); if (lost != 0) { Dropped += lost; Error = "RX BYTE QUEUE OVERFLOW"; }
            if (mockLines != null && Active && !router.Paused && Connected)
            {
                mockClock += Math.Min(Time.unscaledDeltaTime, .5f);
                int budget = 32;
                while (mockIndex < mockLines.Length && mockTimes[mockIndex] <= mockClock + 1e-9 && budget-- > 0)
                    Enqueue(mockLines[mockIndex++]);
            }
            int chunks = 64;
            while (chunks-- > 0 && byteQueue.TryDequeue(out var bytes))
            {
                Interlocked.Decrement(ref queued);
                framer.Feed(bytes, bytes.Length, ConsumeLine);
            }
            int framingErrors = framer.TakeFramingErrors();
            if (framingErrors > 0) { ParseErrors += framingErrors; Error = "BYTE FRAMING ERROR"; }
            if (!Connected) Status = "DISCONNECTED / HOLD";
            else if (lastArrival < 0 && Status.StartsWith("CONNECTING")) Status = "CONNECTED / WAITING POSE";
            else if (lastArrival >= 0 && PacketAgeMs > Math.Max(.05f, staleTimeoutSeconds) * 1000) Status = "STALE / HOLD";
            else if (lastArrival >= 0 && Active) Status = "LIVE / RIGHT ARM XYZ";
        }
        void ConsumeLine(string line)
        {
            if (!UartPose3DProtocol.TryParse(line, out var row, out var why))
            { ParseErrors++; Error = why; if (why.StartsWith("BODY FRAME")) BodyFrameStatus = "UNAVAILABLE"; Status = "PARSE ERROR / " + why; return; }
            PacketsRx++;
            if (hasFrame && (row.FrameId <= LastFrameId || row.Time <= LastTimestamp))
            { Duplicates++; Error = "DUPLICATE / OUT OF ORDER"; return; }
            if (hasFrame && row.FrameId > LastFrameId && row.FrameId - LastFrameId > 1)
                Dropped += (int)Math.Min((uint)int.MaxValue, row.FrameId - LastFrameId - 1);
            hasFrame = true; LastFrameId = row.FrameId; LastTimestamp = row.Time;
            lastArrival = Time.realtimeSinceStartupAsDouble; BodyFrameStatus = "PRESENT / stored basis"; LastPoseRow = row;
            if (!Active || router.Paused || solver == null) return;
            try
            {
                LastSolve = solver.Solve(row, router.Epoch);
                router.SubmitHuman(LastSolve.Target, router.Epoch);
                Status = "LIVE / RIGHT ARM XYZ";
            }
            catch (Exception e) { Error = "SOLVER REJECTED: " + e.Message; Status = Error; }
        }
#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
        void ReceiveLoop(int version, string name, int baud)
        {
            try
            {
                using (var port = new SerialPort(name, baud, Parity.None, 8, StopBits.One))
                {
                    port.ReadTimeout = 50; port.DtrEnable = false; port.RtsEnable = false; port.Open();
                    lock (stateLock) if (version == Volatile.Read(ref session)) connected = true;
                    var buffer = new byte[512];
                    while (version == Volatile.Read(ref session))
                    {
                        try { int n = port.Read(buffer, 0, buffer.Length); if (n > 0) { var bytes = new byte[n]; Array.Copy(buffer, bytes, n); Enqueue(bytes); } }
                        catch (TimeoutException) { }
                    }
                }
            }
            catch (Exception e) { if (version == Volatile.Read(ref session)) { Error = "COM RX ERROR: " + e.Message; Status = Error; } }
            finally { lock (stateLock) if (version == Volatile.Read(ref session)) connected = false; }
        }
#endif
        void OnDisable() { Disconnect(); solver?.Dispose(); solver = null; }
    }
}
