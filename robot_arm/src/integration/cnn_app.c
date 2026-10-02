/*
 * Zybo Z7-20 camera, HDMI, CNN, and keypoint-overlay application.
 * Camera format: OV5640 RAW10, 1280x720 at 60 Hz over MIPI CSI-2.
 * Terminal: 921600 8N1 with ROBOT_TRACE, otherwise 115200 8N1.
 */

#include "xil_types.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "xaxivdma.h"
#include "xuartps_hw.h"
#include "xil_exception.h"
#include "xscugic.h"

#include "integration/cnn_app.h"
#include "integration/platform_vitis.h"
#include "integration/trace.h"
#include "integration/stereo_board.h"
#include "input_pose_cnn.h"
#include "cnn_console.h"
#include "cnn_app_event.h"
#include "frame_capture_sd.h"
#include "../cnn_firmware/cam_gpio/cam_gpio.h"
#include "../cnn_firmware/iic_sccb_cfg/iic_sccb_cfg.h"
#include "../cnn_firmware/ov5640/OV5640.h"
#include "../cnn_firmware/mipi_rx/mipi_rx.h"
#include "../cnn_firmware/gamma/gamma.h"
#include "../cnn_firmware/cam_ae/cam_ae.h"
#include "../cnn_firmware/vdma_api/vdma_api.h"
#include "../cnn_firmware/display_ctrl_hdmi/display_ctrl.h"
#include "../cnn_firmware/keypoint_overlay/keypoint_overlay.h"
#include "../cnn_firmware/cnn/cnn_bringup.h"
#include "../cnn_firmware/cnn/cnn_diag.h"
#include "../cnn_firmware/cnn/cnn_hw.h"
#include "../cnn_firmware/camera_tracking/camera_tracking_app.h"

#define VDMA_ID         XPAR_AXIVDMA_0_DEVICE_ID
#define DISP_VTC_ID     XPAR_VTC_0_DEVICE_ID
#define DISP_DYNCLK_ID  XPAR_DYNCLK_0_DEVICE_ID

/* Three 1280x720 RGB888 buffers at DDR base + 160 MiB. */
#define FRAME_BUFFER_ADDR \
    (XPAR_PS7_DDR_0_S_AXI_BASEADDR + 0x0A000000U)

static XAxiVdma vdma;
static DisplayCtrl display_ctrl;
static VideoMode video_mode;
static cnn_bringup_t cnn_ctx;
static int cnn_continuous_mode;
static int cnn_single_pending;
static u32 cnn_continuous_frames;
static camera_tracking_app_t camera_tracker;
/* x 요청 시 진행 중 프레임이 끝나거나 timeout된 뒤 soft reset을 수행한다. */
static int cnn_stopping;
/* timeout/HW fault/비정상 DONE 이후 x로 복구하기 전까지 유지된다. */
static int cnn_fault_latched;
static int frame_capture_requested;
static int frame_capture_delete_jig;
static int frame_capture_resume_auto;
static FrameCaptureSdFolder frame_capture_folder = FRAME_CAPTURE_FOLDER_CALIB;

void cnn_app_report_pwm_result(unsigned enabled, const char *result, const char *mode)
{
    xil_printf("[PWM] enabled=%u result=%s mode=%s arm=RIGHT_CH0_CH4; "
               "no position feedback; stereo exposure gate unchanged\r\n",
               enabled, result, mode);
}

void cnn_app_report_async_result(unsigned enabled, unsigned pwm_enabled,
                                const char *result, const char *mode)
{
    xil_printf("[ST] async_test=%u pwm=%u result=%s mode=%s; EXPERIMENTAL binocular "
               "control, exposure synchronization NOT verified\r\n",
               enabled, pwm_enabled, result, mode);
}

