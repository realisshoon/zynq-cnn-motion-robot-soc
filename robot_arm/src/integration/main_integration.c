#include "integration/agent_pipeline.h"
#include "integration/input_pose.h"
#include "integration/platform.h"

/*
 * 통합 진입점: HumanPose2D -> Agent1 -> Agent2 -> Agent3.
 * src/main.c(Agent3의 HAL 데모)는 그대로 두고, 이 파일을 Vitis 통합 단계에서 main으로 쓴다.
 * 입력(UART/CNN)과 플랫폼(타이머 등)은 선언만 되어 있고 구현은 Vitis workspace 확정 후에 붙는다.
 */
int main(void)
{
    AgentPipelineContext pipeline;
    HumanPose2D pose;
    float dt_sec;

    if (platform_init() != 0) return -1;
    input_pose_init();
    if (agent_pipeline_init(&pipeline) != 0) return -1;

    for (;;) {
        /* 프레임 경로(가변 주기): 새 pose가 오면 목표를 갱신한다. */
        if (input_pose_ready() && input_pose_take(&pose, &dt_sec)) {
            agent1_run(&pipeline, &pose, dt_sec);
            agent2_run(&pipeline);
        }

        /* 제어 틱 경로(고정 20ms): 램프를 한 틱 진행해서 서보에 적용한다. */
        if (platform_tick_due()) {
            agent2_tick(&pipeline);
            agent3_run(&pipeline);
        }
    }
}
