using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;

namespace HumanMotion.ControlStudio
{
    public sealed class BoardUartFailure
    {
        public string Phase, Type, Message, StackTrace, Utc;
        public int HResult;
        public double At;
        public bool Stopping, Cleanup;
    }

    public sealed class BoardUartDiagnosticSnapshot
    {
        public string Port, SessionId, Utc;
        public int Baud, Generation, SessionGeneration;
        public long ReceivedBytes, ReceivedChunks, ReceivedLines, ReceivedTkLines, LoggedBytes;
        public long QueuedBytes, QueuedChunks, QueuedLines, QueuedTkLines;
        public long QueueHighWaterBytes, QueueHighWaterChunks, DequeuedBytes, DequeuedChunks;
        public long ProcessedBytes, ProcessedChunks, ProcessedLines, ProcessedTkLines, AcceptedTkLines;
        public long DiscardedBytes, DiscardedChunks, DiscardedLines, DiscardedTkLines;
        public long DiagnosticWriteFailures, DiagnosticRecordsDropped;
        public uint? LastReceivedTick, LastProcessedTick, LatestSessionTick, LastAppliedTick;
        public double LastReceivedAt, LastReceivedTkAt, LastProcessedAt, LastProcessedReceivedAt, LastAppliedAt;
        public double LastDelaySeconds, MaxDelaySeconds, WorkerStartedAt, WorkerStoppedAt;
        public double TotalDelaySeconds;
        public long DelaySamples;
        public float[] RawReceivedCommand, ParsedCommand, BoardVerifiedCommand, UnityAppliedCommand;
        public uint? ParsedTick, BoardVerifiedTick, RouterSubmittedTick, UnityAppliedTick;
        public int ParsedGeneration, BoardVerifiedGeneration, RouterSubmittedGeneration, UnityAppliedGeneration, RouterSubmittedEpoch, UnityAppliedEpoch;
        public long ParsedTkCount, BoardVerifiedTkCount, RouterSubmitCount, UnityApplyCount, RouterRejectCount;
        public double ParsedReceivedAt, UnityAppliedAt, LastApplyDelaySeconds, MaxApplyDelaySeconds, TotalApplyDelaySeconds;
        public string UnityAppliedUtc, HoldReason, ParserRejectReason, JointApplyStatus, VisualStatus = "NOT OBSERVED", ApplicationCounterScope = "SOURCE LIFETIME";
        public bool UnityAppliedIsFixture;
        public string RouterExceptionType, RouterExceptionMessage, RouterExceptionStackTrace;
        public int RouterExceptionHResult;
        public double RouterExceptionAt;
        public bool WorkerStopped, StopRequested, WorkerExitRequested, Connected, Trusted, Pwm, AppliedCurrent;
        public BoardUartFailure FirstFailure;
        public BoardUartFailure[] CleanupFailures;
        internal BoardUartDiagnosticSnapshot Copy()
        {
            var copy = (BoardUartDiagnosticSnapshot)MemberwiseClone();
            copy.RawReceivedCommand = RawReceivedCommand == null ? null : (float[])RawReceivedCommand.Clone();
            copy.ParsedCommand = ParsedCommand == null ? null : (float[])ParsedCommand.Clone();
            copy.BoardVerifiedCommand = BoardVerifiedCommand == null ? null : (float[])BoardVerifiedCommand.Clone();
            copy.UnityAppliedCommand = UnityAppliedCommand == null ? null : (float[])UnityAppliedCommand.Clone();
            return copy;
        }
    }

    // Optional observational state only. Never approves a command, changes the parser,
    // opens a COM port, or writes to the UART. The writer is bounded and independent of RX.
    public sealed class BoardUartDiagnostics : IDisposable
    {
        readonly object sync = new object();
        readonly BoardUartDiagnosticSnapshot data = new BoardUartDiagnosticSnapshot();
        readonly System.Collections.Generic.List<BoardUartFailure> cleanup = new System.Collections.Generic.List<BoardUartFailure>();
        readonly StringBuilder receivedLine = new StringBuilder();
        readonly ConcurrentQueue<string> records = new ConcurrentQueue<string>();
        readonly AutoResetEvent wake = new AutoResetEvent(false);
        readonly Thread writer;
        readonly Action<string> sink;
        readonly string path;
        readonly double origin = BoardUartTransport.Now;
        readonly DateTime utcOrigin = DateTime.UtcNow;
        BoardUartSession attached;
        BoardTick observedLatest;
        bool lineContent, lineTruncated, sessionError, abandoned;
        double lastSnapshot = double.NegativeInfinity;
        int pendingRecords, disposed;
        const int RecordLimit = 64;
        public bool IsDisposed => Volatile.Read(ref disposed) != 0;
        public BoardUartFailure FirstFailure { get { lock (sync) return data.FirstFailure; } }
        public BoardUartFailure[] CleanupFailures { get { lock (sync) return cleanup.ToArray(); } }

