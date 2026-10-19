cmake_minimum_required(VERSION 3.16)

project(robot_arm_runtime C)

option(BUILD_TESTING "Build host tests supplied in the local validation bundle" OFF)
if(BUILD_TESTING)
    foreach(required_path
        tests/uart_pose/test_uart_agent1_pc.c
        tests/integration/stubs/xil_types.h
        tests/integration/stubs/telemetry_stub.c
        tools/analyze_forearm_replay.py
        etc/uart_pose_stream.bin
        etc/example_pose2d_1280x720_20hz.csv
    )
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${required_path}")
            message(FATAL_ERROR
                "BUILD_TESTING=ON requires local-only tests/, tools/, and etc/. "
                "Missing: ${required_path}. Restore the validation bundle or set BUILD_TESTING=OFF.")
        endif()
    endforeach()
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_C_STANDARD 11)
endif()
set(ROBOT_BOARD_ROLE "RIGHT" CACHE STRING "Release board role: LEFT or RIGHT")
set_property(CACHE ROBOT_BOARD_ROLE PROPERTY STRINGS LEFT RIGHT)
if(NOT ROBOT_BOARD_ROLE STREQUAL "LEFT" AND NOT ROBOT_BOARD_ROLE STREQUAL "RIGHT")
    message(FATAL_ERROR "ROBOT_BOARD_ROLE must be LEFT or RIGHT")
endif()

# ============================================================
# 공통 빌드 설정
# ============================================================
function(configure_robot_target target_name)
    target_compile_definitions(${target_name} PUBLIC ROBOT_STEREO_${ROBOT_BOARD_ROLE}=1)
    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/config>
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ============================================================
# Human pose interpretation and target generation
# ============================================================
add_library(human_target_angle STATIC
    src/app/robot/vision/human_target/pose_mapping.c
    src/app/robot/vision/human_target/pose_math.c
    src/app/robot/vision/human_target/pose_tracking.c
    src/app/robot/vision/human_target/pose_reconstruction.c
    src/app/robot/vision/human_target/pose_joint.c
    src/app/robot/vision/human_target/pose_hand.c
    src/app/robot/vision/human_target/forearm_mapping.c
    src/app/robot/vision/human_target/forearm_target.c
)
configure_robot_target(human_target_angle)

add_library(stereo_vision STATIC
    src/app/robot/vision/stereo/stereo_geometry.c
    src/app/robot/vision/stereo/stereo_calibration.c
    src/app/robot/vision/stereo/stereo_pose.c
    src/app/robot/vision/stereo/stereo_uart_protocol.c
    src/app/robot/vision/stereo/robot_follow_protocol.c
    src/app/robot/vision/stereo/stereo_link.c
    src/app/robot/vision/stereo/stereo_kalman3d.c
    src/app/robot/vision/stereo/stereo_pose_filter.c
    src/app/robot/vision/stereo/stereo_filter_command.c
)
configure_robot_target(stereo_vision)
if(UNIX OR MINGW)
    target_link_libraries(stereo_vision PUBLIC m)
endif()

# pose_mapping_internal.h는 human target 내부 전용 헤더다.
target_include_directories(human_target_angle
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/robot/vision/human_target
)

# human target은 sqrtf/atan2f/acosf/expf/hypotf 등을 사용한다.
if(UNIX OR MINGW)
    target_link_libraries(human_target_angle PUBLIC m)
endif()

# ============================================================
# XSA 없는 PC용 UART + human target test
# ============================================================

add_library(uart_pose_protocol_pc
    src/drivers/uart/uart_pose_protocol.c
)

