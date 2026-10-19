using System;
using System.Text.RegularExpressions;

namespace HumanMotion.ControlStudio
{
    public enum BoardCameraOverlayMode { Unknown, RobotSixPoints, BodyJoints }

    // Display configuration only. The firmware's lowercase b is a toggle:
    // never infer a new mode from a click/write, and never automatically retry.
    public sealed class BoardCameraOverlayController : IDisposable
    {
        const string RequestName = "HDMI overlay toggle (b)";
        readonly IBoardCommandGateway gateway;
        int generation;
        bool waiting;
        public BoardCameraOverlayMode ReportedMode { get; private set; }
        public double ReportedAt { get; private set; } = double.NaN;
        public BoardRequestState RequestState { get; private set; } = BoardRequestState.Idle;
        public string Status { get; private set; } = "표시 상태 UNKNOWN · 보드 응답 필요";
        public string ReportLabel => ReportedMode == BoardCameraOverlayMode.RobotSixPoints ? "6점" : ReportedMode == BoardCameraOverlayMode.BodyJoints ? "전체 관절점" : "UNKNOWN";

        public BoardCameraOverlayController(IBoardCommandGateway gateway)
        {
            this.gateway = gateway ?? throw new ArgumentNullException(nameof(gateway));
            generation = gateway.Session.Generation;
            gateway.Session.LineReceived += ObserveLine;
        }
        // Narrow exception to CSV/Manual RX ownership: this exact non-motion
        // request only. Existing PWM/follow/gripper owner guards stay intact.
        internal static bool IsToggleRequest(BoardRequest request) => request != null && request.Name == RequestName &&
            request.Wire != null && request.Wire.Length == 1 && request.Wire[0] == (byte)'b';

        public static bool TryParse(string line, out BoardCameraOverlayMode mode)
        {
            mode = BoardCameraOverlayMode.Unknown;
            if (line == null) return false;
            var match = Regex.Match(line, @"^HDMI overlay: (robot 6 points|body joints)( \(next CNN result\))?$");
            if (!match.Success) return false;
            mode = match.Groups[1].Value == "robot 6 points" ? BoardCameraOverlayMode.RobotSixPoints : BoardCameraOverlayMode.BodyJoints;
            return true;
        }
        public void Refresh()
        {
            var session = gateway.Session;
            if (generation != session.Generation || !session.Connected || session.Fault.Length != 0)
            {
                generation = session.Generation;
                ReportedMode = BoardCameraOverlayMode.Unknown; ReportedAt = double.NaN;
                if (!waiting) Status = !session.Connected ? "미연결 · 표시 상태 UNKNOWN" : "연결 세대/오류 변경 · 새 보드 응답 필요";
            }
        }
        void Confirm(BoardCameraOverlayMode mode, double at)
        {
            ReportedMode = mode; ReportedAt = at;
            Status = "보드 보고: " + ReportLabel + " · 영상 반영은 다음 CNN 결과부터";
        }
        void ObserveLine(string line, double at)
        {
            Refresh(); var session = gateway.Session;
            // During our request, only the gateway's post-write correlated
            // response may confirm a mode; buffered pre-write lines cannot.
            if (waiting || !session.Trusted || at < ReportedAt) return;
            if (TryParse(line, out var mode)) Confirm(mode, at);
        }
        public bool CanToggle(out string reason)
        {
            Refresh(); var session = gateway.Session; reason = "";
            if (gateway.IsSimulation) reason = "Fixture는 RX 전용입니다.";
            else if (!session.Connected) reason = "보드 미연결";
            else if (session.Fault.Length != 0) reason = session.Fault;
            else if (waiting || gateway.Busy) reason = "보드 응답 대기 중 · 중복 토글 금지";
            else if (gateway is BoardCommandGateway concrete && concrete.Uncertain) reason = "이전 요청 결과 미확인 · 기존 통신 복구 절차 필요";
            else if (!session.Trusted) reason = "보드 역할/REC_STATE 확인 필요";
            else if (!gateway.BoundaryConfirmed || !session.AtLineBoundary) reason = "Input Boundary 확인 필요";
            else if (session.State.Ui != "IDLE") reason = "보드 메뉴/이름 입력/저장 진행 중에는 토글하지 않습니다.";
            return reason.Length == 0;
        }
        public bool Toggle(out string reason)
        {
            if (!CanToggle(out reason)) return false;
            int requestGeneration = gateway.Session.Generation;
            var replyMode = BoardCameraOverlayMode.Unknown; double replyAt = double.NaN;
            waiting = true;
            var request = new BoardRequest {
                Name = RequestName, Wire = new[] { (byte)'b' }, TimeoutSeconds = 3,
                MatchWithReceivedAt = (session, line, at) => {
                    if (!session.Connected || session.Generation != requestGeneration || !session.Trusted) return BoardReply.Rejected;
                    if (line.StartsWith("CNN 'b' rejected:", StringComparison.Ordinal)) return BoardReply.Rejected;
                    if (!TryParse(line, out replyMode)) return BoardReply.Ignore;
                    replyAt = at; return BoardReply.Accepted;
                },
                Completed = (state, detail) => {
                    waiting = false; RequestState = state; Refresh();
                    if (state == BoardRequestState.Accepted && gateway.Session.Connected && gateway.Session.Generation == requestGeneration && replyMode != BoardCameraOverlayMode.Unknown)
                        Confirm(replyMode, replyAt);
                    else {
                        ReportedMode = BoardCameraOverlayMode.Unknown; ReportedAt = double.NaN;
                        Status = "표시 결과 " + state + " · UNKNOWN · 자동 재전송 없음: " + detail;
                    }
                }
            };
            if (!gateway.TryRequest(request, out reason)) { waiting = false; return false; }
            ReportedMode = BoardCameraOverlayMode.Unknown; ReportedAt = double.NaN;
            RequestState = BoardRequestState.Queued; Status = "b 요청 · 응답 대기 (표시 상태 UNKNOWN)";
            return true;
        }
        public static bool CanToggleSelected(BoardCameraOverlayController right, BoardCameraOverlayController left, int selected, out string reason)
        {
            reason = "";
            if (selected < 0 || selected > 2) { reason = "잘못된 보드 선택"; return false; }
            if (selected == 0) return right.CanToggle(out reason);
            if (selected == 1) return left.CanToggle(out reason);
            bool r = right.CanToggle(out var rr), l = left.CanToggle(out var lr);
            reason = r && l ? "" : "RIGHT: " + (r ? "통신 조건 충족" : rr) + " / LEFT: " + (l ? "통신 조건 충족" : lr);
            return r && l;
        }
        public static bool ToggleSelected(BoardCameraOverlayController right, BoardCameraOverlayController left, int selected, out string reason)
        {
            if (!CanToggleSelected(right, left, selected, out reason)) return false;
            if (selected == 0) return right.Toggle(out reason);
            if (selected == 1) return left.Toggle(out reason);
            // Independent transactions. A second-port enqueue failure must not
            // toggle the first board again in an attempted rollback.
            bool r = right.Toggle(out var rr), l = left.Toggle(out var lr);
            reason = "RIGHT: " + (r ? "b 응답 대기" : rr) + " / LEFT: " + (l ? "b 응답 대기" : lr);
            return r && l;
        }
        public void Dispose() => gateway.Session.LineReceived -= ObserveLine;
    }
}
