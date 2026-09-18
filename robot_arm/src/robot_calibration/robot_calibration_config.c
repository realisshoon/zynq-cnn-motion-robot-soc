#include "robot_calibration/robot_calibration_config.h"

const RobotCalibrationConfig robot_calibration_config = {
    .base = {
        .scale = 1.0f,
        .direction = 1,
        .zero_offset_deg = 90.0f,
        .min_deg = 10.0f,
        .max_deg = 170.0f,
        .max_delta_deg = 3.0f
    },
    .shoulder = {
        .scale = 1.0f,
        .direction = 1,
        .zero_offset_deg = 90.0f,
        .min_deg = 20.0f,
        .max_deg = 160.0f,
        .max_delta_deg = 3.0f
    },
    .elbow = {
        .scale = 1.0f,
        .direction = 1,
        .zero_offset_deg = 90.0f,
        .min_deg = 10.0f,
        .max_deg = 170.0f,
        .max_delta_deg = 4.0f
    },
    .wrist_pitch = {
        .scale = 1.0f,
        .direction = 1,
        .zero_offset_deg = 90.0f,
        .min_deg = 20.0f,
        .max_deg = 160.0f,
        .max_delta_deg = 5.0f
    },
    .wrist_roll = {
        .scale = 1.0f,
        .direction = 1,
        .zero_offset_deg = 90.0f,
        .min_deg = 0.0f,
        .max_deg = 180.0f,
        .max_delta_deg = 5.0f
    },
};

