#ifndef INTEGRATION_PLATFORM_H
#define INTEGRATION_PLATFORM_H

#include <stdint.h>

/*
 * 플랫폼 경계 선언 (구현 없음).
 *
 * 이 함수들은 main이 부르는 계약일 뿐이고 구현은 이 저장소에 없다.
 *  - Vitis workspace 확정 후: 실제 타이머/UART/서보 초기화로 구현
 *  - 호스트 테스트: 테스트 코드가 가짜(test double) 구현을 제공
 */

/*
 * 플랫폼 초기화. 성공 0, 실패 -1.
 * Vitis에서는 UART/타이머 초기화와 servo_hal_init()를 여기서 한다.
 */
int platform_init(void);

/*
 * 20ms 제어 틱이 도래했으면 1, 아니면 0.
 * 구현은 폴링이든 타이머 ISR이 세운 플래그든 상관없다.
 * ISR 안에서 Agent를 직접 실행하면 안 된다.
 */
int platform_tick_due(void);
/* Drain shared CNN-console and ROBOT_TRACE output without waiting for UART. */
void platform_uart_service(void);

/* 시작 이후 버린 20 ms tick의 누적 개수. RECORD/PLAY는 값이 변하면
 * fixed 50 Hz 시간축을 복구할 수 없으므로 현재 동작을 중단한다. */
uint32_t platform_tick_overrun_count(void);

#endif /* INTEGRATION_PLATFORM_H */
