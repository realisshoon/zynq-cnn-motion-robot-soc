#ifndef OUTPUT_CONTROLLER_MOTION_RECORD_H
#define OUTPUT_CONTROLLER_MOTION_RECORD_H

#include <stdint.h>

#include "common/robot_types.h"


/*
 * ============================================================
 * Motion Record Configuration
 * ============================================================
 *
 * 녹화 Sampling Rate:
 *   30 Hz
 *
 * 최대 녹화 시간:
 *   60 sec
 *
 * 따라서:
 *   30 x 60 = 1800개의 JointCommand 저장
 *
 * 나중에 값만 수정하면 저장 길이를 쉽게 변경할 수 있다.
 */
#define MOTION_RECORD_SAMPLE_HZ       (30U)
#define MOTION_RECORD_MAX_SECONDS     (60U)

#define MOTION_RECORD_MAX_FRAMES \
    (MOTION_RECORD_SAMPLE_HZ * MOTION_RECORD_MAX_SECONDS)


/*
 * Playback 반복 여부
 *
 * 0 = 한 번 재생 후 종료
 * 1 = 끝나면 처음부터 반복
 *
 * 현재는 안전하게 1회 재생으로 설정.
 */
#define MOTION_RECORD_PLAYBACK_LOOP   (0U)


/*
 * Motion Record 초기화
 *
 * 프로그램 시작 시 한 번 호출한다.
 */
void motion_record_init(void);


/*
 * 기존 녹화 내용을 논리적으로 삭제한다.
 *
 * 새로운 녹화를 시작할 때 호출한다.
 */
void motion_record_clear(void);


/*
 * JointCommand 하나 저장
 *
 * 반환:
 *   1 = 저장 성공
 *   0 = 저장 실패
 */
uint8_t motion_record_append(
    const JointCommand *joint_cmd
);


/*
 * 녹화 데이터 존재 여부
 *
 * 반환:
 *   1 = 데이터 있음
 *   0 = 데이터 없음
 */
uint8_t motion_record_has_data(void);


/*
 * 현재 저장된 JointCommand 개수
 */
uint32_t motion_record_get_count(void);


/*
 * 최대 저장 가능한 JointCommand 개수
 */
uint32_t motion_record_get_capacity(void);


/*
 * Playback 시작
 *
 * read index를 첫 번째 데이터로 초기화한다.
 *
 * 반환:
 *   1 = Playback 시작 가능
 *   0 = 녹화 데이터 없음
 */
uint8_t motion_record_playback_start(void);


/*
 * 다음 JointCommand를 읽는다.
 *
 * 반환:
 *   1 = 정상적으로 읽음
 *   0 = Playback 종료 또는 오류
 */
uint8_t motion_record_playback_next(
    JointCommand *joint_cmd
);


/*
 * Playback 완료 여부
 *
 * 반환:
 *   1 = 완료
 *   0 = 아직 재생 중
 */
uint8_t motion_record_playback_finished(void);


#endif /* OUTPUT_CONTROLLER_MOTION_RECORD_H */