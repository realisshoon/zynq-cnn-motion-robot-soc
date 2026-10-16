using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace HumanMotion.ControlStudio
{
    public enum BoardSide { Right, Left }

    public sealed class BoardRecordState
    {
        public BoardSide Side;
        public string Ui, Mode;
        public bool Pwm, Unsaved;
        public uint RamSamples, Entries, Selected, Playing, Delete;
    }

    public sealed class BoardTick
    {
        public uint Tick, TimeMs, DurationUs, Writes, Errors;
        public float? Remaining;
        public float[] Command;
        public ushort[] Pwm;
        public double ReceivedAt;
    }

    // No Unity or SerialPort dependency: the same reducer handles real RX and fixtures.
    public sealed class BoardUartSession
    {
        static readonly CultureInfo CI = CultureInfo.InvariantCulture;
        static readonly HashSet<string> Uis = new HashSet<string> { "IDLE", "START_RECORD", "STOP_RECORD", "STOP_MENU", "NAME", "MENU", "SAVE" };
        static readonly HashSet<string> Modes = new HashSet<string> { "LIVE", "RECORDING", "ALIGNING", "PLAYING", "HOLDING" };
        readonly StringBuilder line = new StringBuilder();
        bool discarding, haveTick;
        double lineStartedAt;
        uint previousTick, previousTime;
        public BoardSide Side { get; }
        public int Generation { get; private set; }
        public bool Connected { get; private set; }
        public string Fault { get; private set; } = "";
        public BoardRecordState State { get; private set; }
        public BoardTick Latest { get; private set; }
        public BoardTick LastApplied { get; private set; }
        public bool StopEscape { get; private set; }
        public bool? FollowEnabled { get; private set; }
        public string PendingText => line.ToString();
        public double PendingReceivedAt { get; private set; }
        public event Action<string, double> LineReceived;
        public event Action<string, double> PromptReceived;
        public string LastLine { get; private set; } = "";
        public long Lines { get; private set; }
        public long ParseErrors { get; private set; }
        public long OutOfOrder { get; private set; }
        public long MissingTicks { get; private set; }
        public bool Trusted => Connected && Fault.Length == 0 && State != null;
        public bool AtLineBoundary => line.Length == 0 && !discarding;
        public BoardUartSession(BoardSide side) { Side = side; }

        public void Begin()
        {
            Generation++; Connected = true; Fault = ""; State = null;
            Latest = LastApplied = null; StopEscape = false; FollowEnabled = null; haveTick = false;
            line.Clear(); discarding = false;
            Lines = ParseErrors = OutOfOrder = MissingTicks = 0;
        }
        public void Disconnect()
        {
            Connected = false; State = null; StopEscape = false; FollowEnabled = null;
            line.Clear(); discarding = false;
            // Keep the last displayed pose, marked held; never synthesize Home.
        }
        void InvalidateState()
        {
            State = null; FollowEnabled = null; Latest = null;
            // Trace gaps/malformed data invalidate live state, not a capability verified
            // for this connection. Keep explicit ESC S/X available while state is unknown.
            // LastApplied remains a held display value, never fresh evidence.
        }
        public bool AppliedCurrent(double now, double maxAge = .5)
            => Trusted && State.Pwm && LastApplied != null && ReferenceEquals(Latest, LastApplied)
               && now >= LastApplied.ReceivedAt && now - LastApplied.ReceivedAt <= maxAge;

        public void Feed(byte[] bytes, int count, double receivedAt)
        {
            if (!Connected || bytes == null || count < 0 || count > bytes.Length) return;
            for (int i = 0; i < count; i++)
            {
                byte b = bytes[i];
                if (b == 13 || b == 10)
                {
                    string complete = !discarding && line.Length != 0 ? line.ToString() : null;
                    line.Clear(); discarding = false;
                    if (complete != null) AcceptLine(complete, lineStartedAt);
                    continue;
                }
                if (discarding) continue;
                if ((b < 32 && b != 9) || b > 126 || line.Length >= 8192)
                { ParseErrors++; InvalidateState(); line.Clear(); discarding = true; continue; }
                if (line.Length == 0) lineStartedAt = receivedAt;
                line.Append((char)b);
            }
            if (line.Length != 0 && !discarding)
            { PendingReceivedAt = lineStartedAt; PromptReceived?.Invoke(line.ToString(), lineStartedAt); }
        }

        void Fail(string reason) { Fault = reason; StopEscape = false; InvalidateState(); ParseErrors++; }
        static uint UInt(string s)
        {
            if (!uint.TryParse(s, NumberStyles.None, CI, out var value)) throw new FormatException("Invalid unsigned integer");
            return value;
        }
        static bool Bit(string s)
        {
            if (s != "0" && s != "1") throw new FormatException("Invalid flag");
            return s == "1";
        }
        static float Float(string s)
        {
            if (!float.TryParse(s, NumberStyles.Float, CI, out var value) || float.IsNaN(value) || float.IsInfinity(value))
                throw new FormatException("Nonfinite command");
            return value;
        }
        static Dictionary<string, string> Fields(string s)
        {
            var fields = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var token in s.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries))
            {
                int eq = token.IndexOf('=');
                if (eq <= 0) continue;
                var key = token.Substring(0, eq);
                if (fields.ContainsKey(key)) throw new FormatException("Duplicate key");
                fields.Add(key, token.Substring(eq + 1));
            }
            return fields;
        }
        public void AcceptLine(string value, double receivedAt)
        {
            if (!Connected) return;
            LastLine = value; Lines++;
            ReduceLine(value, receivedAt);
            LineReceived?.Invoke(value, receivedAt);
        }
        void ReduceLine(string value, double receivedAt)
        {
            if (value.StartsWith("EV,0,BOOT,", StringComparison.Ordinal))
            {
                Generation++; State = null; Latest = LastApplied = null;
                StopEscape = false; FollowEnabled = null; haveTick = false; Fault = ""; return;
            }
            if (value.StartsWith("[ST]", StringComparison.Ordinal))
            {
                try { var f = Fields(value.Substring(4)); FollowEnabled = Bit(f["async_test"]); }
                catch (Exception e) when (e is FormatException || e is KeyNotFoundException) { FollowEnabled = null; }
                return;
            }
            if (value.StartsWith("[PWM] enabled=0 result=disabled_last_written_command_latched", StringComparison.Ordinal))
                FollowEnabled = false;
            if (value.StartsWith("[REC_STATE]", StringComparison.Ordinal))
            {
                try
                {
                    var f = Fields(value.Substring(11));
                    if (f["schema"] != "1") { Fail("UNSUPPORTED REC_STATE SCHEMA"); return; }
                    if (f["side"] != (Side == BoardSide.Right ? "r" : "l")) { Fail("BOARD SIDE MISMATCH / reconnect"); return; }
                    if (!Uis.Contains(f["ui"]) || !Modes.Contains(f["mode"])) throw new FormatException("Unknown board state");
                    var state = new BoardRecordState { Side = Side, Ui = f["ui"], Mode = f["mode"], Pwm = Bit(f["pwm"]),
                        Unsaved = Bit(f["unsaved"]), RamSamples = UInt(f["ram_samples"]), Entries = UInt(f["entries"]),
                        Selected = UInt(f["selected"]), Playing = UInt(f["playing"]), Delete = UInt(f["delete"]) };
                    if (state.RamSamples > 2048 || state.Entries > 32 || state.Selected > 32 || state.Playing > 32 || state.Delete > 32)
                        throw new FormatException("REC_STATE outside firmware limits");
                    if (Fault.Length == 0)
                    {
                        // A newly verified PWM state cannot retroactively approve an earlier TK.
                        if (State == null || State.Pwm != state.Pwm) Latest = null;
                        State = state;
                    }
                }
                catch (Exception e) when (e is FormatException || e is KeyNotFoundException) { Fail("MALFORMED REC_STATE / reconnect"); }
                return;
            }
            if (value.StartsWith("[UART]", StringComparison.Ordinal))
            {
                try
                {
                    var f = Fields(value.Substring(6));
                    if (f.TryGetValue("role", out var role) && role != (Side == BoardSide.Right ? "2" : "1")) { Fail("UART ROLE MISMATCH / reconnect"); return; }
                    if (f.TryGetValue("protocol", out var advertisedProtocol) && advertisedProtocol != "ESC_X_S") StopEscape = false;
                    if (Fault.Length == 0 && f.TryGetValue("stop_escape", out var escape))
                        StopEscape = Bit(escape) && f.TryGetValue("role", out var verifiedRole)
                            && verifiedRole == (Side == BoardSide.Right ? "2" : "1")
                            && f.TryGetValue("protocol", out var protocol) && protocol == "ESC_X_S";
                }
                catch (FormatException) { Fail("MALFORMED UART CAPABILITY / reconnect"); }
                return;
            }
            if (!value.StartsWith("TK,", StringComparison.Ordinal)) return;
            try
            {
                var f = value.Split(',');
                if (f.Length != 17) throw new FormatException("TK requires 17 fields");
                var tick = new BoardTick { Tick = UInt(f[1]), TimeMs = UInt(f[2]), DurationUs = UInt(f[3]), ReceivedAt = receivedAt,
                    Pwm = new ushort[5], Remaining = f[14].Length == 0 ? (float?)null : Float(f[14]), Writes = UInt(f[15]), Errors = UInt(f[16]) };
                if (tick.Remaining < 0 || tick.Writes > 1 || tick.Errors > 1) throw new FormatException("Invalid TK tail");
                bool empty = f[4].Length == 0;
                if (!empty) tick.Command = new float[5];
                for (int i = 0; i < 5; i++)
                {
                    if ((f[4 + i].Length == 0) != empty) throw new FormatException("Partial TK command");
                    if (!empty) tick.Command[i] = Float(f[4 + i]);
                    uint pwm = UInt(f[9 + i]);
                    if (pwm > ushort.MaxValue) throw new FormatException("PWM overflow");
                    tick.Pwm[i] = (ushort)pwm;
                }
                // Protocol bounds only. No A2 calibration, filtering, or PWM-to-angle inversion.
                if (!empty)
                {
                    var lo = new[] { 10f, 20f, 10f, 10f, 0f }; var hi = new[] { 170f, 180f, 160f, 170f, 1f };
                    for (int i = 0; i < 5; i++) if (tick.Command[i] < lo[i] || tick.Command[i] > hi[i]) throw new FormatException("TK command outside firmware range");
                }
                if (haveTick)
                {
                    uint delta = unchecked(tick.Tick - previousTick), timeDelta = unchecked(tick.TimeMs - previousTime);
                    if (delta == 0 || delta >= 0x80000000U || timeDelta >= 0x80000000U) { OutOfOrder++; return; }
                    MissingTicks += delta - 1;
                    // A missing trace can hide a state change; require a new REC_STATE.
                    if (delta > 1) InvalidateState();
                }
                previousTick = tick.Tick; previousTime = tick.TimeMs; haveTick = true; Latest = tick;
                if (Trusted && State.Pwm && tick.Command != null && tick.Writes > 0 && tick.Errors == 0) LastApplied = tick;
            }
            catch (FormatException) { ParseErrors++; InvalidateState(); }
        }
    }
}
