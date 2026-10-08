#include "record_replay/motion_library.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "xil_printf.h"
#include "record_replay/motion_sd.h"

#ifdef ROBOT_STEREO_LEFT
#define RECORD_SIDE "l"
#else
#define RECORD_SIDE "r"
#endif

typedef enum { UI_IDLE, UI_STOP_RECORD, UI_STOP_MENU, UI_NAME, UI_MENU, UI_SAVE,
               UI_START_RECORD } LibraryUi;
typedef struct { char name[MOTION_LIBRARY_NAME_MAX + 1U]; uint32_t samples; } LibraryEntry;
typedef enum { SAVE_START_OK, SAVE_START_NAME, SAVE_START_DUPLICATE,
               SAVE_START_FULL, SAVE_START_IO } SaveStartResult;

static LibraryEntry entries[MOTION_LIBRARY_SLOTS];
static LibraryUi ui;
static unsigned quiet_ticks, selected_id, delete_id, save_id, save_index;
static int recording_started, file_open, owned_new, owned_name, published_bin;
static FIL save_file;
static char saved_name[MOTION_LIBRARY_NAME_MAX + 1U];
static char command[64], frame[64];
static unsigned frame_length;
static int frame_active, frame_bad, command_ready;
static MotionRecordReplayReason record_finish_reason;
static uint32_t save_crc;
static uint32_t last_servo_writes;
static unsigned settle_reserve;
static int unsaved_record;
static int state_report_valid;
static LibraryUi reported_ui;
static MotionRecordReplayMode reported_mode;
static unsigned reported_pwm, reported_selected, reported_delete, reported_entries;
static int reported_unsaved;

static int pose_settled(const AgentPipelineContext *pipeline);

static const char *ui_name(void)
{
    switch (ui) {
        case UI_STOP_RECORD: return "STOP_RECORD";
        case UI_STOP_MENU: return "STOP_MENU";
        case UI_NAME: return "NAME";
        case UI_MENU: return "MENU";
        case UI_SAVE: return "SAVE";
        case UI_START_RECORD: return "START_RECORD";
        default: return "IDLE";
    }
}

void motion_library_report_state(const MotionRecordReplay *controller,
                                 const AgentPipelineContext *pipeline, int force)
{
    unsigned index, count = 0U, playing;
    uint32_t samples;
    if (controller == NULL || pipeline == NULL) return;
    for (index = 0U; index < MOTION_LIBRARY_SLOTS; ++index)
        if (entries[index].samples) ++count;
    if (!force && state_report_valid && reported_ui == ui &&
        reported_mode == controller->mode && reported_pwm == pipeline->output_enabled &&
        reported_selected == selected_id && reported_delete == delete_id &&
        reported_entries == count && reported_unsaved == unsaved_record) return;
    playing = controller->mode == MOTION_RR_ALIGNING || controller->mode == MOTION_RR_PLAYING
        ? selected_id : 0U;
    samples = controller->mode == MOTION_RR_RECORDING
        ? controller->record_count : controller->replay_count;
    xil_printf("[REC_STATE] schema=1 side=" RECORD_SIDE " ui=%s mode=%s pwm=%u ram_samples=%lu entries=%u selected=%u playing=%u delete=%u unsaved=%u\r\n",
        ui_name(), motion_record_replay_mode_name(controller->mode),
        (unsigned)pipeline->output_enabled, (unsigned long)samples, count, selected_id,
        playing, delete_id, (unsigned)unsaved_record);
    reported_ui = ui;
    reported_mode = controller->mode;
    reported_pwm = pipeline->output_enabled;
    reported_selected = selected_id;
    reported_delete = delete_id;
    reported_entries = count;
    reported_unsaved = unsaved_record;
    state_report_valid = 1;
}

