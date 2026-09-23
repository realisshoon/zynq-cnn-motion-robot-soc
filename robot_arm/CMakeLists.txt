cmake_minimum_required(VERSION 3.16)

project(robot_arm_2d_control C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ============================================================
# 공통 빌드 설정
# ============================================================
function(configure_robot_target target_name)
    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/config>
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ============================================================
# Agent 1 : Human Pose Interpretation / Joint Target Generation
# ============================================================
add_library(human_target_angle STATIC
    src/human_target_angle/pose_mapping.c
    src/human_target_angle/pose_math.c
    src/human_target_angle/pose_tracking.c
    src/human_target_angle/pose_reconstruction.c
    src/human_target_angle/pose_joint.c
    src/human_target_angle/pose_hand.c
    src/human_target_angle/forearm_mapping.c
    src/human_target_angle/agent1_forearm_stage.c
)
configure_robot_target(human_target_angle)

# pose_mapping_internal.h는 Agent1 내부 전용 헤더다.
target_include_directories(human_target_angle
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/human_target_angle
)

# Agent1은 sqrtf/atan2f/acosf/expf/hypotf 등을 사용한다.
if(UNIX OR MINGW)
    target_link_libraries(human_target_angle PUBLIC m)
endif()

# ============================================================
# XSA 없는 PC용 UART + Agent1 test
# ============================================================

add_library(uart_pose_protocol_pc
    src/uart_pose/uart_pose_protocol.c
)

target_include_directories(uart_pose_protocol_pc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(test_uart_agent1_pc
    tests/uart_pose/test_uart_agent1_pc.c
)

target_link_libraries(test_uart_agent1_pc
    PRIVATE
        uart_pose_protocol_pc
        human_target_angle
        m
)

target_include_directories(test_uart_agent1_pc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/config
)

# ============================================================
# Agent 2 : Robot Calibration / Motion
# 팀원 영역 - 기존 설정 유지
# ============================================================
option(AGENT1_ONLY "Build Agent1 tests without Agent2/3 application dependencies" OFF)
if(AGENT1_ONLY)
    enable_testing()
    foreach(name pose_mapping body_frame forearm pose_csv pose_visual)
        add_executable(test_${name} tests/human_target_angle/test_${name}.c)
        configure_robot_target(test_${name})
        target_link_libraries(test_${name} PRIVATE human_target_angle uart_pose_protocol_pc)
    endforeach()
    foreach(name pose_mapping body_frame forearm)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()
    add_test(NAME forearm_uart COMMAND test_forearm ${CMAKE_CURRENT_SOURCE_DIR}/etc/uart_pose_stream.bin)
    add_test(NAME forearm_csv COMMAND test_pose_csv
        ${CMAKE_CURRENT_SOURCE_DIR}/etc/example_pose2d_1280x720_20hz.csv
        ${CMAKE_CURRENT_BINARY_DIR}/forearm_result.csv)
    add_executable(test_agent1_trace tests/human_target_angle/test_agent1_trace.c
        src/human_target_angle/agent1_stage.c src/human_target_angle/agent1_forearm_stage.c)
    configure_robot_target(test_agent1_trace)
    target_compile_definitions(test_agent1_trace PRIVATE ROBOT_TRACE)
    target_link_libraries(test_agent1_trace PRIVATE human_target_angle)
    add_test(NAME agent1_trace COMMAND test_agent1_trace)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME forearm_replay_stats COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/analyze_forearm_replay.py
            ${CMAKE_CURRENT_BINARY_DIR}/forearm_result.csv --expect-frames 522)
        set_tests_properties(forearm_replay_stats PROPERTIES DEPENDS forearm_csv)
    endif()
    return()
endif()

add_library(robot_calibration STATIC
    src/robot_calibration/robot_calibration.c
    src/robot_calibration/robot_calibration_config.c
    src/robot_calibration/safety_check.c
    src/robot_calibration/motion_control.c
    src/robot_calibration/motion_limits.c
    src/robot_calibration/motion_smoothing.c
)
configure_robot_target(robot_calibration)

if(UNIX OR MINGW)
    target_link_libraries(robot_calibration PUBLIC m)
endif()

# ============================================================
# Agent 3 : Output Controller
# 팀원 영역 - 기존 설정 유지
# ============================================================
add_library(output_controller STATIC
    src/output_controller/output_control.c
    src/output_controller/servo_control.c
    src/output_controller/servo_config.c
    src/output_controller/servo_hal.c
    src/drivers/servo_pwm_driver.c
)
configure_robot_target(output_controller)

# ============================================================
# Main Application
# ============================================================
add_executable(robot_arm_2d_control
    src/main.c
)
configure_robot_target(robot_arm_2d_control)

target_link_libraries(robot_arm_2d_control
    PRIVATE
        human_target_angle
        robot_calibration
        output_controller
)

# ============================================================
# Tests
# ============================================================
include(CTest)

if(BUILD_TESTING)
    # Agent 1
    add_executable(test_pose_mapping
        tests/human_target_angle/test_pose_mapping.c
    )
    configure_robot_target(test_pose_mapping)
    target_link_libraries(test_pose_mapping PRIVATE human_target_angle)
    add_test(NAME pose_mapping COMMAND test_pose_mapping)

    # 실제 CSV 입력용 Agent1 디버그 실행 파일.
    # 입력 파일이 필요하므로 ctest에는 등록하지 않는다.
    add_executable(test_pose_csv
        tests/human_target_angle/test_pose_csv.c
    )
    configure_robot_target(test_pose_csv)
    target_link_libraries(test_pose_csv PRIVATE human_target_angle)

    # Agent 2 - 팀원 영역
    add_executable(test_robot_calibration
        tests/robot_calibration/test_robot_calibration.c
    )
    configure_robot_target(test_robot_calibration)
    target_link_libraries(test_robot_calibration PRIVATE robot_calibration)
    add_test(NAME robot_calibration COMMAND test_robot_calibration)

    add_executable(test_safety_check
        tests/robot_calibration/test_safety_check.c
    )
    configure_robot_target(test_safety_check)
    target_link_libraries(test_safety_check PRIVATE robot_calibration)
    add_test(NAME safety_check COMMAND test_safety_check)

    # Agent 3 - 팀원 영역
    add_executable(test_servo_control
        tests/output_controller/test_servo_control.c
    )
    configure_robot_target(test_servo_control)
    target_link_libraries(test_servo_control PRIVATE output_controller)
    add_test(NAME servo_control COMMAND test_servo_control)

    add_executable(test_output_control
        tests/output_controller/test_output_control.c
    )
    configure_robot_target(test_output_control)
    target_link_libraries(test_output_control PRIVATE output_controller)
    add_test(NAME output_control COMMAND test_output_control)
endif()
