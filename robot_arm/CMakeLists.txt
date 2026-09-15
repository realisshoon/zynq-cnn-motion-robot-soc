cmake_minimum_required(VERSION 3.16)

project(robot_arm_2d_control C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

add_library(human_target_angle STATIC
    src/human_target_angle/pose_mapping.c
)
configure_robot_target(human_target_angle)

add_library(robot_calibration STATIC
    src/robot_calibration/kinematics_2d.c
    src/robot_calibration/motion_control.c
)
configure_robot_target(robot_calibration)

add_library(output_controller STATIC
    src/output_controller/output_control.c
    src/output_controller/servo_control.c
    src/output_controller/servo_config.c
    src/output_controller/motion_record.c
    src/output_controller/robot_mode.c
)
configure_robot_target(output_controller)

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

include(CTest)

if(BUILD_TESTING)
    add_executable(test_pose_mapping tests/human_target_angle/test_pose_mapping.c)
    configure_robot_target(test_pose_mapping)
    target_link_libraries(test_pose_mapping PRIVATE human_target_angle)
    add_test(NAME pose_mapping COMMAND test_pose_mapping)

    add_executable(test_kinematics_2d tests/robot_calibration/test_kinematics_2d.c)
    configure_robot_target(test_kinematics_2d)
    target_link_libraries(test_kinematics_2d PRIVATE robot_calibration)
    add_test(NAME kinematics_2d COMMAND test_kinematics_2d)

    add_executable(test_servo_control tests/output_controller/test_servo_control.c)
    configure_robot_target(test_servo_control)
    target_link_libraries(test_servo_control PRIVATE output_controller)
    add_test(NAME servo_control COMMAND test_servo_control)

    add_executable(test_record tests/output_controller/test_record.c)
    configure_robot_target(test_record)
    target_link_libraries(test_record PRIVATE output_controller)
    add_test(NAME motion_record COMMAND test_record)

    add_executable(test_output_control tests/output_controller/test_output_control.c)
    configure_robot_target(test_output_control)
    target_link_libraries(test_output_control PRIVATE output_controller)
    add_test(NAME output_control COMMAND test_output_control)
endif()
