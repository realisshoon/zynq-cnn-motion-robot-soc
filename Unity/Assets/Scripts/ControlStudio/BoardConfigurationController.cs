using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace HumanMotion.ControlStudio
{
    // The firmware owns settings. This controller correlates one explicit request at a time.
    public sealed class BoardConfigurationController
    {
        static readonly CultureInfo CI = CultureInfo.InvariantCulture;
        public static readonly string[] RgbFieldNames = {
            "red margin", "green margin", "blue margin", "yellow R-G max difference",
            "yellow R/G above B min gap", "red minimum brightness", "green minimum brightness",
            "blue minimum brightness", "yellow minimum red", "yellow minimum green",
            "yellow maximum blue", "minimum detected taps", "color enable mask (R=1,B=2,G=4,Y=8)"
        };
        public static readonly string[] FilterKeys = { "EMA", "KXY", "KZ", "KACC", "KVEL", "MIN", "BETA", "DERIVATIVE" };
        static readonly string[] FilterFields = { "ema_tau_us", "kxy_milli_mm", "kz_milli_mm", "kacc_milli_mm_s2", "kvel_milli_mm_s", "min_mHz", "beta_u_per_mm", "derivative_mHz" };
        static readonly uint[] FilterLow = { 1000, 100, 100, 0, 0, 10, 0, 10 };
        static readonly uint[] FilterHigh = { 1000000, 1000000, 1000000, 5000000, 5000000, 10000, 100000, 10000 };
        static readonly decimal[] FilterScale = { 1000000, 1000, 1000, 1000, 1000, 1000, 1000000, 1000 };
        readonly IBoardCommandGateway gateway;
        readonly Func<double> now;
        int generation = -1;
        bool capabilityFault, pending, cnnKnown, cnnIdle, cnnAuto;
        bool cnnRunning, cnnSingle, cnnStopping, cnnFault, cnnCapture;
        double cnnAt;
        public string Status { get; private set; } = "Read t to verify firmware capability and CNN state";
        public BoardRequestState RequestState { get; private set; } = BoardRequestState.Idle;
        public bool ConfigSupported { get; private set; }
        public bool FilterKnown { get; private set; }
        public string Backend { get; private set; } = "UNKNOWN";
        public bool OneEuroTunable { get; private set; }
        public IReadOnlyDictionary<string, uint> FilterValues => filterValues;
        Dictionary<string, uint> filterValues = new Dictionary<string, uint>();
        public uint[] RgbValues { get; private set; }
        public bool RgbReadbackRequired { get; private set; }
        public bool RgbActive { get; private set; }
        public int RgbField { get; private set; } = -1;
        public uint RgbCurrent { get; private set; }
        public string RgbPrompt { get; private set; } = "";
        uint[] stagedRgb;
        public BoardSide Side => gateway.Session.Side;
        public bool Busy => pending || gateway.Busy;
        public bool CnnVerifiedIdle { get { Refresh(); return cnnKnown && cnnIdle && now() >= cnnAt && now() - cnnAt <= 5; } }
        public string CnnDisplayState {
            get {
                Refresh();
                if (!cnnKnown) return "UNKNOWN";
                if (now() < cnnAt || now() - cnnAt > 5) return "STALE";
                if (cnnFault) return "FAULT";
                if (cnnStopping) return "STOPPING";
                if (cnnAuto) return "ON";
                return cnnIdle ? "OFF / IDLE" : cnnRunning ? "OFF / FRAME FINISHING" : "OFF / FRAME OR CAPTURE BUSY";
            }
        }

        public BoardConfigurationController(IBoardCommandGateway gateway, Func<double> clock = null)
        {
            this.gateway = gateway ?? throw new ArgumentNullException(nameof(gateway));
            now = clock ?? (() => (double)Stopwatch.GetTimestamp() / Stopwatch.Frequency);
            Refresh();
        }
        public void Refresh()
        {
            var s = gateway.Session;
            if (generation != s.Generation || !s.Connected)
            {
                generation = s.Generation; ConfigSupported = FilterKnown = OneEuroTunable = false;
                capabilityFault = cnnKnown = cnnIdle = RgbActive = RgbReadbackRequired = false;
                Backend = "UNKNOWN"; filterValues.Clear(); RgbValues = stagedRgb = null;
                RgbField = -1; RgbPrompt = "";
            }
            if (!s.Trusted) { FilterKnown = false; cnnKnown = false; RgbActive = false; }
        }
        public void ObserveLine(string line, double receivedAt)
        {
            Refresh();
            if (line == null || !gateway.Session.Connected) return;
            if (line.StartsWith("[CFG] supported=", StringComparison.Ordinal))
            {
                try
                {
                    var f = Fields(line);
                    bool valid = f["supported"] == "1" && f["schema"] == "1" && f["protocol"] == "@CFG"
                        && f["role"] == (Side == BoardSide.Right ? "2" : "1");
                    if (!valid) capabilityFault = true;
                    ConfigSupported = valid && !capabilityFault;
                }
                catch (Exception e) when (e is FormatException || e is KeyNotFoundException)
                { capabilityFault = true; ConfigSupported = false; }
            }
            if (line.StartsWith("CNN continuous mode: ON", StringComparison.Ordinal)
                || line.StartsWith("CNN continuous mode: OFF", StringComparison.Ordinal)
                || line.StartsWith("CNN run:", StringComparison.Ordinal)
                || line.StartsWith("CNN soft reset", StringComparison.Ordinal)
                || line.Contains("configuration menu cancelled"))
            { cnnKnown = false; if (line.Contains("configuration menu cancelled")) RgbActive = false; }
        }
        bool Gate(bool mutation, bool storage, out string reason)
        {
            Refresh(); var s = gateway.Session;
            reason = "";
            if (!s.Trusted || capabilityFault) reason = "Verified board state and matching firmware capability required";
            else if (Busy) reason = "Another request is pending; no automatic retry";
            else if (!gateway.BoundaryConfirmed || !s.AtLineBoundary) reason = "Confirm an idle console boundary";
            else if (RgbActive) reason = "Finish the RGBY13 menu or use the explicit escape Stop control";
            else if (s.State.Ui == "NAME" || s.State.Ui == "MENU" || s.State.Ui == "SAVE") reason = "Board menu or save is active";
            else if (mutation && (s.State.Ui != "IDLE" || (s.State.Mode != "LIVE" && s.State.Mode != "HOLDING"))) reason = "Recording, playback and alignment block setting changes";
            else if (storage && (s.State.Pwm || s.State.Mode != "LIVE" || !CnnVerifiedIdle)) reason = "CFG save/load needs PWM OFF, LIVE/IDLE and a fresh idle t response";
            return reason.Length == 0;
        }
        bool Send(string name, string wire, Func<string, BoardReply> match, bool prompt, out string reason)
            => Send(name, wire, (line, receivedAt) => match(line), prompt, out reason);
        bool Send(string name, string wire, Func<string, double, BoardReply> match, bool prompt, out string reason)
        {
            int requestGeneration = gateway.Session.Generation;
            pending = true; RequestState = BoardRequestState.AwaitingReply;
            var request = new BoardRequest {
                Name = name, Wire = Encoding.ASCII.GetBytes(wire), AllowPrompt = prompt, TimeoutSeconds = name.StartsWith("CFG ", StringComparison.Ordinal) ? 10 : 5,
                MatchWithReceivedAt = (s, line, receivedAt) => {
                    if (!s.Connected || s.Generation != requestGeneration || !s.Trusted) return BoardReply.Rejected;
                    ObserveLine(line, receivedAt);
                    try { return match(line, receivedAt); }
                    catch (Exception e) when (e is FormatException || e is KeyNotFoundException || e is OverflowException)
                    { Status = "Malformed configuration reply; re-read required"; return BoardReply.Rejected; }
                },
                Completed = (state, detail) => {
                    pending = false; RequestState = state;
                    if (state != BoardRequestState.Accepted) {
                        FilterKnown = false; cnnKnown = false; RgbActive = false;
                        Status = name + " " + state + " / " + detail + "; do not retry automatically";
                    } else Status = RgbReadbackRequired && name.StartsWith("RGBY13", StringComparison.Ordinal)
                        ? "RGBY13 menu completed; CFG SHOW required to verify actual values"
                        : name + " confirmed by board";
                }
            };
            if (gateway.TryRequest(request, out reason)) { Status = name + " awaiting board evidence"; return true; }
            pending = false; RequestState = BoardRequestState.Rejected; Status = reason; return false;
        }
        public bool ReadCnnStatus(out string reason)
        {
            if (!Gate(false, false, out reason)) return false;
            cnnKnown = false; bool hardware = false, error = false, capture = false; uint hw = 0, err = 0; bool captureIdle = false;
            double evidenceAt = double.PositiveInfinity;
            return Send("CNN / capability t", "t", (line, receivedAt) => {
                var m = Regex.Match(line, @"^\s*CNN status\s*:\s*([0-9a-fA-F]{8})\s*$");
                if (m.Success) { hw = uint.Parse(m.Groups[1].Value, NumberStyles.HexNumber, CI); hardware = true; evidenceAt = Math.Min(evidenceAt, receivedAt); }
                m = Regex.Match(line, @"^\s*CNN error\s*:\s*([0-9a-fA-F]{8})\s*$");
                if (m.Success) { err = uint.Parse(m.Groups[1].Value, NumberStyles.HexNumber, CI); error = true; evidenceAt = Math.Min(evidenceAt, receivedAt); }
                if (line.StartsWith("Frame capture:", StringComparison.Ordinal)) {
                    var f = Fields(line); if (f["pending"] != "0" && f["pending"] != "1") throw new FormatException();
                    captureIdle = f["pending"] == "0"; capture = true; evidenceAt = Math.Min(evidenceAt, receivedAt);
                }
                if (!line.StartsWith("CNN app state:", StringComparison.Ordinal)) return BoardReply.Ignore;
                var a = Fields(line);
                foreach (string key in new[] { "running", "auto", "single", "stopping", "fault" }) if (a[key] != "0" && a[key] != "1") throw new FormatException();
                if (!hardware || !error || !capture) return BoardReply.Rejected;
                // Freshness starts at RX, using the oldest line in the complete evidence.
                cnnKnown = true; cnnAt = Math.Min(evidenceAt, receivedAt);
                cnnAuto = a["auto"] == "1"; cnnRunning = a["running"] == "1";
                cnnSingle = a["single"] == "1"; cnnStopping = a["stopping"] == "1";
                cnnCapture = !captureIdle;
                cnnFault = a["fault"] == "1" || err != 0 || (hw & 4) != 0;
                cnnIdle = captureIdle && (hw & 6) == 0 && err == 0 && a["running"] == "0" && a["auto"] == "0" && a["single"] == "0" && a["stopping"] == "0" && a["fault"] == "0";
                return BoardReply.Accepted;
            }, false, out reason);
        }
        // Firmware 'a' is a toggle, not an idempotent ON/OFF command. Never
        // infer its direction from a button label or an earlier write receipt.
        public bool CanSetCnnRunning(bool enabled, out string reason)
        {
            if (!Gate(true, false, out reason)) return false;
            if (!ConfigSupported || !cnnKnown || now() < cnnAt || now() - cnnAt > 5)
                reason = "Read t first: fresh matching CNN state is required";
            else if (cnnAuto == enabled)
                reason = enabled ? "CNN is already ON; no toggle sent" : "CNN is already OFF; no toggle sent";
            else if (cnnFault || cnnSingle || cnnStopping || cnnCapture)
                reason = "CNN fault, reset, single frame or capture pending; read t and resolve first";
            else if (enabled && !cnnIdle)
                reason = "Wait for the current frame to finish and read t again";
            return reason.Length == 0;
        }
        public bool SetCnnRunning(bool enabled, out string reason)
        {
            if (!CanSetCnnRunning(enabled, out reason)) return false;
            cnnKnown = false;
            return Send(enabled ? "CNN START" : "CNN STOP", "a", line => {
                if (line.StartsWith("CNN 'a' rejected:", StringComparison.Ordinal)) return BoardReply.Rejected;
                bool accepted = enabled
                    ? line == "CNN continuous mode: ON; press 'a' to stop after the current frame"
                    : Regex.IsMatch(line, @"^CNN continuous mode: OFF after \d+ frame\(s\)(; active frame will finish)?$");
                return accepted ? BoardReply.Accepted : BoardReply.Ignore;
            }, false, out reason);
        }
        public bool ReadConfig(out string reason) => ConfigRequest("SHOW", out reason);
        public bool SaveConfig(out string reason) => ConfigRequest("SAVE", out reason);
        public bool LoadConfig(out string reason) => ConfigRequest("LOAD", out reason);
        bool ConfigRequest(string action, out string reason)
        {
            if (!Gate(action != "SHOW", action != "SHOW", out reason)) return false;
            if (!ConfigSupported) { reason = "Read t: matching @CFG schema=1 capability is required"; return false; }
            var values = new uint[13]; bool first = false, second = false, applied = action == "SHOW";
            if (action == "LOAD") { RgbValues = null; FilterKnown = false; }
            return Send("CFG " + action, "@CFG," + action + "\r", line => {
                if (!line.StartsWith("[CFG]", StringComparison.Ordinal)) return BoardReply.Ignore;
                if (line.Contains("REJECTED") || line.Contains("FAILED")) return BoardReply.Rejected;
                if (action == "SAVE") return line.StartsWith("[CFG] SAVED path=0:/UARTCFG.BIN", StringComparison.Ordinal) ? BoardReply.Accepted : BoardReply.Ignore;
                if (line.StartsWith("[CFG] LOADED ", StringComparison.Ordinal)) {
                    if (Fields(line)["role"] != (Side == BoardSide.Right ? "2" : "1")) return BoardReply.Rejected;
                    applied = true;
                }
                if (line.StartsWith("[CFG] role=", StringComparison.Ordinal)) {
                    var f = Fields(line); if (f["role"] != (Side == BoardSide.Right ? "2" : "1")) return BoardReply.Rejected;
                    var names = new[] { "red", "green", "blue", "yellow_delta", "yellow_gap" };
                    for (int i = 0; i < 5; i++) values[i] = Integer(f[names[i]], 0, 255);
                    first = true; second = false;
                }
                if (line.StartsWith("[CFG] brightness ", StringComparison.Ordinal)) {
                    var m = Regex.Match(line, @"^\[CFG\] brightness R/G/B=(\d+)/(\d+)/(\d+) yellow R/G/Bmax=(\d+)/(\d+)/(\d+) min_count=(\d+) mask=(\d+)\s*$");
                    if (!first || !m.Success) return BoardReply.Rejected;
                    for (int i = 0; i < 8; i++) values[5 + i] = Integer(m.Groups[i + 1].Value, RgbLow(5 + i), RgbHigh(5 + i));
                    second = true;
                }
                if (!line.StartsWith("[CFG] persistence=SD path=0:/UARTCFG.BIN;", StringComparison.Ordinal)) return BoardReply.Ignore;
                if (!first || !second || !applied) return BoardReply.Rejected;
                RgbValues = values; RgbReadbackRequired = false; return BoardReply.Accepted;
            }, false, out reason);
        }
        sealed class FilterReport
        {
            public bool Header, Kalman, Euro, TimePolicy, Tunable;
            public string Backend;
            public readonly Dictionary<string, uint> Values = new Dictionary<string, uint>();
            public bool Complete => Header && Kalman && TimePolicy && (!Tunable || Euro);
        }
        static void ParseFilter(string line, FilterReport report)
        {
            if (!line.StartsWith("[FILTER]", StringComparison.Ordinal)) return;
            var f = Fields(line);
            if (f.ContainsKey("backend")) {
                report.Values.Clear(); report.Kalman = report.Euro = report.TimePolicy = false;
                report.Backend = f["backend"]; report.Header = true;
                if (f["one_euro_tunable"] != "0" && f["one_euro_tunable"] != "1") throw new FormatException();
                report.Tunable = f["one_euro_tunable"] == "1";
                report.Values["EMA"] = Integer(f["ema_tau_us"], FilterLow[0], FilterHigh[0]);
                if (f["persistence"] != "RAM") throw new FormatException();
            }
            if (f.ContainsKey("kxy_milli_mm")) {
                if (!report.Header) throw new FormatException();
                for (int i = 1; i <= 4; i++) report.Values[FilterKeys[i]] = Integer(f[FilterFields[i]], FilterLow[i], FilterHigh[i]);
                report.Kalman = true;
            }
            if (f.ContainsKey("time_policy")) {
                if (f["time_policy"] != "G_SHORT_GAP_HISTORY") throw new FormatException();
                report.TimePolicy = true;
            }
            if (f.ContainsKey("min_mHz")) {
                if (!report.Header || !report.Tunable) throw new FormatException();
                for (int i = 5; i <= 7; i++) report.Values[FilterKeys[i]] = Integer(f[FilterFields[i]], FilterLow[i], FilterHigh[i]);
                report.Euro = true;
            }
        }
        void CommitFilter(FilterReport report)
        { Backend = report.Backend; OneEuroTunable = report.Tunable; filterValues = report.Values; FilterKnown = true; }
        public bool ReadFilter(out string reason)
        {
            if (!Gate(false, false, out reason)) return false;
            if (Side != BoardSide.Right || !ConfigSupported) { reason = "RIGHT only; read t capability first"; return false; }
            FilterKnown = false; var report = new FilterReport();
            return Send("FILTER SHOW", "~F,SHOW\r", line => {
                if (line.StartsWith("[FILTER] result=REJECTED", StringComparison.Ordinal)) return BoardReply.Rejected;
                ParseFilter(line, report); if (!report.Complete) return BoardReply.Ignore;
                CommitFilter(report); return BoardReply.Accepted;
            }, false, out reason);
        }
        public bool CanApplyFilter(string key)
        {
            Refresh(); int index = Array.IndexOf(FilterKeys, key);
            return Side == BoardSide.Right && FilterKnown && index >= 0 && filterValues.ContainsKey(key)
                && (index == 0 || (index <= 4 ? Backend == "G_KALMAN" : OneEuroTunable));
        }
        public string DisplayFilter(string key)
        {
            int i = Array.IndexOf(FilterKeys, key);
            return i >= 0 && FilterKnown && filterValues.TryGetValue(key, out var value) ? (value / FilterScale[i]).ToString(CI) : "";
        }
        public bool ApplyFilter(string key, string value, out string reason)
        {
            if (!Gate(true, false, out reason)) return false;
            if (!CanApplyFilter(key)) { reason = "Read valid filter values and backend before applying this parameter"; return false; }
            int i = Array.IndexOf(FilterKeys, key);
            if (!Regex.IsMatch(value ?? "", @"^[0-9]+(?:\.[0-9]+)?$") || !decimal.TryParse(value, NumberStyles.AllowDecimalPoint, CI, out var numeric))
            { reason = "Use ASCII decimal digits; signs and exponents are unsupported"; return false; }
            decimal scaled;
            try { scaled = numeric * FilterScale[i]; } catch (OverflowException) { reason = "Value exceeds range"; return false; }
            if (scaled != decimal.Truncate(scaled) || scaled < FilterLow[i] || scaled > FilterHigh[i])
            { reason = "Value must match firmware range and integer wire precision exactly; no rounding"; return false; }
            uint requested = (uint)scaled; string expectedBackend = Backend; bool acknowledged = false;
            FilterKnown = false; var report = new FilterReport();
            return Send("FILTER " + key, "~F," + key + "," + requested.ToString(CI) + "\r", line => {
                if (line.StartsWith("[FILTER] result=", StringComparison.Ordinal)) {
                    var f = Fields(line); if (f["result"] != "APPLIED") return BoardReply.Rejected;
                    if (f["key"] != key || Integer(f["requested"], 0, uint.MaxValue) != requested) return BoardReply.Rejected;
                    acknowledged = true;
                }
                ParseFilter(line, report);
                if (!report.Complete) return BoardReply.Ignore;
                if (!acknowledged || report.Backend != expectedBackend || !report.Values.TryGetValue(key, out var actual) || actual != requested) return BoardReply.Rejected;
                CommitFilter(report); return BoardReply.Accepted;
            }, false, out reason);
        }
        public static uint RgbLow(int index) => index == 11 ? 1U : 0U;
        public static uint RgbHigh(int index) => index == 11 ? 262143U : index == 12 ? 15U : 255U;
        static bool Prompt(string text, int index, out uint current)
        {
            current = 0;
            var m = Regex.Match(text ?? "", @"^\s*(.*?) \[current (\d+), (\d+)\.\.(\d+), Enter=keep\]:\s*$");
            if (!m.Success || index < 0 || index >= 13 || m.Groups[1].Value != RgbFieldNames[index]) return false;
            if (Integer(m.Groups[3].Value, 0, uint.MaxValue) != RgbLow(index) || Integer(m.Groups[4].Value, 0, uint.MaxValue) != RgbHigh(index)) throw new FormatException();
            current = Integer(m.Groups[2].Value, RgbLow(index), RgbHigh(index)); return true;
        }
        void SetPrompt(string text, int index, uint current)
        { RgbActive = true; RgbField = index; RgbCurrent = current; RgbPrompt = text; stagedRgb[index] = current; }
        public bool BeginRgb(out string reason)
        {
            if (!Gate(true, false, out reason)) return false;
            if (!ConfigSupported || !CnnVerifiedIdle) { reason = "Read t and verify CNN completely idle before m; no automatic CNN toggle"; return false; }
            bool heading = false; stagedRgb = new uint[13]; RgbValues = null; RgbReadbackRequired = false; cnnKnown = false;
            return Send("RGBY13 begin", "m", line => {
                if (line.Trim() == "CNN RGBY detection settings (stop inference first)") heading = true;
                if (line.Contains("rejected") || line.Contains("failed")) return BoardReply.Rejected;
                if (!Prompt(line, 0, out var current)) return BoardReply.Ignore;
                // A buffered pre-write prompt can acquire a post-write CR timestamp.
                // It cannot acknowledge m until the matching post-write menu heading arrived.
                if (!heading) return BoardReply.Ignore;
                SetPrompt(line, 0, current); return BoardReply.Accepted;
            }, true, out reason);
        }
        public bool SubmitRgb(string value, bool keep, out string reason)
        {
            Refresh(); reason = "";
            var s = gateway.Session;
            if (!RgbActive || Busy || !s.Trusted || !gateway.BoundaryConfirmed || !Prompt(s.PendingText, RgbField, out var current) || current != RgbCurrent)
            { reason = "An exact current RGBY13 prompt is required; do not send numbers to an unknown menu"; return false; }
            uint requested = current;
            if (!keep && (!uint.TryParse(value, NumberStyles.None, CI, out requested) || requested < RgbLow(RgbField) || requested > RgbHigh(RgbField)))
            { reason = "Enter an unsigned integer within the displayed prompt range"; return false; }
            int field = RgbField; stagedRgb[field] = requested;
            return Send("RGBY13 field " + (field + 1), keep ? "\r" : requested.ToString(CI) + "\r", line => {
                if (line.Contains("configuration unchanged") || line.Contains("menu cancelled") || line.Contains("RGBY settings failed")) return BoardReply.Rejected;
                if (field < 12) {
                    if (Prompt(line, field + 1, out var next)) { SetPrompt(line, field + 1, next); return BoardReply.Accepted; }
                    return BoardReply.Ignore; // Current prompt with numeric echo is not an acknowledgement.
                }
                var m = Regex.Match(line, @"^\s*RGBY settings applied \(0\); min count=(\d+) mask=0x([0-9a-fA-F]+)\s*$");
                if (!m.Success) return BoardReply.Ignore;
                if (Integer(m.Groups[1].Value, 1, 262143) != stagedRgb[11] || uint.Parse(m.Groups[2].Value, NumberStyles.HexNumber, CI) != stagedRgb[12]) return BoardReply.Rejected;
                // The firmware reports only taps/mask here. Requested margins/thresholds
                // are not readback evidence; publish all 13 values only after CFG SHOW.
                RgbActive = false; RgbField = -1; RgbPrompt = ""; RgbValues = null; RgbReadbackRequired = true; return BoardReply.Accepted;
            }, true, out reason);
        }
        static uint Integer(string text, uint low, uint high)
        { if (!uint.TryParse(text, NumberStyles.None, CI, out var value) || value < low || value > high) throw new FormatException("Invalid setting integer"); return value; }
        static Dictionary<string, string> Fields(string line)
        {
            var result = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (Match m in Regex.Matches(line, @"(?:^|\s)([A-Za-z0-9_]+)=([^\s;]+)")) {
                if (result.ContainsKey(m.Groups[1].Value)) throw new FormatException("Duplicate setting key");
                result.Add(m.Groups[1].Value, m.Groups[2].Value);
            }
            return result;
        }
    }
}
