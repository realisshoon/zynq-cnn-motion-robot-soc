/* Exercise SD paths and the full snapshot lifecycle with FatFs/register models.
 * Snapshot copy requires a live writer parked on a DIFFERENT buffer. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *capture_memcpy(void *dest, const void *source, size_t count);
#define memcpy capture_memcpy
#include "../../src/integration/frame_capture_sd.c"
#undef memcpy

typedef struct { char path[32]; unsigned char attributes; } Entry;
static Entry entries[16];
static unsigned int entry_count;
static int deny_mkdir;
static unsigned int open_count;
static u32 mock_status, mock_control, last_clear, mock_park, current_store, target_store;
static unsigned int clear_count;
static int sticky_error;
static XTime mock_time;
static int capture_mode, scenario, phase, event_armed;
static unsigned int polls, parks, restores, snapshots, unlinks, fresh_events;
static unsigned int file_bytes, saved_store;
static u32 expected_control;
static int delete_mode, delete_fail, mount_fail, read_fail, close_dir_fail;

static void *capture_memcpy(void *dest, const void *source, size_t count)
{
    UINTPTR address = (UINTPTR)source;
    assert(capture_mode && phase == 1 && fresh_events >= 4U);
    assert(mock_control & XAXIVDMA_CR_RUNSTOP_MASK);
    assert(!(mock_control & (XAXIVDMA_CR_TAIL_EN_MASK | XAXIVDMA_CR_FRMCNT_EN_MASK)));
    assert(!(mock_status & XAXIVDMA_SR_HALTED_MASK));
    assert(!(mock_status & CAPTURE_ERROR_MASK));
    assert(address >= 0x0A100000U && count == CNN_FRAME_BYTES);
    saved_store = (unsigned int)((address - 0x0A100000U) / CNN_FRAME_BYTES);
    assert(saved_store < 3U && saved_store != current_store && current_store == target_store);
    ++snapshots;
    if (scenario == 7) mock_status |= 0x8000U;
    return memset(dest, 0x55, count);
}

static void add_entry(const char *path, unsigned char attributes)
{
    assert(entry_count < sizeof(entries) / sizeof(entries[0]));
    assert(strlen(path) < sizeof(entries[0].path));
    strcpy(entries[entry_count].path, path);
    entries[entry_count++].attributes = attributes;
}

FRESULT f_stat(const char *path, FILINFO *info)
{
    unsigned int i;
    for (i = 0; i < entry_count; ++i) {
        if (!strcmp(path, entries[i].path)) {
            info->fattrib = entries[i].attributes;
            return FR_OK;
        }
    }
    return FR_NO_FILE;
}

FRESULT f_mkdir(const char *path)
{
    FILINFO info;
    if (deny_mkdir) return FR_DENIED;
    if (f_stat(path, &info) == FR_OK) return FR_EXIST;
    add_entry(path, AM_DIR);
    return FR_OK;
}

FRESULT f_open(FIL *file, const char *path, unsigned char mode)
{
    FILINFO info;
    (void)file;
    assert(mode == (FA_WRITE | FA_CREATE_NEW));
    ++open_count;
    if (f_stat(path, &info) == FR_OK) return FR_EXIST;
    add_entry(path, 0U);
    return FR_OK;
}

FRESULT f_write(FIL *file, const void *bytes, UINT size, UINT *written)
{
    (void)file; (void)bytes;
    assert(capture_mode && restores == 1U && snapshots == 1U);
    assert(phase == 2 && fresh_events >= 6U && mock_control == expected_control);
    file_bytes += size;
    *written = size;
    return FR_OK;
}
FRESULT f_sync(FIL *file) { (void)file; assert(capture_mode); return FR_OK; }
FRESULT f_close(FIL *file) { (void)file; assert(capture_mode); return FR_OK; }
FRESULT f_unlink(const char *path)
{
    unsigned int i;
    if (!delete_mode) { assert(capture_mode); ++unlinks; return FR_OK; }
    assert(!strncmp(path, "0:/JIG/CAP", 10U));
    if (delete_fail && unlinks == 1U) return FR_DISK_ERR;
    for (i = 0; i < entry_count; ++i) {
        if (!strcmp(path, entries[i].path)) {
            entries[i].path[0] = '\0';
            ++unlinks;
            return FR_OK;
        }
    }
    return FR_NO_FILE;
}
FRESULT f_opendir(DIR *directory, const char *path)
{
    FILINFO info;
    assert(delete_mode && !strcmp(path, "0:/JIG"));
    if (f_stat(path, &info) != FR_OK) return FR_NO_PATH;
    if (!(info.fattrib & AM_DIR)) return FR_DENIED;
    directory->cursor = 0U;
    return FR_OK;
}
FRESULT f_readdir(DIR *directory, FILINFO *info)
{
    unsigned int i;
    if (read_fail) return FR_DISK_ERR;
    while (directory->cursor < entry_count) {
        i = directory->cursor++;
        if (strncmp(entries[i].path, "0:/JIG/", 7U) ||
            strchr(entries[i].path + 7U, '/')) continue;
        strcpy(info->fname, entries[i].path + 7U);
        info->fattrib = entries[i].attributes;
        return FR_OK;
    }
    info->fname[0] = '\0';
    return FR_OK;
}
FRESULT f_closedir(DIR *directory)
{ (void)directory; return close_dir_fail ? FR_DISK_ERR : FR_OK; }
u32 XAxiVdma_CurrFrameStore(XAxiVdma *vdma, unsigned int direction)
{
    (void)vdma; (void)direction;
    return current_store;
}
void XAxiVdma_DmaStop(XAxiVdma *vdma, unsigned int direction)
{
    (void)vdma; (void)direction; abort(); /* Must NEVER stop the camera. */
}
int XAxiVdma_DmaStart(XAxiVdma *vdma, unsigned int direction)
{
    (void)vdma; (void)direction; abort();
}
int XAxiVdma_StartParking(XAxiVdma *vdma, int frame, unsigned int direction)
{
    (void)vdma; (void)direction;
    assert(capture_mode && fresh_events >= 2U);
    ++parks;
    target_store = (u32)frame;
    mock_park = (mock_park & ~XAXIVDMA_PARKPTR_WRTREF_MASK) | ((u32)frame << 8);
    if (scenario == 4) return -1; /* Reference already changed, as in BSP. */
    mock_control &= ~XAXIVDMA_CR_TAIL_EN_MASK;
    phase = 1;
    if (scenario == 6) current_store = (target_store + 1U) % 3U;
    return XST_SUCCESS;
}
void XAxiVdma_StopParking(XAxiVdma *vdma, unsigned int direction)
{
    (void)vdma; (void)direction;
    assert(capture_mode);
    ++restores;
    mock_control |= XAXIVDMA_CR_TAIL_EN_MASK;
    phase = 2;
}
u32 XAxiVdma_GetDmaChannelErrors(XAxiVdma *vdma, unsigned int direction)
{ (void)vdma; (void)direction; abort(); }
u32 XAxiVdma_ReadReg(UINTPTR base, u32 offset)
{
    (void)base;
    if (offset == XAXIVDMA_PARKPTR_OFFSET) return mock_park;
    if (offset == XAXIVDMA_SR_OFFSET && capture_mode && event_armed) {
        if (scenario == 3 && phase == 1) mock_status |= 0x80U;
        if (++polls % 2U == 0U && !(scenario == 2 && phase == 0) &&
            !(scenario == 5 && phase == 2) && !(scenario == 8 && file_bytes != 0U)) {
            ++fresh_events;
            mock_status |= XAXIVDMA_IXR_FRMCNT_MASK;
            if (phase == 1 && scenario != 6) current_store = target_store;
            else if (phase != 1 && scenario != 1) current_store = (current_store + 2U) % 3U;
        }
    }
    return offset == XAXIVDMA_SR_OFFSET ? mock_status : mock_control;
}
void XAxiVdma_WriteReg(UINTPTR base, u32 offset, u32 value)
{
    (void)base;
    if (offset == XAXIVDMA_PARKPTR_OFFSET) { mock_park = value; return; }
    if (offset == XAXIVDMA_CR_OFFSET) {
        assert(capture_mode && (value & XAXIVDMA_CR_RUNSTOP_MASK));
        assert(!(value & XAXIVDMA_CR_FRMCNT_EN_MASK));
        mock_control = value;
        return;
    }
    assert(offset == XAXIVDMA_SR_OFFSET);
    if (value == XAXIVDMA_IXR_FRMCNT_MASK) {
        event_armed = 1;
        polls = 0U;
        mock_status &= ~value;
        return;
    }
    last_clear = value;
    ++clear_count;
    if (!sticky_error) mock_status &= ~value;
}
void XTime_GetTime(XTime *time) { *time = mock_time; mock_time += COUNTS_PER_SECOND / 100U; }
void Xil_DCacheInvalidateRange(INTPTR base, u32 size)
{
    (void)base;
    assert(capture_mode && phase == 1 && size == CNN_FRAME_BYTES);
    assert(!(mock_status & XAXIVDMA_SR_HALTED_MASK));
}
int xil_printf(const char *format, ...) { (void)format; return 0; }
cnn_error_t cnn_sd_mount(void)
{ assert(capture_mode || delete_mode); return mount_fail ? (cnn_error_t)-1 : CNN_OK; }

