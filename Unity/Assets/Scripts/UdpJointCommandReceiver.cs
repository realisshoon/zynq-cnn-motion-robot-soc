using System;
using System.Collections.Generic;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Runtime.Serialization.Json;
using System.Threading;
using System.Xml;
using System.Xml.Linq;
using UnityEngine;

[DisallowMultipleComponent]
[RequireComponent(typeof(RobotArmController))]
public sealed class UdpJointCommandReceiver : MonoBehaviour
{
    public RobotArmController controller;
    [Tooltip("이번 단계 기본값은 localhost. LAN 수신 시 수신 인터페이스 주소로 변경.")]
    public string listenAddress = "127.0.0.1";
    [Range(1, 65535)] public int port = 5005;
    public bool ignoreOldFrames = true;
    public bool debugLogging;

    public bool IsListening => session != null && session.Worker.IsAlive && !session.Stop;
    public bool LastStopJoined { get; private set; } = true;
    public uint LastAppliedFrameId { get; private set; }
    public JointCommandData LastAppliedCommand { get; private set; }
    public long AppliedCount { get; private set; }
    public string LastError { get; private set; } = "";
    public event Action<bool> ListenerStopped;

    // Worker는 이 순수 C# 상태만 접근한다. Unity 객체/Inspector/Debug API에 접근하지 않는다.
    private sealed class Session
    {
        internal readonly object Gate = new object();
        internal UdpClient Socket;
        internal Thread Worker;
        internal volatile bool Stop;
        internal bool IgnoreOld, HasFrame, Pending;
        internal uint LastFrame;
        internal JointCommandData Latest;
        internal long Received, Malformed, Stale, Invalid;
        internal string Error = "";
    }

    public struct Statistics
    {
        public long received, malformed, stale, invalid;
        public uint lastReceivedFrameId;
        public bool hasFrame;
    }

    private Session session;
    private Statistics stoppedStatistics;
    private int boundPort;
    private string boundAddress;
    private bool boundIgnoreOld;
    private bool attemptedStart;
    private float nextLogTime;

    public Statistics GetStatistics()
    {
        var current = session;
        if (current == null) return stoppedStatistics;
        lock (current.Gate) return Snapshot(current);
    }

    private static Statistics Snapshot(Session s) => new Statistics
    {
        received = s.Received, malformed = s.Malformed, stale = s.Stale, invalid = s.Invalid,
        lastReceivedFrameId = s.LastFrame, hasFrame = s.HasFrame
    };

    private void Reset() => controller = GetComponent<RobotArmController>();
    private void OnEnable()
    {
        if (controller == null) controller = GetComponent<RobotArmController>();
        attemptedStart = false;
        if (Application.isPlaying) UpdateConnection();
    }

    private void Update()
    {
        UpdateConnection();
        var current = session;
        if (current == null) return;
        JointCommandData command = default;
        bool pending;
        lock (current.Gate)
        {
            pending = current.Pending;
            if (pending) { command = current.Latest; current.Pending = false; }
            if (current.Error.Length > 0) LastError = current.Error;
        }
        // 한 Update에서 mailbox의 최신 명령을 최대 한 번 적용한다. 보간/스무딩 없음.
        if (pending && controller != null && controller.inputMode == RobotArmController.InputMode.UDP &&
            controller.ApplyCommand(command))
        {
            LastAppliedCommand = command;
            LastAppliedFrameId = command.frame_id;
            AppliedCount++;
        }
        if (debugLogging && Time.unscaledTime >= nextLogTime)
        {
            nextLogTime = Time.unscaledTime + 1f;
            var stats = GetStatistics();
            Debug.Log($"UDP {listenAddress}:{port}: rx={stats.received}, applied={AppliedCount}, " +
                $"frame={LastAppliedFrameId}, malformed={stats.malformed}, stale={stats.stale}, invalid={stats.invalid}. {LastError}", this);
        }
    }

    private void UpdateConnection()
    {
        if (controller == null || controller.inputMode != RobotArmController.InputMode.UDP)
        {
            StopListener();
            attemptedStart = false;
            return;
        }
        if (attemptedStart && (boundPort != port || boundAddress != listenAddress || boundIgnoreOld != ignoreOldFrames))
        {
            StopListener();
            attemptedStart = false;
        }
        if (!attemptedStart) StartListener();
    }

    [ContextMenu("Restart UDP Listener / Reset Frame Sequence")]
    public void RestartListener()
    {
        StopListener();
        attemptedStart = false;
        if (Application.isPlaying && isActiveAndEnabled) UpdateConnection();
    }

