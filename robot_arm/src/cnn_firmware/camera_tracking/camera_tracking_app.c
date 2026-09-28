#include "camera_tracking_app.h"

#include "xil_printf.h"
#include "xuartps_hw.h"
#include "xparameters.h"

static void discard_line_endings(void)
{
    while (XUartPs_IsReceiveData(STDIN_BASEADDRESS)) {
        u8 c = XUartPs_ReadReg(STDIN_BASEADDRESS, XUARTPS_FIFO_OFFSET);
        if (c != '\r' && c != '\n')
            break;
    }
}

static int read_optional_u16(const char *label, u16 current,
                             u16 low, u16 high, u16 *value)
{
    char digits[6];
    unsigned int count = 0U;
    unsigned long parsed = 0U;
    unsigned int i;
    u8 c;

    xil_printf("  %s [%u, %u..%u, Enter=keep]: ", label,
               current, low, high);
    for (;;) {
        c = XUartPs_RecvByte(STDIN_BASEADDRESS);
        if (c == '\r' || c == '\n') {
            xil_printf("\r\n");
            break;
        }
        if ((c == 8U || c == 127U) && count != 0U) {
            --count;
            xil_printf("\b \b");
        }
        else if (c >= '0' && c <= '9' && count < sizeof(digits)) {
            digits[count++] = (char)c;
            xil_printf("%c", c);
        }
    }
    if (count == 0U) {
        *value = current;
        return 1;
    }
    for (i = 0U; i < count; ++i)
        parsed = parsed * 10UL + (unsigned long)(digits[i] - '0');
    if (parsed < low || parsed > high) {
        xil_printf("  out of range; configuration unchanged\r\n");
        return 0;
    }
    *value = (u16)parsed;
    return 1;
}

static void configure_interactive(camera_tracking_app_t *app)
{
    torso_tracker_config_t next;
    u16 deadband_x;
    u16 deadband_y;
    u16 target_x;
    u16 target_y;
    u16 filter_shift;
    u16 jump_limit;
    u16 gain_x;
    u16 gain_y;
    u16 command_delta;
    u16 search_step;
    u16 search_confirm;
    u16 slew;
    u16 pan_center;
    u16 tilt_center;
    u16 pulse_min;
    u16 pulse_max;
    u16 pan_invert;
    u16 tilt_invert;

    next = app->tracker.config;
    deadband_x = next.deadband_x;
    deadband_y = next.deadband_y;
    target_x = next.target_x;
    target_y = next.target_y;
    filter_shift = next.filter_shift;
    jump_limit = next.jump_limit_px;
    gain_x = next.gain_div_x;
    gain_y = next.gain_div_y;
    command_delta = next.max_command_delta_us;
    search_step = next.search_step_us;
    search_confirm = next.search_confirm_frames;
    slew = app->gimbal.slew_step_us;
    pan_center = app->gimbal.center_pulse_us[CAMERA_GIMBAL_PAN];
    tilt_center = app->gimbal.center_pulse_us[CAMERA_GIMBAL_TILT];
    pulse_min = app->gimbal.min_pulse_us[CAMERA_GIMBAL_PAN];
    pulse_max = app->gimbal.max_pulse_us[CAMERA_GIMBAL_PAN];
    pan_invert = next.pan_invert;
    tilt_invert = next.tilt_invert;

    discard_line_endings();
    xil_printf("\r\nCamera torso-tracking configuration\r\n");
    if (!read_optional_u16("target X px", target_x, 0U, 1279U, &target_x) ||
        !read_optional_u16("target Y px", target_y, 0U, 719U, &target_y) ||
        !read_optional_u16("horizontal deadband px", deadband_x, 0U, 320U, &deadband_x) ||
        !read_optional_u16("vertical deadband px", deadband_y, 0U, 180U, &deadband_y) ||
        !read_optional_u16("IIR shift (0=off, 2=1/4)", filter_shift, 0U, 5U, &filter_shift) ||
        !read_optional_u16("jump rejection px", jump_limit, 20U, 640U, &jump_limit) ||
        !read_optional_u16("pan pixels per pulse-us", gain_x, 1U, 64U, &gain_x) ||
        !read_optional_u16("tilt pixels per pulse-us", gain_y, 1U, 64U, &gain_y) ||
        !read_optional_u16("max target change us/frame", command_delta, 1U, 200U, &command_delta) ||
        !read_optional_u16("pair search step us/frame", search_step, 1U, 50U, &search_step) ||
        !read_optional_u16("pair search confirmation frames", search_confirm, 1U, 20U, &search_confirm) ||
        !read_optional_u16("motor slew us/20ms", slew, 1U, 100U, &slew) ||
        !read_optional_u16("servo minimum us", pulse_min, 500U, 1800U, &pulse_min) ||
        !read_optional_u16("servo maximum us", pulse_max, 1200U, 2500U, &pulse_max) ||
        !read_optional_u16("pan center us", pan_center, 500U, 2500U, &pan_center) ||
        !read_optional_u16("tilt center us", tilt_center, 500U, 2500U, &tilt_center) ||
        !read_optional_u16("pan invert 0/1", pan_invert, 0U, 1U, &pan_invert) ||
        !read_optional_u16("tilt invert 0/1", tilt_invert, 0U, 1U, &tilt_invert))
        return;

    if (pulse_min >= pulse_max || pan_center < pulse_min || pan_center > pulse_max ||
        tilt_center < pulse_min || tilt_center > pulse_max) {
        xil_printf("  invalid pulse range/center; configuration unchanged\r\n");
        return;
    }

    next.target_x = target_x;
    next.target_y = target_y;
    next.deadband_x = deadband_x;
    next.deadband_y = deadband_y;
    next.filter_shift = (u8)filter_shift;
    next.jump_limit_px = jump_limit;
    next.gain_div_x = gain_x;
    next.gain_div_y = gain_y;
    next.max_command_delta_us = command_delta;
    next.search_step_us = search_step;
    next.search_confirm_frames = (u8)search_confirm;
    next.pan_invert = (u8)pan_invert;
    next.tilt_invert = (u8)tilt_invert;
    app->tracker.config = next;
    app->tracker.filter_valid = 0U;
    camera_gimbal_pwm_set_limits(&app->gimbal, pulse_min, pulse_max);
    camera_gimbal_pwm_set_centers(&app->gimbal, pan_center, tilt_center);
    camera_gimbal_pwm_set_slew(&app->gimbal, slew);
    xil_printf("camera tracking configuration applied\r\n");
    camera_tracking_app_print_status(app);
}

