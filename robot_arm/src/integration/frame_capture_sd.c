#include "frame_capture_sd.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "xaxivdma_hw.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xtime_l.h"
#include "../cnn_firmware/cnn/cnn_frame_sg.h"
#include "../cnn_firmware/cnn/cnn_weights.h"

#define CAPTURE_FRAME_COUNT 3U
#define CAPTURE_FIRST_FILE 1U
#define CAPTURE_LAST_FILE 9999U
#define CAPTURE_ROWS_PER_WRITE 8U
#define CAPTURE_WAIT_TICKS ((u64)COUNTS_PER_SECOND / 2U)
/* PG020 S2MM status: the BSP's ERR_ALL mask omits EOLLateErr (bit 15). */
#define CAPTURE_ERROR_MASK 0x00008FF0U
#define CAPTURE_SIZE_ERRORS 0x00008980U
#define CAPTURE_INTERNAL_ERROR 0x00000010U
#define CAPTURE_FRAME_SIZE_ERRORS 0x00000880U

/* The PL Bayer-to-RGB stream is Red:Blue:Green in bits [23:0]. With the
 * current little-endian VDMA layout, DDR bytes are G,B,R; P6 needs R,G,B. */
static u8 ppm_rows[CAPTURE_ROWS_PER_WRITE * CNN_FRAME_STRIDE];
/* App DDR is linker-bounded below the VDMA buffers. A private snapshot lets
 * S2MM return to circular mode before the much slower FatFs writes begin. */
static u8 captured_frame[CNN_FRAME_BYTES];
static u32 next_file_index[FRAME_CAPTURE_FOLDER_COUNT] = {
    CAPTURE_FIRST_FILE, CAPTURE_FIRST_FILE
};

const char *frame_capture_sd_folder_path(FrameCaptureSdFolder folder)
{
    switch (folder) {
    case FRAME_CAPTURE_FOLDER_CALIB: return "0:/CALIB";
    case FRAME_CAPTURE_FOLDER_JIG: return "0:/JIG";
    default: return NULL;
    }
}

FrameCaptureSdResult frame_capture_sd_delete_jig(void)
{
    DIR directory;
    FILINFO info;
    FRESULT error, close_error;
    u32 deleted = 0U;
    char path[32];
    unsigned int i;
    const char *name;

    if (cnn_sd_mount() != CNN_OK) {
        xil_printf("Frame capture: JIG delete failed after 0 files (mount)\r\n");
        return FRAME_CAPTURE_SD_IO_ERROR;
    }
    error = f_stat("0:/JIG", &info);
    if (error == FR_NO_PATH || error == FR_NO_FILE) {
        next_file_index[FRAME_CAPTURE_FOLDER_JIG] = CAPTURE_FIRST_FILE;
        xil_printf("Frame capture: JIG deleted 0 files; next CAP0001.PPM\r\n");
        return FRAME_CAPTURE_SD_OK;
    }
    if (error != FR_OK) goto failed;
    if (!(info.fattrib & AM_DIR)) {
        error = FR_INVALID_NAME;
        goto failed;
    }
    error = f_opendir(&directory, "0:/JIG");
    if (error != FR_OK) goto failed;
    for (;;) {
        error = f_readdir(&directory, &info);
        if (error != FR_OK || info.fname[0] == '\0') break;
        name = info.fname;
        if ((info.fattrib & AM_DIR) || strlen(name) != 11U ||
            strncmp(name, "CAP", 3U) || strcmp(name + 7U, ".PPM")) continue;
        for (i = 3U; i < 7U && name[i] >= '0' && name[i] <= '9'; ++i) {}
        if (i != 7U || !strncmp(name + 3U, "0000", 4U)) continue;
        (void)snprintf(path, sizeof(path), "0:/JIG/%.11s", name);
        error = f_unlink(path);
        if (error != FR_OK) break;
        ++deleted;
    }
    close_error = f_closedir(&directory);
    if (error == FR_OK) error = close_error;
    if (error != FR_OK) goto failed;
    next_file_index[FRAME_CAPTURE_FOLDER_JIG] = CAPTURE_FIRST_FILE;
    xil_printf("Frame capture: JIG deleted %lu files; next CAP0001.PPM\r\n",
               (unsigned long)deleted);
    return FRAME_CAPTURE_SD_OK;
failed:
    xil_printf("Frame capture: JIG delete failed after %lu files (FatFs=%d)\r\n",
               (unsigned long)deleted, (int)error);
    return FRAME_CAPTURE_SD_IO_ERROR;
}

