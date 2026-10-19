# STEP 4 이력 보존·Draft PR 준비 검증

실행일: 2026-10-19 (Asia/Seoul).
사용자 승인: “권고안 승인: 원본 이력 270개 연결 후 push·Draft PR”.

## 실제 수행 결과

| 항목 | 결과 |
|---|---|
| 승인 기준 commit | `5ec588fa5332db16e4f5e43295d526cad24bf407` |
| 후속 ours merge | `c31640cdcf9aeb4d830254e809badeb4008a804e` |
| 부모 수 | 19개: 기준 commit + 비중복 원본 tip 18개 |
| merge 전후 전체 tree | `61aa43cafe4e753440a70cdc55e69cd4d63d4fc1` — 동일 |
| merge 전후 파일 diff | 0 bytes |
| 원본 SHA 조상 검사 | 270/270 PASS, 미연결 0 |
| 기존 commit 재작성 | 없음. 원본 SHA·작성자·작성일 보존 |
| source 7개 영역 / PPT media 7개 | tree ID / SHA256 일치 PASS |
| 공개 전 전체 working file 검사 | 2034개 PASS |
| 원래 Unity 작업폴더 | HEAD·index bytes·index entries·untracked·stash 보존 PASS |
| 기존 로컬 branch / 원격 원본 SHA | 보존 PASS |
| main 병합·force push·branch 삭제·ruleset 변경 | 수행하지 않음 |

상세 부모 SHA·원본 270개 SHA·작성자·날짜·조상 판정은 [HISTORY_PRESERVATION.json](HISTORY_PRESERVATION.json)에 있다. 이력 merge 뒤에는 이 README·출처·검증 문서의 상태 갱신만 별도 commit으로 추가한다. 기능 소스와 시연 미디어는 변경하지 않는다.

작업파일 hash 중 2033개는 Git clean-filter 값과 일치한다. `robot_arm/.clang-format`은 index와 원본 blob이 CRLF라 raw bytes로 원본 일치를 확인했다. 줄바꿈 변환이나 내용 수정은 하지 않았다.

## 현행 snapshot과 승인한 방식의 차이

STEP 3 snapshot은 선택 파일 통합에 적합하고 간결하지만 원본 이력 270개를 통합 후보의 조상으로 연결하지 않는다. 개발 branch가 남아 있는 동안 원본을 조회하는 것과, 최종 main에서 원본 SHA에 도달하는 것은 다르다.

승인한 `ours` merge는 파일 tree를 유지하고 원본 18개 tip을 부모로 연결한다. 선택 source 이력 168개와 별도 feature 원본 102개, 합계 270개를 모두 포함한다. 3개 source tip만 연결하는 방안은 별도 feature 원본 102개를 포함하지 못하므로 선택하지 않았다.

부모가 많은 이력 graph와 향후 merge-base 영향은 감수한다. 원본에서 선택하지 않은 과거 파일도 history에는 조회 가능하다. 현재 파일의 `git blame`가 모두 원작성자로 바뀌거나 GitHub Contributors 수치가 동일하게 표시된다고 보장하지 않는다. `ours` 전략은 `-X ours` 옵션과 구분한다. [Git 공식 전략](https://git-scm.com/docs/merge-strategies)을 따른다.

## GitHub 정책과 공개 범위

읽기 전용 API로 main ruleset의 PR 허용 merge 방식 `merge / squash` 및 Repository의 merge commit 허용을 확인했다. 승인 review 1개·토론 해결·main update 제한을 변경하지 않았다. 이력 SHA를 향후 main의 조상으로 보존하려면 **merge commit 방식**이 필요하다. squash/rebase로 최종 반영하지 않는다. [GitHub merge 안내](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/about-merge-methods-on-github).

일반 push와 **Draft PR 생성까지만** 이번 승인 범위다. 실제 원격 head·Draft 상태·README GitHub 이미지 렌더링은 공개 후 확인하여 PR 본문과 완료 보고에 기록한다. 이 파일은 공개 전 검증 기록이며 원격 작업의 완료를 미리 선언하지 않는다.

## 기존 실행 검증 결과

STEP 3 실제 실행 판정은 변경하지 않는다. Directed TB 12 PASS / 3 FAIL / 1 BLOCKED, Packed Golden 5개 입력 PASS, ZIP Golden FAIL, UVM Common 제한된 smoke PASS 및 Control/DMA 실패·전체 회귀 BLOCKED, Firmware 직접 RIGHT/LEFT 빌드 PASS·표준 IDE 경로 BLOCKED, Unity Import/Build·2836개 오프라인 검사 PASS다.

RTL/Firmware VERSION·FEATURES·PACK_ID, RGBY Register Map, XSA·BOOT·가중치 출처, Native DLL 재빌드 대응과 물리 UART·FPGA·로봇 검증은 [미해결 문제](../../INTEGRATION_BLOCKERS.md)를 유지한다. main은 아직 미병합이며 실제 시스템 릴리스 승인 상태가 아니다.