void cnn_app_report_control_result(CnnAppEvent event,
                                   int accepted,
                                   const char *mode,
                                   const char *reason,
                                   unsigned long record_count,
                                   unsigned long replay_count,
                                   const char *record_source,
                                   const char *replay_source)
{
    const char *command = event == CNN_APP_EVENT_RECORD_TOGGLE
        ? "RECORD" : "PLAY";
    xil_printf("[RR] %s %s: mode=%s reason=%s record=%lu(%s) replay=%lu(%s)\r\n",
               command, accepted ? "accepted" : "rejected",
               mode != NULL ? mode : "UNKNOWN",
               reason != NULL ? reason : "UNKNOWN",
               record_count,
               record_source != NULL ? record_source : "UNKNOWN",
               replay_count,
               replay_source != NULL ? replay_source : "UNKNOWN");
}

static void camera_tracker_service_hook(void *context)
{
    camera_tracking_app_service((camera_tracking_app_t *)context);
}

/* 프레임이 DDR 가중치/SG를 사용 중이거나 곧 사용할 수 있으면 참이다. */
static int cnn_busy(void)
{
    return cnn_ctx.running || cnn_continuous_mode ||
           cnn_single_pending || cnn_stopping ||
           (cnn_hw_status()&CNN_STATUS_BUSY);
}

static int cnn_reject_if_busy(char c)
{
    if(cnn_fault_latched) {
        xil_printf("CNN '%c' rejected: fault latched; press 'x' to "
                   "soft-reset CNN first\r\n",c);
        return 1;
    }
    if(cnn_busy()) {
        xil_printf("CNN '%c' rejected: inference busy (running=%d auto=%d "
                   "single=%d stopping=%d); press 'a' to stop continuous "
                   "mode, wait for the frame to finish, then retry\r\n",
                   c,cnn_ctx.running,cnn_continuous_mode,
                   cnn_single_pending,cnn_stopping);
        return 1;
    }
    return 0;
}

static void cnn_finish_soft_reset(void)
{
    cnn_error_t result;

    result=cnn_bringup_recover(&cnn_ctx);
    if(result!=CNN_OK) {
        cnn_stopping=0;
        cnn_fault_latched=1;
        TRACE_CNN_ERROR(cnn_ctx.next_frame_id,result,cnn_bringup_irq_count());
        xil_printf("CNN soft reset failed: %s (%d)\r\n",
                   cnn_error_string(result),(int)result);
    }
    else {
        cnn_stopping=0;
        cnn_fault_latched=0;
        xil_printf("CNN soft reset PASS; inference idle\r\n");
    }
}

/* 비동기 start/service 오류: 한 줄 요약 + fault 시 원래 진단 덤프 1회. */
static void cnn_report_async_error(const char *where, cnn_error_t result)
{
    int hw_suspect=(result==CNN_ERR_TIMEOUT || result==CNN_ERR_HW_FAULT ||
                    result==CNN_ERR_INTERRUPT);

    cnn_continuous_mode=0;
    cnn_single_pending=0;
    TRACE_CNN_ERROR(cnn_ctx.next_frame_id,result,cnn_bringup_irq_count());
    xil_printf("CNN %s failed: %s (%d) frame=%lu irq=%lu; inference stopped\r\n",
               where,cnn_error_string(result),(int)result,
               (unsigned long)cnn_ctx.next_frame_id,
               (unsigned long)cnn_bringup_irq_count());
    if(!hw_suspect) return;
    if(!cnn_fault_latched) {
        xil_printf("CNN fault diagnostic begin\r\n");
        cnn_diag_dump();
        cnn_fault_latched=1;
    }
    if(!cnn_stopping)
        xil_printf("CNN fault latched; press 'x' to soft-reset before 'w'/'g'/'s'/'a'\r\n");
}

/* -------------------------------------------------------------------------
 * CNN command menu
 * ------------------------------------------------------------------------- */