int camera_tracking_app_init(camera_tracking_app_t *app)
{
    torso_tracker_config_t config;

    if (app == 0)
        return XST_INVALID_PARAM;
    if (camera_gimbal_pwm_init(&app->gimbal,
                               camera_gimbal_pwm_default_base()) != XST_SUCCESS)
        return XST_FAILURE;
    torso_tracker_default_config(&config);
    torso_tracker_init(&app->tracker, &app->gimbal, &config);
    app->last_reported_frame = 0U;
    app->initialized = 1U;
    /* 부팅 기본값은 초기화된 중앙 펄스에서의 FIXED 모드다. */
    camera_tracking_app_fix(app);
    return XST_SUCCESS;
}

void camera_tracking_app_fix(camera_tracking_app_t *app)
{
    if (app == 0 || !app->initialized)
        return;
    torso_tracker_set_enable(&app->tracker, 0);
    camera_gimbal_pwm_set_target(&app->gimbal, CAMERA_GIMBAL_PAN,
        app->gimbal.current_pulse_us[CAMERA_GIMBAL_PAN]);
    camera_gimbal_pwm_set_target(&app->gimbal, CAMERA_GIMBAL_TILT,
        app->gimbal.current_pulse_us[CAMERA_GIMBAL_TILT]);
    /* enable은 보존된 current 펄스를 다시 commit한 뒤 출력을 켠다. */
    camera_gimbal_pwm_set_enable(&app->gimbal, 1);
    camera_tracking_app_print_mode(app);
}

const char *camera_tracking_app_mode_string(const camera_tracking_app_t *app)
{
    if (app == 0 || !app->initialized)
        return "UNINITIALIZED";
    if (app->tracker.enabled)
        return app->gimbal.enabled ? "TRACKING" : "TRACKING(PWM OFF)";
    return app->gimbal.enabled ? "FIXED" : "OFF";
}

void camera_tracking_app_print_mode(const camera_tracking_app_t *app)
{
    if (app == 0)
        return;
    xil_printf("camera mode: %s; tracking: %s; camera PWM: %s "
               "pan/tilt=%u/%u us target=%u/%u us CONTROL=%08x\r\n",
               camera_tracking_app_mode_string(app),
               app->tracker.enabled ? "ON" : "OFF",
               app->gimbal.enabled ? "ON" : "OFF",
               app->gimbal.current_pulse_us[CAMERA_GIMBAL_PAN],
               app->gimbal.current_pulse_us[CAMERA_GIMBAL_TILT],
               app->gimbal.target_pulse_us[CAMERA_GIMBAL_PAN],
               app->gimbal.target_pulse_us[CAMERA_GIMBAL_TILT],
               (unsigned int)camera_gimbal_pwm_control_readback(
                   &app->gimbal));
}

