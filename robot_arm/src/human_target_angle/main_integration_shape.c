/*
 * 통합 담당자 참고용 main 형태.
 *
 * Agent2/Agent3 함수명은 repo의 실제 API를 확인한 뒤 wrapper로 연결한다.
 */

#include "human_target_angle/agent1_stage.h"

int agent2_stage_init(void);
int agent3_stage_init(void);

int agent2_stage_run(const HumanJointTarget *human_target, float dt_sec);
int agent2_stage_output_valid(void);

int agent3_stage_run(float dt_sec);

int input_pose_ready(void);
int input_pose_take(HumanPose2D *pose, float *dt_sec);
int system_init(void);

int main(void)
{
    HumanPose2D pose;
    float dt_sec;

    if (system_init() != 0) {
        return -1;
    }

    if (agent1_stage_init() != 0) {
        return -1;
    }

    if (agent2_stage_init() != 0) {
        return -1;
    }

    if (agent3_stage_init() != 0) {
        return -1;
    }

    while (1) {
        if (!input_pose_ready()) {
            continue;
        }

        if (!input_pose_take(&pose, &dt_sec)) {
            continue;
        }

        (void)agent1_stage_run(
            &pose,
            POSE_ARM_RIGHT,
            dt_sec
        );

        if (!agent1_stage_output_valid()) {
            continue;
        }

        (void)agent2_stage_run(
            agent1_stage_output(),
            dt_sec
        );

        if (!agent2_stage_output_valid()) {
            continue;
        }

        (void)agent3_stage_run(dt_sec);
    }
}