static void menu_help(void)
{
    xil_printf("\r\n--- CNN bring-up keys ----------------\r\n");
    xil_printf("  p : probe CNN identity\r\n");
    xil_printf("  w : load and verify SD weights (inference idle only)\r\n");
    xil_printf("  g : build/validate image SG descriptors (inference idle only)\r\n");
    xil_printf("  s : run one frame using completion IRQ (rejected while busy)\r\n");
    xil_printf("  a : start/stop continuous IRQ-driven inference\r\n");
    xil_printf("  q : mute/resume all UART output (CNN and robot keep running)\r\n");
    xil_printf("  z : mute/resume robot TRACE only (CNN logs remain)\r\n");
    xil_printf("  t : print CNN status and last elapsed time\r\n");
    xil_printf("  r : print last CNN result\r\n");
    xil_printf("  d : CNN diagnostic dump\r\n");
    xil_printf("  x : stop inference and soft-reset CNN (deferred until the active frame ends)\r\n");
    xil_printf("  o : dump overlay/color registers\r\n");
    xil_printf("  b : toggle HDMI overlay (robot 6 points / body joints)\r\n");
    xil_printf("  c : force grouped-color overlay test\r\n");
    xil_printf("  n : toggle CNN green-marker detection\r\n");
    xil_printf("  y : toggle CNN yellow-marker detection\r\n");
    xil_printf("  m : edit RGBY margins, brightness and min count\r\n");
    xil_printf("  B : toggle all body joints on HDMI\r\n");
    xil_printf("  J : show/hide one body joint (indices 5..16)\r\n");
    xil_printf("  1 : select SD CALIB folder (checkerboard; boot default)\r\n");
    xil_printf("  2 : select SD JIG folder (marker validation)\r\n");
    xil_printf("  L : probe JIG photo deletion support (no deletion)\r\n");
    xil_printf("  D : delete all JIG/CAPnnnn.PPM photos; restart JIG numbering\r\n");
    xil_printf("  C : save one completed frame in %s/CAPnnnn.PPM\r\n",
               frame_capture_sd_folder_path(frame_capture_folder));
    xil_printf("  %c : toggle robot motion recording\r\n",
               CNN_APP_UART_CMD_RECORD);
    xil_printf("  %c : start/stop robot motion replay\r\n",
               CNN_APP_UART_CMD_PLAY);
    camera_tracking_app_print_help();
    xil_printf("  ? : help\r\n");
    xil_printf("  E : enable robot PWM (LIVE, stationary approved command only; arm may move)\r\n");
    xil_printf("  X : disable robot PWM (torque released; support the arm)\r\n");
    xil_printf("  V : print robot PWM state (not camera PWM)\r\n");
    xil_printf("  A : enable EXPERIMENTAL asynchronous binocular control (RIGHT, PWM ON, LIVE)\r\n");
    xil_printf("  S : disable asynchronous test inputs; last approved goal still completes\r\n");
    xil_printf("  T : print asynchronous test mode (not exposure synchronization)\r\n");
}

