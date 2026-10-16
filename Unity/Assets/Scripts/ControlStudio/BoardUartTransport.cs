using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Threading;

namespace HumanMotion.ControlStudio
{
    // Injectable transport boundary for offline connection/owner tests. The real
    // implementation below retains the existing SerialPort worker and wire policy.
    public interface IBoardUartTransport : IDisposable
    {
        bool Opened {get;} string Error {get;} string PortName {get;} string LogPath {get;}
        long SentBytes {get;} BoardUartDiagnostics Diagnostics {get;}
        bool TryRead(out BoardUartTransport.Chunk chunk);
        bool TryReceipt(out BoardUartTransport.Receipt receipt);
        bool Enqueue(long id,byte[] wire,bool priority);
    }
    // Exactly one owner and one full-duplex SerialPort per board. No connect/exit writes.
    public sealed class BoardUartTransport : IBoardUartTransport
    {
        public sealed class Chunk { public byte[] Bytes; public double ReceivedAt; internal long CompleteLines, TkLines; }
        public sealed class Receipt { public long Id; public double WrittenAt; }
        sealed class Write { public long Id; public byte[] Bytes; }
        readonly ConcurrentQueue<Chunk> rx = new ConcurrentQueue<Chunk>();
        readonly ConcurrentQueue<Write> tx = new ConcurrentQueue<Write>();
        readonly ConcurrentQueue<Receipt> receipts = new ConcurrentQueue<Receipt>();
        readonly object txLock = new object();
        readonly Thread worker;
        volatile bool stopping, opened;
        volatile string error = "";
        long queuedBytes, receivedBytes, sentBytes, dropped;
        public bool Opened => opened;
        public string Error => error;
        public long ReceivedBytes => Interlocked.Read(ref receivedBytes);
        public long SentBytes => Interlocked.Read(ref sentBytes);
        public long Dropped => Interlocked.Read(ref dropped);
        public static double Now => (double)Stopwatch.GetTimestamp() / Stopwatch.Frequency;
        public string PortName { get; }
        public string LogPath { get; }
        public BoardUartDiagnostics Diagnostics { get; }
        public BoardUartTransport(string port, int baud, string logPath, BoardUartDiagnostics diagnostics = null)
        {
            if (string.IsNullOrWhiteSpace(port) || (baud != 921600 && baud != 115200)) throw new ArgumentException("Select a COM port and 921600 (TRACE) or 115200 baud");
            PortName = port.Trim(); LogPath = logPath; Diagnostics = diagnostics;
            worker = new Thread(() => Run(baud)) { IsBackground = true, Name = "Board UART " + PortName };
            worker.Start();
        }
        public bool TryRead(out Chunk chunk)
        {
            if (!rx.TryDequeue(out chunk)) return false;
            Interlocked.Add(ref queuedBytes, -chunk.Bytes.Length); return true;
        }
        public bool TryReceipt(out Receipt receipt) => receipts.TryDequeue(out receipt);
        public bool Enqueue(long id, byte[] wire, bool priority)
        {
            if (wire == null || wire.Length == 0 || wire.Length > 4096) return false;
            lock (txLock)
            {
                if (!Opened || stopping || (!priority && !tx.IsEmpty)) return false;
                if (priority) while (tx.TryDequeue(out _)) { }
                tx.Enqueue(new Write { Id = id, Bytes = (byte[])wire.Clone() }); return true;
            }
        }
        void Receive(FileStream raw, byte[] buffer, int count, double receivedAt, ref string phase)
        {
            long lines = 0, ticks = 0;
            if (Diagnostics != null) Diagnostics.RecordReceived(buffer, count, receivedAt, out lines, out ticks);
            phase = "Raw.Write";
            try { raw.Write(buffer, 0, count); phase = "Raw.Flush"; raw.Flush(); }
            catch { Diagnostics?.RecordRejected(count, lines, ticks); throw; }
            Diagnostics?.RecordLogged(count);
            Interlocked.Add(ref receivedBytes, count);
            phase = "RX.Enqueue";
            if (Interlocked.Read(ref queuedBytes) + count > 4 * 1024 * 1024)
            { Interlocked.Increment(ref dropped); Diagnostics?.RecordRejected(count, lines, ticks); throw new IOException("RX queue overflow / reconnect required"); }
            var bytes = new byte[count]; Buffer.BlockCopy(buffer, 0, bytes, 0, count);
            Interlocked.Add(ref queuedBytes, count);
            Diagnostics?.RecordEnqueued(count, lines, ticks);
            rx.Enqueue(new Chunk { Bytes = bytes, ReceivedAt = receivedAt, CompleteLines = lines, TkLines = ticks });
        }
        void Diagnose(Exception exception, string phase, bool cleanup, ref Exception first, ref Exception last)
        {
            if (Diagnostics == null || ReferenceEquals(last, exception)) return;
            if (first == null) first = exception;
            last = exception;
            Diagnostics.RecordFailure(phase, exception, stopping, cleanup);
        }
        void Run(int baud)
        {
            string phase = "Raw.Open";
            Exception first = null, last = null;
            Diagnostics?.WorkerStarted();
            try
            {
                using (var raw = new FileStream(LogPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
                {
                    try
                    {
                        phase = "Serial.Create";
                        using (var serial = new SerialPort(PortName, baud, Parity.None, 8, StopBits.One)
                        { Handshake = Handshake.None, DtrEnable = false, RtsEnable = false, ReadTimeout = 30, WriteTimeout = 250, ReadBufferSize = 65536 })
                        {
                            try
                            {
                                phase = "Open";
                                serial.Open(); opened = true; Diagnostics?.WorkerOpened();
                                var buffer = new byte[4096];
                                while (!stopping)
                                {
                                    Write write;
                                    lock (txLock) tx.TryDequeue(out write);
                                    if (write != null)
                                    {
                                        // Preserve buffered pre-write evidence and the existing TX policy.
                                        double beforeWrite = Now;
                                        phase = "Read.PreWriteSnapshot";
                                        int buffered = serial.BytesToRead;
                                        if (buffered > 4 * 1024 * 1024) throw new IOException("Pre-write RX backlog exceeds 4 MiB");
                                        while (buffered > 0)
                                        {
                                            phase = "Read.PreWrite";
                                            int n = serial.Read(buffer, 0, Math.Min(buffer.Length, buffered));
                                            Receive(raw, buffer, n, beforeWrite, ref phase); buffered -= n;
                                        }
                                        // No new write may start after Dispose returns; an already
                                        // in-flight write completes without cancellation or retry.
                                        lock (txLock)
                                        {
                                            if (stopping) break;
                                            phase = "Write";
                                            serial.Write(write.Bytes, 0, write.Bytes.Length);
                                            Interlocked.Add(ref sentBytes, write.Bytes.Length);
                                            receipts.Enqueue(new Receipt { Id = write.Id, WrittenAt = Now });
                                        }
                                    }
                                    int count;
                                    phase = "Read";
                                    try { count = serial.Read(buffer, 0, buffer.Length); }
                                    catch (TimeoutException) { continue; }
                                    double receivedAt = Now;
                                    Receive(raw, buffer, count, receivedAt, ref phase);
                                }
                            }
                            // Capture before either using block can replace the exception.
                            catch (Exception e) { Diagnose(e, phase, false, ref first, ref last); throw; }
                            finally { phase = "Serial.Dispose"; }
                        }
                    }
                    catch (Exception e) { Diagnose(e, phase, phase == "Serial.Dispose", ref first, ref last); throw; }
                    finally { phase = "Raw.Dispose"; }
                }
            }
            catch (Exception e)
            {
                Diagnose(e, phase, phase == "Raw.Dispose", ref first, ref last);
                var cause = first ?? e;
                if (!stopping) error = cause.GetType().Name + ": " + cause.Message;
            }
            finally { opened = false; Diagnostics?.WorkerEnded(stopping); }
        }
        public void Dispose()
        {
            stopping = true;
            Diagnostics?.RequestStop();
            // Wait for any write that already passed its stop check, and cancel queued work.
            // Never hold this lock while waiting for the worker's RX/logging to finish.
            lock (txLock) while (tx.TryDequeue(out _)) { }
            if (!worker.Join(1000))
            {
                error = "UART worker did not close within 1s";
                Diagnostics?.Snapshot(null, "worker_join_timeout");
            }
        }
    }
}