        public BoardUartDiagnostics(string port, int baud, int generation, string path, Action<string> sink = null)
        {
            data.Port = port; data.Baud = baud; data.Generation = data.SessionGeneration = generation;
            data.SessionId = Guid.NewGuid().ToString("N"); this.path = path; this.sink = sink;
            writer = new Thread(WriteRecords) { IsBackground = true, Name = "RIGHT UART diagnostics" };
            try { writer.Start(); }
            catch { Interlocked.Increment(ref data.DiagnosticWriteFailures); }
        }

        string Utc(double at) => utcOrigin.AddSeconds(at - origin).ToString("O", CultureInfo.InvariantCulture);
        public BoardUartDiagnosticSnapshot Capture()
        {
            lock (sync)
            {
                var copy = data.Copy();
                copy.Utc = Utc(BoardUartTransport.Now); copy.CleanupFailures = cleanup.ToArray();
                return copy;
            }
        }

        internal void RecordReceived(byte[] bytes, int count, double at, out long lines, out long ticks)
        {
            lines = ticks = 0;
            lock (sync)
            {
                data.ReceivedBytes += count; data.ReceivedChunks++; data.LastReceivedAt = at;
                for (int i = 0; i < count; i++)
                {
                    byte b = bytes[i];
                    if (b == 13 || b == 10)
                    {
                        if (lineContent)
                        {
                            lines++;
                            if (receivedLine.Length >= 3 && receivedLine[0] == 'T' && receivedLine[1] == 'K' && receivedLine[2] == ',')
                            {
                                ticks++;
                                // A header observation, NOT a validated/applied TK command.
                                string value = receivedLine.ToString(); int end = value.IndexOf(',', 3);
                                if (!lineTruncated && end > 3 && uint.TryParse(value.Substring(3, end - 3), NumberStyles.None, CultureInfo.InvariantCulture, out var tick))
                                {
                                    data.LastReceivedTick = tick;
                                    data.LastReceivedTkAt = at;
                                    data.RawReceivedCommand = ObservedCommand(value);
                                }
                            }
                        }
                        receivedLine.Clear(); lineContent = lineTruncated = false;
                    }
                    else
                    {
                        lineContent = true;
                        if (receivedLine.Length < 8192) receivedLine.Append((char)b); else lineTruncated = true;
                    }
                }
                data.ReceivedLines += lines; data.ReceivedTkLines += ticks;
            }
        }
        // Raw wire observation only: never changes parser acceptance or board approval.
        static float[] ObservedCommand(string line)
        {
            var fields = line.Split(','); if (fields.Length != 17) return null;
            var command = new float[5];
            for (int i = 0; i < 5; i++)
                if (!float.TryParse(fields[i + 4], NumberStyles.Float, CultureInfo.InvariantCulture, out command[i]) ||
                    float.IsNaN(command[i]) || float.IsInfinity(command[i])) return null;
            return command;
        }
        internal void RecordLogged(int count) { lock (sync) data.LoggedBytes += count; }
        internal void RecordEnqueued(int count, long lines, long ticks)
        {
            lock (sync)
            {
                if (abandoned) { Discard(count, 1, lines, ticks); return; }
                data.QueuedBytes += count; data.QueuedChunks++; data.QueuedLines += lines; data.QueuedTkLines += ticks;
                data.QueueHighWaterBytes = Math.Max(data.QueueHighWaterBytes, data.QueuedBytes);
                data.QueueHighWaterChunks = Math.Max(data.QueueHighWaterChunks, data.QueuedChunks);
            }
        }
        internal void RecordRejected(int count, long lines, long ticks) { lock (sync) Discard(count, 1, lines, ticks); }
        void Discard(long bytes, long chunks, long lines, long ticks)
        { data.DiscardedBytes += bytes; data.DiscardedChunks += chunks; data.DiscardedLines += lines; data.DiscardedTkLines += ticks; }
        public void RecordDequeued(BoardUartTransport.Chunk chunk, bool acceptedBySession)
        {
            lock (sync)
            {
                data.DequeuedBytes += chunk.Bytes.Length; data.DequeuedChunks++;
                data.LastDelaySeconds = Math.Max(0, BoardUartTransport.Now - chunk.ReceivedAt);
                data.MaxDelaySeconds = Math.Max(data.MaxDelaySeconds, data.LastDelaySeconds);
                data.TotalDelaySeconds += data.LastDelaySeconds; data.DelaySamples++;
                if (abandoned) return; // Already counted when this transport was retired.
                data.QueuedBytes -= chunk.Bytes.Length; data.QueuedChunks--;
                data.QueuedLines -= chunk.CompleteLines; data.QueuedTkLines -= chunk.TkLines;
                if (acceptedBySession) { data.ProcessedBytes += chunk.Bytes.Length; data.ProcessedChunks++; }
                else Discard(chunk.Bytes.Length, 1, chunk.CompleteLines, chunk.TkLines);
            }
        }
        public void AttachSession(BoardUartSession session)
        {
            DetachSession(attached); attached = session; observedLatest = session.Latest;
            session.LineReceived += ProcessedLine; RecordSession(session);
        }
        public void DetachSession(BoardUartSession session)
        {
            if (session == null) return;
            session.LineReceived -= ProcessedLine;
            if (ReferenceEquals(attached, session)) attached = null;
        }
        void ProcessedLine(string value, double at)
        {
            lock (sync)
            {
                data.ProcessedLines++;
                if (value.StartsWith("TK,", StringComparison.Ordinal))
                {
                    data.ProcessedTkLines++;
                    var latest = attached?.Latest;
                    if (latest != null && !ReferenceEquals(latest, observedLatest))
                    {
                        data.AcceptedTkLines++; data.LastProcessedTick = latest.Tick;
                        data.LastProcessedAt = BoardUartTransport.Now; data.LastProcessedReceivedAt = latest.ReceivedAt;
                    }
                }
                observedLatest = attached?.Latest;
            }
        }
        public void RecordSession(BoardUartSession session)
        {
            lock (sync)
            {
                data.SessionGeneration = session.Generation; data.Connected = session.Connected;
                data.Trusted = session.Trusted; data.Pwm = session.State != null && session.State.Pwm;
                data.LatestSessionTick = session.Latest?.Tick; data.LastAppliedTick = session.LastApplied?.Tick;
                data.LastAppliedAt = session.LastApplied?.ReceivedAt ?? 0;
                data.AppliedCurrent = session.AppliedCurrent(BoardUartTransport.Now);
            }
        }
        public void RecordApplication(BoardUnityApplicationTelemetry application)
        {
            var copy = application.Copy();
            lock (sync)
            {
                data.ParsedTick = copy.LastParsedTick; data.ParsedGeneration = copy.LastParsedGeneration;
                data.ParsedCommand = copy.LastParsedCommand;
                data.ParsedReceivedAt = copy.LastParsedReceivedAt;
                data.BoardVerifiedTick = copy.BoardVerifiedTick; data.BoardVerifiedGeneration = copy.BoardVerifiedGeneration;
                data.BoardVerifiedCommand = copy.BoardVerifiedCommand;
                data.RouterSubmittedTick = copy.RouterSubmittedTick; data.RouterSubmittedGeneration = copy.RouterSubmittedGeneration;
                data.RouterSubmittedEpoch = copy.RouterSubmittedEpoch;
                data.UnityAppliedTick = copy.UnityAppliedTick; data.UnityAppliedGeneration = copy.UnityAppliedGeneration;
                data.UnityAppliedEpoch = copy.UnityAppliedEpoch; data.UnityAppliedCommand = copy.UnityAppliedCommand;
                data.ParsedTkCount = copy.ParsedTkCount; data.BoardVerifiedTkCount = copy.BoardVerifiedTkCount;
                data.RouterSubmitCount = copy.RouterSubmitCount; data.UnityApplyCount = copy.UnityApplyCount;
                data.RouterRejectCount = copy.RouterRejectCount; data.UnityAppliedAt = copy.UnityAppliedAt;
                data.UnityAppliedUtc = copy.UnityAppliedTick.HasValue ? Utc(copy.UnityAppliedAt) : null;
                data.LastApplyDelaySeconds = copy.LastApplyDelaySeconds; data.MaxApplyDelaySeconds = copy.MaxApplyDelaySeconds;
                data.TotalApplyDelaySeconds = copy.TotalApplyDelaySeconds;
                data.HoldReason = copy.HoldReason; data.JointApplyStatus = copy.JointApplyStatus;
                data.ParserRejectReason = copy.ParserRejectReason;
                data.VisualStatus = copy.VisualStatus; data.UnityAppliedIsFixture = copy.UnityAppliedIsFixture;
                data.RouterExceptionType = copy.RouterExceptionType; data.RouterExceptionMessage = copy.RouterExceptionMessage;
                data.RouterExceptionStackTrace = copy.RouterExceptionStackTrace; data.RouterExceptionHResult = copy.RouterExceptionHResult;
                data.RouterExceptionAt = copy.RouterExceptionAt;
            }
        }
        public void RecordSessionError(BoardUartSession session)
        {
            lock (sync) { if (sessionError) return; sessionError = true; }
            Snapshot(session, "session_error");
        }
        public void RecordDisconnect(BoardUartSession session)
        {
            lock (sync)
            {
                if (abandoned) return;
                abandoned = true;
                Discard(data.QueuedBytes, data.QueuedChunks, data.QueuedLines, data.QueuedTkLines);
                data.QueuedBytes = data.QueuedChunks = data.QueuedLines = data.QueuedTkLines = 0;
            }
            Snapshot(session, "disconnect");
        }
        public void Snapshot(BoardUartSession session, string eventName = null)
        {
            if (session != null) RecordSession(session);
            if (eventName == null)
            {
                double now = BoardUartTransport.Now;
                lock (sync) { if (now - lastSnapshot < 1) return; lastSnapshot = now; }
            }
            Emit(eventName ?? "snapshot", null);
        }
        internal void RecordFailure(string phase, Exception exception, bool stopping, bool isCleanup)
        {
            try
            {
                double at = BoardUartTransport.Now;
                var failure = new BoardUartFailure { Phase = phase, Type = exception.GetType().FullName,
                    Message = exception.Message, HResult = exception.HResult, StackTrace = exception.StackTrace ?? "",
                    At = at, Utc = Utc(at), Stopping = stopping, Cleanup = isCleanup };
                lock (sync)
                {
                    if (data.FirstFailure == null) data.FirstFailure = failure;
                    if (isCleanup) cleanup.Add(failure);
                }
                Emit(isCleanup ? "cleanup_error" : "worker_error", failure);
            }
            catch { Interlocked.Increment(ref data.DiagnosticWriteFailures); }
        }
        internal void WorkerStarted()
        { lock (sync) data.WorkerStartedAt = BoardUartTransport.Now; Emit("worker_start", null); }
        internal void WorkerOpened() => Emit("port_open", null);
        internal void RequestStop()
        { lock (sync) { if (data.StopRequested) return; data.StopRequested = true; } Emit("stop_requested", null); }
        internal void WorkerEnded(bool stopping)
        {
            lock (sync)
            {
                data.WorkerStopped = true; data.StopRequested |= stopping;
                data.WorkerExitRequested = stopping; data.WorkerStoppedAt = BoardUartTransport.Now;
            }
            Emit(stopping ? "worker_stop_requested" : "worker_stop_unexpected", null);
        }

