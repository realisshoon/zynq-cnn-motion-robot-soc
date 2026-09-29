using System;
using System.IO;
using UnityEngine;
using UnityEngine.Video;

namespace HumanMotion.MediaPipeDebug
{
    [DisallowMultipleComponent]
    public sealed class CsvVideoPoseSource : MonoBehaviour, IPoseSource
    {
        [Tooltip("Unity 프로젝트 기준 상대 경로 또는 절대 경로")]
        public string videoPath = "../../src/동영상/example6_thumb_agent1_xyz_20hz.mp4";
        public string csvPath = "Tools/MediaPipePoseTest/logs/mediapipe_pose_20260929_123649_970082.csv";
        public bool playOnStart = true;
        public PoseFrame Current { get; private set; }
        public event Action<PoseFrame> FrameChanged;
        public VideoPlayer Player { get; private set; }
        public RenderTexture VideoTexture { get; private set; }
        public PoseCsvTable Table { get; private set; }
        public string Error { get; private set; } = "";
        public string SelectionMode { get; private set; } = "Preparing";
        public long DisplayedFrame { get; private set; } = -1;
        public bool Seeking => pendingFrame >= 0;
        public int AppliedFrames { get; private set; }
        private long pendingFrame = -1;
        private bool resumeAfterSeek, firstFrame = true;
        private double seekStarted;

        public static string Resolve(string path) => Path.GetFullPath(Path.IsPathRooted(path) ? path :
            Path.Combine(Application.dataPath, "..", path));

        private void Start()
        {
            try
            {
                Table = PoseCsvTable.Load(Resolve(csvPath));
                string movie = Resolve(videoPath);
                if (!File.Exists(movie)) throw new FileNotFoundException("영상 파일 없음", movie);
                Player = gameObject.AddComponent<VideoPlayer>();
                Player.playOnAwake = false;
                Player.source = VideoSource.Url;
                Player.url = new Uri(movie).AbsoluteUri;
                Player.audioOutputMode = VideoAudioOutputMode.None;
                Player.renderMode = VideoRenderMode.RenderTexture;
                Player.waitForFirstFrame = true;
                Player.skipOnDrop = true;
                Player.isLooping = false;
                Player.playbackSpeed = 1;
                Player.timeUpdateMode = VideoTimeUpdateMode.UnscaledGameTime;
                Player.sendFrameReadyEvents = true;
                Player.prepareCompleted += Prepared;
                Player.frameReady += Ready;
                Player.errorReceived += Failed;
                Player.Prepare();
            }
            catch (Exception e) { Error = e.Message; Debug.LogError(Error, this); }
        }

        private void Prepared(VideoPlayer player)
        {
            if (Math.Abs(player.frameRate - Table.Frames[0].sourceFps) > .02 ||
                (player.frameCount > 0 && Table.Frames[Table.Frames.Count - 1].frameId >= (long)player.frameCount))
            { Failed(player, "영상과 CSV의 FPS/frame 범위가 다릅니다."); return; }
            VideoTexture = new RenderTexture((int)player.width, (int)player.height, 0);
            VideoTexture.name = "MediaPipe Reference Video";
            VideoTexture.Create();
            player.targetTexture = VideoTexture;
            player.Play(); // 첫 decoded frame을 표시한 뒤 필요하면 pause한다.
        }

        private void Ready(VideoPlayer player, long frame)
        {
            if (pendingFrame >= 0 && frame != pendingFrame) return;
            Select(frame, player.time);
            if (pendingFrame >= 0)
            {
                pendingFrame = -1;
                if (resumeAfterSeek) player.Play(); else player.Pause();
            }
            if (firstFrame) { firstFrame = false; if (!playOnStart) player.Pause(); }
        }

        // frame을 얻으면 완전 일치만 사용한다. 누락 CSV를 nearest로 숨기지 않는다.
        public void Select(long frame, double time)
        {
            if (Table == null) return;
            PoseFrame pose;
            if (frame >= 0)
            {
                SelectionMode = "Exact frame";
                if (!Table.TryGet(frame, out pose))
                {
                    Error = "CSV rowなし: video frame " + frame;
                    Current = null;
                    DisplayedFrame = frame;
                    FrameChanged?.Invoke(null);
                    Player?.Pause();
                    return;
                }
            }
            else
            {
                if (double.IsNaN(time) || double.IsInfinity(time)) return;
                SelectionMode = "Time nearest (frame unavailable)";
                pose = Table.Nearest(time);
            }
            DisplayedFrame = frame;
            Error = "";
            if (ReferenceEquals(Current, pose)) return;
            Current = pose;
            ++AppliedFrames;
            FrameChanged?.Invoke(pose);
        }

        private void LateUpdate()
        {
            if (Player == null || !Player.isPrepared) return;
            if (Seeking)
            {
                if (Time.realtimeSinceStartupAsDouble - seekStarted > 8)
                { pendingFrame = -1; Failed(Player, "영상 seek timeout: pose를 미리 적용하지 않습니다."); }
                return;
            }
            // 표시된 frame을 기준으로 한다. Update 횟수나 독립 타이머로 진행하지 않는다.
            if (!firstFrame && Player.frame >= 0 && Player.frame != DisplayedFrame) Select(Player.frame, Player.time);
            else if (Player.frame < 0 && Player.texture != null && Player.time > 0)
            {
                Select(-1, Player.time);
                if (firstFrame) { firstFrame = false; if (!playOnStart) Player.Pause(); }
            }
        }

        public void TogglePause()
        {
            if (Player == null || !Player.isPrepared || Seeking) return;
            if (Player.isPlaying) Player.Pause(); else Player.Play();
        }

        public void Seek(long frame, bool resume = false)
        {
            if (Player == null || !Player.isPrepared || Seeking || !Player.canSetTime) return;
            frame = Math.Max(Table.Frames[0].frameId, Math.Min(Table.Frames[Table.Frames.Count - 1].frameId, frame));
            Player.Pause();
            if (Player.frame == frame) { Select(frame, Player.time); if (resume) Player.Play(); return; }
            pendingFrame = frame;
            resumeAfterSeek = resume;
            seekStarted = Time.realtimeSinceStartupAsDouble;
            Player.frame = frame;
        }

        public void Replay() => Seek(Table == null ? 0 : Table.Frames[0].frameId, true);
        public void Step(int delta) => Seek((Current == null ? 0 : Current.frameId) + delta);
        private void Failed(VideoPlayer player, string error)
        { Error = error; player.Pause(); Debug.LogError(error, this); }

        private void OnDestroy()
        {
            if (Player != null)
            {
                Player.prepareCompleted -= Prepared; Player.frameReady -= Ready; Player.errorReceived -= Failed;
                Player.Stop(); Player.targetTexture = null;
            }
            if (VideoTexture != null) { VideoTexture.Release(); Destroy(VideoTexture); }
        }
    }
}
