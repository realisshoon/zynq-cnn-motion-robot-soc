using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;

[Serializable]
public class DualJointCommandPacket
{
    public uint frame_id;
    public uint source_frame_id;

    public ForearmJointCommandData left;
    public ForearmJointCommandData right;
}

public sealed class DualUdpJointCommandReceiver : MonoBehaviour
{
    [Header("Targets")]
    public ForearmArmController leftArm;
    public ForearmArmController rightArm;

    [Header("UDP")]
    public int port = 5005;

    [Header("Runtime")]
    [SerializeField] private uint received;
    [SerializeField] private uint applied;
    [SerializeField] private uint stale;
    [SerializeField] private uint malformed;
    [SerializeField] private uint invalid;
    [SerializeField] private uint lastFrameId;

    private UdpClient client;
    private Thread thread;
    private volatile bool running;

    private readonly object gate = new object();
    private DualJointCommandPacket pending;
    private bool hasPending;

    public uint Received => received;
    public uint Applied => applied;
    public uint Stale => stale;
    public uint Malformed => malformed;
    public uint Invalid => invalid;

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

    private void StartReceiver()
    {
        if (running)
            return;

        try
        {
            client = new UdpClient(new IPEndPoint(IPAddress.Loopback, port));

            running = true;

            thread = new Thread(ReceiveLoop)
            {
                IsBackground = true,
                Name = "DualUdpJointCommandReceiver"
            };

            thread.Start();
        }
        catch (Exception e)
        {
            Debug.LogError(
                $"Dual UDP start failed on 127.0.0.1:{port}: {e.Message}"
            );

            StopReceiver();
        }
    }

    private void StopReceiver()
    {
        running = false;

        try
        {
            client?.Close();
        }
        catch
        {
            // Ignore shutdown exceptions.
        }

        if (thread != null && thread.IsAlive)
            thread.Join(1000);

        thread = null;
        client = null;
    }

    private void ReceiveLoop()
    {
        IPEndPoint remote = new IPEndPoint(IPAddress.Loopback, 0);

        while (running)
        {
            try
            {
                byte[] data = client.Receive(ref remote);
                string json = Encoding.UTF8.GetString(data);

                DualJointCommandPacket packet =
                    JsonUtility.FromJson<DualJointCommandPacket>(json);

                if (packet == null)
                {
                    malformed++;
                    continue;
                }

                received++;

                lock (gate)
                {
                    pending = packet;
                    hasPending = true;
                }
            }
            catch (SocketException)
            {
                if (!running)
                    break;

                malformed++;
            }
            catch
            {
                malformed++;
            }
        }
    }

    private static bool IsFinite(float value)
    {
        return !float.IsNaN(value) && !float.IsInfinity(value);
    }

    private static bool IsValidCommand(ForearmJointCommandData command)
    {
        return command.valid &&
               IsFinite(command.elbowRoll) &&
               IsFinite(command.elbowPitch) &&
               IsFinite(command.wristPitch) &&
               IsFinite(command.wristRoll) &&
               IsFinite(command.gripper);
    }

    private void Update()
    {
        DualJointCommandPacket packet = null;

        lock (gate)
        {
            if (hasPending)
            {
                packet = pending;
                hasPending = false;
            }
        }

        if (packet == null)
            return;

        if (lastFrameId != 0 &&
            packet.frame_id < lastFrameId)
        {
            lastFrameId = 0;
        }

        if (lastFrameId != 0 &&
            packet.frame_id <= lastFrameId)
        {
            stale++;
            return;
        }

        if (leftArm == null || rightArm == null ||
            !leftArm.IsConfigured || !rightArm.IsConfigured)
        {
            Debug.LogError(
                "DualUdpJointCommandReceiver: Forearm arm references/pivots missing."
            );

            enabled = false;
            return;
        }

        packet.left.frame_id = packet.frame_id;
        packet.right.frame_id = packet.frame_id;

        // Keep the two-arm packet atomic:
        // if either command is invalid, hold both at the previous applied pose.
        if (!IsValidCommand(packet.left) || !IsValidCommand(packet.right))
        {
            invalid++;
            lastFrameId = packet.frame_id;
            return;
        }

        leftArm.ApplyCommand(packet.left);
        rightArm.ApplyCommand(packet.right);

        lastFrameId = packet.frame_id;
        applied++;
    }
}