void camera_tracking_app_on_result(camera_tracking_app_t *app,
                                   const cnn_result_t *result)
{
    torso_tracker_result_t state;

    if (app == 0 || !app->initialized || result == 0)
        return;
    state = torso_tracker_process(&app->tracker, result);
    if (state == TORSO_TRACK_DISABLED || state == TORSO_TRACK_DUPLICATE)
        return;

    app->last_reported_frame = result->frame_id;
#ifndef ROBOT_TRACE
    xil_printf("track frame=%lu state=%s obs=%s point=%ld,%ld filtered=%ld,%ld "
               "err=%ld,%ld pwm=%u/%u target=%u/%u\r\n",
               (unsigned long)result->frame_id,
               torso_tracker_result_string(state),
               torso_tracker_observation_string(app->tracker.last_observation),
               (long)app->tracker.raw_x, (long)app->tracker.raw_y,
               (long)app->tracker.filtered_x, (long)app->tracker.filtered_y,
               (long)app->tracker.error_x, (long)app->tracker.error_y,
               app->gimbal.current_pulse_us[CAMERA_GIMBAL_PAN],
               app->gimbal.current_pulse_us[CAMERA_GIMBAL_TILT],
               app->gimbal.target_pulse_us[CAMERA_GIMBAL_PAN],
               app->gimbal.target_pulse_us[CAMERA_GIMBAL_TILT]);
#endif
}

void camera_tracking_app_service(camera_tracking_app_t *app)
{
    if (app != 0 && app->initialized)
        camera_gimbal_pwm_service(&app->gimbal);
}

void camera_tracking_app_print_help(void)
{
    xil_printf("  u : toggle camera tracking and camera PWM output "
               "(FIXED -> tracking ON)\r\n");
    xil_printf("  f : FIXED camera: tracking OFF, PWM ON at current pulse\r\n");
    xil_printf("  v : print camera tracker/PWM status\r\n");
    xil_printf("  j : edit deadband/filter/speed/servo settings\r\n");
    xil_printf("  i : toggle camera pan direction\r\n");
    xil_printf("  k : toggle camera tilt direction\r\n");
    xil_printf("  h : immediately center camera pan/tilt\r\n");
}

void camera_tracking_app_print_status(const camera_tracking_app_t *app)
{
    if (app == 0)
        return;
    torso_tracker_print(&app->tracker);
    camera_gimbal_pwm_print(&app->gimbal);
}

int camera_tracking_app_handle_key(camera_tracking_app_t *app, char key)
{
    if (app == 0 || !app->initialized)
        return 0;

    switch (key) {
    case 'u':
        torso_tracker_set_enable(&app->tracker, !app->tracker.enabled);
        if (app->tracker.enabled) {
            camera_gimbal_pwm_set_enable(&app->gimbal, 1);
        }
        else {
            camera_gimbal_pwm_set_target(&app->gimbal, CAMERA_GIMBAL_PAN,
                app->gimbal.current_pulse_us[CAMERA_GIMBAL_PAN]);
            camera_gimbal_pwm_set_target(&app->gimbal, CAMERA_GIMBAL_TILT,
                app->gimbal.current_pulse_us[CAMERA_GIMBAL_TILT]);
            camera_gimbal_pwm_set_enable(&app->gimbal, 0);
        }
        camera_tracking_app_print_mode(app);
        return 1;
    case 'f':
        camera_tracking_app_fix(app);
        return 1;
    case 'v':
        camera_tracking_app_print_mode(app);
        camera_tracking_app_print_status(app);
        return 1;
    case 'j':
        configure_interactive(app);
        return 1;
    case 'i':
        app->tracker.config.pan_invert ^= 1U;
        app->tracker.filter_valid = 0U;
        xil_printf("camera pan invert=%u\r\n",
                   app->tracker.config.pan_invert);
        return 1;
    case 'k':
        app->tracker.config.tilt_invert ^= 1U;
        app->tracker.filter_valid = 0U;
        xil_printf("camera tilt invert=%u\r\n",
                   app->tracker.config.tilt_invert);
        return 1;
    case 'h':
        camera_gimbal_pwm_center(&app->gimbal);
        app->tracker.filter_valid = 0U;
        xil_printf("camera gimbal centered immediately\r\n");
        camera_tracking_app_print_mode(app);
        return 1;
    default:
        return 0;
    }
}
