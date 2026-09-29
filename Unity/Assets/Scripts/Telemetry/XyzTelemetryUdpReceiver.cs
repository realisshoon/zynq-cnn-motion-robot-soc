using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using UnityEngine;

public class XyzTelemetryUdpReceiver : MonoBehaviour
{
    private const int PacketSize = 86;
    private const byte Sync0 = 0xA5;
    private const byte Sync1 = 0x5A;
    private const byte Version = 0x01;
    private const byte TypeXyz = 0x02;
    private const byte PayloadSize = 78;

    [Header("UDP")]
    [SerializeField] private int port = 5010;

    [Header("Status")]
    [SerializeField] private bool listening;
    [SerializeField] private uint received;
    [SerializeField] private uint validPackets;
    [SerializeField] private uint badSize;
    [SerializeField] private uint badHeader;
    [SerializeField] private uint crcErrors;
    [SerializeField] private uint lastFrameId;

    [Header("LEFT")]
    [SerializeField] private bool leftValid;
    [SerializeField] private bool leftFresh;
    [SerializeField] private Vector3 leftShoulder;
    [SerializeField] private Vector3 leftElbow;
    [SerializeField] private Vector3 leftWrist;

    [Header("RIGHT")]
    [SerializeField] private bool rightValid;
    [SerializeField] private bool rightFresh;
    [SerializeField] private Vector3 rightShoulder;
    [SerializeField] private Vector3 rightElbow;
    [SerializeField] private Vector3 rightWrist;

    public bool LeftValid => leftValid;
    public bool LeftFresh => leftFresh;
    public Vector3 LeftShoulder => leftShoulder;
    public Vector3 LeftElbow => leftElbow;
    public Vector3 LeftWrist => leftWrist;

    public bool RightValid => rightValid;
    public bool RightFresh => rightFresh;
    public Vector3 RightShoulder => rightShoulder;
    public Vector3 RightElbow => rightElbow;
    public Vector3 RightWrist => rightWrist;

    public uint LastFrameId => lastFrameId;

    private UdpClient client;
    private Thread receiveThread;
    private volatile bool running;

    private readonly object packetLock = new object();
    private Packet latestPacket;
    private bool packetPending;

    private sealed class Packet
    {
        public uint frameId;

        public bool leftValid;
        public bool leftFresh;
        public bool rightValid;
        public bool rightFresh;

        public Vector3 leftShoulder;
        public Vector3 leftElbow;
        public Vector3 leftWrist;

        public Vector3 rightShoulder;
        public Vector3 rightElbow;
        public Vector3 rightWrist;
    }

    private void OnEnable()
    {
        StartReceiver();
    }

    private void OnDisable()
    {
        StopReceiver();
    }

    private void OnDestroy()
    {
        StopReceiver();
    }

    private void Update()
    {
        Packet p = null;

        lock (packetLock)
        {
            if (packetPending)
            {
                p = latestPacket;
                packetPending = false;
            }
        }

        if (p == null)
            return;

        lastFrameId = p.frameId;

        leftValid = p.leftValid;
        leftFresh = p.leftFresh;
        leftShoulder = p.leftShoulder;
        leftElbow = p.leftElbow;
        leftWrist = p.leftWrist;

        rightValid = p.rightValid;
        rightFresh = p.rightFresh;
        rightShoulder = p.rightShoulder;
        rightElbow = p.rightElbow;
        rightWrist = p.rightWrist;
    }

    private void StartReceiver()
    {
        if (running)
            return;

        try
        {
            client = new UdpClient(port);
            client.Client.ReceiveTimeout = 500;

            running = true;
            listening = true;

            receiveThread = new Thread(ReceiveLoop);
            receiveThread.IsBackground = true;
            receiveThread.Name = "XYZ Telemetry UDP";
            receiveThread.Start();

            Debug.Log($"XYZ telemetry listening on UDP {port}", this);
        }
        catch (Exception e)
        {
            listening = false;
            Debug.LogError($"XYZ UDP start failed: {e.Message}", this);
        }
    }

