#include "integration/agent_pipeline.h"
#include "integration/input_pose.h"
#include "integration/platform.h"
#include "integration/trace.h"

/*
 * 통합 진입점: HumanPose2D -> Agent1 -> Agent2 -> Agent3.
 * src/main.c(Agent3의 HAL 데모)는 그대로 두고, 이 파일을 Vitis 통합 단계에서 main으로 쓴다.
 * 입력(UART/CNN)과 플랫폼(타이머 등)은 선언만 되어 있고 구현은 Vitis workspace 확정 후에 붙는다.
 *
 * [TRACE] 표시가 붙은 줄은 UART 디버그 로그용이다. ROBOT_TRACE를 정의하지 않으면 아무것도 하지 않는다
 * (integration/trace.h). 켜면 UART가 921600 baud로 바뀐다.
 */
int main(void)
{
    AgentPipelineContext pipeline;
    HumanPose2D pose;
    float dt_sec;

    if (platform_init() != 0) return -1;
    input_pose_init();
    if (agent_pipeline_init(&pipeline) != 0) return -1;
    TRACE_INIT(); /* [TRACE] 컬럼 정의(# 줄)와 BOOT 이벤트 */

    for (;;) {
        /* 프레임 경로(가변 주기): 새 pose가 오면 목표를 갱신한다. */
        if (input_pose_ready() && input_pose_take(&pose, &dt_sec)) {
            TRACE_MARK(); /* [TRACE] 실행시간 측정 시작 */
            agent1_run(&pipeline, &pose, dt_sec);
            TRACE_A1(&pipeline); /* [TRACE] A1, P3. agent2_run 전에 찍어야 원본이다(unwrap이 타겟을 고친다) */
            agent2_run(&pipeline);
            TRACE_A2(&pipeline); /* [TRACE] A2 */
        }

        /* 제어 틱 경로(고정 20ms): 램프를 한 틱 진행해서 서보에 적용한다. */
        if (platform_tick_due()) {
            TRACE_MARK(); /* [TRACE] 실행시간 측정 시작 */
            agent2_tick(&pipeline);
            agent3_run(&pipeline);
            TRACE_TK(&pipeline); /* [TRACE] TK. 서보 쓰기 뒤에 찍으므로 서보 지연에 영향이 없다 */
        }

        TRACE_POLL(&pipeline); /* [TRACE] 링버퍼를 UART로 비운다(논블로킹). 1초마다 SM 줄 */
    }
}
