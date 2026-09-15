#include "output_controller/motion_record.h"

#include <stddef.h>


/*
 * ============================================================
 * Motion Record Buffer
 * ============================================================
 *
 * Zybo PS(ARM)에서 사용하는 정적 메모리 Buffer.
 *
 * 동적 메모리 할당(malloc)을 사용하지 않는다.
 *
 * 실제 DDR / OCM 배치는 나중에 Linker Script에서
 * 결정할 수 있다.
 */
static JointCommand record_buffer[MOTION_RECORD_MAX_FRAMES];


/*
 * 현재 저장된 JointCommand 개수.
 *
 * 동시에 다음 Write 위치를 의미한다.
 *
 * 예:
 *
 * record_count = 0
 * -> 다음 저장 위치 = record_buffer[0]
 *
 * record_count = 5
 * -> record_buffer[0] ~ [4] 사용 중
 * -> 다음 저장 위치 = record_buffer[5]
 */
static uint32_t record_count = 0U;


/*
 * Playback에서 현재 읽을 위치.
 *
 * Verilog로 비유하면 read address counter.
 */
static uint32_t playback_index = 0U;


/*
 * Playback 완료 상태.
 *
 * 0 = 재생 중 / 재생 전
 * 1 = 재생 완료
 */
static uint8_t playback_finished = 0U;


/*
 * ============================================================
 * Initialization
 * ============================================================
 */
void motion_record_init(void)
{
    record_count = 0U;
    playback_index = 0U;
    playback_finished = 0U;
}


/*
 * ============================================================
 * Clear
 * ============================================================
 *
 * 배열 전체를 0으로 초기화하지 않는다.
 *
 * record_count만 0으로 만들면 이전 값은
 * 더 이상 유효한 Record 데이터로 취급하지 않는다.
 *
 * 큰 Buffer 전체를 memset하는 것보다 효율적이다.
 */
void motion_record_clear(void)
{
    record_count = 0U;
    playback_index = 0U;
    playback_finished = 0U;
}


/*
 * ============================================================
 * Record
 * ============================================================
 */
uint8_t motion_record_append(
    const JointCommand *joint_cmd
)
{
    /*
     * NULL Pointer 방지
     */
    if (joint_cmd == NULL) {
        return 0U;
    }


    /*
     * Invalid JointCommand는 녹화하지 않는다.
     */
    if (!joint_cmd->valid) {
        return 0U;
    }


    /*
     * Buffer가 가득 찬 경우.
     *
     * 기존 데이터를 덮어쓰지 않고
     * 새로운 데이터 저장을 거부한다.
     */
    if (record_count >= MOTION_RECORD_MAX_FRAMES) {
        return 0U;
    }


    /*
     * JointCommand 구조체 전체 복사.
     *
     * Verilog로 비유하면:
     *
     * memory[write_addr] <= write_data;
     */
    record_buffer[record_count] = *joint_cmd;


    /*
     * 다음 Write Address
     */
    record_count++;


    return 1U;
}


/*
 * ============================================================
 * Record Data Check
 * ============================================================
 */
uint8_t motion_record_has_data(void)
{
    return (record_count > 0U) ? 1U : 0U;
}


/*
 * ============================================================
 * Record Count
 * ============================================================
 */
uint32_t motion_record_get_count(void)
{
    return record_count;
}


/*
 * ============================================================
 * Record Capacity
 * ============================================================
 */
uint32_t motion_record_get_capacity(void)
{
    return (uint32_t)MOTION_RECORD_MAX_FRAMES;
}


/*
 * ============================================================
 * Playback Start
 * ============================================================
 */
uint8_t motion_record_playback_start(void)
{
    /*
     * 저장된 Motion이 없으면
     * Playback 불가능.
     */
    if (record_count == 0U) {

        playback_finished = 1U;

        return 0U;
    }


    /*
     * 첫 번째 JointCommand부터 읽기 시작.
     */
    playback_index = 0U;
    playback_finished = 0U;


    return 1U;
}


/*
 * ============================================================
 * Playback Next
 * ============================================================
 */
uint8_t motion_record_playback_next(
    JointCommand *joint_cmd
)
{
    /*
     * NULL Pointer 방지.
     */
    if (joint_cmd == NULL) {
        return 0U;
    }


    /*
     * 녹화 데이터 없음.
     */
    if (record_count == 0U) {

        playback_finished = 1U;

        return 0U;
    }


    /*
     * 현재 Index가 Record 끝까지 도달한 경우.
     */
    if (playback_index >= record_count) {

#if MOTION_RECORD_PLAYBACK_LOOP

        /*
         * 반복 Playback.
         *
         * 마지막 데이터 이후 다시 0번부터 시작.
         */
        playback_index = 0U;

#else

        /*
         * 1회 Playback.
         */
        playback_finished = 1U;

        return 0U;

#endif
    }


    /*
     * 현재 Playback 데이터 출력.
     *
     * Verilog로 비유하면:
     *
     * read_data = memory[read_addr];
     */
    *joint_cmd = record_buffer[playback_index];


    /*
     * 다음 Read Address.
     */
    playback_index++;


#if !MOTION_RECORD_PLAYBACK_LOOP

    /*
     * 방금 읽은 Command가 마지막이었다면
     * Playback 완료 표시.
     */
    if (playback_index >= record_count) {
        playback_finished = 1U;
    }

#endif


    return 1U;
}


/*
 * ============================================================
 * Playback Finished
 * ============================================================
 */
uint8_t motion_record_playback_finished(void)
{
    return playback_finished;
}