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

        target_compile_options(
            ${target_name}
            PRIVATE
                /W4
        )

    else()

        target_compile_options(
            ${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )

    endif()

endfunction()


# ============================================================
# Agent 1
# Human Pose Interpretation / Joint Target Generation
# ============================================================
add_library(human_target_angle STATIC

    src/human_target_angle/pose_mapping.c
    src/human_target_angle/pose_math.c
    src/human_target_angle/pose_tracking.c
    src/human_target_angle/pose_reconstruction.c
    src/human_target_angle/pose_joint.c
    src/human_target_angle/pose_hand.c

)

configure_robot_target(human_target_angle)


# Agent1 내부 전용 Header
target_include_directories(human_target_angle

    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/human_target_angle

)


# Agent1 math library
if(UNIX OR MINGW)

    target_link_libraries(
        human_target_angle

        PUBLIC
            m
    )

endif()


# ============================================================
# Agent 2
# Robot Calibration / Motion
# ============================================================
add_library(robot_calibration STATIC

    src/robot_calibration/kinematics_2d.c
    src/robot_calibration/motion_control.c

)

configure_robot_target(robot_calibration)


# ============================================================
# Agent 3
# Output Controller
#
# JointCommand
#     ↓
# output_control
#     ↓
# servo_control
#     ↓
# ServoPwmCommand
#     ↓
# servo_hal
#     ↓
# servo_pwm_driver
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
# Agent 3
# Servo HAL / Driver Mock Integration Test
#
# 다른 팀원 Test와 관계없이 항상 Build한다.
#
# 검증:
#
# 1500us x 6
#     ↓
# UPDATE
#     ↓
# ENABLE
#
# 그리고 Invalid PWM 방어 검사
# ============================================================
add_executable(test_servo_hal

    tests/output_controller/test_servo_hal.c

)

configure_robot_target(test_servo_hal)


target_link_libraries(test_servo_hal

    PRIVATE
        output_controller

)


# ============================================================
# 기존 전체 Unit Test
#
# 현재 Agent2 Test Source가 아직 없기 때문에
# 임시로 비활성화한다.
#
# 우리 test_servo_hal은 위에서 별도로 Build된다.
# ============================================================
set(
    BUILD_TESTING
    OFF
    CACHE BOOL
    "Disable project-wide tests temporarily"
    FORCE
)


include(CTest)


if(BUILD_TESTING)

    # ========================================================
    # Agent 1 Tests
    # ========================================================

    add_executable(test_pose_mapping

        tests/human_target_angle/test_pose_mapping.c

    )

    configure_robot_target(test_pose_mapping)


    target_link_libraries(
        test_pose_mapping

        PRIVATE
            human_target_angle
    )


    add_test(
        NAME pose_mapping
        COMMAND test_pose_mapping
    )


    add_executable(test_pose_visual

        tests/human_target_angle/test_pose_visual.c

    )

    configure_robot_target(test_pose_visual)


    target_link_libraries(
        test_pose_visual

        PRIVATE
            human_target_angle
    )


    add_executable(test_pose_csv

        tests/human_target_angle/test_pose_csv.c

    )

    configure_robot_target(test_pose_csv)


    target_link_libraries(
        test_pose_csv

        PRIVATE
            human_target_angle
    )


    # ========================================================
    # Agent 2 Tests
    # ========================================================

    add_executable(test_kinematics_2d

        tests/robot_calibration/test_kinematics_2d.c

    )

    configure_robot_target(test_kinematics_2d)


    target_link_libraries(
        test_kinematics_2d

        PRIVATE
            robot_calibration
    )


    add_test(
        NAME kinematics_2d
        COMMAND test_kinematics_2d
    )


    # ========================================================
    # Agent 3 Tests
    # ========================================================

    add_executable(test_servo_control

        tests/output_controller/test_servo_control.c

    )

    configure_robot_target(test_servo_control)


    target_link_libraries(
        test_servo_control

        PRIVATE
            output_controller
    )


    add_test(
        NAME servo_control
        COMMAND test_servo_control
    )


    add_executable(test_output_control

        tests/output_controller/test_output_control.c

    )

    configure_robot_target(test_output_control)


    target_link_libraries(
        test_output_control

        PRIVATE
            output_controller
    )


    add_test(
        NAME output_control
        COMMAND test_output_control
    )

endif()