target_include_directories(uart_pose_protocol_pc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(BUILD_TESTING)
    add_executable(test_uart_agent1_pc
        tests/uart_pose/test_uart_agent1_pc.c
    )

    target_link_libraries(test_uart_agent1_pc
        PRIVATE
            uart_pose_protocol_pc
            human_target_angle
    )

    target_include_directories(test_uart_agent1_pc
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/config
    )
endif()

# ============================================================
# Calibration, safety and motion control
# 팀원 영역 - 기존 설정 유지
# ============================================================

add_library(robot_calibration STATIC
    src/app/robot/control/forearm_calibration.c
    src/app/robot/control/forearm_calibration_config.c
    src/app/robot/control/forearm_motion_control.c
    src/app/robot/control/forearm_safety_check.c
    src/app/robot/control/motion.c
)
configure_robot_target(robot_calibration)

if(UNIX OR MINGW)
    target_link_libraries(robot_calibration PUBLIC m)
endif()

# ============================================================
# PWM conversion and output control
# 팀원 영역 - 기존 설정 유지
# ============================================================
add_library(output_controller STATIC
    src/app/robot/control/servo_control.c
    src/app/robot/control/servo_config.c
    src/drivers/pwm/servo_hal.c
    src/drivers/pwm/servo_pwm_driver.c
)
configure_robot_target(output_controller)
target_link_libraries(output_controller PUBLIC robot_calibration)

add_library(robot_pipeline STATIC
    src/app/robot/vision/human_target.c
    src/app/robot/control/robot_motion.c
    src/app/robot/control/servo_output.c
    src/app/robot/control/gripper_control.c
)
configure_robot_target(robot_pipeline)
target_link_libraries(robot_pipeline PUBLIC human_target_angle robot_calibration output_controller)

if(BUILD_TESTING)
    string(TOLOWER "${ROBOT_BOARD_ROLE}" robot_board_source)
    add_library(robot_board_input STATIC src/app/board/${robot_board_source}_input.c)
    configure_robot_target(robot_board_input)
    target_include_directories(robot_board_input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/cnn
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/stubs
    )
    target_link_libraries(robot_board_input PUBLIC robot_pipeline stereo_vision)
endif()

# ============================================================
# Tests
# ============================================================
include(CTest)

if(BUILD_TESTING)
    foreach(name body_frame forearm pose_visual)
        add_executable(test_${name} tests/human_target_angle/test_${name}.c)
        configure_robot_target(test_${name})
        target_link_libraries(test_${name} PRIVATE human_target_angle uart_pose_protocol_pc)
    endforeach()
    foreach(name body_frame forearm)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()
    add_test(NAME forearm_uart COMMAND test_forearm ${CMAKE_CURRENT_SOURCE_DIR}/etc/uart_pose_stream.bin)
    add_test(NAME forearm_csv COMMAND test_pose_csv
        ${CMAKE_CURRENT_SOURCE_DIR}/etc/example_pose2d_1280x720_20hz.csv
        ${CMAKE_CURRENT_BINARY_DIR}/forearm_result.csv)
    add_executable(test_human_target_trace tests/human_target_angle/test_agent1_trace.c
        src/app/robot/vision/human_target/human_target_stage.c src/app/robot/vision/human_target/forearm_target.c)
    configure_robot_target(test_human_target_trace)
    target_link_libraries(test_human_target_trace PRIVATE human_target_angle)
    add_test(NAME human_target_trace COMMAND test_human_target_trace)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME forearm_replay_stats COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/analyze_forearm_replay.py
            ${CMAKE_CURRENT_BINARY_DIR}/forearm_result.csv --expect-frames 522)
        set_tests_properties(forearm_replay_stats PROPERTIES DEPENDS forearm_csv)
    endif()
    add_library(robot_test_telemetry STATIC tests/integration/stubs/telemetry_stub.c)
    configure_robot_target(robot_test_telemetry)
    target_link_libraries(stereo_vision PUBLIC robot_test_telemetry)
    foreach(name stereo_kalman stereo_pose_filter stereo_filter_command)
        add_executable(test_${name} tests/integration/test_${name}.c)
        configure_robot_target(test_${name})
        target_link_libraries(test_${name} PRIVATE stereo_vision)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()
    add_executable(test_stereo_filter_runtime tests/integration/test_stereo_filter_runtime.c
        src/app/robot/vision/input_pose_cnn.c)
    configure_robot_target(test_stereo_filter_runtime)
    target_include_directories(test_stereo_filter_runtime PRIVATE tests/integration/stubs)
    target_link_libraries(test_stereo_filter_runtime PRIVATE stereo_vision)
    add_test(NAME stereo_filter_runtime COMMAND test_stereo_filter_runtime)
    add_executable(test_forearm_stereo_absolute tests/human_target_angle/test_forearm_stereo_absolute.c)
    configure_robot_target(test_forearm_stereo_absolute)
    target_link_libraries(test_forearm_stereo_absolute PRIVATE human_target_angle)
    add_test(NAME forearm_stereo_absolute COMMAND test_forearm_stereo_absolute)
    # Human target
    add_executable(test_pose_mapping
        tests/human_target_angle/test_pose_mapping.c
    )
    configure_robot_target(test_pose_mapping)
    target_link_libraries(test_pose_mapping PRIVATE human_target_angle)
    add_test(NAME pose_mapping COMMAND test_pose_mapping)

    # 실제 CSV 입력용 human target 디버그 실행 파일.
    # 입력 파일이 필요하므로 ctest에는 등록하지 않는다.
    add_executable(test_pose_csv
        tests/human_target_angle/test_pose_csv.c
    )
    configure_robot_target(test_pose_csv)
    target_link_libraries(test_pose_csv PRIVATE human_target_angle)

    # Active five-axis motion control regressions.
    foreach(name forearm_calibration forearm_safety_check forearm_replay)
        add_executable(test_${name} tests/robot_calibration/test_${name}.c)
        configure_robot_target(test_${name})
        target_link_libraries(test_${name} PRIVATE robot_calibration human_target_angle)
    endforeach()
    add_test(NAME forearm_calibration COMMAND test_forearm_calibration)
    add_test(NAME forearm_safety_check COMMAND test_forearm_safety_check)
    add_test(NAME forearm_replay COMMAND test_forearm_replay
        ${CMAKE_CURRENT_SOURCE_DIR}/etc/example_pose2d_1280x720_20hz.csv)

    # Servo output - 팀원 영역
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
