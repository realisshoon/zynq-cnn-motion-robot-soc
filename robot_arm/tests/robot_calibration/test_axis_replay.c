/* Host-only regression: actual UART parser -> A1 -> A2 -> mock A3.
 * Run from robot_arm with etc/uart_pose_stream.bin (522 frames, fixed 50ms).
 * Optional argv[2] writes target/output evidence as CSV. No board connection. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "integration/agent_pipeline.h"
#include "uart_pose/uart_pose_protocol.h"
#include "output_controller/servo_hal.h"
#include "drivers/servo_pwm_driver.h"

static void joints(const JointCommand *c, float v[5])
{
    v[0]=c->base_deg; v[1]=c->shoulder_deg; v[2]=c->elbow_deg;
    v[3]=c->wrist_pitch_deg; v[4]=c->wrist_roll_deg;
}

static void tick(AgentPipelineContext *ctx, float *max_step)
{
    float before[5],after[5];
    joints(&ctx->output,before);
    assert(agent2_tick(ctx));
    joints(&ctx->output,after);
    for(int i=0;i<5;i++) {
        float step=fabsf(after[i]-before[i]);
        assert(after[i]>=20 && after[i]<=160);
        assert(step<=0.6001f);
        if(step>*max_step) *max_step=step;
    }
    assert(after[3]==90 && after[4]==90);
    servo_pwm_driver_mock_reset(); /* Avoid the mock-only 128-entry log limit. */
    servo_hal_init();
    assert(agent3_run(ctx));
    assert(ctx->pwm.wrist_pitch_pwm_us==1500 && ctx->pwm.wrist_roll_pwm_us==1500);
}

int main(int argc,char **argv)
{
    const char *path=argc>1?argv[1]:"etc/uart_pose_stream.bin";
    FILE *f=fopen(path,"rb"), *csv=argc>2?fopen(argv[2],"w"):NULL;
    AgentPipelineContext ctx;
    PoseUartParser parser;
    HumanPose2D pose;
    float minimum[3]={180,180,180},maximum[3]={0,0,0},max_step=0;
    unsigned saturated[3]={0,0,0},frames=0,next_tick=20;
    int ch;
    assert(f); if(argc>2) assert(csv);
    if(csv) fputs("fid,a1_base,a1_shoulder,a1_elbow,base,shoulder,elbow,wrist_pitch,wrist_roll\n",csv);
    servo_pwm_driver_mock_reset(); servo_hal_init();
    assert(agent_pipeline_init(&ctx)==0);
    pose_uart_parser_init(&parser);
    while((ch=fgetc(f))!=EOF) {
        int parsed=pose_uart_parser_push(&parser,(unsigned char)ch,&pose);
        assert(parsed>=0);
        if(parsed==1) {
            float values[5];
            HumanJointTarget raw;
            unsigned frame_time=frames*50;
            while(next_tick<=frame_time) { tick(&ctx,&max_step);next_tick+=20; }
            assert(agent1_run(&ctx,&pose,0.05f)); raw=ctx.target;
            assert(agent2_run(&ctx));
            joints(&ctx.command,values);
            for(int i=0;i<3;i++) {
                assert(values[i]>=20 && values[i]<=160);
                if(values[i]<minimum[i]) minimum[i]=values[i];
                if(values[i]>maximum[i]) maximum[i]=values[i];
                if(values[i]==20 || values[i]==160) saturated[i]++;
            }
            assert(values[3]==90 && values[4]==90);
            if(csv) fprintf(csv,"%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                pose.frame_id,raw.base_deg,raw.shoulder_deg,raw.elbow_deg,
                values[0],values[1],values[2],values[3],values[4]);
            frames++;
        }
    }
    fclose(f);if(csv)fclose(csv);
    assert(frames==522 && parser.packets_ok==522);
    assert(parser.crc_errors==0 && parser.format_errors==0 && parser.range_errors==0);
    assert(ctx.commands_accepted==522 && ctx.commands_rejected==0);
    for(int i=0;i<3;i++) assert(maximum[i]-minimum[i]>2); /* No constant-limit lock. */
    /* Drain the remaining ramp; motion must settle, not only accept targets. */
    for(unsigned i=0;i<400;i++) tick(&ctx,&max_step);
    assert(fabsf(ctx.output.base_deg-ctx.command.base_deg)<0.001f);
    assert(fabsf(ctx.output.shoulder_deg-ctx.command.shoulder_deg)<0.001f);
    assert(fabsf(ctx.output.elbow_deg-ctx.command.elbow_deg)<0.001f);
    assert(ctx.servo_errors==0);
    printf("UART replay PASS: frames=%u accepted=%u rejected=%u max_step=%.6f deg/20ms\n",
        frames,(unsigned)ctx.commands_accepted,(unsigned)ctx.commands_rejected,max_step);
    for(int i=0;i<3;i++) printf("  joint %d target %.3f..%.3f deg, at limit %u/522\n",
        i,minimum[i],maximum[i],saturated[i]);
    return 0;
}
