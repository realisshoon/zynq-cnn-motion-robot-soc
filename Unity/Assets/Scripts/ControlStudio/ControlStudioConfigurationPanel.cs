using System;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioConfigurationPanel : MonoBehaviour
    {
        readonly BoardConfigurationController[] controllers = new BoardConfigurationController[2];
        readonly Action<string, double>[] observed = new Action<string, double>[2];
        BoardUartSource source;
        Font font, koreanFont;
        RectTransform root;
        Text heading, status, configSummary, filterSummary, filterParameter, rgbSummary, rgbPrompt;
        GameObject configPage, filterPage, rgbPage;
        InputField filterValue, rgbValue;
        Button filterApply, rgbBegin, rgbSubmit, rgbKeep, cnnStop, cnnStart;
        int sideIndex, pageIndex, parameterIndex;
        public void Initialize(RectTransform parent, BoardUartSource source)
        {
            if (root != null) throw new InvalidOperationException("Configuration panel already initialized");
            this.source = source ?? throw new ArgumentNullException(nameof(source));
            font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            koreanFont = Font.CreateDynamicFontFromOSFont("Malgun Gothic", 14);
            for (int i = 0; i < 2; i++) {
                var side = i == 0 ? BoardSide.Right : BoardSide.Left;
                controllers[i] = new BoardConfigurationController(source.Gateway(side), () => BoardUartTransport.Now);
                int index = i; observed[i] = (line, at) => controllers[index].ObserveLine(line, at);
                source.Session(side).LineReceived += observed[i];
            }
            root = Rect("Board configuration", parent, 0, 0, 600, 540);
            heading = Label("Configuration side", root, 12, 4, 370, 30, "RIGHT configuration", 18);
            MakeButton("Configuration RIGHT", root, 390, 4, 92, 30, "RIGHT", () => SelectSide(0));
            MakeButton("Configuration LEFT", root, 490, 4, 98, 30, "LEFT", () => SelectSide(1));
            MakeButton("CFG tab", root, 12, 40, 184, 28, "CFG / SD", () => SelectPage(0));
            MakeButton("Filter tab", root, 208, 40, 184, 28, "FILTER", () => SelectPage(1));
            MakeButton("RGBY13 tab", root, 404, 40, 184, 28, "RGBY13 MENU", () => SelectPage(2));
            configPage = Rect("CFG settings page", root, 0, 76, 600, 352).gameObject;
            filterPage = Rect("Filter settings page", root, 0, 76, 600, 352).gameObject;
            rgbPage = Rect("RGBY13 settings page", root, 0, 76, 600, 352).gameObject;
            BuildConfig((RectTransform)configPage.transform);
            BuildFilter((RectTransform)filterPage.transform);
            BuildRgb((RectTransform)rgbPage.transform);
            status = Label("Configuration transaction", root, 12, 434, 576, 68, "", 13);
            Label("Configuration persistence notice", root, 12, 505, 576, 34,
                "Settings stay in RAM until explicit CFG SAVE. No automatic resend or PWM/CNN toggle.", 12);
            SelectPage(0); UpdateView();
        }
        BoardConfigurationController Current => controllers[sideIndex];
        void SelectSide(int index)
        { sideIndex = index; filterValue.text = Current.DisplayFilter(BoardConfigurationController.FilterKeys[parameterIndex]); rgbValue.text = ""; UpdateView(); }
        void SelectPage(int index)
        { pageIndex = index; configPage.SetActive(index == 0); filterPage.SetActive(index == 1); rgbPage.SetActive(index == 2); }
        void BuildConfig(RectTransform page)
        {
            MakeButton("Settings read t", page, 12, 0, 180, 32, "Read t / capability", () => Show(Current.ReadCnnStatus(out var reason), reason));
            MakeButton("Settings SHOW", page, 204, 0, 120, 32, "CFG SHOW", () => Show(Current.ReadConfig(out var reason), reason));
            MakeButton("Settings SAVE", page, 336, 0, 120, 32, "CFG SAVE", () => Show(Current.SaveConfig(out var reason), reason));
            MakeButton("Settings LOAD", page, 468, 0, 120, 32, "CFG LOAD", () => Show(Current.LoadConfig(out var reason), reason));
            Label("CFG conditions", page, 12, 42, 576, 58,
                "SAVE / LOAD: verified PWM OFF, LIVE / IDLE, CNN fully idle, capture idle.\nRead t immediately before requesting. LOAD does not arm the robot.", 13);
            configSummary = Label("CFG reported settings", page, 12, 106, 576, 230, "No verified CFG report", 14);
        }
        void BuildFilter(RectTransform page)
        {
            MakeButton("Filter read t", page, 12, 0, 280, 32, "Read t / capability", () => Show(Current.ReadCnnStatus(out var reason), reason));
            MakeButton("Filter SHOW", page, 304, 0, 284, 32, "Read filter", () => Show(Current.ReadFilter(out var reason), reason));
            filterSummary = Label("Filter reported backend", page, 12, 44, 576, 94, "", 14);
            MakeButton("Filter parameter", page, 12, 147, 576, 32, "Select next parameter", () => {
                parameterIndex = (parameterIndex + 1) % BoardConfigurationController.FilterKeys.Length;
                filterValue.text = Current.DisplayFilter(BoardConfigurationController.FilterKeys[parameterIndex]); UpdateView();
            });
            filterParameter = Label("Filter parameter units", page, 12, 186, 576, 44, "", 14);
            filterValue = Input("Filter numeric value", page, 12, 235, 378, "");
            filterApply = MakeButton("Filter apply", page, 402, 235, 186, 34, "Apply parameter", () => Show(Current.ApplyFilter(BoardConfigurationController.FilterKeys[parameterIndex], filterValue.text, out var reason), reason));
            Label("Filter accuracy", page, 12, 284, 576, 64,
                "G_KALMAN: EMA / KXY / KZ / KACC / KVEL. One Euro requires reported support.\nExact decimal wire precision; values are never rounded. KVEL affects the next seed.", 13);
        }
        void BuildRgb(RectTransform page)
        {
            MakeButton("RGBY13 read t", page, 12, 0, 280, 32, "Read t / CNN idle", () => Show(Current.ReadCnnStatus(out var reason), reason));
            rgbBegin = MakeButton("RGBY13 begin", page, 304, 0, 284, 32, "Begin m / 13 prompts", () => Show(Current.BeginRgb(out var reason), reason));
            cnnStop = MakeButton("CNN stop inference", page, 12, 40, 280, 32, "CNN 정지 (a)", () => Show(Current.SetCnnRunning(false, out var reason), reason));
            cnnStart = MakeButton("CNN start inference", page, 304, 40, 284, 32, "CNN 시작 (a)", () => Show(Current.SetCnnRunning(true, out var reason), reason));
            Label("RGBY13 contract", page, 12, 80, 576, 42,
                "정지/시작 후 Read t로 상태 확인 → RGB 편집. 자동 재시작 없음.\nCNN 정지는 PWM OFF나 추종 중지(S)가 아닙니다.", 13);
            rgbSummary = Label("RGBY13 field progress", page, 12, 126, 576, 114, "", 14);
            rgbPrompt = Label("RGBY13 current prompt", page, 12, 246, 576, 55, "No verified RGBY13 prompt", 14);
            rgbValue = Input("RGBY13 numeric value", page, 12, 310, 258, "");
            rgbSubmit = MakeButton("RGBY13 next value", page, 282, 310, 148, 34, "Send value", () => Show(Current.SubmitRgb(rgbValue.text, false, out var reason), reason));
            rgbKeep = MakeButton("RGBY13 keep", page, 442, 310, 146, 34, "Keep / next", () => Show(Current.SubmitRgb("", true, out var reason), reason));
        }
        string localReason = "";
        void Show(bool sent, string reason) { localReason = sent ? "" : reason; }
        void Update() { if (root != null) UpdateView(); }
        void UpdateView()
        {
            foreach (var controller in controllers) controller?.Refresh();
            var c = Current;
            heading.text = (sideIndex == 0 ? "RIGHT / robot0" : "LEFT / robot1") + " configuration";
            status.text = (source.IsFixture(c.Side) ? "SIMULATION / no COM TX | " : "") + c.RequestState + " | CFG " + (c.ConfigSupported ? "supported" : "unverified") +
                " | CNN " + c.CnnDisplayState + "\n" + (localReason.Length == 0 ? c.Status : localReason);
            configSummary.text = c.RgbValues == null
                ? (c.RgbReadbackRequired ? "RGBY13 menu completed. Press CFG SHOW to verify all actual values." : "No complete verified CFG report")
                : RgbDisplay(c.RgbValues);
            filterSummary.text = "Backend: " + c.Backend + " | " + (c.FilterKnown ? "verified report" : "read required") +
                "\nEMA=" + c.DisplayFilter("EMA") + " s  KXY=" + c.DisplayFilter("KXY") + " mm  KZ=" + c.DisplayFilter("KZ") + " mm" +
                "\nKACC=" + c.DisplayFilter("KACC") + " mm/s²  KVEL=" + c.DisplayFilter("KVEL") + " mm/s";
            string key = BoardConfigurationController.FilterKeys[parameterIndex];
            var units = new[] { "seconds 0.001..1", "mm 0.1..1000", "mm 0.1..1000", "mm/s² 0..5000", "mm/s 0..5000", "Hz 0.01..10", "/mm 0..0.1", "Hz 0.01..10" };
            filterParameter.text = key + " | " + units[parameterIndex] + " | current " + (c.DisplayFilter(key).Length == 0 ? "unknown" : c.DisplayFilter(key));
            filterApply.interactable = !c.Busy && c.CanApplyFilter(key);
            rgbBegin.interactable = !c.Busy && !c.RgbActive && c.ConfigSupported && c.CnnVerifiedIdle;
            cnnStop.interactable = c.CanSetCnnRunning(false, out _);
            cnnStart.interactable = c.CanSetCnnRunning(true, out _);
            rgbSubmit.interactable = rgbKeep.interactable = c.RgbActive && !c.Busy;
            rgbSummary.text = c.RgbActive ? "Field " + (c.RgbField + 1) + " / 13: " + BoardConfigurationController.RgbFieldNames[c.RgbField] +
                "\nCurrent " + c.RgbCurrent + " / range " + BoardConfigurationController.RgbLow(c.RgbField) + ".." + BoardConfigurationController.RgbHigh(c.RgbField) +
                "\nAfter the final result, use CFG SHOW to verify values. Mask: R=1 B=2 G=4 Y=8." :
                c.RgbReadbackRequired ? "RGBY13 menu completed; actual values are unverified.\nOpen CFG / SD and press CFG SHOW to read all 13 values." :
                "RGBY13 edits the existing detection menu.\nFirst 11 fields: 0..255; taps: 1..262143; mask: 0..15.\nStarting does not change CNN or PWM automatically.";
            rgbPrompt.text = c.RgbActive ? c.RgbPrompt.Trim() : "No active verified RGBY13 prompt";
        }
        static string RgbDisplay(uint[] v)
        {
            return "Margins R/G/B: " + v[0] + " / " + v[1] + " / " + v[2] + "\nYellow difference / gap: " + v[3] + " / " + v[4] +
                "\nBrightness R/G/B: " + v[5] + " / " + v[6] + " / " + v[7] + "\nYellow min R / min G / max B: " + v[8] + " / " + v[9] + " / " + v[10] +
                "\nMinimum taps: " + v[11] + " | mask: " + v[12] + " (R=1 B=2 G=4 Y=8)";
        }
        void OnDestroy()
        {
            if (source == null) return;
            for (int i = 0; i < 2; i++) if (observed[i] != null) source.Session(i == 0 ? BoardSide.Right : BoardSide.Left).LineReceived -= observed[i];
        }
        RectTransform Rect(string name, Transform parent, float x, float y, float width, float height)
        {
            var go = new GameObject(name, typeof(RectTransform)); var rect = (RectTransform)go.transform; rect.SetParent(parent, false);
            rect.anchorMin = rect.anchorMax = rect.pivot = new Vector2(0, 1); rect.anchoredPosition = new Vector2(x, -y); rect.sizeDelta = new Vector2(width, height); return rect;
        }
        Text Label(string name, Transform parent, float x, float y, float width, float height, string text, int size)
        {
            var rect = Rect(name, parent, x, y, width, height); var label = rect.gameObject.AddComponent<Text>();
            label.font = name.StartsWith("CNN ", StringComparison.Ordinal) || name == "RGBY13 contract" ? koreanFont : font;
            label.fontSize = size; label.color = new Color(.88f, .93f, .97f); label.raycastTarget = false; label.text = text; return label;
        }
        Button MakeButton(string name, Transform parent, float x, float y, float width, float height, string caption, UnityEngine.Events.UnityAction action)
        {
            var rect = Rect(name, parent, x, y, width, height); var image = rect.gameObject.AddComponent<Image>(); image.color = new Color(.15f, .24f, .31f);
            var button = rect.gameObject.AddComponent<Button>(); button.targetGraphic = image; button.onClick.AddListener(action);
            if (name.StartsWith("CNN ", StringComparison.Ordinal)) {
                button.transition = Selectable.Transition.ColorTint;
                var colors = button.colors; colors.normalColor = colors.highlightedColor = colors.pressedColor = colors.selectedColor = Color.white;
                colors.disabledColor = new Color(.45f, .45f, .45f); colors.fadeDuration = 0; button.colors = colors;
            }
            Label(name + " label", rect, 4, 0, width - 8, height, caption, 13).alignment = TextAnchor.MiddleCenter; return button;
        }
        InputField Input(string name, Transform parent, float x, float y, float width, string value)
        {
            var rect = Rect(name, parent, x, y, width, 34); var image = rect.gameObject.AddComponent<Image>(); image.color = new Color(.025f, .045f, .065f);
            var input = rect.gameObject.AddComponent<InputField>(); input.targetGraphic = image;
            input.textComponent = Label(name + " value", rect, 8, 0, width - 16, 34, value, 14); input.textComponent.alignment = TextAnchor.MiddleLeft; input.text = value; return input;
        }
    }
}