static void menu_run(void)
{
    cnn_error_t result=CNN_OK;
    int report_pass=0;
    char c;

    if(cnn_console_active()) {
        cnn_console_poll();
        return;
    }
    if(!XUartPs_IsReceiveData(STDIN_BASEADDRESS))
        return;

    c=(char)XUartPs_RecvByte(STDIN_BASEADDRESS);
    if(cnn_app_control_event_post_uart(c))
        return;
    switch(c) {
    case 'p':
        cnn_diag_probe_report();
        break;
    case 'w':
        if(cnn_reject_if_busy(c)) break;
        result=cnn_bringup_load_weights(&cnn_ctx);
        report_pass=1;
        break;
    case 'g':
        if(cnn_reject_if_busy(c)) break;
        result=cnn_bringup_prepare_frame(&cnn_ctx);
        report_pass=1;
        break;
    case 's':
        /* 진행 중 연속 프레임을 단일 프레임으로 재해석하지 않는다. */
        if(cnn_reject_if_busy(c)) break;
        cnn_single_pending=1;
        break;
    case 'a':
        if(cnn_continuous_mode) {
            cnn_continuous_mode=0;
            xil_printf("CNN continuous mode: OFF after %lu frame(s)%s\r\n",
                       (unsigned long)cnn_continuous_frames,
                       cnn_ctx.running?"; active frame will finish":"");
        }
        else if(cnn_stopping) {
            xil_printf("CNN 'a' rejected: soft reset pending; wait for "
                       "'CNN soft reset PASS'\r\n");
        }
        else if(cnn_single_pending) {
            xil_printf("CNN 'a' rejected: single frame pending; wait for "
                       "it to finish\r\n");
        }
        else if(cnn_fault_latched) {
            xil_printf("CNN 'a' rejected: fault latched; press 'x' first\r\n");
        }
        else {
            /* 직전 연속 프레임이 아직 진행 중이면 그대로 이어서 사용한다. */
            cnn_continuous_mode=1;
            cnn_continuous_frames=0U;
            xil_printf("CNN continuous mode: ON; press 'a' to stop after the current frame\r\n");
        }
        break;
    case 't':
        cnn_bringup_print_status(&cnn_ctx);
        xil_printf("Frame capture: folder=%s, pending=%d\r\n",
                   frame_capture_sd_folder_path(frame_capture_folder),
                   frame_capture_requested);
        xil_printf("CNN app state: running=%d auto=%d single=%d stopping=%d "
                   "fault=%d\r\n",cnn_ctx.running,cnn_continuous_mode,
                   cnn_single_pending,cnn_stopping,cnn_fault_latched);
        camera_tracking_app_print_mode(&camera_tracker);
        xil_printf("HDMI overlay: %s\r\n",
                   cnn_bringup_overlay_robot_only() ? "robot 6 points" :
                                                    "body joints");
        break;
    case 'r':
        cnn_bringup_print_last_result(&cnn_ctx);
        break;
    case 'd':
        cnn_diag_dump();
        break;
    case 'x':
        frame_capture_requested=0;
        cnn_continuous_mode=0;
        cnn_single_pending=0;
        if(cnn_ctx.running) {
            /* 실행 중 DMA/SG는 건드리지 않고 다음 service에서 마무리한다. */
            cnn_stopping=1;
            xil_printf("CNN stopping: no new frames; soft reset after the "
                       "active frame finishes or times out\r\n");
        }
        else {
            cnn_finish_soft_reset();
        }
        break;
    case 'o':
        kpo_debug_dump();
        break;
    case 'q':
        if(platform_uart_output_enabled()) {
            TRACE_SET_OUTPUT_ENABLED(0);
            platform_uart_set_output_enabled(0);
        }
        else {
            platform_uart_set_output_enabled(1);
            TRACE_SET_OUTPUT_ENABLED(1);
            xil_printf("UART output resumed; CNN and robot control stayed active\r\n");
        }
        break;
    case 'z':
#ifdef ROBOT_TRACE
        TRACE_SET_ROBOT_OUTPUT_ENABLED(!TRACE_ROBOT_OUTPUT_ENABLED());
        xil_printf("Robot TRACE: %s; CNN logs remain active\r\n",
                   TRACE_ROBOT_OUTPUT_ENABLED() ? "ON" : "OFF");
#else
        xil_printf("Robot TRACE unavailable in this build\r\n");
#endif
        break;
    case 'b':
        cnn_bringup_set_overlay_robot_only(
            !cnn_bringup_overlay_robot_only());
        xil_printf("HDMI overlay: %s (next CNN result)\r\n",
                   cnn_bringup_overlay_robot_only() ? "robot 6 points" :
                                                    "body joints");
        break;
    case 'c':
        kpo_test_group_colors();
        break;
    case 'n':
        result=cnn_hw_set_green_detection(
            !cnn_hw_green_detection_enabled());
        if(result==CNN_OK) {
            xil_printf("CNN green-marker detection: %s\r\n",
                       cnn_hw_green_detection_enabled()?"ON":"OFF");
        }
        break;
    case 'y': {
        u32 mask=cnn_hw_get_color_enable();
        result=cnn_hw_set_color_enable(mask^CNN_COLOR_ENABLE_YELLOW);
        if(result==CNN_OK)
            xil_printf("CNN yellow-marker detection: %s\r\n",
                (cnn_hw_get_color_enable()&CNN_COLOR_ENABLE_YELLOW)?"ON":"OFF");
        break;
    }
    case 'm':
        if(cnn_continuous_mode) {
            cnn_continuous_mode=0;
            xil_printf("CNN continuous mode: OFF for color settings\r\n");
        }
        cnn_console_start_color();
        break;
    case 'B':
        cnn_bringup_set_skeleton_mask(
            cnn_bringup_get_skeleton_mask()?0U:CNN_ROBOT_SKELETON_MASK);
        xil_printf("CNN body skeleton mask=0x%05x\r\n",
                   (unsigned)cnn_bringup_get_skeleton_mask());
        break;
    case 'J':
        cnn_console_start_joint();
        break;
    case '1':
    case '2':
        if(frame_capture_requested) {
            xil_printf("Frame capture: folder change rejected; wait for pending capture\r\n");
        }
        else {
            frame_capture_folder=(c=='1') ? FRAME_CAPTURE_FOLDER_CALIB :
                                           FRAME_CAPTURE_FOLDER_JIG;
            xil_printf("Frame capture: folder=%s; press 'C' to save\r\n",
                       frame_capture_sd_folder_path(frame_capture_folder));
        }
        break;
    case 'L':
        if(frame_capture_requested || cnn_fault_latched || cnn_stopping) {
            xil_printf("Frame capture: JIG delete rejected; capture/fault/stop pending\r\n");
        }
        else {
            xil_printf("Frame capture: JIG delete ready\r\n");
        }
        break;
    case 'D':
    case 'C':
        if(frame_capture_requested) {
            xil_printf("Frame capture: already pending\r\n");
        }
        else if(cnn_fault_latched || cnn_stopping) {
            xil_printf("Frame capture: CNN fault/stop pending; press 'x' first\r\n");
        }
        else {
            frame_capture_resume_auto=cnn_continuous_mode;
            cnn_continuous_mode=0;
            frame_capture_requested=1;
            frame_capture_delete_jig=(c=='D');
            if(frame_capture_delete_jig) {
                xil_printf("Frame capture: JIG delete pending after active CNN frame\r\n");
            }
            else {
                xil_printf("Frame capture: pending in %s after active CNN frame\r\n",
                           frame_capture_sd_folder_path(frame_capture_folder));
            }
        }
        break;
    case 'j':
        cnn_console_start_camera(&camera_tracker);
        break;
    case '?':
        menu_help();
        break;
    default:
        if(!camera_tracking_app_handle_key(&camera_tracker,c))
            return;
        break;
    }

    if(result!=CNN_OK) {
        TRACE_CNN_ERROR(cnn_ctx.next_frame_id,result,cnn_bringup_irq_count());
        xil_printf("CNN command failed: %s (%d)\r\n",
                   cnn_error_string(result),(int)result);
    }
    else if(report_pass) {
        xil_printf("CNN command PASS\r\n");
    }
}

