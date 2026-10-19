using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace HumanMotion.ControlStudio
{
    public sealed class BoardRecordingEntry
    {
        public uint Id, Samples, DurationMs;
        public string Name;
        public override string ToString() => Id + "  " + Name + "  " + Samples + " samples / " + (DurationMs / 1000.0).ToString("0.##", CultureInfo.InvariantCulture) + " s";
    }

    // Board responses own the workflow. This class never advances it by a timer or write success.
    public sealed class BoardRecordingController
    {
        static readonly Regex NamePattern = new Regex("^[A-Za-z0-9_-]{1,24}$", RegexOptions.CultureInvariant);
        static readonly Regex EntryPattern = new Regex(@"^\[REC\] ([0-9]{1,2}) ([A-Za-z0-9_-]{1,24}) samples=([0-9]+) duration_ms=([0-9]+)( selected)?$", RegexOptions.CultureInvariant);
        readonly IBoardCommandGateway gateway;
        readonly List<BoardRecordingEntry> entries = new List<BoardRecordingEntry>();
        readonly Dictionary<uint, BoardRecordingEntry> staged = new Dictionary<uint, BoardRecordingEntry>();
        int generation = -1;
        long observedLine = -1, errors, missing;
        string observedText;
        bool listing, badList, catalogValid, startedNotice;
        bool? manualOpen;
        double? recordingStarted;
        public BoardRecordingController(IBoardCommandGateway gateway) { this.gateway = gateway ?? throw new ArgumentNullException(nameof(gateway)); }
        public IBoardCommandGateway Gateway => gateway;
        public IReadOnlyList<BoardRecordingEntry> Entries => entries;
        public bool CatalogValid { get { Sync(); return catalogValid; } }
        public bool? ManualOpen { get { Sync(); return manualOpen; } }
        public string Message { get; private set; } = "Query board state before recording.";
        public BoardRequestState LastRequestState { get; private set; } = BoardRequestState.Idle;
        public uint LiveMaxMs { get; private set; }
        public uint TailReserveMs { get; private set; }
        public uint TotalMaxMs { get; private set; }
        public bool RecordingActive { get { Sync(); var s = gateway.Session.State; return startedNotice && s != null && s.Ui == "IDLE" && s.Mode == "RECORDING"; } }
        public double RecordingSeconds(double now) => RecordingActive && recordingStarted.HasValue ? Math.Max(0, now - recordingStarted.Value) : 0;

        void ClearEvidence()
        {
            entries.Clear(); staged.Clear(); catalogValid = listing = badList = startedNotice = false;
            manualOpen = null; recordingStarted = null;
            LiveMaxMs = TailReserveMs = TotalMaxMs = 0;
        }
        public void Sync()
        {
            var s = gateway.Session;
            if (generation != s.Generation || errors != s.ParseErrors || missing != s.MissingTicks || !s.Trusted)
            {
                generation = s.Generation; errors = s.ParseErrors; missing = s.MissingTicks; ClearEvidence();
            }
            if (s.State != null && catalogValid && s.State.Entries != entries.Count) { catalogValid = false; entries.Clear(); }
            if (s.State == null || (s.State.Mode != "RECORDING" && s.State.Ui != "START_RECORD")) { startedNotice = false; recordingStarted = null; }
        }
        static bool UInt(string text, out uint value) => uint.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out value);
        static uint Field(string line, string key)
        {
            var match = Regex.Match(line, @"(?:^|[ ;])" + Regex.Escape(key) + @"=([0-9]+)(?:[ ;]|$)", RegexOptions.CultureInvariant);
            return match.Success && UInt(match.Groups[1].Value, out uint value) ? value : 0;
        }
        public void ObserveLine(string line, double receivedAt)
        {
            Sync(); var s = gateway.Session;
            if (observedLine == s.Lines && observedText == line) { StartTimer(receivedAt); return; }
            observedLine = s.Lines; observedText = line;
            if (!s.Trusted) return;
            if (line.StartsWith("[PWM]", StringComparison.Ordinal))
            {
                string arm = s.Side == BoardSide.Right ? "ROBOT0_JB_CH0_CH4" : "ROBOT1_JC_CH0_CH4";
                bool localLiveAck = Regex.IsMatch(line, @"(?:^| )enabled=1(?:[ ;]|$)", RegexOptions.CultureInvariant) &&
                    Regex.IsMatch(line, @"(?:^| )mode=LIVE(?:[ ;]|$)", RegexOptions.CultureInvariant) &&
                    Regex.IsMatch(line, @"(?:^| )arm=" + arm + @"(?:[ ;]|$)", RegexOptions.CultureInvariant);
                if (localLiveAck && Regex.IsMatch(line, @"(?:^| )result=manual_open_until_H(?:[ ;]|$)", RegexOptions.CultureInvariant)) manualOpen = true;
                else if (localLiveAck && Regex.IsMatch(line, @"(?:^| )result=automatic_restored(?:[ ;]|$)", RegexOptions.CultureInvariant)) manualOpen = false;
            }
            else if (line.StartsWith("GM,", StringComparison.Ordinal))
            {
                var f = line.Split(',');
                if (f.Length == 10 && (f[9] == "0" || f[9] == "1")) manualOpen = f[9] == "1";
            }
            if (line.StartsWith("[REC] recording started;", StringComparison.Ordinal))
            {
                LiveMaxMs = Field(line, "live_max_ms"); TailReserveMs = Field(line, "tail_reserve_ms"); TotalMaxMs = Field(line, "total_max_ms");
                startedNotice = LiveMaxMs > 0 && TailReserveMs > 0 && (ulong)LiveMaxMs + TailReserveMs == TotalMaxMs;
                Message = startedNotice ? "Recording started by board; timer excludes preparation." : "Recording start received without valid reported limits; state query required.";
            }
            StartTimer(receivedAt);
            string role = s.Side == BoardSide.Right ? "r" : "l";
            if (line.StartsWith("[REC] SD records;", StringComparison.Ordinal))
            {
                entries.Clear(); catalogValid = false; staged.Clear(); listing = true;
                badList = !line.Contains("select: " + role + " record select"); return;
            }
            if (listing && line.StartsWith("[REC] count=", StringComparison.Ordinal))
            {
                var countMatch = Regex.Match(line, @"^\[REC\] count=([0-9]{1,2});", RegexOptions.CultureInvariant);
                bool valid = !badList && countMatch.Success && UInt(countMatch.Groups[1].Value, out uint count) && count <= 32 &&
                    line.Contains("delete: " + role + " record delete") && count == staged.Count;
                listing = false;
                if (valid) { entries.AddRange(staged.Values); entries.Sort((a, b) => a.Id.CompareTo(b.Id)); catalogValid = true; }
                staged.Clear(); Message = valid ? "Complete SD list received. Select the actual board ID." : "Incomplete/invalid SD list; request LIST again."; return;
            }
            if (listing && line.StartsWith("[REC] ", StringComparison.Ordinal))
            {
                var match = EntryPattern.Match(line);
                if (!match.Success || !UInt(match.Groups[1].Value, out uint id) || id < 1 || id > 32 ||
                    !UInt(match.Groups[3].Value, out uint samples) || samples < 1 || samples > 2048 ||
                    !UInt(match.Groups[4].Value, out uint duration) || duration != samples * 20 || staged.ContainsKey(id)) badList = true;
                else staged.Add(id, new BoardRecordingEntry { Id = id, Name = match.Groups[2].Value, Samples = samples, DurationMs = duration });
            }
            if (line.StartsWith("[REC] SAVED ", StringComparison.Ordinal) || line.StartsWith("[REC] DELETED ", StringComparison.Ordinal) || line.Contains("DELETE_PARTIAL_REQUIRES_SD_REVIEW"))
            { catalogValid = false; entries.Clear(); }
            if (line.StartsWith("[REC]", StringComparison.Ordinal) && (line.Contains("rejected") || line.Contains("FAILED") || line.Contains("DELETE_PARTIAL"))) Message = line;
        }
        void StartTimer(double receivedAt)
        {
            var state = gateway.Session.State;
            if (startedNotice && state != null && state.Ui == "IDLE" && state.Mode == "RECORDING" && !recordingStarted.HasValue && !double.IsNaN(receivedAt) && !double.IsInfinity(receivedAt)) recordingStarted = receivedAt;
        }
        bool Ready(out string reason)
        {
            Sync(); var s = gateway.Session;
            if (!s.Trusted) reason = "Board state is unverified; query V first.";
            else if (gateway.Busy) reason = "One request is already pending; do not retry a toggle.";
            else if (!gateway.BoundaryConfirmed || !s.AtLineBoundary) reason = "Confirm no unfinished frame or numeric settings input.";
            else { reason = ""; return true; }
            Message = reason; return false;
        }
        bool Deny(string text, out string reason) { Message = reason = text; return false; }
        bool HasId(uint id) => catalogValid && entries.Exists(e => e.Id == id);
        bool StorageState(BoardRecordState s) => (s.Ui == "IDLE" || s.Ui == "MENU") && (s.Mode == "LIVE" || s.Mode == "HOLDING") && (!s.Pwm || s.Mode == "HOLDING");
        static bool Rejected(string line) => line.StartsWith("[REC]", StringComparison.Ordinal) && (line.Contains("rejected") || line.Contains("FAILED") || line.Contains("DELETE_PARTIAL") || line.Contains("playback stop=0")) || line.StartsWith("[PWM]", StringComparison.Ordinal) && line.Contains("result=rejected");
        bool Send(string name, string wire, Func<BoardUartSession, string, BoardReply> matcher, out string reason, double timeout = 5)
        {
            LastRequestState = BoardRequestState.Queued;
            var request = new BoardRequest { Name = name, Wire = Encoding.ASCII.GetBytes(wire), TimeoutSeconds = timeout,
                Match = (s, line) => { ObserveLine(line, double.NaN); return Rejected(line) ? BoardReply.Rejected : matcher(s, line); },
                Completed = (state, detail) => { LastRequestState = state; Message = name + ": " + state + " / " + detail + (state == BoardRequestState.TimedOut || state == BoardRequestState.Unknown ? " / Query state; no automatic retry or cancel." : ""); } };
            bool sent = gateway.TryRequest(request, out reason);
            if (!sent) { LastRequestState = BoardRequestState.Rejected; Message = reason; }
            else Message = name + " requested; awaiting board response.";
            return sent;
        }
        static BoardReply StateReply(BoardUartSession s, string line, Func<BoardRecordState, bool> match)
            => line.StartsWith("[REC_STATE]", StringComparison.Ordinal) && s.Trusted && match(s.State) ? BoardReply.Accepted : BoardReply.Ignore;
        public bool StartRecord(bool acknowledgeReplacement, out string reason)
        {
            if (!Ready(out reason)) return false; var s = gateway.Session.State;
            if (s.Ui != "IDLE" || s.Mode != "LIVE" || !s.Pwm) return Deny("Record requires IDLE / LIVE / PWM ON.", out reason);
            if (s.Unsaved && !acknowledgeReplacement) return Deny("Acknowledge replacing unsaved RAM before a new recording.", out reason);
            startedNotice = false; recordingStarted = null; LiveMaxMs = TailReserveMs = TotalMaxMs = 0;
            return Send("Prepare recording", "R", (session, line) => StateReply(session, line, st => st.Ui == "START_RECORD" || st.Ui == "IDLE" && st.Mode == "RECORDING"), out reason);
        }
        public bool CancelPreparation(out string reason)
        {
            if (!Ready(out reason)) return false;
            if (gateway.Session.State.Ui != "START_RECORD") return Deny("Preparation cancellation requires START_RECORD.", out reason);
            return Send("Cancel preparation", "R", (s, line) => StateReply(s, line, st => st.Ui == "IDLE" && st.Mode == "LIVE"), out reason);
        }
        public bool FinishRecord(out string reason)
        {
            if (!Ready(out reason)) return false; var st = gateway.Session.State;
            if (st.Ui != "IDLE" || st.Mode != "RECORDING") return Deny("Finish requires active board recording.", out reason);
            return Send("Finish recording / settling tail", "R", (s, line) => StateReply(s, line, state => state.Ui == "STOP_RECORD" || state.Ui == "NAME"), out reason);
        }
        public bool RefreshList(out string reason)
        {
            if (!Ready(out reason)) return false;
            catalogValid = false; entries.Clear(); listing = false;
            return Send("Read SD list", "!REC,LIST\r", (s, line) => line.StartsWith("[REC] count=", StringComparison.Ordinal) ? catalogValid ? BoardReply.Accepted : BoardReply.Rejected : BoardReply.Ignore, out reason);
        }
        public bool OpenMenu(out string reason)
        {
            if (!Ready(out reason)) return false; var st = gateway.Session.State;
            if (!(st.Ui == "IDLE" && st.Mode == "LIVE" || st.Ui == "MENU" && (st.Mode == "LIVE" || st.Mode == "HOLDING"))) return Deny("P list entry requires IDLE/LIVE or MENU; IDLE/HOLDING uses playback stop first.", out reason);
            if (manualOpen != false) return Deny("Manual gripper release is unverified. Cancel menu, request H, confirm its response, then P.", out reason);
            return Send("Enter SD menu", "P", (s, line) => StateReply(s, line, state => state.Ui == "STOP_MENU" || state.Ui == "MENU") == BoardReply.Accepted || line.StartsWith("[REC] count=", StringComparison.Ordinal) && catalogValid ? BoardReply.Accepted : BoardReply.Ignore, out reason);
        }
        public bool SaveName(string name, out string reason)
        {
            if (!Ready(out reason)) return false; var st = gateway.Session.State;
            if (st.Ui != "NAME" || st.Mode != "HOLDING" || !st.Unsaved) return Deny("Save requires verified NAME / HOLDING / unsaved RAM.", out reason);
            if (name == null || !NamePattern.IsMatch(name)) return Deny("Name requires 1..24 ASCII letters, digits, '_' or '-'.", out reason);
            if (catalogValid && entries.Exists(e => e.Name == name)) return Deny("That case-sensitive name already exists on this board.", out reason);
            uint savedId = 0;
            return Send("Save " + name, "!REC,NAME," + name + "\r", (s, line) => {
                if (line.StartsWith("[REC] SAVED ", StringComparison.Ordinal) && line.Contains("name=" + name + " ") && Field(line, "id") >= 1 && Field(line, "id") <= 32 && Field(line, "samples") > 0 && Field(line, "samples") <= 2048) savedId = Field(line, "id");
                return savedId != 0 ? StateReply(s, line, state => state.Ui == "MENU" && state.Mode == "HOLDING" && !state.Unsaved && state.Selected == savedId) : BoardReply.Ignore;
            }, out reason, 30);
        }
        public bool Select(uint id, bool acknowledgeReplacement, out string reason)
        {
            if (!Ready(out reason)) return false; var st = gateway.Session.State;
            if (st.Ui != "MENU" || st.Mode != "HOLDING" || !st.Pwm || !HasId(id)) return Deny("SELECT requires MENU / HOLDING / PWM ON and a fresh listed board ID.", out reason);
            if (manualOpen != false) return Deny("Confirm automatic gripper restoration with H before entering the menu.", out reason);
            if (st.Unsaved && !acknowledgeReplacement) return Deny("Acknowledge replacing unsaved RAM before loading an SD record.", out reason);
            bool played = false;
            return Send("Play SD ID " + id, "!REC,SELECT," + id + "\r", (s, line) => {
                if (line.StartsWith("[REC] PLAY ", StringComparison.Ordinal) && Field(line, "id") == id) played = true;
                return played ? StateReply(s, line, state => state.Ui == "IDLE" && state.Playing == id && (state.Mode == "ALIGNING" || state.Mode == "PLAYING")) : BoardReply.Ignore;
            }, out reason);
        }
        public bool StopPlayback(out string reason)
        {
            if (!Ready(out reason)) return false; var st = gateway.Session.State;
            if (st.Ui != "IDLE" || st.Mode != "ALIGNING" && st.Mode != "PLAYING" && st.Mode != "HOLDING") return Deny("Playback stop requires ALIGNING / PLAYING or error HOLDING in IDLE.", out reason);
            bool stopped = false;
            return Send("Stop playback", "P", (s, line) => { if (line.StartsWith("[REC] playback stop=1 mode=LIVE", StringComparison.Ordinal)) stopped = true; return stopped ? StateReply(s, line, state => state.Ui == "IDLE" && state.Mode == "LIVE" && state.Playing == 0) : BoardReply.Ignore; }, out reason);
        }
        public bool RequestDelete(uint id, out string reason)
        {
            if (!Ready(out reason)) return false;
            if (!StorageState(gateway.Session.State) || !HasId(id)) return Deny("DELETE requires a fresh listed ID, IDLE/MENU, PWM OFF or HOLDING.", out reason);
            bool prompted = false;
            return Send("Prepare deletion of ID " + id, "!REC,DELETE," + id + "\r", (s, line) => { if (line.StartsWith("[REC] confirm deleting " + id + " ", StringComparison.Ordinal)) prompted = true; return prompted ? StateReply(s, line, state => state.Delete == id) : BoardReply.Ignore; }, out reason);
        }
        public bool ConfirmDelete(uint id, bool explicitlyConfirmed, out string reason)
        {
            if (!Ready(out reason)) return false;
            if (!explicitlyConfirmed || !StorageState(gateway.Session.State) || gateway.Session.State.Delete != id || !HasId(id)) return Deny("Explicitly confirm the board's pending deletion of this listed ID.", out reason);
            bool deleted = false;
            return Send("Delete SD ID " + id, "!REC,CONFIRM," + id + "\r", (s, line) => { if (line.StartsWith("[REC] DELETED ", StringComparison.Ordinal) && Field(line, "id") == id) deleted = true; return deleted ? StateReply(s, line, state => state.Delete == 0) : BoardReply.Ignore; }, out reason);
        }
        public bool Cancel(bool acknowledgeDiscard, out string reason)
        {
            if (!Ready(out reason)) return false; var st = gateway.Session.State;
            if ((st.Ui != "NAME" && st.Ui != "MENU" && !(st.Ui == "IDLE" && st.Delete != 0)) || st.Mode != "LIVE" && st.Mode != "HOLDING") return Deny("CANCEL is unavailable during preparation, settling, recording, saving or playback.", out reason);
            if (st.Unsaved && !acknowledgeDiscard) return Deny("Acknowledge that unsaved RAM may be lost or replaced; CANCEL does not save it.", out reason);
            return Send("Cancel recording UI", "!REC,CANCEL\r", (s, line) => StateReply(s, line, state => state.Ui == "IDLE" && !state.Unsaved && state.Delete == 0), out reason);
        }
    }
}