static int write_exact(FIL *file, const void *bytes, UINT length, FRESULT *error)
{
    UINT written = 0U;
    *error = f_write(file, bytes, length, &written);
    if (*error == FR_OK && written != length) *error = FR_DISK_ERR;
    return *error == FR_OK;
}

static int open_next_file(FIL *file, char *path, size_t path_size,
                          FrameCaptureSdFolder folder, u32 *index, FRESULT *error)
{
    FILINFO info;
    const char *directory = frame_capture_sd_folder_path(folder);
    int path_length;

    if (directory == NULL) {
        *error = FR_INVALID_NAME;
        return 0;
    }
    *error = f_mkdir(directory);
    if (*error == FR_EXIST) {
        *error = f_stat(directory, &info);
        if (*error == FR_OK && !(info.fattrib & AM_DIR))
            *error = FR_EXIST; /* Do not accept an ordinary file named CALIB/JIG. */
    }
    if (*error != FR_OK) return 0;

    for (*index = next_file_index[folder]; *index <= CAPTURE_LAST_FILE; ++*index) {
        path_length = snprintf(path, path_size, "%s/CAP%04lu.PPM",
                               directory, (unsigned long)*index);
        if (path_length < 0 || (size_t)path_length >= path_size) {
            *error = FR_INVALID_NAME;
            return 0;
        }
        *error = f_stat(path, &info);
        if (*error == FR_NO_FILE) {
            *error = f_open(file, path, FA_WRITE | FA_CREATE_NEW);
            return *error == FR_OK;
        }
        if (*error != FR_OK) return 0;
    }
    *error = FR_EXIST;
    return 0;
}

static u32 write_status(XAxiVdma *vdma)
{
    return XAxiVdma_ReadReg(vdma->WriteChannel.ChanBase, XAXIVDMA_SR_OFFSET);
}

static int prepare_write_channel(XAxiVdma *vdma)
{
    u32 status, control, errors, clearable;
    if (vdma == NULL || vdma->MaxNumFrames != CAPTURE_FRAME_COUNT ||
        !vdma->WriteChannel.IsValid ||
        CNN_FRAME_STRIDE != CNN_IMAGE_WIDTH * CNN_PIXEL_BYTES) {
        xil_printf("Frame capture: invalid VDMA configuration (stores=%d)\r\n",
                   vdma == NULL ? -1 : (int)vdma->MaxNumFrames);
        return 0;
    }
    status = write_status(vdma);
    control = XAxiVdma_ReadReg(vdma->WriteChannel.ChanBase, XAXIVDMA_CR_OFFSET);
    xil_printf("Frame capture: info S2MM SR=0x%08x CR=0x%08x stores=%d flush=%d\r\n",
               (unsigned)status, (unsigned)control, (int)vdma->MaxNumFrames,
               vdma->WriteChannel.FlushonFsync);
    errors = status & CAPTURE_ERROR_MASK;
    clearable = CAPTURE_SIZE_ERRORS;
    /* IntErr is recoverable only alongside a frame-size error on a running
     * channel. Never clear a bus/decode error or restart a fault-halted DMA. */
    if (errors & CAPTURE_FRAME_SIZE_ERRORS) clearable |= CAPTURE_INTERNAL_ERROR;
    if ((status & XAXIVDMA_SR_HALTED_MASK) ||
        !(control & XAXIVDMA_CR_RUNSTOP_MASK) || (errors & ~clearable)) {
        xil_printf("Frame capture: VDMA not running or fatal error SR=0x%08x CR=0x%08x\r\n",
                   (unsigned)status, (unsigned)control);
        return 0;
    }
    if (errors) {
        /* Acknowledge only observed, recoverable W1C bits. In a build where
         * a flag is read-only, the following readback rejects it. Do not use
         * the BSP clear helper: its read/OR/write also clears other IRQ bits. */
        XAxiVdma_WriteReg(vdma->WriteChannel.ChanBase, XAXIVDMA_SR_OFFSET, errors);
        status = write_status(vdma);
        if (status & (CAPTURE_ERROR_MASK | XAXIVDMA_SR_HALTED_MASK)) {
            xil_printf("Frame capture: VDMA error persists after clear SR=0x%08x\r\n",
                       (unsigned)status);
            return 0;
        }
        xil_printf("Frame capture: info cleared old size flags=0x%08x; checking fresh frames\r\n",
                   (unsigned)errors);
    }
    return 1;
}

