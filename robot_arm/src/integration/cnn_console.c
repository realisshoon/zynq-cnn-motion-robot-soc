#include "cnn_console.h"
#include "../cnn_firmware/cnn/cnn_hw.h"
#include "../cnn_firmware/cnn/cnn_bringup.h"

#include <string.h>
#include "xil_printf.h"
#include "xparameters.h"
#include "xuartps_hw.h"

typedef struct { const char *name; u32 low, high; } MenuField;
enum { MENU_NONE, MENU_COLOR, MENU_CAMERA, MENU_JOINT };
enum { CAMERA_FIELD_COUNT = 18U, COLOR_FIELD_COUNT = 13U };

static const MenuField color_fields[COLOR_FIELD_COUNT] = {
    {"red margin",0U,255U}, {"green margin",0U,255U},
    {"blue margin",0U,255U},
    {"yellow R-G max difference",0U,255U},
    {"yellow R/G above B min gap",0U,255U},
    {"red minimum brightness",0U,255U},
    {"green minimum brightness",0U,255U},
    {"blue minimum brightness",0U,255U},
    {"yellow minimum red",0U,255U},
    {"yellow minimum green",0U,255U},
    {"yellow maximum blue",0U,255U},
    {"minimum detected taps",1U,262143U},
    {"color enable mask (R=1,B=2,G=4,Y=8)",0U,15U}
};
static const MenuField camera_fields[CAMERA_FIELD_COUNT] = {
    {"target X px",0U,1279U}, {"target Y px",0U,719U},
    {"horizontal deadband px",0U,320U},
    {"vertical deadband px",0U,180U},
    {"IIR shift",0U,5U}, {"jump rejection px",20U,640U},
    {"pan pixels per pulse-us",1U,64U},
    {"tilt pixels per pulse-us",1U,64U},
    {"max target change us/frame",1U,200U},
    {"pair search step us/frame",1U,50U},
    {"pair search confirmation frames",1U,20U},
    {"motor slew us/20ms",1U,100U},
    {"servo minimum us",500U,1800U},
    {"servo maximum us",1200U,2500U},
    {"pan center us",500U,2500U},
    {"tilt center us",500U,2500U},
    {"pan invert 0/1",0U,1U}, {"tilt invert 0/1",0U,1U}
};
static const MenuField joint_fields[2] = {
    {"body joint index (5..16)",5U,16U},
    {"show joint (0/1)",0U,1U}
};

static camera_tracking_app_t *s_camera;
static u32 s_values[CAMERA_FIELD_COUNT];
static unsigned s_mode, s_field, s_digits;
static char s_text[6];
static int s_skip_lf;

static const MenuField *current_field(void)
{
    return s_mode == MENU_COLOR ? &color_fields[s_field] :
           s_mode == MENU_JOINT ? &joint_fields[s_field] :
                                  &camera_fields[s_field];
}

static void prompt(void)
{
    const MenuField *f = current_field();
    xil_printf("  %s [current %u, %u..%u, Enter=keep]: ",f->name,
               (unsigned)s_values[s_field],(unsigned)f->low,(unsigned)f->high);
}

int cnn_console_active(void) { return s_mode != MENU_NONE; }

void cnn_console_start_color(void)
{
    u8 red,green,blue;
    u32 packed;
    cnn_hw_get_color_margins(&red,&green,&blue);
    s_values[0]=red; s_values[1]=green; s_values[2]=blue;
    packed=cnn_hw_read(CNN_REG_YELLOW_MARGIN);
    s_values[3]=(u16)((packed>>8)&0xffU);
    s_values[4]=(u16)(packed&0xffU);
    packed=cnn_hw_read(CNN_REG_RED_THRESHOLD);
    s_values[5]=(u16)(packed&0xffU);
    packed=cnn_hw_read(CNN_REG_GREEN_THRESHOLD);
    s_values[6]=(u16)((packed>>8)&0xffU);
    packed=cnn_hw_read(CNN_REG_BLUE_THRESHOLD);
    s_values[7]=(u16)((packed>>16)&0xffU);
    packed=cnn_hw_read(CNN_REG_YELLOW_THRESHOLD);
    s_values[8]=(u16)(packed&0xffU);
    s_values[9]=(u16)((packed>>8)&0xffU);
    s_values[10]=(u16)((packed>>16)&0xffU);
    s_values[11]=cnn_hw_read(CNN_REG_MIN_COUNT)&0x3ffffU;
    s_values[12]=cnn_hw_get_color_enable();
    s_mode=MENU_COLOR; s_field=0U; s_digits=0U; s_skip_lf=0;
    xil_printf("\r\nCNN RGBY detection settings (stop inference first)\r\n");
    prompt();
}

void cnn_console_start_camera(camera_tracking_app_t *app)
{
    torso_tracker_config_t *c;
    if(app==0 || !app->initialized) return;
    s_camera=app; c=&app->tracker.config;
    s_values[0]=c->target_x; s_values[1]=c->target_y;
    s_values[2]=c->deadband_x; s_values[3]=c->deadband_y;
    s_values[4]=c->filter_shift; s_values[5]=c->jump_limit_px;
    s_values[6]=c->gain_div_x; s_values[7]=c->gain_div_y;
    s_values[8]=c->max_command_delta_us;
    s_values[9]=c->search_step_us;
    s_values[10]=c->search_confirm_frames;
    s_values[11]=app->gimbal.slew_step_us;
    s_values[12]=app->gimbal.min_pulse_us[CAMERA_GIMBAL_PAN];
    s_values[13]=app->gimbal.max_pulse_us[CAMERA_GIMBAL_PAN];
    s_values[14]=app->gimbal.center_pulse_us[CAMERA_GIMBAL_PAN];
    s_values[15]=app->gimbal.center_pulse_us[CAMERA_GIMBAL_TILT];
    s_values[16]=c->pan_invert; s_values[17]=c->tilt_invert;
    s_mode=MENU_CAMERA; s_field=0U; s_digits=0U; s_skip_lf=0;
    xil_printf("\r\nCamera torso-tracking configuration\r\n");
    prompt();
}

