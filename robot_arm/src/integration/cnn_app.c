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
#include "input_pose_cnn.h"
#include "cnn_console.h"
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
#include "../cnn_firmware/cnn/cnn_csv_logger.h"
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
static cnn_csv_logger_t cnn_logger;

static void camera_tracker_service_hook(void *context)
{
    camera_tracking_app_service((camera_tracking_app_t *)context);
}

/* -------------------------------------------------------------------------
 * CNN command menu
 * ------------------------------------------------------------------------- */
static void menu_help(void)
{
    xil_printf("\r\n--- CNN bring-up keys ----------------\r\n");
    xil_printf("  p : probe CNN identity\r\n");
    xil_printf("  w : load and verify SD weights\r\n");
    xil_printf("  g : build/validate image SG descriptors\r\n");
    xil_printf("  s : run one frame using completion IRQ\r\n");
    xil_printf("  a : start/stop continuous IRQ-driven inference\r\n");
    xil_printf("  t : print CNN status and last elapsed time\r\n");
    xil_printf("  r : print last CNN result\r\n");
    xil_printf("  d : CNN diagnostic dump\r\n");
    xil_printf("  x : stop continuous mode and soft-reset CNN\r\n");
    xil_printf("  o : dump overlay/color registers\r\n");
    xil_printf("  c : force grouped-color overlay test\r\n");
    xil_printf("  n : toggle CNN green-marker detection\r\n");
    xil_printf("  m : enter red/green/blue detection margins\r\n");
    xil_printf("  l : start/stop SD CSV logging\r\n");
    camera_tracking_app_print_help();
    xil_printf("  ? : help\r\n");
}