static void cnn_continuous_step(void)
{
    cnn_error_t result;

    if(!cnn_ctx.running) {
        if(cnn_stopping) {
            cnn_finish_soft_reset();
            return;
        }
        if(!cnn_continuous_mode && !cnn_single_pending) return;
        result=cnn_bringup_start(&cnn_ctx,cnn_single_pending);
        if(result!=CNN_OK)
            cnn_report_async_error("start",result);
        return;
    }

    result=cnn_bringup_service(&cnn_ctx);
    if(result==CNN_PENDING) return;
    if(result!=CNN_OK) {
        cnn_report_async_error("frame",result);
        /* timeout/fault 후에도 x 요청이 있었다면 여기서 복구를 마친다. */
        if(cnn_stopping) cnn_finish_soft_reset();
        return;
    }

    camera_tracking_app_on_result(&camera_tracker,&cnn_ctx.last_result);
#if defined(ROBOT_STEREO_LEFT) || defined(ROBOT_STEREO_RIGHT)
    (void)stereo_board_on_result(&cnn_ctx.last_result,NULL);
#else
    (void)input_pose_cnn_publish(&cnn_ctx.last_result);
#endif
    TRACE_CN(cnn_ctx.last_result.frame_id,cnn_ctx.last_result.result_seq,
             cnn_bringup_irq_count(),cnn_ctx.last_elapsed_us,
             cnn_ctx.last_result.joint_flags,input_pose_cnn_overwritten());
    TRACE_CAM(cnn_ctx.last_result.frame_id,camera_tracker.tracker.last_observation,
              camera_tracker.gimbal.current_pulse_us[CAMERA_GIMBAL_PAN],
              camera_tracker.gimbal.current_pulse_us[CAMERA_GIMBAL_TILT],
              camera_tracker.gimbal.target_pulse_us[CAMERA_GIMBAL_PAN],
              camera_tracker.gimbal.target_pulse_us[CAMERA_GIMBAL_TILT]);
    cnn_continuous_frames++;
    if(cnn_single_pending) {
        cnn_single_pending=0;
        xil_printf("CNN timing: %lu.%03lu ms by completion IRQ; hardware cycles=%lu\r\n",
                   (unsigned long)(cnn_ctx.last_elapsed_us/1000U),
                   (unsigned long)(cnn_ctx.last_elapsed_us%1000U),
                   (unsigned long)cnn_ctx.last_result.cycle_count);
        cnn_diag_print_result(&cnn_ctx.last_result);
        kpo_debug_dump();
        xil_printf("CNN command PASS\r\n");
    }
#ifndef ROBOT_TRACE
    xil_printf("CNN auto #%lu frame=%lu seq=%lu time=%lu.%03lu ms cycles=%lu "
               "valid=0x%05x R/B/G/Y=%08x/%08x/%08x/%08x\r\n",
               (unsigned long)cnn_continuous_frames,
               (unsigned long)cnn_ctx.last_result.frame_id,
               (unsigned long)cnn_ctx.last_result.result_seq,
               (unsigned long)(cnn_ctx.last_elapsed_us/1000U),
               (unsigned long)(cnn_ctx.last_elapsed_us%1000U),
               (unsigned long)cnn_ctx.last_result.cycle_count,
               (unsigned int)cnn_ctx.last_result.joint_flags,
               (unsigned int)cnn_ctx.last_result.red_marker,
               (unsigned int)cnn_ctx.last_result.blue_marker,
               (unsigned int)cnn_ctx.last_result.green_marker,
               (unsigned int)cnn_ctx.last_result.yellow_marker);
#endif
}

