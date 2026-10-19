using System;
using System.IO;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Observations only. BoardVerified is not a Unity controller or renderer acknowledgement.
    public sealed class BoardUnityApplicationTelemetry
    {
        public int SessionGeneration, LastParsedGeneration, BoardVerifiedGeneration, RouterSubmittedGeneration, UnityAppliedGeneration, RouterSubmittedEpoch, UnityAppliedEpoch;
        public long ParsedTkCount, BoardVerifiedTkCount, RouterSubmitCount, UnityApplyCount, RouterRejectCount;
        public uint? LastParsedTick, BoardVerifiedTick, RouterSubmittedTick, UnityAppliedTick;
        public float[] LastParsedCommand, BoardVerifiedCommand, UnityAppliedCommand;
        public double LastParsedReceivedAt, UnityAppliedAt, LastApplyDelaySeconds, MaxApplyDelaySeconds, TotalApplyDelaySeconds;
        public bool UnityAppliedIsFixture;
        public string RouterExceptionType, RouterExceptionMessage, RouterExceptionStackTrace;
        public int RouterExceptionHResult;
        public double RouterExceptionAt;
        public string HoldReason = "NO TK", ParserRejectReason = "NONE", JointApplyStatus = "NOT ATTEMPTED", VisualStatus = "NOT OBSERVED";
        public BoardUnityApplicationTelemetry Copy()
        {
            var copy = (BoardUnityApplicationTelemetry)MemberwiseClone();
            copy.LastParsedCommand = LastParsedCommand == null ? null : (float[])LastParsedCommand.Clone();
            copy.BoardVerifiedCommand = BoardVerifiedCommand == null ? null : (float[])BoardVerifiedCommand.Clone();
            copy.UnityAppliedCommand = UnityAppliedCommand == null ? null : (float[])UnityAppliedCommand.Clone();
            return copy;
        }
    }

    [DefaultExecutionOrder(-50)]
    public sealed class BoardUartSource : MonoBehaviour
    {
        public SingleArmCommandRouter router;
        [Tooltip("Explicitly enable RIGHT UART diagnostic logging for the next real connection. No commands are sent.")]
        public bool rightUartDiagnosticsEnabled = false;
        public BoardUartSession Right { get; } = new BoardUartSession(BoardSide.Right);
        public BoardUartSession Left { get; } = new BoardUartSession(BoardSide.Left);
        readonly IBoardUartTransport[] transports = new IBoardUartTransport[2];
        internal Func<string,int,string,BoardUartDiagnostics,IBoardUartTransport> TransportFactory =
            (port,baud,path,diagnostics) => new BoardUartTransport(port,baud,path,diagnostics);
        readonly BoardCommandGateway[] gateways = new BoardCommandGateway[2];
        readonly bool[] boundary = new bool[2], fixture = new bool[2];
        readonly int[] generations = new int[2];
        readonly string[] messages = { "DISCONNECTED", "DISCONNECTED" };
        readonly double[] queryTime = new double[2];
        int appliedGeneration, ownerEpoch;
        uint appliedTick;
        bool haveApplied;
        readonly BoardUnityApplicationTelemetry rightApplication = new BoardUnityApplicationTelemetry();
        readonly BoardUnityApplicationTelemetry leftApplication = new BoardUnityApplicationTelemetry();
        readonly BoardTick[] observedParsed = new BoardTick[2], observedVerified = new BoardTick[2];
        readonly long[] observedOutOfOrder = new long[2], observedParseErrors = new long[2];
        bool observing;
        BoardSide appliedSide;
        ControlStudioBoardControlPanel control;
        int lastVisualTarget = -1;
        string lastVisualBlock;
        double nextVisualReport, nextVisualBlock;
        public bool visualDiagnosticsEnabled = true;
        public BoardUnityApplicationTelemetry RightApplication => rightApplication.Copy();
        public BoardUnityApplicationTelemetry LeftApplication => leftApplication.Copy();
        public BoardUnityApplicationTelemetry ApplicationState(BoardSide side) => ApplicationFor(side).Copy();
        BoardUnityApplicationTelemetry ApplicationFor(BoardSide side) => side == BoardSide.Right ? rightApplication : leftApplication;
        int VisualTarget { get { if (control == null) control = GetComponent<ControlStudioBoardControlPanel>(); return control == null ? 0 : control.SelectedTarget; } }
        public static bool ResolveVisualSource(int selectedTarget, out BoardSide side)
        { side = selectedTarget == 1 ? BoardSide.Left : BoardSide.Right; return selectedTarget >= 0 && selectedTarget <= 2; }
        public BoardSide? VisualSource => Active && ResolveVisualSource(VisualTarget, out var side) ? side : (BoardSide?)null;
        public string VisualSummary {
            get {
                if (!Active) return "G51 / " + (router?.Source ?? "UNKNOWN") + " OWNER";
                var selected = VisualSource;
                if (!selected.HasValue) return "G51 SOURCE UNKNOWN";
                var side = selected.Value; var session = Session(side); var application = ApplicationFor(side);
                bool current = haveApplied && appliedSide == side && session.AppliedCurrent(BoardUartTransport.Now) &&
                    application.UnityAppliedGeneration == session.Generation && application.UnityAppliedEpoch == router.Epoch &&
                    application.UnityAppliedTick == session.Latest?.Tick;
                return "G51 " + (side == BoardSide.Right ? "RIGHT" : "LEFT") + " / " +
                    (current ? "CURRENT" : "HOLD: " + HoldReason(side, BoardUartTransport.Now));
            }
        }
        public BoardUartDiagnosticSnapshot RightDiagnosticsSnapshot => transports[0]?.Diagnostics?.Capture();
        public bool Active => router != null && router.Source == "BOARD";
        public BoardUartSession Session(BoardSide side) => side == BoardSide.Right ? Right : Left;
        static int Index(BoardSide side) => side == BoardSide.Right ? 0 : 1;
        public string Message(BoardSide side) => messages[Index(side)];
        public string LogPath(BoardSide side) => transports[Index(side)]?.LogPath ?? "";
        public string ConnectedPortName(BoardSide side) => transports[Index(side)]?.PortName ?? "";
        public bool IsPortOpen(BoardSide side) => transports[Index(side)]?.Opened == true;
        public long TxBytes(BoardSide side) => transports[Index(side)]?.SentBytes ?? 0;
        public bool IsFixture(BoardSide side) => fixture[Index(side)];
        public string PendingText(BoardSide side) => Session(side).PendingText;
        public IBoardCommandGateway Gateway(BoardSide side) => Commands(side);
        public BoardCommandGateway Commands(BoardSide side)
        {
            int i = Index(side);
            if (gateways[i] == null)
            {
                gateways[i] = new BoardCommandGateway(Session(side),
                    () => transports[i] != null && transports[i].Opened && transports[i].Error.Length == 0,
                    () => boundary[i], () => fixture[i],
                    (id, bytes, stop) => transports[i]?.Enqueue(id, bytes, stop) ?? false,
                    ownerAllows: request => Active || IsStatusQuery(request) || BoardCameraOverlayController.IsToggleRequest(request));
                gateways[i].Changed += (state, status) =>
                {
                    messages[i] = state + ": " + status;
                    if (state == BoardRequestState.Unknown || state == BoardRequestState.TimedOut) boundary[i] = false;
                };
            }
            return gateways[i];
        }
        static bool IsStatusQuery(BoardRequest request) => request?.Wire != null && request.Wire.Length == 1 &&
            (request.Wire[0] == (byte)'V' || request.Wire[0] == (byte)'T' || request.Wire[0] == (byte)'t');
        public bool SelectSource()
        {
            EnsureObservation();
            if (Active) return true;
            // Retire the provisional independent RX/TX workers before assigning ownership.
            GetComponent<UartPose3DSource>()?.Disconnect();
            GetComponent<ControlStudioUartOutput>()?.Disconnect();
            bool ok = router != null && router.ResetOwner("BOARD");
            if (ok) { haveApplied = false; ownerEpoch = router.Epoch; }
            return ok;
        }
        public bool Connect(BoardSide side, string port, int baud)
        {
            int i = Index(side);
            // A port is a communication session, never an implicit visual input selection.
            EnsureObservation();
            if (transports[1-i] != null && string.Equals(transports[1-i].PortName, port?.Trim(), StringComparison.OrdinalIgnoreCase))
            { messages[i] = "Port already assigned to the other board"; return false; }
            Disconnect(side);
            BoardUartDiagnostics diagnostics = null;
            try
            {
                var dir = Path.Combine(Application.persistentDataPath, "BoardUartLogs"); Directory.CreateDirectory(dir);
                var path = Path.Combine(dir, DateTime.UtcNow.ToString("yyyyMMdd_HHmmss_fff") + "_" + side + "_" + Guid.NewGuid().ToString("N") + ".uart");
                Session(side).Begin(); generations[i] = Session(side).Generation;
                Observe(side); ApplicationFor(side).HoldReason = "REC_STATE MISSING";
                if (side == BoardSide.Right && rightUartDiagnosticsEnabled)
                {
                    diagnostics = new BoardUartDiagnostics(port?.Trim() ?? "", baud, Session(side).Generation, path + ".diagnostic.jsonl");
                    diagnostics.AttachSession(Session(side));
                }
                transports[i] = TransportFactory(port, baud, path, diagnostics);
                messages[i] = "CONNECTING / state unknown"; return true;
            }
            catch (Exception e)
            {
                diagnostics?.DetachSession(Session(side)); diagnostics?.Dispose();
                Session(side).Disconnect(); messages[i] = e.Message; return false;
            }
        }
        public void Disconnect(BoardSide side)
        {
            int i = Index(side); var diagnostics = transports[i]?.Diagnostics;
            gateways[i]?.Reset(); transports[i]?.Dispose();
            diagnostics?.DetachSession(Session(side));
            transports[i] = null;
            fixture[i] = boundary[i] = false; Session(side).Disconnect(); messages[i] = "DISCONNECTED / pose held";
            ApplicationFor(side).HoldReason = "DISCONNECTED";
            // Record the final disconnected state and retire only this connection's RX.
            if (side == BoardSide.Right) diagnostics?.RecordApplication(rightApplication);
            diagnostics?.RecordDisconnect(Session(side)); diagnostics?.Dispose();
        }
        public void ConfirmCommandBoundary(BoardSide side, bool confirmed) { boundary[Index(side)] = confirmed; }
        public bool CommandBoundaryConfirmed(BoardSide side) => boundary[Index(side)];
        public bool Query(BoardSide side, char key)
        {
            int i = Index(side);
            if (key != 'V' && key != 'T' && key != 't') return false;
            if (BoardUartTransport.Now - queryTime[i] < .3) return false;
            var request = new BoardRequest { Name = "Query " + key, Wire = new[] { (byte)key }, RequireTrusted = false,
                IsRecovery = key == 'V', Match = (session, line) =>
                    key == 'V' ? (line.StartsWith("[REC_STATE]", StringComparison.Ordinal) && session.Trusted ? BoardReply.Accepted : BoardReply.Ignore) :
                    key == 'T' ? (line.StartsWith("[ST]", StringComparison.Ordinal) && session.FollowEnabled.HasValue ? BoardReply.Accepted : BoardReply.Ignore) :
                    (line.StartsWith("[UART]", StringComparison.Ordinal) && session.StopEscape ? BoardReply.Accepted : BoardReply.Ignore) };
            bool sent = Commands(side).TryRequest(request, out var reason);
            if (sent) queryTime[i] = BoardUartTransport.Now;
            else messages[i] = reason;
            return sent;
        }
        public bool Control(BoardSide side, char key)
        {
            bool ok = BoardControlCommands.TrySend(Gateway(side), key, out var reason);
            if (!ok) messages[Index(side)] = reason;
            return ok;
        }
        public bool LoadFixture(BoardSide side, string path)
        {
            EnsureObservation();
            Disconnect(side); int i = Index(side);
            try
            {
                // Bounded local fixture; no COM port is opened and no bytes are transmitted.
                var file = new FileInfo(path);
                if (file.Length > 4 * 1024 * 1024) throw new IOException("Fixture exceeds 4 MiB");
                var bytes = File.ReadAllBytes(path); var s = Session(side); s.Begin();
                Observe(side);
                generations[i] = s.Generation; s.Feed(bytes, bytes.Length, BoardUartTransport.Now); fixture[i] = true;
                messages[i] = "FIXTURE / RX only / no hardware"; ApplySelected(); return true;
            }
            catch (Exception e) { Session(side).Disconnect(); messages[i] = e.Message; return false; }
        }
        void Update()
        {
            EnsureObservation();
            if (!Active) rightApplication.HoldReason = leftApplication.HoldReason = "SOURCE NOT BOARD";
            for (int boardIndex = 0; boardIndex < 2; boardIndex++)
            {
                var side = boardIndex == 0 ? BoardSide.Right : BoardSide.Left;
                int i = boardIndex; var t = transports[i]; var s = Session(side);
                if (t == null) continue;
                var diagnostics = t.Diagnostics;
                var gateway = Commands(side);
                while (t.TryReceipt(out var receipt)) gateway.Sent(receipt.Id, receipt.WrittenAt);
                long previousMissing = s.MissingTicks, previousErrors = s.ParseErrors;
                int budget = 256;
                while (budget-- > 0 && t.TryRead(out var chunk))
                {
                    // The worker can enqueue a write receipt after the initial drain.
                    // Its receipt always precedes its subsequent RX chunk.
                    while (t.TryReceipt(out var written)) gateway.Sent(written.Id, written.WrittenAt);
                    diagnostics?.RecordDequeued(chunk, s.Connected);
                    s.Feed(chunk.Bytes, chunk.Bytes.Length, chunk.ReceivedAt);
                    diagnostics?.RecordSession(s);
                }
                if (s.Generation != generations[i] || s.MissingTicks != previousMissing || s.ParseErrors != previousErrors)
                { generations[i] = s.Generation; boundary[i] = false; }
                if (t.Error.Length != 0)
                {
                    diagnostics?.RecordSessionError(s);
                    s.Disconnect(); boundary[i] = false; messages[i] = t.Error;
                }
                else if (t.Opened && gateway.State == BoardRequestState.Idle) messages[i] = s.Fault.Length != 0 ? s.Fault : s.Trusted ? "COM / verified state" : "COM / awaiting REC_STATE";
                gateway.Advance(BoardUartTransport.Now);
                diagnostics?.RecordSession(s);
            }
            ApplySelected();
            // Include this frame's controller result, not merely the parser candidate.
            transports[0]?.Diagnostics?.RecordApplication(rightApplication);
            transports[0]?.Diagnostics?.Snapshot(Right);
        }
        void EnsureObservation()
        {
            if (observing) return;
            Right.LineReceived += ObserveRightLine; Left.LineReceived += ObserveLeftLine; observing = true;
        }
        void ObserveRightLine(string line, double at) => Observe(BoardSide.Right);
        void ObserveLeftLine(string line, double at) => Observe(BoardSide.Left);
        void Observe(BoardSide side)
        {
            int i = Index(side); var s = Session(side); var application = ApplicationFor(side);
            if (application.SessionGeneration != s.Generation)
            {
                application.SessionGeneration = s.Generation;
                observedParsed[i] = observedVerified[i] = null;
                observedOutOfOrder[i] = s.OutOfOrder; observedParseErrors[i] = s.ParseErrors;
                application.LastParsedTick = application.BoardVerifiedTick = null;
                application.LastParsedCommand = application.BoardVerifiedCommand = null;
                application.ParserRejectReason = "NONE";
                // Last successful Unity pose remains held with its original generation.
            }
            // Also observes framer failures that do not produce a complete LineReceived event.
            if (s.OutOfOrder > observedOutOfOrder[i]) application.ParserRejectReason = "DUPLICATE / OUT OF ORDER TK";
            else if (s.ParseErrors > observedParseErrors[i]) application.ParserRejectReason = "MALFORMED RX / TK";
            observedOutOfOrder[i] = s.OutOfOrder; observedParseErrors[i] = s.ParseErrors;
            if (s.Latest != null && !ReferenceEquals(observedParsed[i], s.Latest))
            {
                observedParsed[i] = s.Latest; application.ParsedTkCount++;
                application.LastParsedTick = observedParsed[i].Tick;
                application.LastParsedGeneration = s.Generation;
                application.LastParsedCommand = observedParsed[i].Command == null ? null : (float[])observedParsed[i].Command.Clone();
                application.LastParsedReceivedAt = observedParsed[i].ReceivedAt;
            }
            if (s.LastApplied != null && !ReferenceEquals(observedVerified[i], s.LastApplied))
            {
                observedVerified[i] = s.LastApplied; application.BoardVerifiedTkCount++;
                application.BoardVerifiedTick = observedVerified[i].Tick;
                application.BoardVerifiedGeneration = s.Generation;
                application.BoardVerifiedCommand = (float[])observedVerified[i].Command.Clone();
            }
        }
        string HoldReason(BoardSide side, double now)
        {
            var s = Session(side);
            if (!Active) return "SOURCE NOT BOARD";
            if (!s.Connected) return "DISCONNECTED";
            if (s.Fault.Length != 0) return "FAULT: " + s.Fault;
            if (s.State == null) return "REC_STATE MISSING";
            if (!s.State.Pwm) return "PWM OFF";
            var latest = s.Latest;
            if (latest == null) return "WAITING NEW TK";
            if (latest.Errors != 0) return "TK err != 0";
            if (latest.Writes == 0) return "TK w = 0";
            if (!ReferenceEquals(latest, s.LastApplied)) return "NO CURRENT BOARD VERIFIED TK";
            if (now < latest.ReceivedAt || now - latest.ReceivedAt > .5) return "STALE TK";
            return "NO CURRENT BOARD VERIFIED TK";
        }
        void ApplySelected()
        {
            Observe(BoardSide.Right); Observe(BoardSide.Left);
            double now = BoardUartTransport.Now;
            if (!Active) return;
            int target = VisualTarget;
            if (!ResolveVisualSource(target, out var side)) {
                rightApplication.HoldReason = leftApplication.HoldReason = "VISUAL SOURCE UNKNOWN"; return;
            }
            if (lastVisualTarget != target) {
                lastVisualTarget = target;
                if (visualDiagnosticsEnabled) Debug.Log("[VISUAL_SOURCE] selected=" + (target == 0 ? "RIGHT" : target == 1 ? "LEFT" : "BOTH") + " source=" + side.ToString().ToUpperInvariant());
            }
            ApplicationFor(side == BoardSide.Right ? BoardSide.Left : BoardSide.Right).HoldReason = "NOT SELECTED / G51 " + side.ToString().ToUpperInvariant();
            var session = Session(side); var application = ApplicationFor(side); int i = Index(side);
            if (!session.AppliedCurrent(now)) { application.HoldReason = HoldReason(side, now); ReportVisual(side, application, now); return; }
            if (ownerEpoch != router.Epoch) { ownerEpoch = router.Epoch; haveApplied = false; }
            var tick = session.LastApplied;
            if (haveApplied && appliedSide == side && appliedGeneration == session.Generation && appliedTick == tick.Tick)
            { application.HoldReason = "DUPLICATE / ALREADY APPLIED"; ReportVisual(side, application, now); return; }
            application.RouterSubmitCount++;
            application.RouterSubmittedGeneration = session.Generation;
            application.RouterSubmittedEpoch = router.Epoch;
            application.RouterSubmittedTick = tick.Tick;
            application.JointApplyStatus = "NOT CONFIRMED";
            bool accepted;
            try { accepted = router.SubmitBoard(tick, router.Epoch); }
            catch (Exception exception)
            {
                bool changed = application.RouterExceptionType != exception.GetType().FullName ||
                    application.RouterExceptionMessage != exception.Message || application.RouterExceptionHResult != exception.HResult;
                application.HoldReason = "ROUTER EXCEPTION / APPLY NOT CONFIRMED";
                application.RouterExceptionType = exception.GetType().FullName;
                application.RouterExceptionMessage = exception.Message;
                application.RouterExceptionHResult = exception.HResult;
                application.RouterExceptionAt = BoardUartTransport.Now;
                application.RouterExceptionStackTrace = exception.StackTrace ?? "";
                transports[i]?.Diagnostics?.RecordApplication(application);
                transports[i]?.Diagnostics?.Snapshot(session, changed ? "router_apply_exception" : null);
                throw; // Preserve the existing exception behavior; do not claim controller success.
            }
            if (accepted)
            {
                appliedSide = side; appliedGeneration = session.Generation; appliedTick = tick.Tick; haveApplied = true;
                // Production SubmitBoard returns true only after ForearmArmController.ApplyCommand succeeds.
                application.UnityAppliedGeneration = session.Generation; application.UnityAppliedTick = tick.Tick;
                application.UnityAppliedEpoch = router.Epoch; application.UnityAppliedIsFixture = fixture[i];
                application.UnityAppliedCommand = (float[])router.Applied.Clone();
                application.UnityAppliedAt = BoardUartTransport.Now; application.UnityApplyCount++;
                application.LastApplyDelaySeconds = Math.Max(0, application.UnityAppliedAt - tick.ReceivedAt);
                application.MaxApplyDelaySeconds = Math.Max(application.MaxApplyDelaySeconds, application.LastApplyDelaySeconds);
                application.TotalApplyDelaySeconds += application.LastApplyDelaySeconds;
                application.JointApplyStatus = "SUCCEEDED"; application.HoldReason = "NONE";
                application.VisualStatus = "TRANSFORMS WRITTEN / RENDER NOT OBSERVED";
            }
            else
            {
                application.RouterRejectCount++;
                application.JointApplyStatus = router.Status == "BOARD / CONTROLLER REJECTED" ? "CONTROLLER REJECTED" : "NOT CONFIRMED";
                application.HoldReason = "ROUTER REJECT: " + router.Status;
            }
            ReportVisual(side, application, now);
        }
        void ReportVisual(BoardSide side, BoardUnityApplicationTelemetry application, double now)
        {
            if (!visualDiagnosticsEnabled) return;
            string block = application.HoldReason;
            if (block != "NONE" && block != "DUPLICATE / ALREADY APPLIED" &&
                block != lastVisualBlock && now >= nextVisualBlock) {
                lastVisualBlock = block; nextVisualBlock = now + 1;
                Debug.Log("[G51_BLOCK] source=" + side.ToString().ToUpperInvariant() + " reason=" + block);
            }
            if (now < nextVisualReport) return;
            nextVisualReport = now + 1;
            Debug.Log("[TK_RX] R=" + (Right.Latest?.Tick.ToString() ?? "--") + " L=" + (Left.Latest?.Tick.ToString() ?? "--") +
                " RParsed=" + rightApplication.ParsedTkCount + " LParsed=" + leftApplication.ParsedTkCount);
            var q = application.UnityAppliedCommand;
            Debug.Log("[G51_APPLY] source=" + side.ToString().ToUpperInvariant() + " generation=" + application.UnityAppliedGeneration +
                " tick=" + (application.UnityAppliedTick?.ToString() ?? "--") + " count=" + application.UnityApplyCount + " state=" + block +
                (q == null ? " cmd=--" : string.Format(System.Globalization.CultureInfo.InvariantCulture, " M0={0:F1} M1={1:F1} M2={2:F1} M3={3:F1} M4={4:F2}", q[0], q[1], q[2], q[3], q[4])));
        }
        void OnDestroy() { Right.LineReceived -= ObserveRightLine; Left.LineReceived -= ObserveLeftLine; Disconnect(BoardSide.Right); Disconnect(BoardSide.Left); gateways[0]?.Dispose(); gateways[1]?.Dispose(); }
    }
}
