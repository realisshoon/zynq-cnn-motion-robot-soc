using System;
using System.Collections;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioVideoCapture : MonoBehaviour
    {
        sealed class WebcamBackend : IVideoCaptureBackend
        {
            public string[] Enumerate() => Array.ConvertAll(WebCamTexture.devices, d => d.name);
            public IVideoCaptureStream Create(string device, int width, int height, int fps) => new WebcamStream(device, width, height, fps);
        }
        sealed class WebcamStream : IVideoCaptureStream
        {
            readonly WebCamTexture texture;
            public WebcamStream(string device, int width, int height, int fps) { texture = new WebCamTexture(device, width, height, fps); }
            public object Texture => texture;
            public bool IsPlaying => texture.isPlaying;
            public bool DidUpdate => texture.didUpdateThisFrame;
            public int Width => texture.width;
            public int Height => texture.height;
            public int Rotation => texture.videoRotationAngle;
            public bool VerticallyMirrored => texture.videoVerticallyMirrored;
            public void Play() { texture.Play(); }
            public void Dispose() { try { texture.Stop(); } finally { UnityEngine.Object.Destroy(texture); } }
        }
        VideoCaptureSession session;
        Coroutine permission;
        public bool MirrorHorizontally { get; set; }
        public VideoCaptureSession Session => session ?? (session = new VideoCaptureSession(new WebcamBackend()));
        public VideoCaptureMode Mode => permission != null ? VideoCaptureMode.Starting : Session.Mode;
        public bool IsCapturing => permission != null || Session.IsCapturing;
        public bool HasFrame => Session.HasFrame;
        public string Status => permission != null ? "Waiting for camera permission / no video frame" : Session.Message;
        public static ControlStudioVideoCapture GetOrAttach(GameObject owner) => owner.GetComponent<ControlStudioVideoCapture>() ?? owner.AddComponent<ControlStudioVideoCapture>();
        // No device enumeration or camera opening occurs in Awake/Start.
        public void RefreshDevices() { Session.RefreshDevices(); }
        public bool StartCapture(string device, int width, int height, int fps)
        {
            StopCapture();
            if (Array.IndexOf(Session.Devices, device) < 0) { Session.Fail("Refresh and select a listed device / MOCK preview"); return false; }
            permission = StartCoroutine(AuthorizeAndStart(device, width, height, fps)); return true;
        }
        IEnumerator AuthorizeAndStart(string device, int width, int height, int fps)
        {
            // Authorization is requested only after an explicit Start click.
            yield return Application.RequestUserAuthorization(UserAuthorization.WebCam);
            permission = null;
            if (!isActiveAndEnabled) yield break;
            if (!Application.HasUserAuthorization(UserAuthorization.WebCam))
            { Session.Fail("Camera access denied. Check Windows camera privacy settings / MOCK preview"); yield break; }
            Session.Start(device, width, height, fps, Time.realtimeSinceStartupAsDouble);
        }
        public void StopCapture()
        {
            if (permission != null) { StopCoroutine(permission); permission = null; }
            session?.Stop();
        }
        void Update() { session?.Update(Time.realtimeSinceStartupAsDouble); }
        public bool Render(RawImage image, float width, float height)
        {
            if (image == null) return false;
            bool live = HasFrame;
            image.gameObject.SetActive(live); image.texture = live ? Session.Texture as Texture : null;
            if (!live) return false;
            var fit = VideoCaptureFit.Calculate(Session.Width, Session.Height, Session.Rotation, width, height, MirrorHorizontally);
            var rect = image.rectTransform; rect.anchorMin = rect.anchorMax = rect.pivot = new Vector2(.5f, .5f);
            rect.anchoredPosition = Vector2.zero; rect.sizeDelta = new Vector2(fit.Width, fit.Height);
            rect.localRotation = Quaternion.Euler(0, 0, fit.Angle);
            image.uvRect = new Rect(MirrorHorizontally ? 1 : 0, Session.VerticallyMirrored ? 1 : 0, MirrorHorizontally ? -1 : 1, Session.VerticallyMirrored ? -1 : 1);
            return true;
        }
        void OnDisable() { StopCapture(); }
        void OnDestroy() { StopCapture(); }
    }
}