        static string Quote(string value)
        {
            var result = new StringBuilder("\"");
            foreach (char c in value ?? "")
            {
                if (c == '\\' || c == '"') result.Append('\\').Append(c);
                else if (c < 32) result.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                else result.Append(c);
            }
            return result.Append('"').ToString();
        }
        static string FailureJson(BoardUartFailure failure)
        {
            if (failure == null) return "null";
            return "{\"Phase\":" + Quote(failure.Phase) + ",\"Type\":" + Quote(failure.Type) + ",\"Message\":" + Quote(failure.Message)
                + ",\"HResult\":" + failure.HResult.ToString(CultureInfo.InvariantCulture) + ",\"StackTrace\":" + Quote(failure.StackTrace)
                + ",\"Utc\":" + Quote(failure.Utc) + ",\"At\":" + failure.At.ToString("R", CultureInfo.InvariantCulture)
                + ",\"Stopping\":" + (failure.Stopping ? "true" : "false") + ",\"Cleanup\":" + (failure.Cleanup ? "true" : "false") + "}";
        }
        void Emit(string name, BoardUartFailure failure)
        {
            try
            {
                if (Volatile.Read(ref disposed) != 0) return;
                var snapshot = Capture(); var json = new StringBuilder("{\"event\":").Append(Quote(name));
                foreach (var field in typeof(BoardUartDiagnosticSnapshot).GetFields())
                {
                    object value = field.GetValue(snapshot); string encoded;
                    if (field.FieldType == typeof(BoardUartFailure)) encoded = FailureJson((BoardUartFailure)value);
                    else if (field.FieldType == typeof(BoardUartFailure[]))
                    {
                        var failures = (BoardUartFailure[])value; var list = new StringBuilder("[");
                        for (int i = 0; i < failures.Length; i++) { if (i != 0) list.Append(','); list.Append(FailureJson(failures[i])); }
                        encoded = list.Append(']').ToString();
                    }
                    else if (value == null) encoded = "null";
                    else if (value is string) encoded = Quote((string)value);
                    else if (value is bool) encoded = (bool)value ? "true" : "false";
                    else if (value is double && (double.IsNaN((double)value) || double.IsInfinity((double)value))) encoded = "null";
                    else if (value is float && (float.IsNaN((float)value) || float.IsInfinity((float)value))) encoded = "null";
                    else if (value is float[])
                    {
                        var values = (float[])value; var array = new StringBuilder("[");
                        for (int i = 0; i < values.Length; i++)
                        {
                            if (i != 0) array.Append(',');
                            array.Append(float.IsNaN(values[i]) || float.IsInfinity(values[i]) ? "null" : values[i].ToString("R", CultureInfo.InvariantCulture));
                        }
                        encoded = array.Append(']').ToString();
                    }
                    else encoded = Convert.ToString(value, CultureInfo.InvariantCulture);
                    json.Append(',').Append(Quote(field.Name)).Append(':').Append(encoded);
                }
                json.Append(",\"failure\":").Append(FailureJson(failure)).Append('}');
                if (Interlocked.Increment(ref pendingRecords) > RecordLimit)
                { Interlocked.Decrement(ref pendingRecords); Interlocked.Increment(ref data.DiagnosticRecordsDropped); return; }
                records.Enqueue(json.ToString()); wake.Set();
            }
            catch { Interlocked.Increment(ref data.DiagnosticWriteFailures); }
        }
        void WriteRecords()
        {
            StreamWriter output = null;
            int logSlot = 0;
            try
            {
                while (Volatile.Read(ref disposed) == 0 || !records.IsEmpty)
                {
                    if (!records.TryDequeue(out var record)) { wake.WaitOne(100); continue; }
                    Interlocked.Decrement(ref pendingRecords);
                    try
                    {
                        if (sink != null) sink(record);
                        else
                        {
                            if (output == null) output = new StreamWriter(path, false, new UTF8Encoding(false));
                            output.WriteLine(record); output.Flush();
                            // Two bounded diagnostic files; raw .uart is never rotated here.
                            if (output.BaseStream.Position >= 4 * 1024 * 1024)
                            {
                                output.Dispose(); output = null; logSlot = 1 - logSlot;
                                output = new StreamWriter(logSlot == 0 ? path : path + ".1", false, new UTF8Encoding(false));
                            }
                        }
                    }
                    catch { Interlocked.Increment(ref data.DiagnosticWriteFailures); }
                }
            }
            catch { Interlocked.Increment(ref data.DiagnosticWriteFailures); }
            finally
            {
                try { output?.Dispose(); } catch { Interlocked.Increment(ref data.DiagnosticWriteFailures); }
                wake.Dispose();
            }
        }
        public void Dispose()
        {
            if (Interlocked.Exchange(ref disposed, 1) != 0) return;
            DetachSession(attached);
            try { wake.Set(); if (writer.IsAlive) writer.Join(100); } catch { }
        }
    }
}