static void test_jig_deletion(void)
{
    FILINFO info;
    FIL file;
    FRESULT error;
    char path[32];
    u32 index;
    unsigned int mode;
    delete_mode = 1;
    for (mode = 0U; mode < 5U; ++mode) {
        entry_count = unlinks = 0U;
        delete_fail = (mode == 1U); mount_fail = (mode == 2U);
        read_fail = (mode == 3U); close_dir_fail = (mode == 4U);
        next_file_index[FRAME_CAPTURE_FOLDER_JIG] = 9U;
        next_file_index[FRAME_CAPTURE_FOLDER_CALIB] = 42U;
        add_entry("0:/JIG", AM_DIR);
        add_entry("0:/JIG/CAP0001.PPM", 0U);
        add_entry("0:/JIG/CAP0009.PPM", 0U);
        add_entry("0:/JIG/CAP0010.PPM", AM_DIR);
        add_entry("0:/JIG/CAP0000.PPM", 0U);
        add_entry("0:/JIG/CAP12X4.PPM", 0U);
        add_entry("0:/JIG/CAP0001.PNG", 0U);
        add_entry("0:/JIG/NOTES.TXT", 0U);
        add_entry("0:/CALIB/CAP0001.PPM", 0U);
        add_entry("0:/CAP0001.PPM", 0U);
        add_entry("0:/BOOT.BIN", 0U);
        add_entry("0:/WGT_V4.BIN", 0U);
        assert(frame_capture_sd_delete_jig() ==
               (mode == 0U ? FRAME_CAPTURE_SD_OK : FRAME_CAPTURE_SD_IO_ERROR));
        assert(next_file_index[FRAME_CAPTURE_FOLDER_CALIB] == 42U);
        assert(next_file_index[FRAME_CAPTURE_FOLDER_JIG] == (mode == 0U ? 1U : 9U));
        assert(f_stat("0:/CALIB/CAP0001.PPM", &info) == FR_OK);
        assert(f_stat("0:/CAP0001.PPM", &info) == FR_OK);
        assert(f_stat("0:/BOOT.BIN", &info) == FR_OK);
        assert(f_stat("0:/WGT_V4.BIN", &info) == FR_OK);
        assert(f_stat("0:/JIG/CAP0010.PPM", &info) == FR_OK);
        assert(f_stat("0:/JIG/CAP0000.PPM", &info) == FR_OK);
        assert(f_stat("0:/JIG/CAP12X4.PPM", &info) == FR_OK);
        assert(f_stat("0:/JIG/CAP0001.PNG", &info) == FR_OK);
        assert(f_stat("0:/JIG/NOTES.TXT", &info) == FR_OK);
        if (mode == 0U) {
            assert(unlinks == 2U);
            assert(open_next_file(&file, path, sizeof(path), FRAME_CAPTURE_FOLDER_JIG, &index, &error));
            assert(index == 1U && !strcmp(path, "0:/JIG/CAP0001.PPM"));
        }
    }
    mount_fail = delete_fail = read_fail = close_dir_fail = 0;
    entry_count = 0U;
    assert(frame_capture_sd_delete_jig() == FRAME_CAPTURE_SD_OK); /* Missing JIG. */
    add_entry("0:/JIG", 0U);
    assert(frame_capture_sd_delete_jig() == FRAME_CAPTURE_SD_IO_ERROR);
    delete_mode = 0;
}

