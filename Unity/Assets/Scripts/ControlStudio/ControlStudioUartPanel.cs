using System.IO;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    // Runtime panel; real serial output is opt-in and defaults to disconnected/TX OFF.
    public sealed class ControlStudioUartPanel : MonoBehaviour
    {
        ControlStudioUartOutput output;
        UartPose3DSource input;
        GameObject panel;
        InputField port, baud, mockPath;
        Text rxState, rxCounters, rxDetail, txState, txLabel, mockLabel;
        bool visible, mock = true;
        int portIndex = -1;
        string[] ports = System.Array.Empty<string>();
        Font font;
        public bool Visible => visible;

        public static void Attach(ControlStudioRuntimeUI studio, Transform canvas)
        {
            var output = studio.gameObject.AddComponent<ControlStudioUartOutput>();
            output.router = studio.router;
            var input = studio.gameObject.AddComponent<UartPose3DSource>();
            input.router = studio.router;
            var ui = studio.gameObject.AddComponent<ControlStudioUartPanel>();
            ui.output = output;
            ui.input = input;
            ui.Build(canvas);
        }

        RectTransform Rect(string name, Transform parent, float x, float y, float w, float h)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var r = (RectTransform)go.transform;
            r.SetParent(parent, false);
            r.anchorMin = r.anchorMax = r.pivot = new Vector2(0, 1);
            r.anchoredPosition = new Vector2(x, -y);
            r.sizeDelta = new Vector2(w, h);
            return r;
        }

        Text Label(string name, Transform parent, float x, float y, float w, float h, string value, int size = 17)
        {
            var r = Rect(name, parent, x, y, w, h);
            var t = r.gameObject.AddComponent<Text>();
            t.font = font; t.fontSize = size; t.color = new Color(.88f, .93f, .97f);
            t.alignment = TextAnchor.MiddleLeft; t.raycastTarget = false; t.text = value;
            return t;
        }

        Button Button(string name, Transform parent, float x, float y, float w, float h,
            string caption, UnityEngine.Events.UnityAction action)
        {
            var r = Rect(name, parent, x, y, w, h);
            var img = r.gameObject.AddComponent<Image>(); img.color = new Color(.15f, .24f, .31f);
            var b = r.gameObject.AddComponent<Button>(); b.targetGraphic = img; b.onClick.AddListener(action);
            Label(name + " label", r, 8, 0, w - 16, h, caption, 16).alignment = TextAnchor.MiddleCenter;
            return b;
        }

        InputField Input(string name, Transform parent, float x, float y, float w, string value)
        {
            var r = Rect(name, parent, x, y, w, 42);
            var bg = r.gameObject.AddComponent<Image>(); bg.color = new Color(.025f, .045f, .065f);
            var input = r.gameObject.AddComponent<InputField>(); input.targetGraphic = bg;
            input.textComponent = Label(name + " value", r, 12, 0, w - 24, 42, value, 18);
            input.text = value; return input;
        }

        void Build(Transform canvas)
        {
            font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            var root = Rect("UART / Hardware panel", canvas, 960, 96, 640, 714);
            var bg = root.gameObject.AddComponent<Image>(); bg.color = new Color(.055f, .085f, .12f, .98f);
            panel = root.gameObject;
            Label("UART RX title", root, 20, 8, 600, 35, "UART / COMMUNICATION", 22);
            Label("UART RX contract", root, 20, 45, 600, 42,
                "POSE3D_V1 / PROVISIONAL  |  processed relative XYZ + BodyFrame\nRX selects virtual robot input; Connect never enables TX.", 14);
            Button("RX XYZ A", root, 20, 92, 285, 32, "M4: auxiliary gripper", () => input.SelectSource(CsvInputMode.XyzStoredBodyAuxGripper));
            Button("RX XYZ B", root, 315, 92, 305, 32, "M4: current Applied HOLD", () => input.SelectSource(CsvInputMode.XyzStoredBodyHoldGripper));
            Label("COM port caption",root,20,125,185,12,"COM Port",11);
            Label("Baudrate caption",root,215,125,135,12,"Baudrate",11);
            port = Input("COM port input", root, 20, 137, 185, "");
            baud = Input("UART baud input", root, 215, 137, 135, "115200");
            Button("Refresh COM ports", root, 360, 137, 260, 42, "Refresh / Next COM", RefreshPort);
            Button("RX Mock Connect", root, 20, 191, 190, 38, "Mock RX Connect", () => input.ConnectMock());
            Button("RX Real Connect", root, 220, 191, 190, 38, "Real COM RX", ConnectRx);
            Button("RX Disconnect", root, 420, 191, 200, 38, "RX Disconnect", () => input.Disconnect());
            mockPath = Input("Mock UART stream path", root, 20, 242, 600,
                Path.Combine(Application.streamingAssetsPath, "ControlStudioSamples", "cnn_pose3d_v1.uart"));
            Button("Load Mock RX stream", root, 20, 295, 600, 38, "Load / Replay Mock Byte Stream", () => input.LoadMockStream(mockPath.text));
            rxState = Label("RX status", root, 20, 344, 600, 47, "DISCONNECTED", 16);
            rxCounters = Label("RX counters", root, 20, 393, 600, 57, "Packets RX: 0", 15);
            rxDetail = Label("RX detail", root, 20, 450, 600, 175, "BODY FRAME: UNAVAILABLE", 14);
            rxDetail.alignment=TextAnchor.UpperLeft;
            Label("UART TX debug title", root, 20, 508, 600, 33, "UART OUTPUT / DEBUG  (separate, TX OFF)", 19);
            mockLabel = Button("TX mock mode", root, 20, 550, 125, 34, "Mock TX", ToggleMock).GetComponentInChildren<Text>();
            Button("TX Connect", root, 155, 550, 130, 34, "TX Connect", Connect);
            Button("TX Disconnect", root, 295, 550, 145, 34, "TX Disconnect", () => output.Disconnect());
            txLabel = Button("TX enable", root, 450, 550, 170, 34, "TX: OFF", () => output.SetTxEnabled(!output.TxEnabled)).GetComponentInChildren<Text>();
            Button("TX Send Applied", root, 20, 593, 285, 34, "Send Applied (debug)", () => output.SendCurrent());
            Button("TX changed only", root, 315, 593, 305, 34, "Changed-only", () => output.ChangedOnly = !output.ChangedOnly);
            txState = Label("TX status", root, 20, 638, 600, 62, "TX DISCONNECTED / real TX = 0", 14);
            // RX mode exposes only input controls. Preserve the old output widgets/backend, hidden as one group.
            var outputControls=Rect("Legacy TX debug controls (hidden)",root,0,0,640,714);
            foreach(var name in new[]{"UART TX debug title","TX mock mode","TX Connect","TX Disconnect","TX enable","TX Send Applied","TX changed only","TX status"})
            {var item=root.Find(name);if(item!=null)item.SetParent(outputControls,false);}
            outputControls.gameObject.SetActive(false);
            RefreshPort();
            panel.SetActive(false);
        }

        public void ShowPanel(bool show)
        {
            visible = show;
            if (show) GetComponent<Tool1RuntimeUI>()?.ShowToolPanel(false);
        }

        void RefreshPort()
        {
            ports = ControlStudioUartOutput.AvailablePorts();
            if (ports.Length == 0) { port.SetTextWithoutNotify(""); return; }
            portIndex = (portIndex + 1) % ports.Length;
            port.SetTextWithoutNotify(ports[portIndex]);
        }

        void CycleBaud()
        {
            string next = baud.text == "9600" ? "57600" : baud.text == "57600" ? "115200" : "9600";
            baud.SetTextWithoutNotify(next);
        }

        void ToggleMock() { if (!output.Connected) mock = !mock; }
        void ConnectRx()
        {
            if (!int.TryParse(baud.text, out var rate)) rate = 0;
            input.ConnectReal(port.text, rate);
        }

        void Connect()
        {
            if (!int.TryParse(baud.text, out var rate)) rate = 0;
            output.Connect(port.text, rate, mock);
        }

        void Update()
        {
            if (panel == null || output == null) return;
            var presentation = GetComponent<ControlStudioPresentationUI>();
            panel.SetActive(visible && (presentation == null || presentation.UartExpanded));
            mockLabel.text = mock ? "Mock TX" : "Real TX";
            txLabel.text = "TX: " + (output.TxEnabled ? "ENABLED" : "OFF");
            rxState.text = "RX STATUS: " + input.Status + "   |   SOURCE: " + (input.Active ? "UART RIGHT XYZ" : "NOT SELECTED") + " / " + (input.Mode == CsvInputMode.XyzStoredBodyAuxGripper ? "M4 AUX" : "M4 HOLD");
            rxCounters.text = "Last frame: " + (input.LastPoseRow == null ? "--" : input.LastFrameId.ToString()) +
                "   Time: " + input.LastTimestamp.ToString("F3") + "s   Age: " + (double.IsNaN(input.PacketAgeMs) ? "--" : input.PacketAgeMs.ToString("F0")) + " ms\nPackets RX: " + input.PacketsRx + "   Parse errors: " + input.ParseErrors + "   Dropped: " + input.Dropped + "   Duplicate: " + input.Duplicates;
            rxDetail.text = "BODY FRAME: " + input.BodyFrameStatus + "   |   Solver: " + (input.LastSolve == null ? "--" : input.LastSolve.State) +
                "\n" + (string.IsNullOrEmpty(input.Error) ? "Point quality: UNKNOWN" : input.Error);
            var xyz=input.LastPoseRow?.Pose?.Xyz;
            if(xyz!=null&&xyz.Length==18){
                rxDetail.text+="\nRX XYZ (processed relative):";
                for(int i=1;i<6;i++)rxDetail.text+="\n"+new[]{"Shoulder L","Shoulder R","Elbow","Wrist","Thumb","Index"}[i]+"  "+new Vector3(xyz[3*i],xyz[3*i+1],xyz[3*i+2]).ToString("F3");
            }
            txState.text = output.Status + "   |   Real TX: " + output.HardwareTxCount + "   Mock TX: " + output.MockTxCount +
                "\nLast TX: " + output.LastSent;
        }
    }
}