static unsigned record_settle_reserve(const MotionRecordReplay *controller,
                                     const AgentPipelineContext *pipeline)
{
    unsigned axis;
    double seconds = 0.0;
    if (!isfinite(controller->align_gripper_max_delta_norm) ||
        controller->align_gripper_max_delta_norm <= 0.0f) return MOTION_RECORD_REPLAY_MAX_SAMPLES;
    for (axis = 0U; axis < FOREARM_MOTION_JOINT_COUNT; ++axis) {
        const Motion *motion = &pipeline->motion.axes[axis];
        double duration;
        if (!isfinite(motion->lower) || !isfinite(motion->upper) ||
            !isfinite(motion->vmax) || !isfinite(motion->amax) ||
            motion->vmax <= 0.0 || motion->amax <= 0.0 || motion->upper <= motion->lower)
            return MOTION_RECORD_REPLAY_MAX_SAMPLES;
        duration = (motion->upper - motion->lower) / motion->vmax +
            2.0 * motion->vmax / motion->amax;
        if (duration > seconds) seconds = duration;
    }
    if (0.020 / controller->align_gripper_max_delta_norm > seconds)
        seconds = 0.020 / controller->align_gripper_max_delta_norm;
    if (seconds / 0.020 + 8.0 >= MOTION_RECORD_REPLAY_MAX_SAMPLES)
        return MOTION_RECORD_REPLAY_MAX_SAMPLES;
    return (unsigned)ceil(seconds / 0.020) + 8U;
}

static void path_for(char *path, unsigned id, const char *extension)
{
    (void)snprintf(path, 32U, "0:/MOTION/R%04u.%s", id, extension);
}

static uint32_t get_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void put_u32(unsigned char *bytes, uint32_t value)
{
    unsigned index;
    for (index = 0U; index < 4U; ++index) bytes[index] = (unsigned char)(value >> (index * 8U));
}

