using System;
using System.Collections.Generic;

namespace HumanMotion.ControlStudio
{
    public static class BoardControlCommands
    {
        static Dictionary<string, string> Fields(string line)
        {
            var result = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var token in line.Split(new[] { ' ', '\t', ';' }, StringSplitOptions.RemoveEmptyEntries))
            {
                int eq = token.IndexOf('=');
                if (eq > 0) { var key = token.Substring(0, eq); if (result.ContainsKey(key)) return null; result.Add(key, token.Substring(eq + 1)); }
            }
            return result;
        }
        public static bool CanSend(IBoardCommandGateway gateway, char key, out string reason)
        {
            reason = "";
            if (gateway == null || "EASXOH".IndexOf(key) < 0) reason = "Unknown board control key";
            else if (gateway.IsSimulation) reason = "Fixture is RX only";
            else if (!gateway.Session.Connected) reason = "Board disconnected";
            else if (gateway.Session.Fault.Length != 0) reason = gateway.Session.Fault;
            else if (key == 'S' || key == 'X')
            {
                if (!gateway.Session.StopEscape && (!gateway.BoundaryConfirmed || !gateway.Session.AtLineBoundary || gateway.Busy
                    || !gateway.Session.Trusted || gateway.Session.State.Ui != "IDLE"))
                    reason = "Stop escape not verified: confirm idle command boundary first";
            }
            else if (gateway.Busy) reason = "Awaiting board response";
            else if (gateway is BoardCommandGateway concrete && concrete.Uncertain) reason = "Recover console and query V after unknown result";
            else if (!gateway.Session.Trusted) reason = "Query V for verified REC_STATE";
            else if (!gateway.BoundaryConfirmed || !gateway.Session.AtLineBoundary) reason = "Console boundary unconfirmed";
            else if (gateway.Session.State.Ui != "IDLE" || gateway.Session.State.Mode != "LIVE") reason = "Control requires IDLE / LIVE";
            else if (key != 'E' && !gateway.Session.State.Pwm) reason = "Enable PWM with E and verify the board response first";
            return reason.Length == 0;
        }
        public static bool TrySend(IBoardCommandGateway gateway, char key, out string reason)
        {
            if (!CanSend(gateway, key, out reason)) return false;
            var side = gateway.Session.Side;
            bool stop = key == 'S' || key == 'X';
            var request = new BoardRequest { Name = side + " " + key, IsStop = stop, RequireTrusted = !stop,
                Wire = stop && gateway.Session.StopEscape ? new[] { (byte)27, (byte)key } : new[] { (byte)key },
                Match = (s, line) => Match(side, key, line) };
            return gateway.TryRequest(request, out reason);
        }
        public static bool CanSendBoth(IBoardCommandGateway right, IBoardCommandGateway left, char key, out string reason)
        {
            bool r = CanSend(right, key, out var rightReason);
            bool l = CanSend(left, key, out var leftReason);
            reason = r && l ? "" : "RIGHT: " + (r ? "ready" : rightReason) + "; LEFT: " + (l ? "ready" : leftReason);
            // A stop must remain available to either eligible port when its peer is unavailable.
            return key == 'S' || key == 'X' ? r || l : r && l;
        }
        public static bool TrySendBoth(IBoardCommandGateway right, IBoardCommandGateway left, char key, out string status)
        {
            if (!CanSendBoth(right, left, key, out status)) return false;
            bool r = TrySend(right, key, out var rightReason);
            bool l = TrySend(left, key, out var leftReason);
            status = "RIGHT: " + (r ? "request queued" : rightReason) + "; LEFT: " + (l ? "request queued" : leftReason);
            return key == 'S' || key == 'X' ? r || l : r && l;
        }
        public static BoardReply Match(BoardSide side, char key, string line)
        {
            bool follow = key == 'A' || key == 'S';
            if (!line.StartsWith(follow ? "[ST] " : "[PWM] ", StringComparison.Ordinal)) return BoardReply.Ignore;
            var f = Fields(line);
            if (f == null || !f.TryGetValue("result", out var result) || !f.TryGetValue("mode", out var mode)) return BoardReply.Ignore;
            if (!follow && (!f.TryGetValue("arm", out var arm) || arm != (side == BoardSide.Right ? "ROBOT0_JB_CH0_CH4" : "ROBOT1_JC_CH0_CH4"))) return BoardReply.Ignore;
            if (result.StartsWith("rejected_", StringComparison.Ordinal) || result.StartsWith("HAL_", StringComparison.Ordinal)) return BoardReply.Rejected;
            if (follow)
            {
                string expected = key == 'A' ? (side == BoardSide.Right ? "enabled_UNSYNCHRONIZED_test" : "enabled_remote_UNSYNCHRONIZED_test")
                    : (side == BoardSide.Right ? "disabled_new_async_targets" : "disabled_new_remote_targets");
                return result == expected && f.TryGetValue("async_test", out var enabled) && enabled == (key == 'A' ? "1" : "0")
                    && (key != 'A' || (mode == "LIVE" && f.TryGetValue("pwm", out var pwm) && pwm == "1")) ? BoardReply.Accepted : BoardReply.Ignore;
            }
            string wanted = key == 'E' ? "enabled_physical_pose_not_verified" : key == 'X' ? "disabled_last_written_command_latched"
                : key == 'O' ? "manual_open_until_H" : "automatic_restored";
            return result == wanted && f.TryGetValue("enabled", out var bit) && bit == (key == 'X' ? "0" : "1")
                && (key == 'X' || mode == "LIVE") ? BoardReply.Accepted : BoardReply.Ignore;
        }
    }
}
