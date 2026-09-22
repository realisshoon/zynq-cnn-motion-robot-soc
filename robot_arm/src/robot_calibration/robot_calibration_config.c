#include "robot_calibration/robot_calibration_config.h"

/* Initial bench profile: 0.6 deg / 20 ms = 30 deg/s command limit.
 * This is not a load/torque rating. All five rotary servos are limited to 20..160.
 * Gripper 0/1 intent and A3 PWM conversion are unchanged. */
const RobotCalibrationConfig robot_calibration_config = {
    .base = {
        .scale = 1.0f,
        .direction = 1,
        .zero_offset_deg = 90.0f,
        .min_deg = 20.0f,
        .max_deg = 160.0f,
        .max_delta_deg = 0.6f
    },
    .shoulder = {
        .scale = 1.0f,
        .direction = 1,
        .zero_offset_deg = 90.0f,
        .min_deg = 20.0f,
        .max_deg = 160.0f,
        .max_delta_deg = 0.6f
    },
    .elbow = {
        .scale = 1.0f,
        .direction = -1,
        .zero_offset_deg = 270.0f,
        .min_deg = 20.0f,
        .max_deg = 160.0f,
        .max_delta_deg = 0.6f
    },
    .wrist_pitch = {
        .scale = 0.0f, /* Neutral lock until wrist direction/reference is measured. */
        .direction = 1,
        .zero_offset_deg = 90.0f,
        .min_deg = 20.0f,
        .max_deg = 160.0f,
        .max_delta_deg = 0.6f
    },
    .wrist_roll = {
        .scale = 0.0f, /* Neutral lock until wrist direction/reference is measured. */
        .direction = 1,
        .zero_offset_deg = 90.0f,
        .min_deg = 20.0f,
        .max_deg = 160.0f,
        .max_delta_deg = 0.6f
    },
};

