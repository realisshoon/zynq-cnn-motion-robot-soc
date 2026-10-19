#ifndef FRAME_CAPTURE_SD_H
#define FRAME_CAPTURE_SD_H

#include "xaxivdma.h"
#include "xil_types.h"

typedef enum {
    FRAME_CAPTURE_SD_OK = 0,
    FRAME_CAPTURE_SD_IO_ERROR,
    FRAME_CAPTURE_SD_VDMA_ERROR
} FrameCaptureSdResult;

typedef enum {
    FRAME_CAPTURE_FOLDER_CALIB = 0,
    FRAME_CAPTURE_FOLDER_JIG,
    FRAME_CAPTURE_FOLDER_COUNT
} FrameCaptureSdFolder;

/* Absolute SD directory, or NULL for an invalid folder. No SD I/O. */
const char *frame_capture_sd_folder_path(FrameCaptureSdFolder folder);

/* Foreground-only diagnostic command. The caller must first quiesce CNN
 * inference; S2MM stays running and briefly parks on another frame store
 * while the protected completed store is copied. Circular mode is restored
 * and fresh incoming frames are verified before SD writes.
 * Creates the selected directory on demand; numbering is independent per folder. */
FrameCaptureSdResult frame_capture_sd_save(XAxiVdma *vdma, u32 frame_base,
                                         FrameCaptureSdFolder folder);

/* Delete only regular JIG/CAP0001.PPM..CAP9999.PPM files; no recursion.
 * Foreground SD I/O; caller must serialize with capture/other SD operations.
 * Resets JIG numbering to 1 only after successful completion. */
FrameCaptureSdResult frame_capture_sd_delete_jig(void);

#endif