static void menu_run(void)
{
    cnn_error_t result=CNN_OK;
    char c;

    if(cnn_console_active()) {
        cnn_console_poll();
        return;
    }
    if(!XUartPs_IsReceiveData(STDIN_BASEADDRESS))
        return;

    c=(char)XUartPs_RecvByte(STDIN_BASEADDRESS);
    switch(c) {
    case 'p':
        cnn_diag_probe_report();
        break;
    case 'w':
        result=cnn_bringup_load_weights(&cnn_ctx);
        break;
    case 'g':
        result=cnn_bringup_prepare_frame(&cnn_ctx);
        break;
    case 's':
        cnn_continuous_mode=0;
        cnn_single_pending=1;
        break;
    case 'a':
        cnn_continuous_mode=!cnn_continuous_mode;
        if(cnn_continuous_mode) {
            cnn_continuous_frames=0U;
            xil_printf("CNN continuous mode: ON; press 'a' to stop after the current frame\r\n");
        }
        else {
            if(cnn_logger.active)
                cnn_csv_logger_sync(&cnn_logger);
            xil_printf("CNN continuous mode: OFF after %lu frame(s)\r\n",
                       (unsigned long)cnn_continuous_frames);
        }
        break;
    case 't':
        cnn_bringup_print_status(&cnn_ctx);
        cnn_csv_logger_print_status(&cnn_logger);
        break;
    case 'r':
        cnn_bringup_print_last_result(&cnn_ctx);
        break;
    case 'd':
        cnn_diag_dump();
        break;
    case 'x':
        cnn_continuous_mode=0;
        cnn_single_pending=0;
        cnn_csv_logger_stop(&cnn_logger);
        result=cnn_bringup_recover(&cnn_ctx);
        break;
    case 'o':
        kpo_debug_dump();
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
    case 'm':
        cnn_console_start_color();
        break;
    case 'j':
        cnn_console_start_camera(&camera_tracker);
        break;
    case 'l':
        if(cnn_logger.active)
            cnn_csv_logger_stop(&cnn_logger);
        else if(!cnn_csv_logger_start(&cnn_logger))
            xil_printf("CNN CSV logger start failed (%lu)\r\n",
                       (unsigned long)cnn_logger.last_error);
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
    else if(c=='w' || c=='g' || c=='x') {
        xil_printf("CNN command PASS\r\n");
    }
}

static void cnn_continuous_step(void)
{
    cnn_error_t result;

    if(!cnn_ctx.running) {
        if(!cnn_continuous_mode && !cnn_single_pending) return;
        result=cnn_bringup_start(&cnn_ctx,cnn_single_pending);
        if(result!=CNN_OK) {
            cnn_continuous_mode=0;
            cnn_single_pending=0;
            TRACE_CNN_ERROR(cnn_ctx.next_frame_id,result,cnn_bringup_irq_count());
            xil_printf("CNN start failed: %s (%d)\r\n",
                       cnn_error_string(result),(int)result);
        }
        return;
    }

    result=cnn_bringup_service(&cnn_ctx);
    if(result==CNN_PENDING) return;
    if(result!=CNN_OK) {
        cnn_continuous_mode=0;
        cnn_single_pending=0;
        TRACE_CNN_ERROR(cnn_ctx.next_frame_id,result,cnn_bringup_irq_count());
        xil_printf("CNN continuous mode stopped: %s (%d)\r\n",
                   cnn_error_string(result),(int)result);
        return;
    }

    camera_tracking_app_on_result(&camera_tracker,&cnn_ctx.last_result);
    (void)input_pose_cnn_publish(&cnn_ctx.last_result);
    TRACE_CN(cnn_ctx.last_result.frame_id,cnn_ctx.last_result.result_seq,
             cnn_bringup_irq_count(),cnn_ctx.last_elapsed_us,
             cnn_ctx.last_result.joint_flags,input_pose_cnn_overwritten());
    TRACE_CAM(cnn_ctx.last_result.frame_id,camera_tracker.tracker.last_observation,
              camera_tracker.gimbal.current_pulse_us[CAMERA_GIMBAL_PAN],
              camera_tracker.gimbal.current_pulse_us[CAMERA_GIMBAL_TILT],
              camera_tracker.gimbal.target_pulse_us[CAMERA_GIMBAL_PAN],
              camera_tracker.gimbal.target_pulse_us[CAMERA_GIMBAL_TILT]);
    if(cnn_logger.active &&
       !cnn_csv_logger_write(&cnn_logger,&cnn_ctx.last_result)) {
        xil_printf("CNN CSV logger write failed (%lu); logging stopped\r\n",
                   (unsigned long)cnn_logger.last_error);
        cnn_csv_logger_stop(&cnn_logger);
    }
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
               "valid=0x%05x R/B/G=%08x/%08x/%08x\r\n",
               (unsigned long)cnn_continuous_frames,
               (unsigned long)cnn_ctx.last_result.frame_id,
               (unsigned long)cnn_ctx.last_result.result_seq,
               (unsigned long)(cnn_ctx.last_elapsed_us/1000U),
               (unsigned long)(cnn_ctx.last_elapsed_us%1000U),
               (unsigned long)cnn_ctx.last_result.cycle_count,
               (unsigned int)cnn_ctx.last_result.joint_flags,
               (unsigned int)cnn_ctx.last_result.red_marker,
               (unsigned int)cnn_ctx.last_result.blue_marker,
               (unsigned int)cnn_ctx.last_result.green_marker);
#endif
}

static void cnn_auto_start(void)
{
    cnn_error_t result;

    xil_printf("\r\nCNN automatic startup: w -> u -> a\r\n");
    result=cnn_bringup_load_weights(&cnn_ctx);
    if(result!=CNN_OK) {
        TRACE_CNN_ERROR(cnn_ctx.next_frame_id,result,cnn_bringup_irq_count());
        xil_printf("CNN automatic startup stopped: %s (%d)\r\n",
                   cnn_error_string(result),(int)result);
        xil_printf("UART menu remains available; fix the SD card and press 'w', 'u', 'a'.\r\n");
        return;
    }

    if(!cnn_csv_logger_start(&cnn_logger)) {
        xil_printf("CNN automatic CSV logging unavailable (%lu); inference will continue\r\n",
                   (unsigned long)cnn_logger.last_error);
    }

    if(!camera_tracker.tracker.enabled) {
        if(!camera_tracking_app_handle_key(&camera_tracker,'u')) {
            xil_printf("CNN automatic startup stopped: camera tracker unavailable\r\n");
            return;
        }
    }

    cnn_continuous_frames=0U;
    cnn_continuous_mode=1;
    xil_printf("CNN continuous mode: ON (automatic boot)\r\n");
    xil_printf("CNN automatic startup PASS; press 'a' to stop inference or 'u' to stop tracking.\r\n");
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
    run_vdma_frame_buffer(&vdma,VDMA_ID,video_mode.width,video_mode.height,
                          FRAME_BUFFER_ADDR,0,0,BOTH);

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
    cnn_csv_logger_init(&cnn_logger);
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
}
