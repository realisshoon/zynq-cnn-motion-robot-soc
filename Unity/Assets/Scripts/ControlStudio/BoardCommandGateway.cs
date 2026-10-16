using System;
using System.Diagnostics;

namespace HumanMotion.ControlStudio
{
    // One host transaction per port. IDs are local queue receipts, never wire ACK IDs.
    public sealed class BoardCommandGateway : IBoardCommandGateway, IDisposable
    {
        readonly Func<bool> available, boundary, simulation;
        readonly Func<long, byte[], bool, bool> enqueue;
        readonly Func<double> clock;
        readonly Func<BoardRequest,bool> ownerAllows;
        BoardRequest pending;
        long nextId, pendingId;
        int generation;
        double sentAt, queuedAt;
        public BoardUartSession Session { get; }
        public bool Busy => pending != null;
        public bool BoundaryConfirmed => boundary();
        public bool IsSimulation => simulation();
        public bool Uncertain { get; private set; }
        public BoardRequestState State { get; private set; }
        public string Status { get; private set; } = "No request";
        public Action<BoardRequestState, string> Changed;
        public BoardCommandGateway(BoardUartSession session, Func<bool> available,
            Func<bool> boundary, Func<bool> simulation, Func<long, byte[], bool, bool> enqueue, Func<double> clock = null, Func<BoardRequest,bool> ownerAllows = null)
        {
            Session = session; this.available = available; this.boundary = boundary;
            this.simulation = simulation; this.enqueue = enqueue;
            this.clock = clock ?? (() => (double)Stopwatch.GetTimestamp() / Stopwatch.Frequency);
            this.ownerAllows = ownerAllows ?? (_ => true);
            Session.LineReceived += Observe;
            Session.PromptReceived += ObservePrompt;
        }
        public bool TryRequest(BoardRequest request, out string reason)
        {
            reason = "";
            if (request == null || request.Wire == null || request.Wire.Length == 0 || request.Wire.Length > 4096
                || (request.Match == null && request.MatchWithReceivedAt == null) || request.TimeoutSeconds <= 0 || double.IsNaN(request.TimeoutSeconds)
                || double.IsInfinity(request.TimeoutSeconds)) reason = "Invalid request";
            else if (!available() || !Session.Connected || IsSimulation) reason = "Open a real board COM connection first";
            else if (!ownerAllows(request)) reason = "Input owner permits status queries only; hardware commands require BOARD mode";
            else if (Session.Fault.Length != 0) reason = Session.Fault;
            else if (Busy && !request.IsStop) reason = "Another request is awaiting a board response";
            else if (Uncertain && !request.IsStop && !request.IsRecovery) reason = "Previous result unknown: recover console and query V first";
            else if (request.RequireTrusted && !Session.Trusted && !request.IsStop) reason = "Verified REC_STATE required";
            else if (!(request.IsStop && Session.StopEscape) &&
                (!BoundaryConfirmed || (!request.AllowPrompt && !Session.AtLineBoundary))) reason = "Confirm command boundary / finish or recover partial input";
            if (reason.Length != 0) return false;
            if (Busy) Finish(BoardRequestState.Cancelled, "Superseded by explicit stop; original outcome unknown");
            pending = request; pendingId = ++nextId; generation = Session.Generation;
            sentAt = double.PositiveInfinity; queuedAt = clock();
            Set(BoardRequestState.Queued, request.Name + " queued");
            if (!enqueue(pendingId, (byte[])request.Wire.Clone(), request.IsStop))
            { pending = null; Set(BoardRequestState.Cancelled, "TX queue unavailable"); reason = Status; return false; }
            return true;
        }
        public void Sent(long id, double time)
        {
            if (pending == null || id != pendingId) return;
            sentAt = time; Set(BoardRequestState.AwaitingReply, pending.Name + " written / awaiting board response");
        }
        void ObservePrompt(string value, double time)
        { if (pending != null && pending.AllowPrompt) Observe(value, time); }
        void Observe(string value, double time)
        {
            if (pending == null) return;
            if (Session.Generation != generation || !Session.Connected || Session.Fault.Length != 0)
            { Finish(BoardRequestState.Unknown, "Board reset, disconnected, or protocol fault / outcome unknown"); return; }
            // Discard buffered pre-write evidence, including unsolicited identical status lines.
            if (State != BoardRequestState.AwaitingReply || time <= sentAt) return;
            BoardReply reply;
            try { reply = pending.MatchWithReceivedAt != null ? pending.MatchWithReceivedAt(Session, value, time) : pending.Match(Session, value); }
            catch (Exception e) { Finish(BoardRequestState.Unknown, "Response parser failed: " + e.Message); return; }
            if (reply == BoardReply.Accepted) Finish(BoardRequestState.Accepted, value);
            else if (reply == BoardReply.Rejected) Finish(BoardRequestState.Rejected, value);
        }
        public void Advance(double now)
        {
            if (pending == null) return;
            if (!available() || !ownerAllows(pending) || !Session.Connected || Session.Generation != generation || Session.Fault.Length != 0)
                Finish(BoardRequestState.Unknown, "Connection or board epoch changed / outcome unknown");
            else if (now - (State == BoardRequestState.Queued ? queuedAt : sentAt) > pending.TimeoutSeconds)
                Finish(BoardRequestState.TimedOut, "No matching board response / outcome unknown; no retry");
        }
        public void Cancel(string reason)
        { if (pending != null) Finish(BoardRequestState.Unknown, reason); }
        void Finish(BoardRequestState state, string status)
        {
            var completed = pending; pending = null;
            if (state == BoardRequestState.Unknown || state == BoardRequestState.TimedOut || state == BoardRequestState.Cancelled) Uncertain = true;
            if (state == BoardRequestState.Accepted && completed != null && completed.IsRecovery) Uncertain = false;
            Set(state, status); completed?.Completed?.Invoke(state, status);
        }
        void Set(BoardRequestState state, string status) { State = state; Status = status; Changed?.Invoke(state, status); }
        public void Reset() { Cancel("Disconnected / outcome unknown"); Uncertain = false; State = BoardRequestState.Idle; Status = "No request"; }
        public void Dispose() { Cancel("Disposed / outcome unknown"); Session.LineReceived -= Observe; Session.PromptReceived -= ObservePrompt; }
    }
}