static void test_snapshot_lifecycle(void)
{
    XAxiVdma vdma = { 3U, { 0U, 1, 1 }, 0U };
    unsigned int mode;
    for (mode = 0; mode < 9U; ++mode) {
        capture_mode = 1;
        scenario = (int)mode;
        entry_count = 0U;
        next_file_index[FRAME_CAPTURE_FOLDER_CALIB] = 1U;
        mock_time = current_store = 0U;
        mock_status = 0x00011000U; /* Real failure log: stale frame IRQ, no error. */
        expected_control = mock_control = 0x0001408BU;
        mock_park = 0x0201U; /* Preserve both original read/write references. */
        snapshots = parks = restores = unlinks = file_bytes = polls = fresh_events = 0U;
        phase = event_armed = 0;
        if (mode < 2U) {
            assert(frame_capture_sd_save(&vdma, 0x0A100000U,
                       FRAME_CAPTURE_FOLDER_CALIB) == FRAME_CAPTURE_SD_OK);
            assert(snapshots == 1U && parks == 1U && restores == 1U);
            assert(file_bytes == CNN_FRAME_BYTES + 16U && unlinks == 0U);
            assert(mock_control == expected_control && mock_park == 0x0201U);
            /* A second capture must work without a reboot or reconfiguration. */
            snapshots = parks = restores = file_bytes = fresh_events = 0U;
            phase = event_armed = 0;
            assert(frame_capture_sd_save(&vdma, 0x0A100000U,
                       FRAME_CAPTURE_FOLDER_CALIB) == FRAME_CAPTURE_SD_OK);
            assert(snapshots == 1U && parks == 1U && restores == 1U);
            assert(file_bytes == CNN_FRAME_BYTES + 16U);
            assert(mock_control == expected_control && mock_park == 0x0201U);
            assert(next_file_index[FRAME_CAPTURE_FOLDER_CALIB] == 3U);
        } else {
            assert(frame_capture_sd_save(&vdma, 0x0A100000U,
                       FRAME_CAPTURE_FOLDER_CALIB) == FRAME_CAPTURE_SD_VDMA_ERROR);
            assert(file_bytes == (mode == 8U ? CNN_FRAME_BYTES + 16U : 0U));
            assert(unlinks == 1U);
            assert(snapshots == ((mode == 5U || mode == 7U || mode == 8U) ? 1U : 0U));
            assert(restores == (mode == 2U ? 0U : 1U));
            assert(mock_control == expected_control && mock_park == 0x0201U);
        }
    }
    capture_mode = event_armed = 0;
}

