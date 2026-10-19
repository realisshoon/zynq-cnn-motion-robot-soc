using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

namespace HumanMotion.ControlStudio
{
    public sealed class ControlStudioRecordingPanel : MonoBehaviour
    {
        BoardUartSource source;
        readonly BoardRecordingController[] controllers = new BoardRecordingController[2];
        readonly BoardRecordsUxFlow[] flows = new BoardRecordsUxFlow[2];
        readonly Action<string, double>[] observers = new Action<string, double>[2];
        readonly List<Button> actions = new List<Button>();
        BoardSide side = BoardSide.Right;
        Font font;
        Text stateText, budgetText, feedback;
        InputField nameInput;
        Dropdown catalog;
        Toggle acknowledge;
        string catalogKey = "";
        string consentContext = "";
        readonly uint[] selectedIds = new uint[2];
        bool refreshingCatalog;
        public BoardSide SelectedBoard => side;
        public int SharedTarget=>GetComponent<ControlStudioBoardControlPanel>()?.SelectedTarget??0;
        public BoardRecordingController CurrentController => Controller;
        public BoardRecordsUxFlow CurrentFlow => Flow;
        public uint SelectedRecordId => SelectedId();
        public bool SelectRecord(uint id)
        {
            if(!Controller.CatalogValid)return false;
            for(int i=0;i<Controller.Entries.Count;i++)if(Controller.Entries[i].Id==id){catalog.value=i;catalog.RefreshShownValue();selectedIds[side==BoardSide.Right?0:1]=id;return true;}
            return false;
        }
        Button record, prepareCancel, finish, menu, stop, list, save, play, delete, confirm, cancel, returnFollow;
        BoardRecordingController Controller => controllers[side == BoardSide.Right ? 0 : 1];
        BoardRecordsUxFlow Flow => flows[side == BoardSide.Right ? 0 : 1];
        public void Initialize(RectTransform parent, BoardUartSource boardSource)
        {
            source = boardSource ?? throw new ArgumentNullException(nameof(boardSource));
            font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            for (int i = 0; i < 2; i++)
            {
                var s = i == 0 ? BoardSide.Right : BoardSide.Left;
                controllers[i] = new BoardRecordingController(source.Gateway(s));
                observers[i] = controllers[i].ObserveLine;
                source.Session(s).LineReceived += observers[i];
                flows[i] = new BoardRecordsUxFlow(controllers[i], () => source.Active, () => BoardUartTransport.Now);
            }
            ButtonAt(parent, "Recording RIGHT", 0, 0, 292, "RIGHT / robot0", () => SelectSide(BoardSide.Right));
            ButtonAt(parent, "Recording LEFT", 302, 0, 292, "LEFT / robot1", () => SelectSide(BoardSide.Left));
            stateText = Label(parent, "Recording state", 0, 40, 594, 52, "상태 미확인", 14);
            budgetText = Label(parent, "Recording budget", 0, 93, 594, 29, "녹화 제한은 보드의 녹화 시작 응답에서 확인합니다.", 13);
            record = ButtonAt(parent, "Start recording", 0, 126, 192, "녹화 준비", () => Controller.StartRecord(acknowledge.isOn, out _));
            prepareCancel = ButtonAt(parent, "Cancel preparation", 202, 126, 192, "준비 취소", () => Controller.CancelPreparation(out _));
            finish = ButtonAt(parent, "Finish recording", 404, 126, 190, "녹화 종료", () => Controller.FinishRecord(out _));
            menu = ButtonAt(parent, "Enter recording menu", 0, 164, 192, "P: SD 메뉴 열기", () => Controller.OpenMenu(out _));
            stop = ButtonAt(parent, "Stop playback", 202, 164, 192, "P: 재생 중지", () => { Flow.StopWaiting(); Controller.StopPlayback(out _); });
            list = ButtonAt(parent, "Refresh SD list", 404, 164, 190, "SD 목록 조회", () => Controller.RefreshList(out _));
            nameInput = Input(parent, "SD record name", 0, 202, 394); nameInput.characterLimit = 24;
            save = ButtonAt(parent, "Save SD recording", 404, 202, 190, "SD에 저장", () => Flow.Save(nameInput.text, out _));
            catalog = CreateDropdown(parent, 0, 244, 594);
            play = ButtonAt(parent, "Play SD record", 0, 282, 192, "선택 및 반복 재생", () => Flow.Play(SelectedId(), acknowledge.isOn, out _));
            delete = ButtonAt(parent, "Request SD deletion", 202, 282, 192, "삭제 준비", () => Controller.RequestDelete(SelectedId(), out _));
            confirm = ButtonAt(parent, "Confirm SD deletion", 404, 282, 190, "삭제 확인", () => Controller.ConfirmDelete(SelectedId(), acknowledge.isOn, out _));
            acknowledge = CreateToggle(parent, 0, 325, "미저장 RAM의 교체·폐기 또는 선택한 SD 기록 삭제를 확인합니다.");
            cancel = ButtonAt(parent, "Cancel recording workflow", 0, 365, 280, "이름·메뉴·삭제 취소", () => Controller.Cancel(acknowledge.isOn, out _));
            returnFollow = ButtonAt(parent, "Return to follow", 291, 365, 303, "추종 복귀", () => Flow.ReturnToFollow(out _));
            feedback = Label(parent, "Recording feedback", 0, 405, 594, 90, "", 14);
            Label(parent, "Recording wire note", 0, 501, 594, 36, "PLAY: 추종 OFF 가능 · 복귀는 클릭 후 V/CANCEL/LIVE 확인/A/T. 자동 E/H·재시도 없음.\n취소는 RAM 저장이 아닙니다. S는 재생 중지가 아니며 X는 토크 해제. PC 제한은 물리 안정화 증거가 아닙니다.", 12);
            gameObject.AddComponent<RecordsQuickHud>().Initialize(this, source);
        }
        public bool CanChangeTarget(out string reason)
        {
            for(int i=0;i<2;i++){
                var c=controllers[i];if(c==null)continue;var s=c.Gateway.Session.State;
                if(flows[i].Busy||c.Gateway.Busy||s!=null&&(s.Mode=="RECORDING"||s.Mode=="ALIGNING"&&(s.Ui!="IDLE"||s.Playing==0)||s.Ui=="START_RECORD"||s.Ui=="STOP_RECORD"||s.Ui=="STOP_MENU"||s.Ui=="SAVE"||s.Ui=="NAME"&&s.Unsaved)){
                    reason="녹화/저장/재생 승인·응답 대기 중 대상 고정 — 응답 완료 후 선택하세요. 승인된 재생은 보드별로 유지됩니다.";return false;}
            }reason="";return true;
        }
        void SelectSide(BoardSide selected){var control=GetComponent<ControlStudioBoardControlPanel>();if(control!=null)control.TrySelectTarget(selected==BoardSide.Right?0:1);else ChangeSide(selected);}
        void ChangeSide(BoardSide selected){if(side==selected)return;if(Controller.CatalogValid)selectedIds[side==BoardSide.Right?0:1]=SelectedId();side=selected;acknowledge.isOn=false;catalogKey="";nameInput.text="";}
        public void SynchronizeTarget(){int target=SharedTarget;if(target<2)ChangeSide(target==0?BoardSide.Right:BoardSide.Left);}
        uint SelectedId() { if(!Controller.CatalogValid)return 0; uint id=selectedIds[side==BoardSide.Right?0:1]; foreach(var entry in Controller.Entries)if(entry.Id==id)return id; return id==0&&Controller.Entries.Count>0?Controller.Entries[0].Id:0; }
        void Update()
        {
            if (source == null || stateText == null) return;
            SynchronizeTarget();
            foreach (var controller in controllers) controller.Sync();
            foreach (var flow in flows) flow.Advance();
            var c = Controller; var gateway = c.Gateway; var s = gateway.Session; var st = s.State;
            string context = side + "/" + s.Generation + "/" + (st == null ? "UNKNOWN" : st.Ui + "/" + st.Mode + "/" + st.Unsaved + "/" + st.Selected + "/" + st.Delete);
            if (context != consentContext) { consentContext = context; acknowledge.isOn = false; }
            bool baseReady = SharedTarget<2 && source.Active && s.Trusted && !gateway.Busy && gateway.BoundaryConfirmed && s.AtLineBoundary && !gateway.IsSimulation;
            bool ready = baseReady && !Flow.Busy;
            foreach (var b in actions) b.interactable = ready;
            record.interactable = ready && st.Ui == "IDLE" && st.Mode == "LIVE" && st.Pwm && (!st.Unsaved || acknowledge.isOn);
            prepareCancel.interactable = ready && st.Ui == "START_RECORD";
            finish.interactable = ready && st.Ui == "IDLE" && st.Mode == "RECORDING";
            menu.interactable = ready && c.ManualOpen == false && (st.Ui == "IDLE" && st.Mode == "LIVE" || st.Ui == "MENU" && (st.Mode == "LIVE" || st.Mode == "HOLDING"));
            stop.interactable = baseReady && st.Ui == "IDLE" && (st.Mode == "ALIGNING" || st.Mode == "PLAYING" || st.Mode == "HOLDING");
            save.interactable = ready && st.Ui == "NAME" && st.Mode == "HOLDING" && st.Unsaved;
            bool listed = c.CatalogValid && SelectedId() != 0;
            play.interactable = ready && listed && Flow.CanPlay(SelectedId(), acknowledge.isOn, out _);
            bool canReturn = Flow.CanReturn(out var returnReason);
            returnFollow.interactable = ready && canReturn;
            bool storage = ready && listed && (st.Ui == "IDLE" || st.Ui == "MENU") && (st.Mode == "LIVE" || st.Mode == "HOLDING") && (!st.Pwm || st.Mode == "HOLDING");
            delete.interactable = storage;
            confirm.interactable = storage && acknowledge.isOn && st.Delete == SelectedId();
            cancel.interactable = ready && (st.Ui == "NAME" || st.Ui == "MENU" || st.Ui == "IDLE" && st.Delete != 0) && (st.Mode == "LIVE" || st.Mode == "HOLDING") && (!st.Unsaved || acknowledge.isOn);
            list.interactable = ready;
            stateText.text = side.ToString().ToUpperInvariant() + (gateway.IsSimulation ? " / 테스트 데이터: 녹화 송신 없음" : " / SD 녹화") + "\n" +
                (s.Trusted ? StateKo(st.Ui) + " / " + StateKo(st.Mode) + " / PWM " + (st.Pwm ? "ON" : "OFF") + " / RAM 샘플(최근 보고) " + st.RamSamples + " / 미저장 " + (st.Unsaved ? "있음" : "없음") : "상태 미확인 / V로 조회하세요") +
                "\n그리퍼 수동 열기: " + (c.ManualOpen.HasValue ? c.ManualOpen.Value ? "켜짐; 메뉴 전 H로 복원" : "꺼짐 확인" : "미확인; 메뉴 전 H 응답을 확인하세요");
            budgetText.text = c.TotalMaxMs == 0 ? "녹화 시간은 보드 시작 응답과 IDLE/RECORDING 확인 후 측정합니다." :
                "보드 제한: 실시간 " + c.LiveMaxMs + " ms / 안정화 여유 " + c.TailReserveMs + " ms / 전체 " + c.TotalMaxMs + " ms" + (c.RecordingActive ? " | 경과 " + c.RecordingSeconds(BoardUartTransport.Now).ToString("F1") + " s" : "");
            feedback.text = (Flow.Message.Length == 0 ? MessageKo(c.Message) : Flow.Message) +
                (Flow.Busy ? "" : !canReturn && (st == null || st.Ui == "MENU" || st.Unsaved || st.Ui == "NAME" || st.Ui == "SAVE" || st.Ui == "STOP_RECORD") ? "\n추종 복귀: " + returnReason : "") +
                "\n미완성 수신: " + source.PendingText(side);
            if(SharedTarget==2)feedback.text="BOTH: 기록/재생은 단일 보드 선택 필요 — RIGHT 또는 LEFT를 선택하세요.";
            string key = c.CatalogValid ? string.Join(";", System.Linq.Enumerable.Select(c.Entries, e => e.ToString())) : "UNVERIFIED";
            if (key != catalogKey)
            {
                uint selected = selectedIds[side==BoardSide.Right?0:1]; catalogKey = key; refreshingCatalog=true;catalog.ClearOptions();
                var options = new List<string>(); foreach (var entry in c.Entries) options.Add(entry.Id + "  " + entry.Name + "  " + entry.Samples + " 샘플 / " + (entry.DurationMs / 1000.0).ToString("0.##", System.Globalization.CultureInfo.InvariantCulture) + " 초");
                if (options.Count == 0) options.Add(c.CatalogValid ? "이 보드에 SD 기록이 없습니다." : "먼저 SD 목록을 조회하세요.");
                catalog.AddOptions(options); int index = 0; for (int i = 0; i < c.Entries.Count; i++) if (c.Entries[i].Id == selected) index = i;
                catalog.value = index; catalog.RefreshShownValue(); acknowledge.isOn = false;refreshingCatalog=false;
                if(c.CatalogValid&&c.Entries.Count>0)selectedIds[side==BoardSide.Right?0:1]=c.Entries[index].Id;
            }
            if(Flow.TakeSavedSelection(out var savedId))SelectRecord(savedId);
        }
        // Display localization only; never changes recording state or UART input/output.
        static string StateKo(string value)
        {
            switch (value)
            {
                case "IDLE": return "대기";
                case "LIVE": return "실시간";
                case "RECORDING": return "녹화 중";
                case "PLAYING": return "재생 중";
                case "ALIGNING": return "시작 자세 정렬 중";
                case "HOLDING": return "자세 유지";
                case "START_RECORD": return "녹화 준비 중";
                case "STOP_RECORD": return "녹화 종료·안정화 중";
                case "STOP_MENU": return "메뉴 준비 중";
                case "MENU": return "SD 메뉴";
                case "NAME": return "이름 입력";
                case "SAVING": return "저장 중";
                case "UNKNOWN": return "상태 미확인";
                default: return value;
            }
        }
        static readonly Dictionary<string, string> MessagesKo = new Dictionary<string, string>
        {
            { "Query board state before recording.", "녹화 전 보드 상태를 확인하세요." },
            { "Recording started by board; timer excludes preparation.", "보드에서 녹화를 시작했습니다. 준비 시간은 제외됩니다." },
            { "Recording start received without valid reported limits; state query required.", "녹화 시작 응답의 제한값이 유효하지 않습니다. 상태 조회가 필요합니다." },
            { "Complete SD list received. Select the actual board ID.", "SD 목록을 모두 수신했습니다. 실제 보드의 기록 ID를 선택하세요." },
            { "Incomplete/invalid SD list; request LIST again.", "SD 목록이 불완전하거나 유효하지 않습니다. 목록을 다시 조회하세요." },
            { "Board state is unverified; query V first.", "보드 상태 미확인: 먼저 V로 조회하세요." },
            { "One request is already pending; do not retry a toggle.", "이미 응답 대기 중입니다. 토글 명령을 반복하지 마세요." },
            { "Confirm no unfinished frame or numeric settings input.", "미완성 프레임이나 숫자 설정 입력이 없는지 확인하세요." },
            { "Record requires IDLE / LIVE / PWM ON.", "녹화에는 IDLE / LIVE / PWM ON 상태가 필요합니다." },
            { "Acknowledge replacing unsaved RAM before a new recording.", "새 녹화 전에 미저장 RAM 교체를 확인하세요." },
            { "Preparation cancellation requires START_RECORD.", "준비 취소는 START_RECORD 상태에서 가능합니다." },
            { "Finish requires active board recording.", "녹화 종료는 보드에서 녹화 중일 때 가능합니다." },
            { "P list entry requires IDLE/LIVE or MENU; IDLE/HOLDING uses playback stop first.", "SD 메뉴는 IDLE/LIVE 또는 MENU에서 열 수 있습니다. IDLE/HOLDING에서는 먼저 재생을 중지하세요." },
            { "Manual gripper release is unverified. Cancel menu, request H, confirm its response, then P.", "그리퍼 수동 해제 상태 미확인: 메뉴를 취소하고 H 응답을 확인한 뒤 P로 진입하세요." },
            { "Save requires verified NAME / HOLDING / unsaved RAM.", "저장에는 확인된 NAME / HOLDING / 미저장 RAM 상태가 필요합니다." },
            { "Name requires 1..24 ASCII letters, digits, '_' or '-'.", "이름은 영문·숫자·_·-로 1~24자 입력하세요." },
            { "That case-sensitive name already exists on this board.", "이 보드에 같은 이름이 있습니다. 대소문자를 구분합니다." },
            { "SELECT requires MENU / HOLDING / PWM ON and a fresh listed board ID.", "선택에는 MENU / HOLDING / PWM ON과 최신 SD 목록의 ID가 필요합니다." },
            { "Confirm automatic gripper restoration with H before entering the menu.", "메뉴 진입 전에 H 응답으로 그리퍼 자동 제어 복원을 확인하세요." },
            { "Acknowledge replacing unsaved RAM before loading an SD record.", "SD 기록을 불러오기 전에 미저장 RAM 교체를 확인하세요." },
            { "Playback stop requires ALIGNING / PLAYING or error HOLDING in IDLE.", "재생 중지는 IDLE에서 ALIGNING / PLAYING 또는 오류 HOLDING 상태일 때 가능합니다." },
            { "DELETE requires a fresh listed ID, IDLE/MENU, PWM OFF or HOLDING.", "삭제에는 최신 목록의 ID, IDLE/MENU, PWM OFF 또는 HOLDING 상태가 필요합니다." },
            { "Explicitly confirm the board's pending deletion of this listed ID.", "보드에서 삭제 대기 중인 해당 기록 ID를 명시적으로 확인하세요." },
            { "CANCEL is unavailable during preparation, settling, recording, saving or playback.", "준비·안정화·녹화·저장·재생 중에는 취소할 수 없습니다." },
            { "Acknowledge that unsaved RAM may be lost or replaced; CANCEL does not save it.", "미저장 RAM이 폐기·교체될 수 있음을 확인하세요. 취소는 저장하지 않습니다." },
            { "Invalid request", "유효하지 않은 요청" },
            { "Open a real board COM connection first", "먼저 실제 보드의 COM을 연결하세요." },
            { "Another request is awaiting a board response", "다른 요청의 보드 응답을 기다리고 있습니다." },
            { "Previous result unknown: recover console and query V first", "이전 결과 미확인: 콘솔 입력을 정리하고 V로 상태를 조회하세요." },
            { "Verified REC_STATE required", "검증된 REC_STATE가 필요합니다." },
            { "Confirm command boundary / finish or recover partial input", "명령 입력 경계를 확인하고 미완성 입력을 마치거나 복구하세요." },
            { "Superseded by explicit stop; original outcome unknown", "명시적 중지 요청으로 대체됨: 이전 요청 결과는 미확인입니다." },
            { "TX queue unavailable", "송신 큐를 사용할 수 없습니다." },
            { "No matching board response / outcome unknown; no retry", "일치하는 보드 응답 없음 / 결과 미확인: 자동 재시도하지 않습니다." },
        };
        static readonly KeyValuePair<string, string>[] RequestNamesKo =
        {
            new KeyValuePair<string, string>("Prepare recording", "녹화 준비"),
            new KeyValuePair<string, string>("Cancel preparation", "준비 취소"),
            new KeyValuePair<string, string>("Finish recording / settling tail", "녹화 종료 / 안정화"),
            new KeyValuePair<string, string>("Read SD list", "SD 목록 조회"),
            new KeyValuePair<string, string>("Enter SD menu", "SD 메뉴 열기"),
            new KeyValuePair<string, string>("Save ", "저장 "),
            new KeyValuePair<string, string>("Play SD ID ", "SD ID 재생 "),
            new KeyValuePair<string, string>("Stop playback", "재생 중지"),
            new KeyValuePair<string, string>("Prepare deletion of ID ", "ID 삭제 준비 "),
            new KeyValuePair<string, string>("Delete SD ID ", "SD ID 삭제 "),
            new KeyValuePair<string, string>("Cancel recording UI", "이름·메뉴·삭제 취소"),
        };
        static string MessageKo(string value)
        {
            if (string.IsNullOrEmpty(value)) return "요청 없음";
            if (MessagesKo.TryGetValue(value, out string translated)) return translated;
            // Raw board error frames are evidence, not translated protocol text.
            if (value.StartsWith("[REC]", StringComparison.Ordinal)) return "보드 응답: " + value;
            foreach (var item in RequestNamesKo)
                if (value.StartsWith(item.Key, StringComparison.Ordinal))
                { value = item.Value + value.Substring(item.Key.Length); break; }
            value = value.Replace(" requested; awaiting board response.", " 요청됨; 보드 응답 대기 중.")
                .Replace("Query state; no automatic retry or cancel.", "상태를 조회하세요. 자동 재시도·취소하지 않습니다.");
            foreach (var item in MessagesKo) value = value.Replace(item.Key, item.Value);
            // Translate request status delimiters without modifying stored state or user record names.
            foreach (string state in new[] { "Idle", "Queued", "Written", "Pending", "Accepted", "Rejected", "TimedOut", "Unknown", "Cancelled" })
            {
                string label = state == "Idle" ? "요청 없음" : state == "Queued" ? "송신 대기" : state == "Written" ? "송신 완료" : state == "Pending" ? "응답 대기" : state == "Accepted" ? "승인됨" : state == "Rejected" ? "거부됨" : state == "TimedOut" ? "시간 초과" : state == "Cancelled" ? "취소됨" : "결과 미확인";
                if (value == state) value = label;
                else value = value.Replace(": " + state + " / ", ": " + label + " / ").Replace(state + " / ", label + " / ");
            }
            return value;
        }
        void OnDestroy()
        {
            if (source == null) return;
            foreach (var flow in flows) flow?.Dispose();
            for (int i = 0; i < 2; i++) if (observers[i] != null) source.Session(i == 0 ? BoardSide.Right : BoardSide.Left).LineReceived -= observers[i];
        }
        RectTransform Rect(Transform parent, string name, float x, float y, float width, float height)
        {
            var go = new GameObject(name, typeof(RectTransform)); var r = (RectTransform)go.transform; r.SetParent(parent, false);
            r.anchorMin = r.anchorMax = r.pivot = new Vector2(0, 1); r.anchoredPosition = new Vector2(x, -y); r.sizeDelta = new Vector2(width, height); return r;
        }
        Text Label(Transform parent, string name, float x, float y, float width, float height, string text, int size)
        {
            var r = Rect(parent, name, x, y, width, height); var t = r.gameObject.AddComponent<Text>(); t.font = font; t.fontSize = size;
            t.color = new Color(.88f, .93f, .97f); t.text = text; t.raycastTarget = false; return t;
        }
        Button ButtonAt(Transform parent, string name, float x, float y, float width, string text, UnityEngine.Events.UnityAction action)
        {
            var r = Rect(parent, name, x, y, width, 32); var image = r.gameObject.AddComponent<Image>(); image.color = new Color(.15f, .24f, .31f);
            var b = r.gameObject.AddComponent<Button>(); b.targetGraphic = image; b.onClick.AddListener(() => {
                if (!b.interactable) return;
                if (name != "Play SD record" && name != "Return to follow" && name != "Save SD recording" && name != "Stop playback") Flow.ClearNotice();
                action();
            });
            var label = Label(r, name + " label", 4, 0, width - 8, 32, text, 13); label.alignment = TextAnchor.MiddleCenter;
            if (y > 0) actions.Add(b); return b;
        }
        InputField Input(Transform parent, string name, float x, float y, float width)
        {
            var r = Rect(parent, name, x, y, width, 32); var image = r.gameObject.AddComponent<Image>(); image.color = new Color(.025f, .045f, .065f);
            var input = r.gameObject.AddComponent<InputField>(); input.targetGraphic = image; input.textComponent = Label(r, name + " value", 8, 0, width - 16, 32, "", 14); input.textComponent.alignment = TextAnchor.MiddleLeft;
            var placeholder = Label(r, name + " placeholder", 8, 0, width - 16, 32, "기록 이름: 영문·숫자·_·- (1~24자)", 13); placeholder.color = new Color(.48f, .57f, .64f); placeholder.alignment = TextAnchor.MiddleLeft; input.placeholder = placeholder; return input;
        }
        Toggle CreateToggle(Transform parent, float x, float y, string text)
        {
            var r = Rect(parent, "Recording destructive acknowledgement", x, y, 594, 28); var toggle = r.gameObject.AddComponent<Toggle>();
            var box = Rect(r, "box", 0, 3, 22, 22).gameObject.AddComponent<Image>(); box.color = new Color(.2f, .3f, .38f); toggle.targetGraphic = box;
            var mark = Rect(box.transform, "mark", 4, 4, 14, 14).gameObject.AddComponent<Image>(); mark.color = new Color(.22f, .85f, .75f); toggle.graphic = mark; mark.canvasRenderer.SetAlpha(0);
            Label(r, "acknowledgement", 30, 0, 564, 28, text, 12); return toggle;
        }
        Dropdown CreateDropdown(Transform parent, float x, float y, float width)
        {
            var r = Rect(parent, "SD catalog IDs", x, y, width, 32); var image = r.gameObject.AddComponent<Image>(); image.color = new Color(.1f, .17f, .23f);
            var dropdown = r.gameObject.AddComponent<Dropdown>(); dropdown.targetGraphic = image;
            dropdown.captionText = Label(r, "Selected SD entry", 8, 0, width - 32, 32, "SD 목록을 조회하세요", 14); dropdown.captionText.alignment = TextAnchor.MiddleLeft;
            Label(r, "Dropdown arrow", width - 26, 0, 22, 32, "v", 15).alignment = TextAnchor.MiddleCenter;
            var template = Rect(r, "Template", 0, 32, width, 154); template.gameObject.AddComponent<Image>().color = new Color(.06f, .11f, .16f);
            var scroll = template.gameObject.AddComponent<ScrollRect>(); scroll.horizontal = false;
            var viewport = Rect(template, "Viewport", 0, 0, width, 154); viewport.gameObject.AddComponent<RectMask2D>();
            var content = Rect(viewport, "Content", 0, 0, width, 32); scroll.viewport = viewport; scroll.content = content;
            var item = Rect(content, "Item", 0, 0, width, 32); var itemImage = item.gameObject.AddComponent<Image>(); itemImage.color = new Color(.16f, .26f, .34f);
            var toggle = item.gameObject.AddComponent<Toggle>(); toggle.targetGraphic = itemImage;
            var itemText = Label(item, "Item label", 8, 0, width - 16, 32, "", 14); itemText.alignment = TextAnchor.MiddleLeft;
            dropdown.template = template; dropdown.itemText = itemText; template.gameObject.SetActive(false);
            dropdown.onValueChanged.AddListener(_ => {acknowledge.isOn = false;if(!refreshingCatalog&&Controller.CatalogValid&&dropdown.value<Controller.Entries.Count)selectedIds[side==BoardSide.Right?0:1]=Controller.Entries[dropdown.value].Id;}); return dropdown;
        }
    }
}
