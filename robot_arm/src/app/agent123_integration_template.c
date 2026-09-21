/*
 * UART -> Agent1 -> Agent2 -> Agent3 통합용 호출 구조 예시
 *
 * UART receiver/parser는 Agent1/2/3을 몰라야 한다.
 * 새 HumanPose2D 한 frame을 얻은 main/app layer에서 Agent chain을 호출한다.
 *
 * 아래 Agent1 호출은 현재 공개 API에 맞춘 실제 코드다.
 *
 * Agent2/Agent3의 정확한 public 함수 signature는 이 패키지 입력에 없으므로
 * 임의의 함수명을 만들어 넣지 않았다. 프로젝트의 기존 main.c에서 쓰던
 * Agent2 -> Agent3 호출부를 run_existing_agent2_agent3() 안으로 그대로
 * 옮기면 된다.
 */

#include "common/robot_types.h"
#include "human_target_angle/pose_mapping.h"

typedef struct {
    PoseMappingContext agent1;
} Agent123UartBridge;

int agent123_uart_bridge_init(Agent123UartBridge *bridge)
{
    if (bridge == 0) return -1;
    return pose_mapping_init(&bridge->agent1);
}

/*
 * 이 함수 안에 현재 프로젝트의 "기존 Agent2 -> Agent3 호출부"만 넣는다.
 *
 * 예:
 *   HumanJointTarget
 *       -> Agent2 robot calibration / motion
 *       -> Agent3 servo/output
 *       -> custom HW pwm_gen register write
 *
 * UART/CNN 여부와 무관하게 이 부분은 동일해야 한다.
 */
static void run_existing_agent2_agent3(
    const HumanJointTarget *human_target,
    float dt_sec
)
{
    (void)human_target;
    (void)dt_sec;

    /*
     * TODO:
     * 기존 src/main.c에 이미 있는 Agent2 -> Agent3 호출을 이곳으로 이동.
     *
     * 여기서 새 API를 만들거나 Agent1 안에 Agent2/3 코드를 넣지 말 것.
     */
}

int agent123_uart_bridge_process(
    Agent123UartBridge *bridge,
    const HumanPose2D *pose,
    PoseArmSide active_arm,
    float dt_sec
)
{
    HumanJointTarget human_target;
    int agent1_ret;

    if (bridge == 0 || pose == 0) return -1;

    agent1_ret = pose_mapping_update(
        &bridge->agent1,
        pose,
        active_arm,
        dt_sec,
        &human_target
    );

    /*
     * ret=0(HOLD)이어도 target.valid=1이면 기존 정상 target을
     * 유지해서 Agent2/3으로 보낼 수 있다.
     */
    if (human_target.valid) {
        run_existing_agent2_agent3(&human_target, dt_sec);
    }

    return agent1_ret;
}