static void cnn_auto_start(void)
{
    cnn_error_t result;

    xil_printf("\r\nCNN automatic startup: w -> camera FIXED -> a "
               "(SD CSV logging disabled)\r\n");
    result=cnn_bringup_load_weights(&cnn_ctx);
    if(result!=CNN_OK) {
        TRACE_CNN_ERROR(cnn_ctx.next_frame_id,result,cnn_bringup_irq_count());
        xil_printf("CNN automatic startup stopped: %s (%d)\r\n",
                   cnn_error_string(result),(int)result);
        xil_printf("UART menu remains available; fix the SD card and press 'w', 'a'.\r\n");
        return;
    }

    cnn_continuous_frames=0U;
    cnn_continuous_mode=1;
    xil_printf("CNN continuous mode: ON (automatic boot)\r\n");
    xil_printf("CNN automatic startup PASS; press 'a' to stop inference, "
               "'u' to start camera tracking, 'f' to freeze camera.\r\n");
}

/* -------------------------------------------------------------------------
 * Hardware initialization
 * ------------------------------------------------------------------------- */
static void print_banner(void)
{
    xil_printf("\r\n\r\n");
    xil_printf("=================================================\r\n");
    xil_printf(" Zybo Z7-20 CNN camera platform, 720p60\r\n");
    xil_printf("=================================================\r\n");
}

static int initialize_camera_capture(void)
{
    if(cam_gpio_init()!=CAM_GPIO_OK) {
        xil_printf("EMIO GPIO did not come up. Stopping.\r\n");
        return 0;
    }

    mipi_rx_reset();
    mipi_rx_print_version();

    if(OV5640_Init()!=IIC_SCCB_OK) {
        xil_printf("SCCB bus did not come up. Stopping.\r\n");
        return 0;
    }

    OV5640_PowerCycle();
    if(OV5640_InitSensor()!=0) {
        xil_printf("OV5640 detection failed. Stopping.\r\n");
        return 0;
    }
    xil_printf("OV5640 detected successfully.\r\n");

    if(iic_sccb_error_count()) {
        xil_printf("WARNING: %d SCCB error(s) during initialization\r\n",
                   (int)iic_sccb_error_count());
        iic_sccb_clear_errors();
    }

    video_mode=VMODE_1280x720;
    if (run_vdma_frame_buffer(&vdma,VDMA_ID,video_mode.width,video_mode.height,
                             FRAME_BUFFER_ADDR,0,0,BOTH) != XST_SUCCESS) {
        xil_printf("Camera VDMA initialization failed. Stopping.\r\n");
        return 0;
    }

    mipi_rx_enable();
    gamma_init();
    OV5640_SetMode720p();
    OV5640_SetAWB(AWB_ADVANCED);
    cam_ae_init();
    return 1;
}