static int wait_for_fresh_frames(XAxiVdma *vdma, const char *phase)
{
    XTime start, now;
    unsigned int events = 0U;
    u32 status;

    /* Discard the old completion indication. Each subsequent W1C/observed
     * assertion represents NEW progress, including repeated frame stores.
     * IRQFrameCount is required to be 1 by snapshot_live_frame(). Two events
     * also cover an in-flight transfer when the first event denotes a start. */
    XAxiVdma_WriteReg(vdma->WriteChannel.ChanBase, XAXIVDMA_SR_OFFSET,
                     XAXIVDMA_IXR_FRMCNT_MASK);
    XTime_GetTime(&start);
    do {
        status = write_status(vdma);
        if (status & (CAPTURE_ERROR_MASK | XAXIVDMA_SR_HALTED_MASK)) break;
        if (status & XAXIVDMA_IXR_FRMCNT_MASK) {
            XAxiVdma_WriteReg(vdma->WriteChannel.ChanBase, XAXIVDMA_SR_OFFSET,
                             XAXIVDMA_IXR_FRMCNT_MASK);
            if (++events == 2U) return 1;
        }
        XTime_GetTime(&now);
    } while ((u64)(now - start) < CAPTURE_WAIT_TICKS);
    xil_printf("Frame capture: no fresh frames at %s SR=0x%08x store=%lu events=%u\r\n",
               phase, (unsigned)status,
               (unsigned long)XAxiVdma_CurrFrameStore(vdma, XAXIVDMA_WRITE), events);
    return 0;
}

static int snapshot_live_frame(XAxiVdma *vdma, u32 frame_base, u32 *selected)
{
    u32 control, park_reference, park_value, target, source_base;
    int ok = 0;
    control = XAxiVdma_ReadReg(vdma->WriteChannel.ChanBase, XAXIVDMA_CR_OFFSET);
    if (!(control & XAXIVDMA_CR_RUNSTOP_MASK) || !(control & XAXIVDMA_CR_TAIL_EN_MASK) ||
        (control & XAXIVDMA_CR_FRMCNT_EN_MASK) ||
        (control & XAXIVDMA_FRMCNT_MASK) != (1U << XAXIVDMA_FRMCNT_SHIFT)) {
        xil_printf("Frame capture: unsupported VDMA run mode CR=0x%08x\r\n",
                   (unsigned)control);
        return 0;
    }
    *selected = XAxiVdma_CurrFrameStore(vdma, XAXIVDMA_WRITE);
    if (*selected >= CAPTURE_FRAME_COUNT) {
        xil_printf("Frame capture: invalid frame store %lu\r\n", (unsigned long)*selected);
        return 0;
    }
    /* Prove the observed store has completed at least one whole transfer. */
    if (!wait_for_fresh_frames(vdma, "before park")) return 0;
    target = (*selected + 1U) % CAPTURE_FRAME_COUNT;
    park_reference = XAxiVdma_ReadReg(vdma->BaseAddr, XAXIVDMA_PARKPTR_OFFSET) &
                     XAXIVDMA_PARKPTR_WRTREF_MASK;
    /* Keep accepting the camera stream. Never clear RS or enable automatic
     * halt: stopping S2MM froze the live camera pipeline on the real boards.
     * Park on a DIFFERENT store; after two fresh events and target readback,
     * any intervening write to the selected store has also completed. */
    if (XAxiVdma_StartParking(vdma, (int)target, XAXIVDMA_WRITE) != XST_SUCCESS) {
        xil_printf("Frame capture: VDMA parking failed\r\n");
        goto restore;
    }
    if (!wait_for_fresh_frames(vdma, "park settle")) goto restore;
    if (XAxiVdma_CurrFrameStore(vdma, XAXIVDMA_WRITE) != target) {
        xil_printf("Frame capture: VDMA park target not reached\r\n");
        goto restore;
    }
    source_base = frame_base + *selected * CNN_FRAME_BYTES;
    Xil_DCacheInvalidateRange((INTPTR)source_base, CNN_FRAME_BYTES);
    memcpy(captured_frame, (const void *)(UINTPTR)source_base, sizeof(captured_frame));
    ok = !(write_status(vdma) & (CAPTURE_ERROR_MASK | XAXIVDMA_SR_HALTED_MASK));
    if (!ok) xil_printf("Frame capture: VDMA error during snapshot\r\n");

restore:
    /* Restore only the changed mode/reference fields, preserving a hardware
     * fault's cleared RS bit. No stop/start or reset is issued on any path. */
    XAxiVdma_StopParking(vdma, XAXIVDMA_WRITE);
    park_value = XAxiVdma_ReadReg(vdma->BaseAddr, XAXIVDMA_PARKPTR_OFFSET);
    XAxiVdma_WriteReg(vdma->BaseAddr, XAXIVDMA_PARKPTR_OFFSET,
                     (park_value & ~XAXIVDMA_PARKPTR_WRTREF_MASK) | park_reference);
    if (!wait_for_fresh_frames(vdma, "after circular restore")) return 0;
    if (ok) xil_printf("Frame capture: info live snapshot store=%lu; fresh frames after restore\r\n",
                       (unsigned long)*selected);
    return ok;
}

