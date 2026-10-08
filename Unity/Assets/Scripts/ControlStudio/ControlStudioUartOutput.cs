using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.IO.Ports;
using System.Text;
using System.Threading;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Output observer only. The router remains the sole ApplyCommand writer.
    public sealed class ControlStudioUartOutput : MonoBehaviour
    {
        public SingleArmCommandRouter router;
        public bool Mock { get; private set; }
        public bool Connected { get { lock (stateLock) return connected; } }
        public bool TxEnabled { get { lock (stateLock) return txEnabled; } }
        public int HardwareTxCount { get { lock (stateLock) return hardwareTxCount; } }
        public int MockTxCount { get { lock (stateLock) return mockTxCount; } }
        public string Status { get { lock (stateLock) return status; } }
        public string LastSent { get { lock (stateLock) return lastSent; } }
        public string LastReceived { get { lock (stateLock) return lastReceived; } }
        public string PortName { get { lock (stateLock) return portName; } }
        public int BaudRate { get { lock (stateLock) return baudRate; } }
        public bool ChangedOnly { get; set; } = true;

        readonly object stateLock = new object();
        readonly ConcurrentQueue<string> pending = new ConcurrentQueue<string>();
        Thread worker;
        int sessionVersion;
        int observedEpoch;
        bool connected, txEnabled;
        int hardwareTxCount, mockTxCount, baudRate = 115200;
        string status = "DISCONNECTED", portName = "", lastSent = "--", lastReceived = "--", lastQueued = "";

        public static string[] AvailablePorts()
        {
#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
            try { return SerialPort.GetPortNames(); }
            catch { return Array.Empty<string>(); }
#else
            return Array.Empty<string>();
#endif
        }

        public static string FormatApplied(float[] applied)
        {
            if (applied == null || applied.Length != 5) throw new ArgumentException("Five Applied M values required");
            var b = new StringBuilder(96);
            for (int i = 0; i < 5; i++)
            {
                if (float.IsNaN(applied[i]) || float.IsInfinity(applied[i])) throw new ArgumentException("Nonfinite Applied value");
                if (i != 0) b.Append(',');
                b.Append('M').Append(i).Append('=').Append(applied[i].ToString("0.###", CultureInfo.InvariantCulture));
            }
            return b.Append('\n').ToString();
        }

        void OnEnable()
        {
            if (router == null) router = GetComponent<SingleArmCommandRouter>();
            if (router != null) { observedEpoch = router.Epoch; router.OutputApplied += OnApplied; }
        }

        void OnDisable()
        {
            if (router != null) router.OutputApplied -= OnApplied;
            Disconnect();
        }

        public bool Connect(string selectedPort, int selectedBaud, bool mock)
        {
            Disconnect();
            if (selectedBaud < 1200 || selectedBaud > 1000000) { SetStatus("INVALID BAUDRATE"); return false; }
            if (!mock && string.IsNullOrWhiteSpace(selectedPort)) { SetStatus("SELECT COM PORT"); return false; }
#if !UNITY_EDITOR_WIN && !UNITY_STANDALONE_WIN
            if (!mock) { SetStatus("SERIAL UNSUPPORTED ON THIS PLATFORM"); return false; }
#endif
            Mock = mock;
            lock (stateLock)
            {
                portName = mock ? "MOCK" : selectedPort.Trim();
                baudRate = selectedBaud;
                txEnabled = false; // Every connection requires a separate, explicit TX action.
                connected = mock;
                status = mock ? "MOCK CONNECTED / TX OFF" : "CONNECTING / TX OFF";
                lastQueued = "";
            }
            if (mock) return true;
#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
            int session = Volatile.Read(ref sessionVersion);
            worker = new Thread(() => SerialLoop(session)) { IsBackground = true, Name = "ControlStudio UART" };
            worker.Start();
            return true;
#else
            return false;
#endif
        }

        public void Disconnect()
        {
            lock (stateLock) { txEnabled = false; connected = false; status = "DISCONNECTED"; lastQueued = ""; }
            Interlocked.Increment(ref sessionVersion);
            while (pending.TryDequeue(out _)) { }
            // Port lifetime is owned by the background worker; never join on Unity's main thread.
            worker = null;
        }

        public void SetTxEnabled(bool enabled)
        {
            bool armed;
            lock (stateLock)
            {
                txEnabled = enabled && connected;
                armed = txEnabled;
                if (connected) status = (Mock ? "MOCK" : portName) + (txEnabled ? " / TX ENABLED" : " / TX OFF");
                else if (!status.StartsWith("UART ERROR:", StringComparison.Ordinal)) status = "DISCONNECTED";
                lastQueued = "";
            }
            if (!armed) while (pending.TryDequeue(out _)) { }
        }

        public bool SendCurrent()
        {
            if (router == null || !router.Ready || router.Paused || !router.HasApplied) return false;
            return QueueApplied(router.Applied, true);
        }

        void OnApplied()
        {
            if (router.Epoch != observedEpoch)
            {
                observedEpoch = router.Epoch;
                SetTxEnabled(false);
                return;
            }
            if (ChangedOnly) QueueApplied(router.Applied, false);
        }

        void Update()
        {
            if (router == null) return;
            if (router.Epoch != observedEpoch)
            {
                observedEpoch = router.Epoch;
                SetTxEnabled(false);
            }
            if (!router.Ready || router.Paused) SetTxEnabled(false);
        }

        void OnApplicationFocus(bool focused) { if (!focused) SetTxEnabled(false); }

        bool QueueApplied(float[] applied, bool forced)
        {
            if (router == null || !router.Ready || router.Paused) return false;
            string packet;
            try { packet = FormatApplied(applied); }
            catch (Exception e) { SetStatus("PACKET REJECTED: " + e.Message); return false; }
            lock (stateLock)
            {
                if (!connected || !txEnabled) return false;
                if (!forced && packet == lastQueued) return false;
                lastQueued = packet;
                if (Mock)
                {
                    mockTxCount++;
                    lastSent = packet.TrimEnd();
                    lastReceived = "MOCK ACK " + mockTxCount;
                    return true;
                }
            }
            while (pending.Count >= 16 && pending.TryDequeue(out _)) { }
            pending.Enqueue(packet);
            return true;
        }

#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
        void SerialLoop(int session)
        {
            SerialPort port = null;
            try
            {
                string name; int baud;
                lock (stateLock) { name = portName; baud = baudRate; }
                port = new SerialPort(name, baud, Parity.None, 8, StopBits.One)
                {
                    Encoding = Encoding.ASCII,
                    NewLine = "\n",
                    ReadTimeout = 30,
                    WriteTimeout = 100,
                    DtrEnable = false,
                    RtsEnable = false
                };
                port.Open();
                lock (stateLock)
                {
                    if (session == Volatile.Read(ref sessionVersion)) { connected = true; status = name + " CONNECTED / TX OFF"; }
                }
                while (session == Volatile.Read(ref sessionVersion))
                {
                    if (pending.TryDequeue(out var packet))
                    {
                        bool allowed;
                        lock (stateLock) allowed = session == Volatile.Read(ref sessionVersion) && connected && txEnabled;
                        if (allowed)
                        {
                            port.Write(packet);
                            lock (stateLock) { hardwareTxCount++; lastSent = packet.TrimEnd(); }
                        }
                    }
                    if (port.BytesToRead > 0)
                    {
                        var received = port.ReadExisting();
                        if (received.Length > 0) lock (stateLock) lastReceived = received.Trim();
                    }
                    Thread.Sleep(10);
                }
            }
            catch (Exception e)
            {
                lock (stateLock) if (session == Volatile.Read(ref sessionVersion))
                { connected = false; txEnabled = false; status = "UART ERROR: " + e.Message; }
            }
            finally
            {
                try { port?.Close(); port?.Dispose(); } catch { }
                lock (stateLock) if (session == Volatile.Read(ref sessionVersion))
                { bool wasConnected = connected; connected = false; txEnabled = false; if (wasConnected) status = "DISCONNECTED"; }
                if (session == Volatile.Read(ref sessionVersion))
                    while (pending.TryDequeue(out _)) { }
            }
        }
#endif

        void SetStatus(string value) { lock (stateLock) status = value; }
    }
}
