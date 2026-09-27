/* Host-only regression: actual UART parser -> A1(forearm) -> A2(forearm) -> mock A3.
 * Run from robot_arm with etc/uart_pose_stream.bin (522 frames, fixed 50ms).
 * Optional argv[2] writes target/output evidence as CSV. No board connection.
 *
 * 2026-09-22: agent_pipeline.c가 legacy HumanJointTarget/JointCommand에서
 * 새 HumanForearmTarget/ForearmJointCommand(elbow_roll/elbow_pitch/wrist_pitch/
 * wrist_roll/gripper)로 옮겨갔다. 옛 wrist는 scale=0으로 잠겨 있어서 90도
 * 고정을 그대로 검증했지만, 새 4관절은 전부 scale=1이라 잠긴 축이 없다 --
 * 그래서 "wrist==90 고정" 검증은 지웠다. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "integration/agent_pipeline.h"
#include "uart_pose/uart_pose_protocol.h"
#include "output_controller/servo_hal.h"
#include "drivers/servo_pwm_driver.h"

static void joints(const ForearmJointCommand *c, float v[4])
{
    v[0]=c->elbow_roll_deg; v[1]=c->elbow_pitch_deg;
    v[2]=c->wrist_pitch_deg; v[3]=c->wrist_roll_deg;
}

static void tick(AgentPipelineContext *ctx, float *max_step)
{
    float before[4],after[4];
    joints(&ctx->output,before);
    assert(agent2_tick(ctx));
    joints(&ctx->output,after);
    for(int i=0;i<4;i++) {
        float step=fabsf(after[i]-before[i]);
        assert(after[i]>=20 && after[i]<=160);
        assert(step<=0.6001f);
        if(step>*max_step) *max_step=step;
    }
    servo_pwm_driver_mock_reset(); /* Avoid the mock-only 128-entry log limit. */
    servo_hal_init();
    assert(agent3_run(ctx));
}

int main(int argc,char **argv)
{
    const char *path=argc>1?argv[1]:"etc/uart_pose_stream.bin";
    FILE *f=fopen(path,"rb"), *csv=argc>2?fopen(argv[2],"w"):NULL;
    AgentPipelineContext ctx;
    PoseUartParser parser;
    HumanPose2D pose;
    float minimum[4]={180,180,180,180},maximum[4]={0,0,0,0},max_step=0;
    unsigned saturated[4]={0,0,0,0},frames=0,next_tick=20;
    int ch;
    assert(f); if(argc>2) assert(csv);
    if(csv) fputs("fid,a1_er,a1_ep,a1_wp,a1_wr,er,ep,wp,wr\n",csv);
    servo_pwm_driver_mock_reset(); servo_hal_init();
    assert(agent_pipeline_init(&ctx)==0);
    pose_uart_parser_init(&parser);
    while((ch=fgetc(f))!=EOF) {
        int parsed=pose_uart_parser_push(&parser,(unsigned char)ch,&pose);
        assert(parsed>=0);
        if(parsed==1) {
            float values[4];
            HumanForearmTarget raw;
            unsigned frame_time=frames*50;
            while(next_tick<=frame_time) { tick(&ctx,&max_step);next_tick+=20; }
            assert(agent1_run(&ctx,&pose,0.05f)); raw=ctx.target;
            /* 2026-09-22: wrist_pitch=90=90도 굽힘으로 바뀌면서 자기충돌
             * (wrist_pitch>155)이 [20,160] 범위 안에서 도달 가능해져
             * 일부 프레임이 거부될 수 있다 -- 매 프레임 승인을 더 이상
             * 강제하지 않는다. 거부되면 ctx.command는 직전 승인값 그대로다. */
            agent2_run(&ctx);
            joints(&ctx.command,values);
            for(int i=0;i<4;i++) {
                assert(values[i]>=20 && values[i]<=160);
                if(values[i]<minimum[i]) minimum[i]=values[i];
                if(values[i]>maximum[i]) maximum[i]=values[i];
                if(values[i]==20 || values[i]==160) saturated[i]++;
            }
            if(csv) fprintf(csv,"%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                pose.frame_id,raw.elbow_roll_deg,raw.elbow_pitch_deg,
                raw.wrist_pitch_deg,raw.wrist_roll_deg,
                values[0],values[1],values[2],values[3]);
            frames++;
        }
    }
    fclose(f);if(csv)fclose(csv);
    assert(frames==522 && parser.packets_ok==522);
    assert(parser.crc_errors==0 && parser.format_errors==0 && parser.range_errors==0);
    /* [20,160] 범위에서 테이블충돌은 여전히 도달 불가능하지만(위 참고),
     * wrist_pitch=90=90도 굽힘 기준으로 바뀌면서 자기충돌(wrist_pitch>155)은
     * 도달 가능해졌다 -- 실행해서 실제 승인/거부 수를 확인했다. dev/robot의
     * PR#57(Agent1 손목 계산 수정) 병합 이후 481/41 -> 479/43으로 바뀌었다. */
    printf("commands_accepted=%u commands_rejected=%u\n",
        (unsigned)ctx.commands_accepted, (unsigned)ctx.commands_rejected);
    assert(ctx.commands_accepted==479 && ctx.commands_rejected==43);
    /* Drain the remaining ramp; motion must settle, not only accept targets. */
    for(unsigned i=0;i<400;i++) tick(&ctx,&max_step);
    assert(fabsf(ctx.output.elbow_roll_deg-ctx.command.elbow_roll_deg)<0.001f);
    assert(fabsf(ctx.output.elbow_pitch_deg-ctx.command.elbow_pitch_deg)<0.001f);
    assert(fabsf(ctx.output.wrist_pitch_deg-ctx.command.wrist_pitch_deg)<0.001f);
    assert(fabsf(ctx.output.wrist_roll_deg-ctx.command.wrist_roll_deg)<0.001f);
    assert(ctx.servo_errors==0);
    printf("UART replay PASS: frames=%u accepted=%u rejected=%u max_step=%.6f deg/20ms\n",
        frames,(unsigned)ctx.commands_accepted,(unsigned)ctx.commands_rejected,max_step);
    for(int i=0;i<4;i++) printf("  joint %d target %.3f..%.3f deg, at limit %u/522\n",
        i,minimum[i],maximum[i],saturated[i]);
    return 0;
}
