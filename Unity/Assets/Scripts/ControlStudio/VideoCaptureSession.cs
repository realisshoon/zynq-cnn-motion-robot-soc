using System;

namespace HumanMotion.ControlStudio
{
    public enum VideoCaptureMode { Mock, Starting, Live, Unavailable, Error, Stale }

    // Device access is isolated so state/timeout checks never require a camera.
    public interface IVideoCaptureStream : IDisposable
    {
        object Texture { get; }
        bool IsPlaying { get; }
        bool DidUpdate { get; }
        int Width { get; }
        int Height { get; }
        int Rotation { get; }
        bool VerticallyMirrored { get; }
        void Play();
    }
    public interface IVideoCaptureBackend
    {
        string[] Enumerate();
        IVideoCaptureStream Create(string device, int width, int height, int fps);
    }

    public sealed class VideoCaptureSession : IDisposable
    {
        readonly IVideoCaptureBackend backend;
        IVideoCaptureStream stream;
        double startTime, lastFrame, lastDeviceCheck;
        public const double FrameGraceSeconds = 2;
        public string[] Devices { get; private set; } = Array.Empty<string>();
        public VideoCaptureMode Mode { get; private set; } = VideoCaptureMode.Mock;
        public string Message { get; private set; } = "MOCK PREVIEW / no video input";
        public string Device { get; private set; } = "";
        public int Width { get; private set; }
        public int Height { get; private set; }
        public int Rotation { get; private set; }
        public bool VerticallyMirrored { get; private set; }
        public long Frames { get; private set; }
        public bool IsCapturing => Mode == VideoCaptureMode.Starting || Mode == VideoCaptureMode.Live || Mode == VideoCaptureMode.Stale;
        public bool DeviceAvailable => Device.Length > 0 && Array.IndexOf(Devices, Device) >= 0;
        public double LastFrameTime => lastFrame;
        public bool HasFrame => Mode == VideoCaptureMode.Live && stream != null;
        public object Texture => HasFrame ? stream.Texture : null;
        public VideoCaptureSession(IVideoCaptureBackend backend) { this.backend = backend ?? throw new ArgumentNullException(nameof(backend)); }
        public void RefreshDevices()
        {
            try
            {
                Devices = backend.Enumerate() ?? Array.Empty<string>();
                if (!IsCapturing)
                {
                    Mode = Devices.Length == 0 ? VideoCaptureMode.Unavailable : VideoCaptureMode.Mock;
                    Message = Devices.Length == 0 ? "No Windows camera/capture device / MOCK preview" : "Device list refreshed / capture stopped / MOCK preview";
                }
            }
            catch (Exception e) { Devices = Array.Empty<string>(); if (IsCapturing) Stale("Device enumeration failed: " + e.Message); else Fail("Device enumeration failed: " + e.Message); }
        }
        public bool Start(string device, int width, int height, int fps, double now)
        {
            Stop();
            if (string.IsNullOrWhiteSpace(device) || Array.IndexOf(Devices, device) < 0)
            { Mode = VideoCaptureMode.Unavailable; Message = "Refresh and select a listed capture device / MOCK preview"; return false; }
            if (width < 64 || width > 4096 || height < 64 || height > 4096 || fps < 1 || fps > 120)
            { Fail("Requested size must be 64–4096 pixels; FPS 1–120 / MOCK preview"); return false; }
            try
            {
                Device = device; stream = backend.Create(device, width, height, fps);
                if (stream == null) throw new InvalidOperationException("Capture backend returned no stream");
                Mode = VideoCaptureMode.Starting; startTime = lastFrame = lastDeviceCheck = now;
                Message = "Waiting for first frame: " + device;
                stream.Play(); return true;
            }
            catch (Exception e) { Fail("Capture start failed: " + e.Message); return false; }
        }
        public void Update(double now)
        {
            if (!IsCapturing || stream == null) return;
            try
            {
                // Enumeration observes availability; it never opens or restarts a device.
                if (now - lastDeviceCheck >= FrameGraceSeconds) { lastDeviceCheck = now; RefreshDevices(); }
                if (!DeviceAvailable) { Stale("Selected camera is not available / preview hidden; stream retained"); return; }
                if (stream.IsPlaying && stream.DidUpdate && stream.Width > 16 && stream.Height > 16)
                {
                    Width = stream.Width; Height = stream.Height;
                    Rotation = ((stream.Rotation % 360) + 360) % 360;
                    VerticallyMirrored = stream.VerticallyMirrored; Frames++; lastFrame = now;
                    Mode = VideoCaptureMode.Live; Message = "LIVE CAPTURE / " + Device;
                }
                else if (Mode == VideoCaptureMode.Starting && now - startTime > 8)
                    Stale("No first frame within 8 seconds / preview hidden; waiting on the same stream");
                else if (Mode == VideoCaptureMode.Live && now - lastFrame > FrameGraceSeconds)
                    Stale("Video stopped or stalled for 2 seconds / preview hidden; stream retained");
            }
            catch (Exception e) { Stale("Capture read failed: " + e.Message + " / preview hidden; stream retained"); }
        }
        void Stale(string message) { Mode = VideoCaptureMode.Stale; Message = message; }
        void Release()
        {
            var previous = stream; stream = null;
            try { previous?.Dispose(); } catch { /* Always relinquish ownership even if a device disappears. */ }
        }
        public void Fail(string message)
        {
            Release(); Mode = VideoCaptureMode.Error; Message = message;
        }
        public void Stop()
        {
            Release(); Mode = VideoCaptureMode.Mock; Message = "Capture stopped / MOCK PREVIEW / no video input";
            Width = Height = Rotation = 0; VerticallyMirrored = false; Frames = 0;
        }
        public void Dispose() { Stop(); }
    }

    public readonly struct VideoCaptureFit
    {
        public readonly float Width, Height, Angle;
        public VideoCaptureFit(float width, float height, float angle) { Width = width; Height = height; Angle = angle; }
        public static VideoCaptureFit Calculate(int width, int height, int rotation, float boundsWidth, float boundsHeight, bool mirrorHorizontally = false)
        {
            if (width <= 0 || height <= 0 || boundsWidth <= 0 || boundsHeight <= 0) return new VideoCaptureFit(0, 0, 0);
            double radians = rotation * Math.PI / 180, c = Math.Abs(Math.Cos(radians)), s = Math.Abs(Math.Sin(radians));
            double scale = Math.Min(boundsWidth / (width * c + height * s), boundsHeight / (width * s + height * c));
            // Mx R(-angle) = R(angle) Mx: mirror stays horizontal in the displayed frame.
            return new VideoCaptureFit((float)(width * scale), (float)(height * scale), mirrorHorizontally ? rotation : -rotation);
        }
    }
}