static void test_vdma_preflight(void)
{
    XAxiVdma vdma = { 3U, { 0U, 1, 1 }, 0U };
    u32 fatal_errors[] = { 0x10U, 0x20U, 0x40U, 0x200U, 0x400U, 0xA0U };
    unsigned int i, before;
    mock_control = XAXIVDMA_CR_RUNSTOP_MASK;
    assert(prepare_write_channel(&vdma));
    assert(clear_count == 0U);
    /* Frame/line startup flags plus recoverable IntErr; IRQs stay untouched. */
    mock_status = 0xF990U;
    assert(prepare_write_channel(&vdma));
    assert(last_clear == 0x8990U && mock_status == 0x7000U);
    before = clear_count;
    for (i = 0; i < sizeof(fatal_errors) / sizeof(fatal_errors[0]); ++i) {
        mock_status = fatal_errors[i];
        assert(!prepare_write_channel(&vdma));
        assert(clear_count == before);
    }
    mock_status = 0x81U;
    assert(!prepare_write_channel(&vdma));
    assert(clear_count == before);
    mock_status = 0x80U;
    mock_control = 0U;
    assert(!prepare_write_channel(&vdma));
    assert(clear_count == before);
    mock_control = 1U;
    sticky_error = 1;
    assert(!prepare_write_channel(&vdma));
    sticky_error = 0;
    mock_status = 0x80U;
    assert(prepare_write_channel(&vdma));
    mock_status = 0U;
    vdma.MaxNumFrames = 2U;
    assert(!prepare_write_channel(&vdma));
    vdma.MaxNumFrames = 3U;
    vdma.WriteChannel.IsValid = 0;
    assert(!prepare_write_channel(&vdma));
    assert(!prepare_write_channel(NULL));
}

