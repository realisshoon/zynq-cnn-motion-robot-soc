using System;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioVideoCapturePanel : MonoBehaviour
    {
        ControlStudioVideoCapture capture;
        Font font;
        RectTransform root;
        Text devices, status, mirror, placeholder;
        InputField width, height, fps;
        Button start, stop, previous, next;
        RawImage image;
        int selected = -1;
        public ControlStudioVideoCapture Capture => capture;
        public string SelectedDevice => selected >= 0 && selected < capture.Session.Devices.Length ? capture.Session.Devices[selected] : "";
        public static ControlStudioVideoCapturePanel Attach(RectTransform parent, BoardUartSource source)
        {
            var go = new GameObject("Video capture controls", typeof(RectTransform));
            var panel = go.AddComponent<ControlStudioVideoCapturePanel>(); panel.Initialize(parent, source); return panel;
        }
        // This 600 x 540 surface can be placed in the hardware tabs by its owner.
        public void Initialize(RectTransform parent, BoardUartSource source)
        {
            if (root != null) throw new InvalidOperationException("Capture panel already initialized");
            capture = ControlStudioVideoCapture.GetOrAttach(source != null ? source.gameObject : gameObject);
            root = (RectTransform)transform; root.SetParent(parent, false);
            root.anchorMin = root.anchorMax = root.pivot = new Vector2(0, 1); root.anchoredPosition = Vector2.zero; root.sizeDelta = new Vector2(600, 540);
            font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            gameObject.AddComponent<Image>().color = new Color(.055f, .085f, .12f, .98f);
            Label("Video title", root, 16, 10, 568, 28, "VIDEO / HDMI USB CAPTURE", 22);
            Label("Video boundary", root, 16, 42, 568, 28, "Windows camera input. UART carries no video. Start is always explicit.", 13);
            Button("Refresh video devices", root, 16, 77, 174, 34, "Refresh device list", RefreshDevices);
            previous = Button("Previous video device", root, 202, 77, 80, 34, "Previous", () => Choose(-1));
            next = Button("Next video device", root, 292, 77, 80, 34, "Next", () => Choose(1));
            devices = Label("Selected video device", root, 16, 117, 568, 36, "No device selected / press Refresh, then Next", 15);
            Label("Capture size labels", root, 16, 153, 330, 20, "Requested width       height           FPS", 13);
            width = Input("Capture width", root, 16, 176, 108, "1280"); height = Input("Capture height", root, 136, 176, 108, "720"); fps = Input("Capture FPS", root, 256, 176, 82, "30");
            mirror = Button("Horizontal video mirror", root, 350, 176, 234, 32, "Horizontal mirror: OFF", () => capture.MirrorHorizontally = !capture.MirrorHorizontally).GetComponentInChildren<Text>();
            start = Button("Start video capture", root, 16, 222, 278, 36, "Start selected device", StartCapture);
            stop = Button("Stop video capture", root, 306, 222, 278, 36, "Stop / return to MOCK", () => capture.StopCapture());
            status = Label("Video capture state", root, 16, 266, 568, 62, "MOCK PREVIEW / no video input", 13);
            var box = Rect("Video preview viewport", root, 16, 336, 568, 158); box.gameObject.AddComponent<Image>().color = new Color(.025f, .045f, .065f);
            placeholder = Label("Video preview placeholder", box, 12, 0, 544, 158, "MOCK PREVIEW\nNo live video frame", 18); placeholder.alignment = TextAnchor.MiddleCenter;
            image = Rect("Live device video", box, 0, 0, 568, 158).gameObject.AddComponent<RawImage>(); image.raycastTarget = false; image.gameObject.SetActive(false);
            Label("Capture timing scope", root, 16, 503, 568, 30, "Requested size/FPS may differ from device output. Video and TK are not time synchronized.", 12);
        }
        RectTransform Rect(string name, Transform parent, float x, float y, float w, float h)
        {
            var r = new GameObject(name, typeof(RectTransform)).GetComponent<RectTransform>(); r.SetParent(parent, false);
            r.anchorMin = r.anchorMax = r.pivot = new Vector2(0, 1); r.anchoredPosition = new Vector2(x, -y); r.sizeDelta = new Vector2(w, h); return r;
        }
        Text Label(string name, Transform parent, float x, float y, float w, float h, string value, int size)
        {
            var text = Rect(name, parent, x, y, w, h).gameObject.AddComponent<Text>(); text.font = font; text.fontSize = size;
            text.color = new Color(.88f, .93f, .97f); text.text = value; text.raycastTarget = false; text.alignment = TextAnchor.MiddleLeft; return text;
        }
        Button Button(string name, Transform parent, float x, float y, float w, float h, string value, UnityEngine.Events.UnityAction action)
        {
            var r = Rect(name, parent, x, y, w, h); var background = r.gameObject.AddComponent<Image>(); background.color = new Color(.15f, .24f, .31f);
            var b = r.gameObject.AddComponent<Button>(); b.targetGraphic = background; b.onClick.AddListener(action);
            Label(name + " label", r, 4, 0, w - 8, h, value, 14).alignment = TextAnchor.MiddleCenter; return b;
        }
        InputField Input(string name, Transform parent, float x, float y, float w, string value)
        {
            var r = Rect(name, parent, x, y, w, 32); var background = r.gameObject.AddComponent<Image>(); background.color = new Color(.025f, .045f, .065f);
            var input = r.gameObject.AddComponent<InputField>(); input.targetGraphic = background; input.contentType = InputField.ContentType.IntegerNumber;
            input.textComponent = Label(name + " value", r, 8, 0, w - 16, 32, value, 15); input.text = value; return input;
        }
        public void RefreshDevices()
        {
            string old = SelectedDevice; capture.RefreshDevices(); selected = Array.IndexOf(capture.Session.Devices, old);
        }
        public void Choose(int direction)
        {
            int count = capture.Session.Devices.Length; if (count == 0) { selected = -1; return; }
            selected = selected < 0 ? (direction >= 0 ? 0 : count - 1) : (selected + (direction >= 0 ? 1 : -1) + count) % count;
        }
        public void StartCapture()
        {
            if (!int.TryParse(width.text, out int w) || !int.TryParse(height.text, out int h) || !int.TryParse(fps.text, out int f) ||
                w < 64 || w > 4096 || h < 64 || h > 4096 || f < 1 || f > 120)
            { capture.Session.Fail("Requested size must be 64–4096 pixels; FPS 1–120 / MOCK preview"); return; }
            capture.StartCapture(SelectedDevice, w, h, f);
        }
        void LateUpdate()
        {
            if (root == null || capture == null) return;
            var s = capture.Session; bool active = capture.IsCapturing;
            devices.text = s.Devices.Length == 0 ? "No device list / Refresh device list" : "Device " + (selected < 0 ? "--" : (selected + 1).ToString()) + "/" + s.Devices.Length + "  " + (SelectedDevice.Length == 0 ? "Choose with Previous / Next" : SelectedDevice);
            previous.interactable = next.interactable = s.Devices.Length > 0;
            start.interactable = !active && SelectedDevice.Length > 0; stop.interactable = active || s.Mode != VideoCaptureMode.Mock;
            width.interactable = height.interactable = fps.interactable = !active;
            mirror.text = "Horizontal mirror: " + (capture.MirrorHorizontally ? "ON" : "OFF");
            status.text = capture.Status + (s.HasFrame ? "\nActual " + s.Width + " x " + s.Height + " | frames " + s.Frames + " | rotation " + s.Rotation + " | vertical mirror " + s.VerticallyMirrored : "\nMOCK fallback / no live frame");
            placeholder.gameObject.SetActive(!capture.Render(image, 568, 158));
        }
    }
}
