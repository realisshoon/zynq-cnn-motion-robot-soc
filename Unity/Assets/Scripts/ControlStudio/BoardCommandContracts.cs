using System;

namespace HumanMotion.ControlStudio
{
    public enum BoardReply { Ignore, Accepted, Rejected }
    public enum BoardRequestState { Idle, Queued, AwaitingReply, Accepted, Rejected, TimedOut, Unknown, Cancelled }

    // Host-side correlation only; firmware has no wire transaction identifier.
    public sealed class BoardRequest
    {
        public string Name;
        public byte[] Wire;
        public Func<BoardUartSession, string, BoardReply> Match;
        // Uses the first byte's receive time, even when the main thread drains RX later.
        public Func<BoardUartSession, string, double, BoardReply> MatchWithReceivedAt;
        public double TimeoutSeconds = 3;
        public bool IsStop;
        public bool AllowPrompt;
        public bool RequireTrusted = true;
        public bool IsRecovery;
        public Action<BoardRequestState, string> Completed;
    }

    public interface IBoardCommandGateway
    {
        BoardUartSession Session { get; }
        bool Busy { get; }
        bool BoundaryConfirmed { get; }
        bool IsSimulation { get; }
        bool TryRequest(BoardRequest request, out string reason);
    }
}