int main(void)
{
    FIL file;
    FILINFO info;
    FRESULT error;
    char path[32];
    u32 index;
    unsigned int before;

    /* Existing root captures and boot data must not affect or be overwritten by new folders. */
    add_entry("0:/CAP0001.PPM", 0U);
    add_entry("0:/BOOT.BIN", 0U);
    assert(open_next_file(&file, path, sizeof(path), FRAME_CAPTURE_FOLDER_CALIB, &index, &error));
    assert(!strcmp(path, "0:/CALIB/CAP0001.PPM") && index == 1U);
    assert(f_stat("0:/CALIB", &info) == FR_OK && (info.fattrib & AM_DIR));
    assert(open_next_file(&file, path, sizeof(path), FRAME_CAPTURE_FOLDER_JIG, &index, &error));
    assert(!strcmp(path, "0:/JIG/CAP0001.PPM") && index == 1U);
    /* Restart-style scan from 1 skips files already on the SD. */
    assert(open_next_file(&file, path, sizeof(path), FRAME_CAPTURE_FOLDER_CALIB, &index, &error));
    assert(!strcmp(path, "0:/CALIB/CAP0002.PPM") && index == 2U);
    assert(open_next_file(&file, path, sizeof(path), FRAME_CAPTURE_FOLDER_JIG, &index, &error));
    assert(!strcmp(path, "0:/JIG/CAP0002.PPM") && index == 2U);
    assert(f_stat("0:/CAP0001.PPM", &info) == FR_OK);
    assert(f_stat("0:/BOOT.BIN", &info) == FR_OK);

    before = open_count;
    assert(!open_next_file(&file, path, 5U, FRAME_CAPTURE_FOLDER_CALIB, &index, &error));
    assert(error == FR_INVALID_NAME && open_count == before);
    assert(!open_next_file(&file, path, sizeof(path), (FrameCaptureSdFolder)99, &index, &error));
    assert(error == FR_INVALID_NAME && open_count == before);
    deny_mkdir = 1;
    assert(!open_next_file(&file, path, sizeof(path), FRAME_CAPTURE_FOLDER_CALIB, &index, &error));
    assert(error == FR_DENIED && open_count == before);
    deny_mkdir = 0;

    next_file_index[FRAME_CAPTURE_FOLDER_CALIB] = CAPTURE_LAST_FILE;
    add_entry("0:/CALIB/CAP9999.PPM", 0U);
    assert(!open_next_file(&file, path, sizeof(path), FRAME_CAPTURE_FOLDER_CALIB, &index, &error));
    assert(error == FR_EXIST && open_count == before);
    /* An ordinary file named JIG must fail, not silently fall back to root. */
    entry_count = 0U;
    add_entry("0:/JIG", 0U);
    assert(!open_next_file(&file, path, sizeof(path), FRAME_CAPTURE_FOLDER_JIG, &index, &error));
    assert(error == FR_EXIST && open_count == before);
    test_vdma_preflight();
    test_snapshot_lifecycle();
    test_jig_deletion();
    puts("test_frame_capture_paths: PASS (paths, live parking, repeated captures, freeze/errors/restore)");
    return 0;
}
