/* Virtual-only ABI v2. Original Agent2 and calibration sources are linked unchanged.
 * No Agent1 reconstruction, Agent3, output_control or servo HAL entry is called. */
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "integration/agent_pipeline.h"
#include "robot_calibration/forearm_calibration_config.h"
#define API __declspec(dllexport)
typedef struct { AgentPipelineContext ctx; } Studio;
API int studio_abi_version(void) { return 2; }
static const JointCalibration *joint(int i) {
    const JointCalibration *j[]={&forearm_calibration_config.elbow_roll,&forearm_calibration_config.elbow_pitch,&forearm_calibration_config.wrist_pitch,&forearm_calibration_config.wrist_roll};return j[i];
}
API void studio_limits(float *lo,float *hi) {int i;for(i=0;i<4;i++){lo[i]=joint(i)->min_deg;hi[i]=joint(i)->max_deg;}lo[4]=0;hi[4]=1;}
static ForearmJointCommand command(const float *q) {ForearmJointCommand c={q[0],q[1],q[2],q[3],q[4],1};return c;}
static void values(const ForearmJointCommand *c,float *q) {q[0]=c->elbow_roll_deg;q[1]=c->elbow_pitch_deg;q[2]=c->wrist_pitch_deg;q[3]=c->wrist_roll_deg;q[4]=c->gripper_norm;}
API int studio_validate(const float *q) {
    int i;ForearmSafetyCheckFlags flags=0;ForearmJointCommand c;if(!q)return -1;
    for(i=0;i<5;i++)if(!isfinite(q[i]))return -1;
    for(i=0;i<5;i++)if(q[i]<(i<4?joint(i)->min_deg:0)||q[i]>(i<4?joint(i)->max_deg:1))return -2;
    c=command(q);if(!forearm_safety_check_apply(&c,&flags))return 100+(int)flags;return 0;
}
API void *studio_create(const float *seed) {
    Studio *s;if(studio_validate(seed)!=0)return NULL;s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->ctx.output=command(seed);s->ctx.command=s->ctx.output;
    forearm_motion_control_unwrap_state_init(&s->ctx.unwrap);forearm_calibration_state_init(&s->ctx.motion);
    forearm_calibration_set_target(&s->ctx.motion,&s->ctx.output);return s;
}
API void studio_destroy(void *h){free(h);}
API int studio_submit(void *h,const float *q) {
    Studio *s=h;int rc;if(!s)return -3;rc=studio_validate(q);if(rc)return rc;
    s->ctx.command=command(q);s->ctx.command_valid=1;forearm_calibration_set_target(&s->ctx.motion,&s->ctx.command);return 0;
}
/* flags: target_valid, major_fresh, finger_fresh, elbow_roll_observable,
 * hand_fresh, wrist_valid, gripper_hold, body_frame_valid.
 * Only HumanForearmTarget fields enter Agent2; the other flags remain CSV metadata.
 * info: A2 result, candidate present, reachability/invalid-wrist hold mask,
 * candidate safety flags. Wrapped is the actual Agent2-mutated human target. */
API int studio_submit_human(void *h,const float *human,unsigned target_frame_id,const int *flags,
                           float *wrapped,float *candidate,float *approved,int *info) {
    Studio *s=h;AgentPipelineContext *c;int i,rc;ForearmSafetyCheckFlags safety=0;
    if(!s||!human||!flags||!wrapped||!candidate||!approved||!info)return -3;
    for(i=0;i<5;i++)if(!isfinite(human[i]))return -1;
    for(i=0;i<8;i++)if(flags[i]!=0&&flags[i]!=1)return -1;
    c=&s->ctx;memset(&c->target,0,sizeof(c->target));memset(&c->a2_mapped,0,sizeof(c->a2_mapped));memset(info,0,4*sizeof(int));
    c->target.elbow_roll_deg=human[0];c->target.elbow_pitch_deg=human[1];c->target.wrist_pitch_deg=human[2];c->target.wrist_roll_deg=human[3];c->target.gripper_norm=human[4];
    c->target.frame_id=target_frame_id;c->target.valid=(uint8_t)flags[0];c->target.elbow_roll_observable=(uint8_t)flags[3];c->target.hand_fresh=(uint8_t)flags[4];c->target.wrist_valid=(uint8_t)flags[5];
    c->target_ready=c->target.valid;rc=agent2_run(c);c->target_ready=0;
    wrapped[0]=c->target.elbow_roll_deg;wrapped[1]=c->target.elbow_pitch_deg;wrapped[2]=c->target.wrist_pitch_deg;wrapped[3]=c->target.wrist_roll_deg;wrapped[4]=c->target.gripper_norm;
    info[0]=c->a2_result;info[1]=(c->a2_result==A2_RESULT_NEW||c->a2_result==A2_RESULT_SAME||c->a2_result==A2_RESULT_REJECT_SAFETY);
    if(info[1]) {
        values(&c->a2_mapped,candidate);
        if(!forearm_motion_control_elbow_roll_reachable(wrapped[0]))info[2]|=1;
        if(!c->target.wrist_valid)info[2]|=28;
        else {if(!forearm_motion_control_wrist_pitch_reachable(wrapped[2]))info[2]|=4;if(!forearm_motion_control_wrist_roll_reachable(wrapped[3]))info[2]|=8;}
        forearm_safety_check_apply(&c->a2_mapped,&safety);info[3]=(int)safety;
    } else for(i=0;i<5;i++)candidate[i]=NAN;
    values(c->command_valid?&c->command:&c->output,approved);return rc;
}
API int studio_tick(void *h,float *q,float *velocity) {
    Studio *s=h;int i;if(!s||!q||!velocity)return -3;
    if(!agent2_tick(&s->ctx))return -4;
    values(&s->ctx.output,q);
    for(i=0;i<4;i++)velocity[i]=(float)s->ctx.motion.axes[i].v;
    return s->ctx.motion.held?100+(int)s->ctx.motion.blocked_flags:0;
}