    private void StartListener()
    {
        attemptedStart = true;
        boundPort = port;
        boundAddress = listenAddress;
        boundIgnoreOld = ignoreOldFrames;
        LastError = "";
        stoppedStatistics = default;
        AppliedCount = 0;
        LastAppliedFrameId = 0;
        LastAppliedCommand = default;
        UdpClient socket = null;
        try
        {
            if (port < 1 || port > 65535 || !IPAddress.TryParse(listenAddress, out var address) ||
                address.AddressFamily != AddressFamily.InterNetwork)
                throw new ArgumentException("유효한 IPv4 listenAddress와 1~65535 port가 필요합니다.");
            socket = new UdpClient(AddressFamily.InterNetwork);
            socket.ExclusiveAddressUse = true;
            socket.Client.ReceiveTimeout = 200;
            socket.Client.Bind(new IPEndPoint(address, port));
            var current = new Session { Socket = socket, IgnoreOld = ignoreOldFrames };
            current.Worker = new Thread(() => ReceiveLoop(current)) { IsBackground = true, Name = "JointCommand UDP" };
            session = current;
            current.Worker.Start();
        }
        catch (Exception exception)
        {
            socket?.Close();
            session = null;
            LastError = "UDP 시작 실패: " + exception.Message;
            if (debugLogging) Debug.LogWarning(LastError, this);
        }
    }

    private static void ReceiveLoop(Session current)
    {
        var endpoint = new IPEndPoint(IPAddress.Any, 0);
        try
        {
            while (!current.Stop)
            {
                byte[] bytes;
                try { bytes = current.Socket.Receive(ref endpoint); }
                catch (SocketException exception) when (exception.SocketErrorCode == SocketError.TimedOut) { continue; }
                bool parsed = JointCommandJson.TryParse(bytes, out var command);
                lock (current.Gate)
                {
                    current.Received++;
                    if (!parsed) { current.Malformed++; continue; }
                    // uint32 wrap을 허용하는 serial-number 비교. 중복도 stale로 처리한다.
                    if (current.IgnoreOld && current.HasFrame && unchecked((int)(command.frame_id - current.LastFrame)) <= 0)
                    { current.Stale++; continue; }
                    current.HasFrame = true;
                    current.LastFrame = command.frame_id;
                    if (!command.valid) current.Invalid++;
                    // 최신 invalid도 mailbox를 교체한다. 앞선 미적용 valid를 뒤늦게 적용하지 않는다.
                    current.Latest = command;
                    current.Pending = true;
                }
            }
        }
        catch (Exception exception)
        {
            if (!current.Stop) lock (current.Gate) current.Error = "UDP 수신 종료: " + exception.Message;
        }
        finally { current.Socket.Close(); }
    }

    private void OnDisable() => StopListener();
    private void OnDestroy() => StopListener();
    private void OnApplicationQuit() => StopListener();

    private void StopListener()
    {
        var current = session;
        if (current == null) return;
        current.Stop = true;
        current.Socket.Close(); // blocking Receive를 깨운다. ReceiveTimeout도 종료를 보조한다.
        LastStopJoined = current.Worker.Join(1500);
        lock (current.Gate) stoppedStatistics = Snapshot(current);
        session = null;
        if (!LastStopJoined) LastError = "UDP 수신 스레드 종료 대기 시간 초과";
        ListenerStopped?.Invoke(LastStopJoined);
        if (debugLogging) Debug.Log($"UDP 종료: threadJoined={LastStopJoined}", this);
    }
}

// Unity API나 신규 package 없이 .NET JSON reader로 엄격한 필드/타입 검사를 한다.
internal static class JointCommandJson
{
    internal static bool TryParse(byte[] bytes, out JointCommandData command)
    {
        command = default;
        if (bytes == null || bytes.Length == 0 || bytes.Length > 4096) return false;
        try
        {
            var quotas = new XmlDictionaryReaderQuotas { MaxDepth = 4, MaxStringContentLength = 4096 };
            using (var reader = JsonReaderWriterFactory.CreateJsonReader(bytes, quotas))
            {
                var root = XDocument.Load(reader).Root;
                if (root == null || (string)root.Attribute("type") != "object") return false;
                var fields = new Dictionary<string, XElement>(StringComparer.Ordinal);
                foreach (var element in root.Elements()) fields.Add(element.Name.LocalName, element);
                if (fields.Count != 8) return false;
                if ((string)fields["frame_id"].Attribute("type") != "number" ||
                    !uint.TryParse(fields["frame_id"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out command.frame_id)) return false;
                if ((string)fields["valid"].Attribute("type") != "boolean") return false;
                command.valid = bool.Parse(fields["valid"].Value);
                command.base_deg = Number(fields["base_deg"]);
                command.shoulder_deg = Number(fields["shoulder_deg"]);
                command.elbow_deg = Number(fields["elbow_deg"]);
                command.wrist_pitch_deg = Number(fields["wrist_pitch_deg"]);
                command.wrist_roll_deg = Number(fields["wrist_roll_deg"]);
                command.gripper_norm = Number(fields["gripper_norm"]);
                return true;
            }
        }
        catch (Exception) { return false; } // 잘못된 외부 입력은 수신 루프를 종료하지 않는다.
    }

    private static float Number(XElement element)
    {
        if ((string)element.Attribute("type") != "number" || element.HasElements ||
            !float.TryParse(element.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out float value) ||
            float.IsNaN(value) || float.IsInfinity(value)) throw new FormatException("유한한 JSON 숫자가 필요합니다.");
        return value;
    }
}