FrameCaptureSdResult frame_capture_sd_save(XAxiVdma *vdma, u32 frame_base,
                                         FrameCaptureSdFolder folder)
{
    FIL file;
    FRESULT error = FR_OK;
    FrameCaptureSdResult result = FRAME_CAPTURE_SD_IO_ERROR;
    u32 index, selected, y, row, x;
    char path[32] = "";
    char header[32];
    int header_length;
    int opened = 0;

    if (!prepare_write_channel(vdma)) {
        return FRAME_CAPTURE_SD_VDMA_ERROR;
    }
    if (cnn_sd_mount() != CNN_OK) return FRAME_CAPTURE_SD_IO_ERROR;
    if (!open_next_file(&file, path, sizeof(path), folder, &index, &error)) {
        xil_printf("Frame capture: folder/file open failed (%d)\r\n", (int)error);
        return FRAME_CAPTURE_SD_IO_ERROR;
    }
    opened = 1;

    if (!snapshot_live_frame(vdma, frame_base, &selected)) {
        result = FRAME_CAPTURE_SD_VDMA_ERROR;
        goto cleanup;
    }

    header_length = snprintf(header, sizeof(header), "P6\n%u %u\n255\n",
                             (unsigned)CNN_IMAGE_WIDTH,
                             (unsigned)CNN_IMAGE_HEIGHT);
    if (header_length <= 0 || (size_t)header_length >= sizeof(header) ||
        !write_exact(&file, header, (UINT)header_length, &error)) goto cleanup;

    for (y = 0U; y < CNN_IMAGE_HEIGHT; y += CAPTURE_ROWS_PER_WRITE) {
        const u8 *source = &captured_frame[y * CNN_FRAME_STRIDE];
        for (row = 0U; row < CAPTURE_ROWS_PER_WRITE; ++row) {
            u8 *destination = &ppm_rows[row * CNN_FRAME_STRIDE];
            for (x = 0U; x < CNN_IMAGE_WIDTH; ++x) {
                destination[3U * x] = source[3U * x + 2U];
                destination[3U * x + 1U] = source[3U * x];
                destination[3U * x + 2U] = source[3U * x + 1U];
            }
            source += CNN_FRAME_STRIDE;
        }
        if (!write_exact(&file, ppm_rows, (UINT)sizeof(ppm_rows), &error))
            goto cleanup;
    }
    error = f_sync(&file);
    if (error == FR_OK) {
        result = wait_for_fresh_frames(vdma, "after SD save") ?
                 FRAME_CAPTURE_SD_OK : FRAME_CAPTURE_SD_VDMA_ERROR;
    }

cleanup:
    if (opened) {
        FRESULT close_error = f_close(&file);
        if (close_error != FR_OK) {
            error = close_error;
            if (result != FRAME_CAPTURE_SD_VDMA_ERROR) result = FRAME_CAPTURE_SD_IO_ERROR;
        }
        if (result != FRAME_CAPTURE_SD_OK) (void)f_unlink(path);
    }
    if (result == FRAME_CAPTURE_SD_OK) {
        next_file_index[folder] = index + 1U;
        xil_printf("Frame capture: saved %s, frame store %lu, 1280x720 P6\r\n",
                   path, (unsigned long)selected);
    } else {
        xil_printf("Frame capture: failed (result=%d FatFs=%d)\r\n", (int)result, (int)error);
    }
    return result;
}
