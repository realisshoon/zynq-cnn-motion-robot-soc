"""Build/run A2 and its integration regressions using host GCC, without CMake.

Usage: python robot_arm/tests/robot_calibration/run_tests.py
Requires GCC and Python's standard library. Artifacts go to a temporary directory.
No Vitis, board access, or tracked build output.
"""
import os
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", action="append", default=[], help="Run only named test suites")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    if not compiler:
        raise SystemExit("Host GCC not found; set CC to the compiler executable.")
    env = dict(os.environ)
    # On Windows avoid incompatible DLLs from another GCC earlier on PATH.
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    output = Path(tempfile.mkdtemp(prefix="robot-a2-tests-"))
    print(f"Artifacts: {output}", flush=True)
    a1 = [f"src/human_target_angle/{name}.c" for name in (
        "agent1_stage", "pose_hand", "pose_joint", "pose_mapping", "pose_math",
        "pose_reconstruction", "pose_tracking")]
    a3 = [f"src/output_controller/{name}.c" for name in (
        "output_control", "servo_config", "servo_control", "servo_hal")]
    a3 += ["src/drivers/servo_pwm_driver.c"]
    uart = ["src/uart_pose/uart_pose_protocol.c"]
    forearm_a1 = a1 + ["src/human_target_angle/forearm_mapping.c"]
    forearm_a2 = [f"src/robot_calibration/{name}.c" for name in (
        "forearm_calibration", "forearm_calibration_config", "forearm_motion_control",
        "forearm_safety_check", "motion")]
    pipeline = (a1 + ["src/human_target_angle/agent1_forearm_stage.c",
                       "src/human_target_angle/forearm_mapping.c"] +
                forearm_a2 + a3 + ["src/integration/agent_pipeline.c",
                                    "src/integration/cnn_app_event.c",
                                    "src/record_replay/motion_record_replay.c"])
    cases = []
    for role in ("LEFT", "RIGHT"):
        cases.append((f"test_robot_pwm_uart_{role.lower()}",
            [source for source in pipeline if source not in (
                "src/output_controller/servo_hal.c", "src/drivers/servo_pwm_driver.c")] +
            ["tests/integration/test_robot_pwm_uart.c"],
            [f"-DROBOT_STEREO_{role}", "-Isrc/cnn_firmware",
             "-Itests/integration/stubs", "-ffunction-sections", "-fdata-sections",
             "-Wl,--gc-sections"], []))
    stereo = [f"src/stereo_vision/{name}.c" for name in
              ("stereo_geometry", "stereo_calibration", "stereo_pose", "stereo_uart_protocol", "stereo_link")]
    stereo_filter = [f"src/stereo_vision/{name}.c" for name in
                     ("stereo_one_euro", "g_kalman3d", "stereo_pose_filter")]
    stereo += stereo_filter
    cases += [
        ("test_stereo_filter_command", ["src/stereo_vision/stereo_filter_command.c",
         "tests/integration/test_stereo_filter_command.c"], [], []),
        ("test_stereo_filter_console_dispatch", ["src/stereo_vision/stereo_filter_command.c",
         "tests/integration/test_stereo_filter_console_dispatch.c"], [], []),
        ("test_stereo_filter_runtime", stereo + ["src/integration/input_pose_cnn.c",
         "tests/integration/test_stereo_filter_runtime.c"], ["-Itests/integration/stubs"], []),
        ("test_stereo_one_euro", stereo_filter + ["tests/integration/test_stereo_one_euro.c"], [], []),
        ("test_stereo_pose_filter", stereo_filter + ["tests/integration/test_stereo_pose_filter.c"], [], []),
        ("test_stereo_filter_epoch", stereo_filter + ["src/integration/input_pose_cnn.c",
         "tests/integration/test_stereo_filter_epoch.c"], ["-Itests/integration/stubs"], []),
        ("test_stereo_filter_integration", stereo_filter + ["src/integration/input_pose_cnn.c",
         "tests/integration/test_stereo_filter_integration.c"], ["-Itests/integration/stubs"], []),
        ("test_stereo_protocol", stereo + ["tests/integration/test_stereo_protocol.c"], [], []),
        ("test_stereo_link", stereo + forearm_a1 + ["src/integration/input_pose_cnn.c",
         "tests/integration/test_stereo_link.c"], ["-Itests/integration/stubs"], []),
        ("test_stereo_async", stereo + forearm_a1 + ["src/integration/input_pose_cnn.c",
         "tests/integration/test_stereo_async.c"], ["-Itests/integration/stubs"], []),
    ]
    for role in ("LEFT", "RIGHT"):
        cases.append((f"test_stereo_board_{role.lower()}", stereo +
            ["src/integration/stereo_board.c", "src/integration/input_pose_cnn.c",
             "tests/integration/test_stereo_board.c"],
            [f"-DROBOT_STEREO_{role}", "-Isrc/cnn_firmware",
             "-Itests/integration/stereo_stubs", "-Itests/integration/stubs"], []))
    cases.append(("test_camera_missing_pwm", [
        "src/cnn_firmware/camera_tracking/camera_gimbal_pwm.c",
        "src/cnn_firmware/camera_tracking/torso_tracker.c",
        "src/cnn_firmware/camera_tracking/camera_tracking_app.c",
        "tests/integration/test_camera_missing_pwm.c"],
        ["-Itests/integration/stereo_stubs", "-Itests/integration/stubs",
         "-Isrc/cnn_firmware/camera_tracking"], []))
    for name in ("test_pose_mapping", "test_body_frame"):
        cases.append((name, a1 + [f"tests/human_target_angle/{name}.c"], [], []))
    cases.append(("test_forearm_stereo_absolute", forearm_a1 +
        ["src/human_target_angle/agent1_forearm_stage.c",
         "tests/human_target_angle/test_forearm_stereo_absolute.c"], [], []))
    cases += [
        ("test_stereo_reacquisition", stereo_filter + ["src/integration/input_pose_cnn.c",
         "tests/integration/test_stereo_reacquisition.c"],
         ["-Itests/integration/stubs", "-DROBOT_STEREO_ONE_EURO_ENABLE=0"], []),
        ("test_input_pose_cnn", stereo_filter + ["src/integration/input_pose_cnn.c",
         "tests/integration/test_input_pose_cnn.c"],
         ["-Itests/integration/stubs"], []),
        ("test_cnn_app_event", ["src/integration/cnn_app_event.c",
         "tests/integration/test_cnn_app_event.c"], [], []),
        ("test_frame_capture_paths", ["tests/integration/test_frame_capture_paths.c"],
         ["-Itests/integration/capture_stubs", "-Itests/integration/stubs"], []),
        ("test_cnn_trace", ["src/integration/trace.c",
         "tests/integration/test_cnn_trace.c"],
         ["-DROBOT_TRACE", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"], []),
        ("test_camera_fixed", [
            "src/cnn_firmware/camera_tracking/camera_gimbal_pwm.c",
            "src/cnn_firmware/camera_tracking/torso_tracker.c",
            "src/cnn_firmware/camera_tracking/camera_tracking_app.c",
            "tests/integration/test_camera_fixed.c"],
            ["-Itests/integration/stubs", "-Isrc/cnn_firmware/camera_tracking"], []),
        ("test_robot_pwm_disabled", pipeline +
         ["tests/integration/test_robot_pwm_disabled.c"], [], []),
        ("test_integration_smoke", pipeline + uart + ["tests/integration/test_integration_smoke.c"], [], []),
        ("test_major_only", pipeline + ["tests/integration/test_major_only.c"], [], []),
        ("test_trace", pipeline + ["tests/integration/test_trace.c"], ["-DROBOT_TRACE"], []),
        ("test_pixel_trace", pipeline + ["tests/integration/test_trace.c"], ["-DROBOT_TRACE"], ["--pixel-only"]),
        ("test_motion_record_replay", pipeline +
         ["tests/record_replay/test_motion_record_replay.c"], [], []),
        ("test_axis_replay", pipeline + uart + ["tests/robot_calibration/test_axis_replay.c"], [],
         ["etc/uart_pose_stream.bin", str(output / "axis_replay.csv")]),
        ("test_forearm_calibration", forearm_a2 + ["tests/robot_calibration/test_forearm_calibration.c"], [], []),
        ("test_forearm_geometry", forearm_a2 + ["tests/robot_calibration/test_forearm_calibration.c"], [], ["--geometry-only"]),
        ("test_forearm_safety_check", ["src/robot_calibration/forearm_safety_check.c",
         "tests/robot_calibration/test_forearm_safety_check.c"], [], []),
        ("test_forearm_replay", forearm_a1 + forearm_a2 + ["tests/robot_calibration/test_forearm_replay.c"], [],
         ["etc/example_pose2d_1280x720_20hz.csv"]),
    ]
    flags = [compiler, "-std=c99", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Iinclude", "-Iconfig"]
    if options.only:
        unknown = set(options.only) - {case[0] for case in cases}
        if unknown:
            raise SystemExit("Unknown test suites: " + ", ".join(sorted(unknown)))
        cases = [case for case in cases if case[0] in options.only]
    failed = []
    for name, sources, extra, args in cases:
        print(f"\n{name}", flush=True)
        exe = output / (name + (".exe" if os.name == "nt" else ""))
        build = subprocess.run(flags + extra + sources + ["-lm", "-o", str(exe)], cwd=root, env=env)
        if build.returncode or subprocess.run([str(exe)] + args, cwd=root, env=env).returncode:
            failed.append(name)
    if failed:
        raise SystemExit("FAILED: " + ", ".join(failed))
    print(f"\nPASS: {len(cases)} suites. Replay CSV: {output / 'axis_replay.csv'}")


if __name__ == "__main__":
    main()
