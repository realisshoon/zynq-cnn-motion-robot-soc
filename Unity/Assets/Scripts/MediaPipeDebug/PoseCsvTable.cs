using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using UnityEngine;

namespace HumanMotion.MediaPipeDebug
{
    public sealed class PoseCsvTable
    {
        public readonly List<PoseFrame> Frames = new List<PoseFrame>();
        private readonly Dictionary<long, PoseFrame> byId = new Dictionary<long, PoseFrame>();
        private static readonly string[] Names = {
            "left_shoulder", "left_elbow", "left_wrist", "right_shoulder", "right_elbow", "right_wrist" };
        public bool TryGet(long id, out PoseFrame frame) => byId.TryGetValue(id, out frame);

        public static PoseCsvTable Load(string path)
        {
            var table = new PoseCsvTable();
            using (var reader = new StreamReader(path, Encoding.UTF8, true))
            {
                string first = reader.ReadLine();
                if (first == null) throw new InvalidDataException("CSV가 비어 있습니다.");
                var header = Split(first.TrimStart('\uFEFF'));
                var columns = new Dictionary<string, int>(StringComparer.Ordinal);
                for (int i = 0; i < header.Count; ++i) columns.Add(header[i], i);
                int Col(string key) => columns.TryGetValue(key, out int i) ? i :
                    throw new InvalidDataException("필수 CSV 열 없음: " + key);
                int idCol = Col("video_frame_id"), timeCol = Col("video_time_sec");
                int fpsCol = Col("source_fps"), validCol = Col("landmark_valid");
                var xyz = new int[6, 5];
                string[] suffixes = { "x", "y", "z", "visibility", "presence" };
                for (int j = 0; j < 6; ++j)
                    for (int k = 0; k < 5; ++k) xyz[j, k] = Col(Names[j] + "_world_" + suffixes[k]);
                string line;
                int number = 1;
                while ((line = reader.ReadLine()) != null)
                {
                    ++number;
                    if (string.IsNullOrWhiteSpace(line)) continue;
                    var cells = Split(line);
                    if (cells.Count != header.Count) throw new InvalidDataException("CSV 열 수 오류: " + number);
                    if (!long.TryParse(cells[idCol], NumberStyles.Integer, CultureInfo.InvariantCulture, out long id) || id < 0)
                        throw new InvalidDataException("video_frame_id 오류: " + number);
                    double time = double.Parse(cells[timeCol], CultureInfo.InvariantCulture);
                    double fps = double.Parse(cells[fpsCol], CultureInfo.InvariantCulture);
                    if (double.IsNaN(time) || double.IsInfinity(time) || time < 0 ||
                        double.IsNaN(fps) || double.IsInfinity(fps) || fps <= 0)
                        throw new InvalidDataException("CSV timeline 오류: " + number);
                    if (table.Frames.Count > 0)
                    {
                        var prev = table.Frames[table.Frames.Count - 1];
                        if (id <= prev.frameId || time <= prev.timeSec || Math.Abs(fps - prev.sourceFps) > .001)
                            throw new InvalidDataException("CSV frame/time 순서 또는 FPS 오류: " + number);
                    }
                    var frame = new PoseFrame { frameId = id, timeSec = time, sourceFps = fps,
                        valid = cells[validCol] == "1" || cells[validCol].Equals("true", StringComparison.OrdinalIgnoreCase) };
                    for (int j = 0; j < 6; ++j)
                        frame.landmarks[j] = new PoseLandmark {
                            position = new Vector3(Value(cells[xyz[j, 0]]), Value(cells[xyz[j, 1]]), Value(cells[xyz[j, 2]])),
                            visibility = Value(cells[xyz[j, 3]]), presence = Value(cells[xyz[j, 4]]) };
                    table.Frames.Add(frame);
                    table.byId.Add(id, frame);
                }
            }
            if (table.Frames.Count == 0) throw new InvalidDataException("CSV pose row 없음");
            return table;
        }

        public PoseFrame Nearest(double time)
        {
            int lo = 0, hi = Frames.Count - 1;
            while (lo < hi)
            {
                int mid = (lo + hi) / 2;
                if (Frames[mid].timeSec < time) lo = mid + 1; else hi = mid;
            }
            if (lo > 0 && Math.Abs(Frames[lo - 1].timeSec - time) <= Math.Abs(Frames[lo].timeSec - time)) --lo;
            return Frames[lo];
        }

        private static float Value(string s) => float.TryParse(s, NumberStyles.Float,
            CultureInfo.InvariantCulture, out float value) ? value : float.NaN;

        private static List<string> Split(string line)
        {
            var result = new List<string>();
            var cell = new StringBuilder();
            bool quoted = false;
            for (int i = 0; i < line.Length; ++i)
            {
                char c = line[i];
                if (c == '"')
                {
                    if (quoted && i + 1 < line.Length && line[i + 1] == '"') { cell.Append('"'); ++i; }
                    else quoted = !quoted;
                }
                else if (c == ',' && !quoted) { result.Add(cell.ToString()); cell.Clear(); }
                else cell.Append(c);
            }
            if (quoted) throw new InvalidDataException("CSV 따옴표 오류");
            result.Add(cell.ToString());
            return result;
        }
    }
}