static uint32_t crc_update(uint32_t crc, const unsigned char *bytes, unsigned length)
{
    unsigned index, bit;
    for (index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc;
}

static int valid_name(const char *name)
{
    unsigned length = 0U;
    if (name == NULL) return 0;
    while (name[length]) {
        unsigned char byte = (unsigned char)name[length++];
        if (length > MOTION_LIBRARY_NAME_MAX ||
            !((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') || byte == '_' || byte == '-')) return 0;
    }
    return length > 0U;
}

static unsigned parse_id(const char *text)
{
    unsigned value = 0U, digits = 0U;
    while (*text) {
        if (*text < '0' || *text > '9' || ++digits > 2U) return 0U;
        value = value * 10U + (unsigned)(*text++ - '0');
    }
    return digits && value > 0U && value <= MOTION_LIBRARY_SLOTS ? value : 0U;
}

static void scan_catalog(void)
{
    unsigned id;
    memset(entries, 0, sizeof(entries));
    for (id = 1U; id <= MOTION_LIBRARY_SLOTS; ++id) {
        FIL file;
        FILINFO info;
        unsigned char header[24];
        char path[32], name[MOTION_LIBRARY_NAME_MAX + 1U] = {0};
        UINT received;
        FRESULT result, closed;
        uint32_t count;
        path_for(path, id, "BIN");
        if (f_stat(path, &info) != FR_OK || f_open(&file, path, FA_READ) != FR_OK) continue;
        result = f_read(&file, header, sizeof(header), &received);
        closed = f_close(&file);
        if (result != FR_OK || closed != FR_OK || received != sizeof(header) ||
            memcmp(header, "MRP1", 4U) || get_u32(header + 4) != 1U ||
            get_u32(header + 8) != 20000U || get_u32(header + 16) != 0x77D4E3BBU) continue;
        count = get_u32(header + 12);
        if (!count || count > MOTION_RECORD_REPLAY_MAX_SAMPLES ||
            info.fsize != 24U + count * sizeof(MotionSample)) continue;
        path_for(path, id, "TXT");
        if (f_stat(path, &info) != FR_OK || !info.fsize ||
            info.fsize > MOTION_LIBRARY_NAME_MAX || f_open(&file, path, FA_READ) != FR_OK) continue;
        result = f_read(&file, name, (UINT)info.fsize, &received);
        closed = f_close(&file);
        if (result != FR_OK || closed != FR_OK || received != info.fsize ||
            strlen(name) != received || !valid_name(name)) continue;
        memcpy(entries[id - 1U].name, name, sizeof(name));
        entries[id - 1U].samples = count;
    }
}

static void print_list(void)
{
    unsigned id, count = 0U;
    xil_printf("[REC] SD records; select: " RECORD_SIDE " record select <number>\r\n");
    for (id = 1U; id <= MOTION_LIBRARY_SLOTS; ++id) {
        const LibraryEntry *entry = &entries[id - 1U];
        if (!entry->samples) continue;
        ++count;
        xil_printf("[REC] %u %s samples=%lu duration_ms=%lu%s\r\n", id, entry->name,
            (unsigned long)entry->samples, (unsigned long)entry->samples * 20UL,
            id == selected_id ? " selected" : "");
    }
    xil_printf("[REC] count=%u; delete: " RECORD_SIDE " record delete <number>; cancel: " RECORD_SIDE " record cancel\r\n", count);
}

void motion_library_init(void)
{
    ui = UI_IDLE;
    quiet_ticks = selected_id = delete_id = 0U;
    recording_started = file_open = owned_new = owned_name = published_bin = 0;
    frame_active = frame_bad = command_ready = 0;
    frame_length = 0U;
    last_servo_writes = 0U;
    settle_reserve = 0U;
    unsaved_record = state_report_valid = 0;
    scan_catalog();
    xil_printf("[REC] SD library ready; R=record, P=list/stop; names=ASCII letters/digits/_/-\r\n");
}

void motion_library_status(void)
{
    unsigned index, count = 0U;
    for (index = 0U; index < MOTION_LIBRARY_SLOTS; ++index)
        if (entries[index].samples) ++count;
    xil_printf("[REC] entries=%u selected=%u ui=%u; " RECORD_SIDE " record list\r\n", count, selected_id, (unsigned)ui);
}

int motion_library_input_allowed(void)
{
    return ui == UI_IDLE;
}

int motion_library_record_preparing(void)
{
    return ui == UI_START_RECORD;
}

int motion_library_input_active(void)
{
    return frame_active;
}

void motion_library_abort_input(void)
{
    frame_active = frame_bad = command_ready = 0;
    frame_length = 0U;
}

int motion_library_feed(uint8_t byte, int menu_active)
{
    if (!frame_active) {
        if (byte != '!') return 0;
        frame_active = 1;
        frame_bad = menu_active;
        frame_length = 0U;
        return 1;
    }
    if (byte == '\r') {
        frame[frame_length] = '\0';
        if (!frame_bad && !command_ready && strncmp(frame, "REC,", 4U) == 0) {
            memcpy(command, frame + 4, frame_length - 3U);
            command_ready = 1;
        } else xil_printf("[REC] rejected FRAME_OR_MENU_OR_BUSY\r\n");
        frame_active = frame_bad = 0;
        frame_length = 0U;
    } else if (byte < 32U || byte > 126U || frame_length + 1U >= sizeof(frame)) {
        frame_bad = 1;
    } else if (!frame_bad) frame[frame_length++] = (char)byte;
    return 1;
}

int motion_library_event(MotionRecordReplay *controller, AgentPipelineContext *pipeline,
                         CnnAppEvent event)
{
    if (event != CNN_APP_EVENT_RECORD_TOGGLE && event != CNN_APP_EVENT_PLAY_TOGGLE) return 0;
    if (event == CNN_APP_EVENT_RECORD_TOGGLE && ui == UI_START_RECORD) {
        if (controller->mode == MOTION_RR_RECORDING)
            (void)motion_record_replay_stop_record(controller);
        recording_started = unsaved_record = 0;
        ui = UI_IDLE;
        quiet_ticks = 0U;
        xil_printf("[REC] preparation cancelled; no SD overwrite\r\n");
        return 1;
    }
    if (ui != UI_IDLE && !(event == CNN_APP_EVENT_PLAY_TOGGLE && ui == UI_MENU)) {
        xil_printf("[REC] rejected UI_BUSY; finish naming or " RECORD_SIDE " record cancel\r\n");
        return 1;
    }
    if (event == CNN_APP_EVENT_PLAY_TOGGLE) {
        if (ui == UI_MENU) {
            if (controller->mode == MOTION_RR_LIVE && pipeline->output_enabled) {
                ui = UI_STOP_MENU;
                quiet_ticks = 0U;
            } else print_list();
        } else if (controller->mode == MOTION_RR_ALIGNING ||
                   controller->mode == MOTION_RR_PLAYING || controller->mode == MOTION_RR_HOLDING) {
            int accepted = motion_record_replay_stop_play(controller, pipeline);
            xil_printf("[REC] playback stop=%u mode=%s\r\n", (unsigned)accepted,
                motion_record_replay_mode_name(controller->mode));
        } else if (controller->mode == MOTION_RR_RECORDING) {
            xil_printf("[REC] rejected RECORDING; finish with " RECORD_SIDE " R\r\n");
        } else if (!pipeline->output_enabled) {
            print_list();
            xil_printf("[REC] enable PWM with " RECORD_SIDE " E, then " RECORD_SIDE " P to select\r\n");
        } else {
            ui = UI_STOP_MENU;
            quiet_ticks = 0U;
            xil_printf("[REC] finishing last approved target before selection\r\n");
        }
    } else if (controller->mode == MOTION_RR_RECORDING) {
        ui = UI_STOP_RECORD;
        quiet_ticks = 0U;
        xil_printf("[REC] stop requested; recording safe settling tail, then name\r\n");
    } else {
        if (controller->mode == MOTION_RR_HOLDING)
            (void)motion_record_replay_resume_live(controller, pipeline);
        settle_reserve = record_settle_reserve(controller, pipeline);
        if (!pipeline->output_enabled || controller->mode != MOTION_RR_LIVE)
            xil_printf("[REC] record rejected PWM_OR_MODE\r\n");
        else if (settle_reserve + 2U >= MOTION_RECORD_REPLAY_MAX_SAMPLES)
            xil_printf("[REC] record rejected SETTLE_RESERVE_CONFIG\r\n");
        else {
            ui = UI_START_RECORD;
            quiet_ticks = 0U;
            recording_started = unsaved_record = 0;
            record_finish_reason = MOTION_RR_REASON_STOPPED;
            xil_printf("[REC] preparing; finishing last approved target; " RECORD_SIDE " R cancels; no samples saved yet\r\n");
        }
    }
    return 1;
}

static int pose_settled(const AgentPipelineContext *pipeline)
{
    unsigned axis;
    float applied[4], output[4];
    if (!pipeline->applied_command_valid) return 0;
    applied[0] = pipeline->applied_command.elbow_roll_deg;
    applied[1] = pipeline->applied_command.elbow_pitch_deg;
    applied[2] = pipeline->applied_command.wrist_pitch_deg;
    applied[3] = pipeline->applied_command.wrist_roll_deg;
    output[0] = pipeline->output.elbow_roll_deg;
    output[1] = pipeline->output.elbow_pitch_deg;
    output[2] = pipeline->output.wrist_pitch_deg;
    output[3] = pipeline->output.wrist_roll_deg;
    for (axis = 0U; axis < FOREARM_MOTION_JOINT_COUNT; ++axis)
        if (!isfinite(pipeline->motion.axes[axis].v) || fabs(pipeline->motion.axes[axis].v) > .05 ||
            fabs(pipeline->motion.axes[axis].target - pipeline->motion.axes[axis].q) > .0002 ||
            !isfinite(applied[axis]) || !isfinite(output[axis]) ||
            fabsf(applied[axis] - output[axis]) > .0002f) return 0;
    return fabsf(pipeline->applied_command.gripper_norm - pipeline->output.gripper_norm) < .0001f;
}

void motion_library_tick(MotionRecordReplay *controller, AgentPipelineContext *pipeline)
{
    int fresh_hal = pipeline->servo_writes != last_servo_writes;
    last_servo_writes = pipeline->servo_writes;
    if (ui == UI_START_RECORD) {
        if (!pipeline->output_enabled ||
            (recording_started && controller->mode != MOTION_RR_RECORDING) ||
            (controller->mode != MOTION_RR_LIVE && controller->mode != MOTION_RR_RECORDING)) {
            if (controller->mode == MOTION_RR_RECORDING)
                (void)motion_record_replay_stop_record(controller);
            ui = UI_IDLE;
            recording_started = unsaved_record = 0;
            xil_printf("[REC] preparation cancelled PWM_OR_MODE; no SD overwrite\r\n");
            return;
        }
        quiet_ticks = fresh_hal && pose_settled(pipeline) ? quiet_ticks + 1U : 0U;
        if (controller->mode == MOTION_RR_LIVE) {
            if (quiet_ticks >= 3U && motion_record_replay_start_record(controller))
                recording_started = 1;
        } else if (fresh_hal && pose_settled(pipeline) && controller->record_count >= 2U) {
            ui = UI_IDLE;
            unsaved_record = 1;
            xil_printf("[REC] recording started; " RECORD_SIDE " R to finish; live_max_ms=%lu tail_reserve_ms=%lu total_max_ms=%lu\r\n",
                (unsigned long)(MOTION_RECORD_REPLAY_MAX_SAMPLES - settle_reserve) * 20UL,
                (unsigned long)settle_reserve * 20UL,
                (unsigned long)MOTION_RECORD_REPLAY_MAX_SAMPLES * 20UL);
        }
        return;
    }
    if (recording_started && controller->mode == MOTION_RR_RECORDING && ui == UI_IDLE &&
        controller->record_count >= MOTION_RECORD_REPLAY_MAX_SAMPLES - settle_reserve) {
        record_finish_reason = MOTION_RR_REASON_BUFFER_FULL;
        ui = UI_STOP_RECORD;
        quiet_ticks = 0U;
        xil_printf("[REC] live limit reached; reserving safe settling tail before name\r\n");
    }
    if (recording_started && controller->mode != MOTION_RR_RECORDING && ui == UI_IDLE) {
        record_finish_reason = controller->reason;
        ui = UI_STOP_RECORD;
        quiet_ticks = 0U;
        xil_printf("[REC] recording ended reason=%s; settling\r\n",
            motion_record_replay_reason_name(record_finish_reason));
    }
    if (ui != UI_STOP_RECORD && ui != UI_STOP_MENU) return;
    if (!pipeline->output_enabled) {
        recording_started = 0;
        unsaved_record = 0;
        ui = UI_IDLE;
        xil_printf("[REC] cancelled after PWM OFF; no SD overwrite\r\n");
        return;
    }
    quiet_ticks = fresh_hal && pose_settled(pipeline) ? quiet_ticks + 1U : 0U;
    if (quiet_ticks < 3U) return;
    if (ui == UI_STOP_RECORD) {
        MotionRecordReplayReason reason;
        if (controller->mode == MOTION_RR_RECORDING) {
            if (!motion_record_replay_on_record_button_pulse(controller)) return;
        } else if (record_finish_reason == MOTION_RR_REASON_STOPPED) record_finish_reason = controller->reason;
        recording_started = 0;
        reason = motion_record_replay_validate_replay(controller);
        if (!motion_record_replay_hold_live(controller, pipeline)) return;
        if (reason != MOTION_RR_REASON_NONE ||
            (record_finish_reason != MOTION_RR_REASON_STOPPED && record_finish_reason != MOTION_RR_REASON_BUFFER_FULL)) {
            ui = UI_MENU;
            xil_printf("[REC] save rejected reason=%s end=%s; existing SD records unchanged\r\n",
                motion_record_replay_reason_name(reason), motion_record_replay_reason_name(record_finish_reason));
            print_list();
        } else {
            ui = UI_NAME;
            xil_printf("[REC] enter name: " RECORD_SIDE " record name <name> (1..24 ASCII letters/digits/_/-); pose held\r\n");
        }
    } else if (motion_record_replay_hold_live(controller, pipeline)) {
        ui = UI_MENU;
        print_list();
    }
}

static int storage_safe(const MotionRecordReplay *controller, const AgentPipelineContext *pipeline)
{
    return !pipeline->output_enabled ||
        (controller->mode == MOTION_RR_HOLDING && pose_settled(pipeline));
}

static void cleanup_save(void)
{
    char path[32];
    if (file_open) { (void)f_close(&save_file); file_open = 0; }
    if (owned_new) { path_for(path, save_id, "NEW"); (void)f_unlink(path); }
    if (owned_name) { path_for(path, save_id, "NTX"); (void)f_unlink(path); }
    if (published_bin) { path_for(path, save_id, "BIN"); (void)f_unlink(path); }
    owned_new = owned_name = published_bin = 0;
}

static SaveStartResult begin_save(MotionRecordReplay *controller, const char *name)
{
    unsigned id, index;
    char path[32];
    FILINFO info;
    unsigned char header[24];
    uint32_t crc = 0xFFFFFFFFU;
    UINT written;
    FRESULT result;
    if (!valid_name(name)) return SAVE_START_NAME;
    for (id = 0U; id < MOTION_LIBRARY_SLOTS; ++id)
        if (entries[id].samples && strcmp(entries[id].name, name) == 0) return SAVE_START_DUPLICATE;
    result = f_mkdir("0:/MOTION");
    if (result != FR_OK && result != FR_EXIST) return SAVE_START_IO;
    for (id = 1U; id <= MOTION_LIBRARY_SLOTS; ++id) {
        const char *extensions[] = {"BIN", "TXT", "NEW", "NTX", "DEL"};
        int free_slot = 1;
        for (index = 0U; index < 5U; ++index) {
            path_for(path, id, extensions[index]);
            if (f_stat(path, &info) != FR_NO_FILE) free_slot = 0;
        }
        if (free_slot) break;
    }
    if (id > MOTION_LIBRARY_SLOTS) return SAVE_START_FULL;
    save_id = id;
    save_index = 0U;
    owned_new = owned_name = published_bin = 0;
    for (index = 0U; index < controller->replay_count; ++index) {
        MotionSample sample;
        if (!motion_record_replay_get_replay_sample(controller, index, &sample)) return SAVE_START_IO;
        crc = crc_update(crc, (const unsigned char *)&sample, sizeof(sample));
    }
    memcpy(header, "MRP1", 4U);
    put_u32(header + 4, 1U);
    put_u32(header + 8, 20000U);
    put_u32(header + 12, controller->replay_count);
    put_u32(header + 16, 0x77D4E3BBU);
    put_u32(header + 20, crc ^ 0xFFFFFFFFU);
    save_crc = crc ^ 0xFFFFFFFFU;
    path_for(path, id, "NEW");
    if (f_open(&save_file, path, FA_WRITE | FA_CREATE_NEW) != FR_OK) return SAVE_START_IO;
    file_open = owned_new = 1;
    if (f_write(&save_file, header, sizeof(header), &written) != FR_OK || written != sizeof(header)) {
        cleanup_save();
        return SAVE_START_IO;
    }
    strcpy(saved_name, name);
    ui = UI_SAVE;
    xil_printf("[REC] saving %s id=%u; fixed PWM pose held; SD latency may delay UART\r\n", name, id);
    return SAVE_START_OK;
}

static int verify_saved_data(uint32_t samples)
{
    FIL file;
    unsigned char header[24], chunk[320];
    char path[32];
    UINT received;
    uint32_t remaining = samples * (uint32_t)sizeof(MotionSample), crc = 0xFFFFFFFFU;
    int valid = 0;
    path_for(path, save_id, "NEW");
    if (f_open(&file, path, FA_READ) != FR_OK) return 0;
    if (f_size(&file) != 24U + remaining ||
        f_read(&file, header, sizeof(header), &received) != FR_OK || received != sizeof(header) ||
        memcmp(header, "MRP1", 4U) || get_u32(header + 4) != 1U ||
        get_u32(header + 8) != 20000U || get_u32(header + 12) != samples ||
        get_u32(header + 16) != 0x77D4E3BBU || get_u32(header + 20) != save_crc) goto close_file;
    while (remaining) {
        UINT length = remaining > sizeof(chunk) ? (UINT)sizeof(chunk) : (UINT)remaining;
        if (f_read(&file, chunk, length, &received) != FR_OK || received != length) goto close_file;
        crc = crc_update(crc, chunk, received);
        remaining -= received;
    }
    valid = (crc ^ 0xFFFFFFFFU) == save_crc;
close_file:
    if (f_close(&file) != FR_OK) valid = 0;
    return valid;
}

static void save_step(MotionRecordReplay *controller)
{
    MotionSample chunk[16];
    unsigned count = 0U;
    UINT written;
    char source[32], destination[32];
    FRESULT result, closed;
    while (count < 16U && save_index + count < controller->replay_count) {
        if (!motion_record_replay_get_replay_sample(controller, save_index + count, &chunk[count])) goto failed;
        ++count;
    }
    if (count) {
        if (f_write(&save_file, chunk, count * sizeof(MotionSample), &written) != FR_OK ||
            written != count * sizeof(MotionSample)) goto failed;
        save_index += count;
        return;
    }
    result = f_sync(&save_file);
    closed = f_close(&save_file);
    file_open = 0;
    if (result != FR_OK || closed != FR_OK) goto failed;
    if (!verify_saved_data(controller->replay_count)) goto failed;
    path_for(source, save_id, "NTX");
    if (f_open(&save_file, source, FA_WRITE | FA_CREATE_NEW) != FR_OK) goto failed;
    file_open = owned_name = 1;
    result = f_write(&save_file, saved_name, (UINT)strlen(saved_name), &written);
    if (result != FR_OK || written != strlen(saved_name)) goto failed;
    result = f_sync(&save_file);
    closed = f_close(&save_file);
    file_open = 0;
    if (result != FR_OK || closed != FR_OK) goto failed;
    path_for(source, save_id, "NEW"); path_for(destination, save_id, "BIN");
    if (f_rename(source, destination) != FR_OK) goto failed;
    owned_new = 0; published_bin = 1;
    path_for(source, save_id, "NTX"); path_for(destination, save_id, "TXT");
    if (f_rename(source, destination) != FR_OK) goto failed;
    owned_name = published_bin = 0;
    selected_id = save_id;
    scan_catalog();
    ui = UI_MENU;
    unsaved_record = 0;
    xil_printf("[REC] SAVED id=%u name=%s samples=%lu; persistent on SD; " RECORD_SIDE " P lists choices\r\n",
        save_id, saved_name, (unsigned long)controller->replay_count);
    return;
failed:
    cleanup_save();
    ui = UI_NAME;
    xil_printf("[REC] SAVE_FAILED; RAM retained, old files unchanged; retry " RECORD_SIDE " record name <name>\r\n");
}

int motion_library_service(MotionRecordReplay *controller, AgentPipelineContext *pipeline,
                           uint32_t tick_overruns)
{
    unsigned id;
    char path[32];
    if (ui == UI_SAVE) {
        if (storage_safe(controller, pipeline)) save_step(controller);
        else { cleanup_save(); ui = UI_NAME; xil_printf("[REC] SAVE_FAILED unsafe storage state\r\n"); }
    }
    if (!command_ready) return 0;
    command_ready = 0;
    if (strcmp(command, "LIST") == 0) { print_list(); return 0; }
    if (ui == UI_SAVE) { xil_printf("[REC] rejected SAVE_BUSY\r\n"); return 0; }
    if (strcmp(command, "CANCEL") == 0) {
        int idle = ui == UI_IDLE;
        if (ui == UI_START_RECORD || ui == UI_STOP_RECORD || ui == UI_STOP_MENU || controller->mode == MOTION_RR_RECORDING ||
            controller->mode == MOTION_RR_ALIGNING || controller->mode == MOTION_RR_PLAYING) {
            xil_printf("[REC] rejected STILL_SETTLING\r\n"); return 0;
        }
        delete_id = 0U;
        if (controller->mode == MOTION_RR_HOLDING)
            (void)motion_record_replay_resume_live(controller, pipeline);
        ui = UI_IDLE;
        unsaved_record = 0;
        xil_printf(idle ? "[REC] deletion confirmation cleared; mode unchanged\r\n" :
            "[REC] cancelled; unsaved RAM may be replaced by next recording; async remains OFF\r\n");
    } else if (strncmp(command, "NAME,", 5U) == 0) {
        if (ui != UI_NAME) xil_printf("[REC] name rejected NAME_STATE\r\n");
        else if (!storage_safe(controller, pipeline))
            xil_printf("[REC] name rejected UNSAFE_STORAGE; RAM retained\r\n");
        else if (motion_record_replay_validate_replay(controller) != MOTION_RR_REASON_NONE)
            xil_printf("[REC] name rejected REPLAY_VALIDATION; RAM retained\r\n");
        else {
            SaveStartResult result = begin_save(controller, command + 5);
            if (result != SAVE_START_OK) {
                const char *reason = result == SAVE_START_NAME ? "NAME_FORMAT" :
                    result == SAVE_START_DUPLICATE ? "DUPLICATE" :
                    result == SAVE_START_FULL ? "LIBRARY_FULL" : "IO";
                xil_printf("[REC] name rejected %s; RAM retained; not saved on SD\r\n", reason);
            }
        }
    } else if (strncmp(command, "SELECT,", 7U) == 0) {
        MotionSdResult result;
        if (pipeline->gripper_motion_hold.manual_open) {
            xil_printf("[REC] select rejected MANUAL_OPEN; cancel menu, " RECORD_SIDE " H, then " RECORD_SIDE " P\r\n");
            return 0;
        }
        if (ui != UI_MENU || !pipeline->output_enabled ||
            controller->mode != MOTION_RR_HOLDING || !(id = parse_id(command + 7)) ||
            !entries[id - 1U].samples) {
            xil_printf("[REC] select rejected; " RECORD_SIDE " E then " RECORD_SIDE " P; choose listed number\r\n"); return 0;
        }
        delete_id = 0U;
        path_for(path, id, "BIN");
        if (!motion_record_replay_resume_live(controller, pipeline)) return 0;
        result = motion_sd_load_path(controller, path);
        if (result != MOTION_SD_OK) {
            const char *reason = motion_record_replay_reason_name(controller->reason);
            (void)motion_record_replay_hold_live(controller, pipeline);
            xil_printf("[REC] select rejected %s reason=%s; pose held\r\n", motion_sd_result_name(result), reason);
            return 0;
        }
        if (!motion_record_replay_start_play(controller, pipeline, tick_overruns)) {
            const char *reason = motion_record_replay_reason_name(controller->reason);
            (void)motion_record_replay_hold_live(controller, pipeline);
            xil_printf("[REC] play rejected reason=%s; pose held\r\n", reason);
            return 0;
        }
        selected_id = id;
        ui = UI_IDLE;
        xil_printf("[REC] PLAY id=%u name=%s samples=%lu repeat=1; " RECORD_SIDE " P stops\r\n", id,
            entries[id - 1U].name, (unsigned long)controller->replay_count);
        return 1;
    } else if (strncmp(command, "DELETE,", 7U) == 0) {
        if ((ui != UI_IDLE && ui != UI_MENU) || !storage_safe(controller, pipeline) ||
            !(id = parse_id(command + 7)) || !entries[id - 1U].samples)
            xil_printf("[REC] delete rejected BUSY_OR_NUMBER; stop playback first\r\n");
        else {
            delete_id = id;
            xil_printf("[REC] confirm deleting %u %s: " RECORD_SIDE " record confirm %u; cancel: " RECORD_SIDE " record cancel\r\n",
                id, entries[id - 1U].name, id);
        }
    } else if (strncmp(command, "CONFIRM,", 8U) == 0) {
        char tombstone[32];
        FRESULT removed_name, removed_bin;
        if ((ui != UI_IDLE && ui != UI_MENU) || !storage_safe(controller, pipeline) ||
            !(id = parse_id(command + 8)) || id != delete_id || !entries[id - 1U].samples) {
            xil_printf("[REC] delete confirmation rejected\r\n"); return 0;
        }
        delete_id = 0U;
        path_for(path, id, "BIN"); path_for(tombstone, id, "DEL");
        if (f_rename(path, tombstone) != FR_OK) { xil_printf("[REC] DELETE_FAILED unchanged\r\n"); return 0; }
        path_for(path, id, "TXT"); removed_name = f_unlink(path);
        removed_bin = f_unlink(tombstone);
        if (selected_id == id) { selected_id = 0U; controller->replay_count = 0U; }
        scan_catalog();
        xil_printf("[REC] %s id=%u\r\n",
            removed_name == FR_OK && removed_bin == FR_OK ? "DELETED" : "DELETE_PARTIAL_REQUIRES_SD_REVIEW", id);
    } else xil_printf("[REC] rejected UNKNOWN_COMMAND\r\n");
    return 0;
}