static int initialize_display(void)
{
    if(DisplayInitialize(&display_ctrl,DISP_VTC_ID,DISP_DYNCLK_ID)
            !=XST_SUCCESS) {
        xil_printf("HDMI output initialization failed. Stopping.\r\n");
        return 0;
    }

    DisplaySetMode(&display_ctrl,&video_mode);
    if(DisplayStart(&display_ctrl)!=XST_SUCCESS) {
        xil_printf("Pixel-clock generation failed. Stopping.\r\n");
        return 0;
    }
    return 1;
}

static int initialize_cnn(void)
{
    cnn_error_t result;

    kpo_init();
    cnn_bringup_init(&cnn_ctx,&vdma,FRAME_BUFFER_ADDR);
    result=cnn_bringup_interrupt_init(platform_vitis_gic());
    if(result!=CNN_OK) {
        xil_printf("CNN interrupt initialization failed: %s (%d)\r\n",
                   cnn_error_string(result),(int)result);
        return 0;
    }

    cnn_diag_probe_report();
    result=cnn_hw_initialize_color_defaults();
    if(result!=CNN_OK) {
        xil_printf("CNN color-margin initialization failed: %s (%d)\r\n",
                   cnn_error_string(result),(int)result);
        return 0;
    }
    xil_printf("CNN color margins initialized R/G/B=%u/%u/%u\r\n",
               (unsigned int)CNN_DEFAULT_RED_MARGIN,
               (unsigned int)CNN_DEFAULT_GREEN_MARGIN,
               (unsigned int)CNN_DEFAULT_BLUE_MARGIN);
    if(camera_tracking_app_init(&camera_tracker)!=XST_SUCCESS) {
        xil_printf("Camera gimbal initialization failed. Stopping.\r\n");
        return 0;
    }
    cnn_bringup_set_idle_hook(&cnn_ctx,camera_tracker_service_hook,
                              &camera_tracker);
    return 1;
}

static void print_video_status(void)
{
    u16 width;
    u16 height;

    OV5640_GetImageInfo(&width,&height);
    xil_printf("camera  : %dx%d RAW10, 2 lane MIPI\r\n",width,height);
    xil_printf("display : %dx%d @ %d Hz\r\n",video_mode.width,
               video_mode.height,(int)display_ctrl.pxlFreqHz);
    xil_printf("frame buffer: 0x%08X\r\n",(unsigned int)FRAME_BUFFER_ADDR);
}

int cnn_app_init(void)
{
    cnn_app_control_event_reset();
    print_banner();

    if(!initialize_camera_capture())
        return 1;
    if(!initialize_display())
        return 1;
    if(!initialize_cnn())
        return 1;

    print_video_status();
    menu_help();
    cnn_auto_start();

    return 0;
}

void cnn_app_service(void)
{
    menu_run();
    camera_tracking_app_service(&camera_tracker);
    cnn_continuous_step();
    if(frame_capture_requested && (cnn_fault_latched || cnn_stopping)) {
        frame_capture_requested=0;
        xil_printf("Frame capture: cancelled after CNN fault/stop\r\n");
        return;
    }
    if(frame_capture_requested && !cnn_ctx.running && !cnn_single_pending &&
       !(cnn_hw_status()&CNN_STATUS_BUSY)) {
        FrameCaptureSdResult result;
        frame_capture_requested=0;
        result=frame_capture_delete_jig ? frame_capture_sd_delete_jig() :
               frame_capture_sd_save(&vdma,FRAME_BUFFER_ADDR,frame_capture_folder);
        if(result!=FRAME_CAPTURE_SD_VDMA_ERROR && frame_capture_resume_auto) {
            cnn_continuous_mode=1;
            xil_printf("CNN continuous mode: resumed after frame capture\r\n");
        }
    }
}
