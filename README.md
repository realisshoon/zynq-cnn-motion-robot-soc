# zynq-cnn-motion-robot-soc

Zybo Z7-20 기반의 **CNN·스테레오 비전·5축 로봇팔 제어 펌웨어** 저장소다.
카메라 관측을 사람 오른팔의 전완·손목 목표로 변환하고, 로봇별 보정과 안전검사,
50 Hz 궤적 제어를 거쳐 PWM으로 출력한다. SD 동작 녹화·반복 재생과 Unity UART 연동을 지원한다.

## 저장소 구조

```text
robot_arm/
├── src/          실행 코드: app(기능·정책), drivers(하드웨어 접근)
├── include/      src 구조에 대응하는 공개 헤더·공통 타입
├── config/       통신·제어 주기·로봇 기구 설정
├── vitis/        Vitis 빌드 스크립트·XSA·과거 BOOT 자료
├── pc/           과거 PC 입력 스트림 시험 도구
├── sample/       예제 영상
├── scripts/      과거 CMake 기반 호스트 도구(현행 빌드에 사용하지 않음)
└── README.md     상세 코드 트리·모듈 역할·빌드 방법
```

## 보드 역할

| 펌웨어 역할 | 제어 대상 | 로봇 PWM 출력 |
|---|---|---|
| RIGHT / 메인 보드 | 로봇0 | JB, CH0~4 |
| LEFT / 서브 보드 | 로봇1 | JC, CH0~4 |

두 로봇 모두 **사람 오른팔**을 기준으로 추종한다. LEFT는 RIGHT가 전달한 사람 목표를
자기 로봇의 보정·안전검사에 연결하며, 각 보드의 실시간 추종과 재생 모드는 독립적이다.

자세한 설명은 [로봇팔 코드 안내](robot_arm/README.md)를 참고한다.
현재 펌웨어의 빌드 진입점은 [Vitis 설정 스크립트](robot_arm/vitis/setup_vitis.ps1)다.

`robot_arm/docs/`, `tests/`, `tools/`, `etc/`는 로컬 전용이며 Git 배포본에 포함하지 않는다.
현재 소스와 과거 SD 설치 BOOT의 동일성이나 실물 검증 완료를 의미하는 저장소는 아니다.
