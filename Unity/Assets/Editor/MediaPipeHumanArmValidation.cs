using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using HumanMotion.MediaPipeDebug;

// 명시적인 메뉴/배치 호출에서만 실행한다. 기존 Scene을 자동 수정하지 않는다.
[InitializeOnLoad]
public static class MediaPipeHumanArmValidation
{
    private const string Active = "MediaPipeHumanArm.Validation";
    private const string ScenePath = "Assets/Scenes/Demo_04_MediaPipeHumanArmDebug.unity";
    private static CsvVideoPoseSource source;
    private static HumanArmDebugController arm;
    private static int stage, seekIndex, checkedFrames;
    private static double deadline, pauseStarted, replayStarted;
    private static long pausedFrame;
    private static double pausedTime;
    private static readonly long[] SeekFrames = { 240, 293, 294, 574, 721, 852 };
    private static readonly List<string> results = new List<string>();

    static MediaPipeHumanArmValidation() { EditorApplication.update += Tick; }

    [MenuItem("Tools/Human Motion/MediaPipe Debug/Open Replay Scene")]
    public static void Open()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode) return;
        if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
        EditorSceneManager.OpenScene(ScenePath);
    }

    [MenuItem("Tools/Human Motion/MediaPipe Debug/Run Replay Validation")]
    public static void Run()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode) return;
        if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
        StartValidation();
    }

    public static void RunBatch() { StartValidation(); }
    private static void StartValidation()
    {
        EditorSceneManager.OpenScene(ScenePath);
        var s = UnityEngine.Object.FindFirstObjectByType<CsvVideoPoseSource>();
        string[] args = Environment.GetCommandLineArgs();
        for (int i = 0; i + 1 < args.Length; ++i)
        {
            if (args[i] == "-poseCsv") s.csvPath = args[i + 1];
            if (args[i] == "-poseVideo") s.videoPath = args[i + 1];
        }
        s.playOnStart = false;
        SessionState.SetBool(Active, true);
        EditorApplication.isPlaying = true;
    }

    private static void Require(bool ok, string message)
    { if (!ok) throw new InvalidOperationException(message); }

    private static void CheckGeometry(PoseFrame f)
    {
        Require(f != null && arm.AppliedPose == f, "source/controller frame 불일치");
        for (int i = 0; i < 6; ++i)
        {
            var p = f.landmarks[i].position;
            if (!f.landmarks[i].HasPosition) { Require(!arm.Joints[i].gameObject.activeSelf, "비유한 점 숨김 실패"); continue; }
            var expected = new Vector3(p.x, -p.y, -p.z);
            Require(Vector3.Distance(expected, arm.Joints[i].position) < .000001f, "RAW 좌표 변경: " + i);
        }
        for (int i = 0; i < 4; ++i)
        {
            int a = i / 2 * 3 + i % 2;
            Require(Vector3.Distance(arm.Links[i].GetPosition(0), arm.Joints[a].position) < .000001f &&
                Vector3.Distance(arm.Links[i].GetPosition(1), arm.Joints[a + 1].position) < .000001f, "링크 endpoint 불일치");
        }
    }

    private static void Tick()
    {
        if (!SessionState.GetBool(Active, false) || !EditorApplication.isPlaying) return;
        try
        {
            if (source == null)
            {
                source = UnityEngine.Object.FindFirstObjectByType<CsvVideoPoseSource>();
                arm = UnityEngine.Object.FindFirstObjectByType<HumanArmDebugController>();
                deadline = EditorApplication.timeSinceStartup + 30;
            }
            double now = EditorApplication.timeSinceStartup;
            Require(now < deadline, "검증 timeout stage=" + stage);
            if (source == null || source.Table == null) return;
            Require(string.IsNullOrEmpty(source.Error), source.Error);
            if (source.Current == null) return;
            switch (stage)
            {
                case 0:
                    Require(source.Current.frameId == 0 && !source.Player.isPlaying, "초기 frame 0 pause 실패");
                    Require(source.Table.Frames.Count == 853 && source.Player.frameCount == 853, "853 frame 계약 실패");
                    Require(Math.Abs(source.Player.frameRate - 20) < .001, "20 FPS 계약 실패");
                    foreach (var f in source.Table.Frames)
                    {
                        Require(source.Table.TryGet(f.frameId, out var same) && ReferenceEquals(f, same), "frame lookup");
                        Require(source.Table.Nearest(f.timeSec) == f, "time lookup");
                        arm.Apply(f); CheckGeometry(f);
                    }
                    arm.Apply(source.Current);
                    source.Select(-1, 14.70);
                    Require(source.Current.frameId == 294 && source.SelectionMode.StartsWith("Time nearest"), "frame unavailable fallback");
                    source.Select(0, 0);
                    results.Add("PASS: 853행 RAW XYZ / 양팔 6관절 / 4링크 / frame/time 조회 (invalid 포함)");
                    pauseStarted = now; pausedFrame = source.Player.frame; pausedTime = source.Player.time;
                    stage = 1;
                    break;
                case 1:
                    Require(source.Player.frame == pausedFrame && Math.Abs(source.Player.time - pausedTime) < .001 && source.Current.frameId == pausedFrame, "pause 이동");
                    if (now - pauseStarted < .7) return;
                    results.Add("PASS: pause 0.7초 동안 영상/pose 정지");
                    source.TogglePause(); stage = 2;
                    break;
                case 2:
                    if (source.Current.frameId < 5) return;
                    CheckGeometry(source.Current);
                    Require(source.DisplayedFrame == source.Current.frameId, "resume sync");
                    results.Add("PASS: resume frame 동기화");
                    source.Seek(SeekFrames[0]); stage = 3;
                    break;
                case 3:
                    if (source.Seeking || source.Current.frameId != SeekFrames[seekIndex]) return;
                    Require(source.Player.frame == SeekFrames[seekIndex], "seek video/CSV 불일치");
                    CheckGeometry(source.Current);
                    Capture(SeekFrames[seekIndex]);
                    if (SeekFrames[seekIndex] == 574 || SeekFrames[seekIndex] == 721)
                        Require(arm.LowConfidenceCount > 0 && !source.Current.valid, "LOW CONFIDENCE 표시 실패");
                    results.Add("PASS: exact seek + RAW geometry frame " + SeekFrames[seekIndex]);
                    ++seekIndex;
                    if (seekIndex < SeekFrames.Length) source.Seek(SeekFrames[seekIndex]);
                    else { source.Step(-1); stage = 4; }
                    break;
                case 4:
                    if (source.Seeking || source.Current.frameId != 851) return;
                    Require(source.Player.frame == 851 && !source.Player.isPlaying, "frame step 실패");
                    results.Add("PASS: frame step");
                    source.Replay(); stage = 5;
                    deadline = now + 60;
                    break;
                case 5:
                    if (source.Seeking || source.Current.frameId != 0) return;
                    replayStarted = now; checkedFrames = 0;
                    source.FrameChanged += OnPlaybackFrame;
                    stage = 6;
                    break;
                case 6:
                    if (source.Current.frameId < 852) return;
                    double elapsed = now - replayStarted;
                    Require(Math.Abs(elapsed - 42.6) < 2.0, "실시간 1x 재생 실패: " + elapsed);
                    results.Add($"PASS: 전체 재생 {elapsed:F3}초 / 적용 event {checkedFrames} / 마지막 frame 852");
                    Finish(true);
                    break;
            }
        }
        catch (Exception e) { results.Add("FAIL: " + e); Finish(false); }
    }

    private static void OnPlaybackFrame(PoseFrame f)
    {
        // controller는 source의 첫 구독자이므로 이벤트 시점에 이미 적용되어 있다.
        try
        {
            CheckGeometry(f);
            Require(source.DisplayedFrame == f.frameId, "실시간 frame 불일치");
            Require(source.Player.frame == f.frameId, "VideoPlayer.frame / pose 불일치");
            ++checkedFrames;
        }
        catch (Exception e) { results.Add("FAIL: " + e); Finish(false); }
    }

    private static void Capture(long frame)
    {
        if (frame != 240 && frame != 574) return;
        string dir = Path.Combine(Application.dataPath, "../Validation/MediaPipeHumanArm");
        Directory.CreateDirectory(dir);
        foreach (var c in UnityEngine.Object.FindObjectsByType<Camera>(FindObjectsSortMode.None))
            if (c.name == "HumanArmDebugCamera")
            {
                c.Render();
                SaveTexture(c.targetTexture, Path.Combine(dir, "skeleton-" + frame + ".png"));
            }
        SaveTexture(source.VideoTexture, Path.Combine(dir, "video-" + frame + ".png"));
    }

    private static void SaveTexture(RenderTexture target, string path)
    {
        var old = RenderTexture.active;
        RenderTexture.active = target;
        var texture = new Texture2D(target.width, target.height, TextureFormat.RGB24, false);
        texture.ReadPixels(new Rect(0, 0, target.width, target.height), 0, 0);
        texture.Apply(); File.WriteAllBytes(path, texture.EncodeToPNG());
        UnityEngine.Object.Destroy(texture); RenderTexture.active = old;
    }

    private static void Finish(bool pass)
    {
        SessionState.SetBool(Active, false);
        if (source != null) source.FrameChanged -= OnPlaybackFrame;
        string dir = Path.Combine(Application.dataPath, "../Validation/MediaPipeHumanArm");
        Directory.CreateDirectory(dir);
        File.WriteAllText(Path.Combine(dir, "runtime-validation.txt"), string.Join("\n", results));
        Debug.Log(string.Join("\n", results));
        if (Application.isBatchMode) EditorApplication.Exit(pass ? 0 : 1);
        else EditorApplication.isPlaying = false;
    }
}
