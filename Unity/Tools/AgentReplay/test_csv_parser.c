/* 실제 converter의 parser를 그대로 호출한다. 별도 parser 구현이 아니다. */
#define main replay_program_main
#include "csv_pose_to_joint_trace.c"
#undef main
#include <assert.h>

static void test_frame_invalid_pipeline(void)
{
    AgentPipelineContext ctx;
    PoseRow row;
    HumanJointTarget previous;
    unsigned i;
    /* 입력 CSV의 첫 정상 행을 실제 Agent1에 넣은 뒤 frame_valid만 누락시킨다. */
    assert(parse_pose_line("1,0,1,684.956,245.461,1,544.187,237.334,1,516.303,357.309,1,516.063,465.777,1,523.029,497.793,1,510.780,497.562,1",&row));
    servo_hal_init();
    assert(agent_pipeline_init(&ctx)==0);
    assert(agent1_run(&ctx,&row.pose,0.05f) && ctx.a1_rc==1);
    previous=ctx.target;
    assert(agent1_run(&ctx,&row.pose,0.001f) && ctx.a1_rc==0);
    row.pose.valid=0;
    row.pose.frame_id=2;
    assert(agent1_run(&ctx,&row.pose,0.05f) && ctx.a1_rc==0);
    assert(ctx.target.base_deg==previous.base_deg && ctx.target.shoulder_deg==previous.shoulder_deg);
    assert(ctx.target.elbow_deg==previous.elbow_deg && ctx.target.wrist_pitch_deg==previous.wrist_pitch_deg);
    assert(ctx.target.wrist_roll_deg==previous.wrist_roll_deg && ctx.target.gripper_norm==previous.gripper_norm);
    for(i=0;i<5;i++) {
        row.pose.frame_id++;
        (void)agent1_run(&ctx,&row.pose,0.1f);
    }
    assert(ctx.a1_rc==-1 && !ctx.target_ready);
    row.pose.frame_id++;
    row.pose.valid=1;
    assert(agent1_run(&ctx,&row.pose,0.05f) && ctx.a1_rc==1);
    puts("PASS: actual Agent1 repeated frame, frame_valid=0 short HOLD / timeout / recovery");
}

int main(void)
{
    PoseRow row;
    const char *normal="42,1.25,1,100,101,1,200,201,1,300,301,1,400,401,1,500,501,1,600,601,1";
    assert(parse_pose_line(normal,&row));
    assert(row.time_sec==1.25 && row.pose.frame_id==42 && row.pose.valid==1);
    assert(row.pose.shoulder_l.x==100 && row.pose.shoulder_l.y==101 && row.pose.shoulder_l.valid);
    assert(row.pose.shoulder_r.x==200 && row.pose.shoulder_r.y==201 && row.pose.shoulder_r.valid);
    assert(row.pose.elbow.x==300 && row.pose.elbow.y==301 && row.pose.elbow.valid);
    assert(row.pose.wrist.x==400 && row.pose.wrist.y==401 && row.pose.wrist.valid);
    assert(row.pose.finger1.x==500 && row.pose.finger1.y==501 && row.pose.finger1.valid);
    assert(row.pose.finger2.x==600 && row.pose.finger2.y==601 && row.pose.finger2.valid);
    assert(parse_pose_line("43,1.30,0,100,101,1,200,201,1,300,301,1,400,401,1,500,501,1,600,601,1",&row));
    assert(!row.pose.valid && !row.pose.shoulder_l.valid && !row.pose.shoulder_r.valid && !row.pose.elbow.valid && !row.pose.wrist.valid && !row.pose.finger1.valid && !row.pose.finger2.valid);
    assert(parse_pose_line("44,1.35,1,100,101,1,200,201,1,300,301,0,400,401,1,500,501,0,600,601,0",&row));
    assert(row.pose.valid && row.pose.shoulder_l.valid && !row.pose.elbow.valid && row.pose.wrist.valid && !row.pose.finger1.valid && !row.pose.finger2.valid);
    assert(parse_pose_line("45,1.4,1,1280,100,1,200,720,1,300,301,1,400,401,1,500,501,1,600,601,1",&row));
    assert(!row.pose.shoulder_l.valid && !row.pose.shoulder_r.valid);
    assert(!parse_pose_line("42,1.25,1,100",&row));
    assert(!parse_pose_line("4294967296,1.25,1,100,101,1,200,201,1,300,301,1,400,401,1,500,501,1,600,601,1",&row));
    assert(!parse_pose_line("42,1.25,2,100,101,1,200,201,1,300,301,1,400,401,1,500,501,1,600,601,1",&row));
    assert(!parse_pose_line("42,1.25,1,100,101,1,200,201,1,300,301,1,400,401,1,500,501,1,600,601,1,999",&row));
    puts("PASS: CSV field mapping, whole-frame invalid, body/finger flags, range, malformed row");
    test_frame_invalid_pipeline();
    return 0;
}