    private void StopReceiver()
    {
        running = false;
        listening = false;

        try
        {
            client?.Close();
        }
        catch
        {
        }

        if (receiveThread != null &&
            receiveThread.IsAlive &&
            Thread.CurrentThread != receiveThread)
        {
            receiveThread.Join(1000);
        }

        receiveThread = null;
        client = null;
    }

    private void ReceiveLoop()
    {
        IPEndPoint remote = new IPEndPoint(IPAddress.Any, 0);

        while (running)
        {
            try
            {
                byte[] data = client.Receive(ref remote);

                received++;

                if (data.Length != PacketSize)
                {
                    badSize++;
                    continue;
                }

                Packet decoded;

                if (!TryDecode(data, out decoded))
                    continue;

                validPackets++;

                lock (packetLock)
                {
                    latestPacket = decoded;
                    packetPending = true;
                }
            }
            catch (SocketException e)
            {
                if (!running)
                    break;

                if (e.SocketErrorCode != SocketError.TimedOut)
                    Debug.LogWarning($"XYZ UDP socket: {e.Message}", this);
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            catch (Exception e)
            {
                if (running)
                    Debug.LogWarning($"XYZ UDP receive: {e.Message}", this);
            }
        }
    }

    private bool TryDecode(byte[] data, out Packet p)
    {
        p = null;

        if (data[0] != Sync0 ||
            data[1] != Sync1 ||
            data[2] != Version ||
            data[3] != TypeXyz ||
            data[4] != PayloadSize)
        {
            badHeader++;
            return false;
        }

        ushort rxCrc = (ushort)(
            data[84] |
            (data[85] << 8)
        );

        ushort calcCrc = Crc16Ccitt(
            data,
            2,
            82
        );

        if (rxCrc != calcCrc)
        {
            crcErrors++;
            return false;
        }

        int offset = 6;

        uint frameId = ReadUInt32(data, ref offset);

        byte flags = data[offset++];
        offset++; // payload reserved byte

        Vector3 ls = ReadVector3(data, ref offset);
        Vector3 le = ReadVector3(data, ref offset);
        Vector3 lw = ReadVector3(data, ref offset);

        Vector3 rs = ReadVector3(data, ref offset);
        Vector3 re = ReadVector3(data, ref offset);
        Vector3 rw = ReadVector3(data, ref offset);

        p = new Packet
        {
            frameId = frameId,

            leftValid = (flags & (1 << 0)) != 0,
            leftFresh = (flags & (1 << 1)) != 0,

            rightValid = (flags & (1 << 2)) != 0,
            rightFresh = (flags & (1 << 3)) != 0,

            leftShoulder = ls,
            leftElbow = le,
            leftWrist = lw,

            rightShoulder = rs,
            rightElbow = re,
            rightWrist = rw
        };

        return true;
    }

    private static uint ReadUInt32(byte[] data, ref int offset)
    {
        uint value =
            (uint)data[offset] |
            ((uint)data[offset + 1] << 8) |
            ((uint)data[offset + 2] << 16) |
            ((uint)data[offset + 3] << 24);

        offset += 4;
        return value;
    }

    private static float ReadFloat(byte[] data, ref int offset)
    {
        if (!BitConverter.IsLittleEndian)
        {
            byte[] temp =
            {
                data[offset + 3],
                data[offset + 2],
                data[offset + 1],
                data[offset]
            };

            offset += 4;
            return BitConverter.ToSingle(temp, 0);
        }

        float value = BitConverter.ToSingle(data, offset);
        offset += 4;
        return value;
    }

    private static Vector3 ReadVector3(byte[] data, ref int offset)
    {
        return new Vector3(
            ReadFloat(data, ref offset),
            ReadFloat(data, ref offset),
            ReadFloat(data, ref offset)
        );
    }

    private static ushort Crc16Ccitt(
        byte[] data,
        int offset,
        int count)
    {
        ushort crc = 0xFFFF;

        for (int i = 0; i < count; i++)
        {
            crc ^= (ushort)(data[offset + i] << 8);

            for (int bit = 0; bit < 8; bit++)
            {
                if ((crc & 0x8000) != 0)
                    crc = (ushort)((crc << 1) ^ 0x1021);
                else
                    crc <<= 1;
            }
        }

        return crc;
    }
}