void cnn_console_start_joint(void)
{
    s_values[0]=5U;
    s_values[1]=(cnn_bringup_get_skeleton_mask()&(1U<<5))?1U:0U;
    s_mode=MENU_JOINT; s_field=0U; s_digits=0U; s_skip_lf=0;
    xil_printf("\r\nBody joint visibility (face joints remain hidden)\r\n");
    prompt();
}

static void apply_camera(void)
{
    torso_tracker_config_t next;
    if(s_values[12]>=s_values[13] ||
       s_values[14]<s_values[12] || s_values[14]>s_values[13] ||
       s_values[15]<s_values[12] || s_values[15]>s_values[13]) {
        xil_printf("  invalid pulse range/center; configuration unchanged\r\n");
        return;
    }
    next=s_camera->tracker.config;
    next.target_x=s_values[0]; next.target_y=s_values[1];
    next.deadband_x=s_values[2]; next.deadband_y=s_values[3];
    next.filter_shift=(u8)s_values[4]; next.jump_limit_px=s_values[5];
    next.gain_div_x=s_values[6]; next.gain_div_y=s_values[7];
    next.max_command_delta_us=s_values[8];
    next.search_step_us=s_values[9];
    next.search_confirm_frames=(u8)s_values[10];
    next.pan_invert=(u8)s_values[16]; next.tilt_invert=(u8)s_values[17];
    s_camera->tracker.config=next;
    s_camera->tracker.filter_valid=0U;
    camera_gimbal_pwm_set_limits(&s_camera->gimbal,s_values[12],s_values[13]);
    camera_gimbal_pwm_set_centers(&s_camera->gimbal,s_values[14],s_values[15]);
    camera_gimbal_pwm_set_slew(&s_camera->gimbal,s_values[11]);
    xil_printf("camera tracking configuration applied\r\n");
    camera_tracking_app_print_status(s_camera);
}

static void finish(void)
{
    if(s_mode==MENU_COLOR) {
        cnn_error_t e=cnn_hw_set_color_margins((u8)s_values[0],
                                                (u8)s_values[1],(u8)s_values[2]);
        if(e==CNN_OK)
            e=cnn_hw_set_yellow_margins((u8)s_values[3],(u8)s_values[4]);
        if(e==CNN_OK)
            e=cnn_hw_set_color_thresholds((u8)s_values[5],
                (u8)s_values[6],(u8)s_values[7],(u8)s_values[8],
                (u8)s_values[9],(u8)s_values[10]);
        if(e==CNN_OK)
            e=cnn_hw_set_color_min_count(s_values[11]);
        if(e==CNN_OK)
            e=cnn_hw_set_color_enable(s_values[12]);
        xil_printf("  RGBY settings %s (%d); min count=%u mask=0x%x\r\n",
                   e==CNN_OK?"applied":"failed",(int)e,
                   (unsigned)s_values[11],(unsigned)s_values[12]);
    } else if(s_mode==MENU_CAMERA) {
        apply_camera();
    } else if(s_mode==MENU_JOINT) {
        cnn_error_t e=cnn_bringup_set_skeleton_joint(s_values[0],s_values[1]);
        xil_printf("  joint %u visibility %s (%d); mask=0x%05x\r\n",
                   (unsigned)s_values[0],e==CNN_OK?"updated":"failed",
                   (int)e,(unsigned)cnn_bringup_get_skeleton_mask());
    }
    s_mode=MENU_NONE;
}

void cnn_console_poll(void)
{
    unsigned budget=32U;
    while(s_mode!=MENU_NONE && budget-- &&
          XUartPs_IsReceiveData(STDIN_BASEADDRESS)) {
        u8 c=XUartPs_ReadReg(STDIN_BASEADDRESS,XUARTPS_FIFO_OFFSET);
        const MenuField *f=current_field();
        if(s_skip_lf) { s_skip_lf=0; if(c=='\n') continue; }
        if(c=='\r' || c=='\n') {
            unsigned parsed=0U,i;
            xil_printf("\r\n");
            for(i=0U;i<s_digits;++i)
                parsed=parsed*10U+(unsigned)(s_text[i]-'0');
            if(s_digits && (parsed<f->low || parsed>f->high)) {
                xil_printf("  out of range; configuration unchanged\r\n");
                s_mode=MENU_NONE;
                return;
            }
            if(s_digits) s_values[s_field]=(u32)parsed;
            s_digits=0U;
            s_skip_lf=(c=='\r');
            ++s_field;
            if(s_field==(s_mode==MENU_COLOR?COLOR_FIELD_COUNT:
                         s_mode==MENU_JOINT?2U:CAMERA_FIELD_COUNT))
                finish();
            else
                prompt();
        } else if((c==8U || c==127U) && s_digits) {
            --s_digits;
            xil_printf("\b \b");
        } else if(c>='0' && c<='9' && s_digits<sizeof(s_text)) {
            s_text[s_digits++]=(char)c;
            xil_printf("%c",c);
        }
    }
}
