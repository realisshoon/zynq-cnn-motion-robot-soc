"""Build/run A2 and its integration regressions using host GCC, without CMake.

Usage: python robot_arm/tests/robot_calibration/run_tests.py
Requires GCC and Python's standard library. Artifacts go to a temporary directory.
No Vitis, board access, or tracked build output.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
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
                forearm_a2 + a3 + ["src/integration/agent_pipeline.c"])
    cases = []
    for name in ("test_pose_mapping", "test_body_frame"):
        cases.append((name, a1 + [f"tests/human_target_angle/{name}.c"], [], []))
    cases += [
        ("test_integration_smoke", pipeline + uart + ["tests/integration/test_integration_smoke.c"], [], []),
        ("test_trace", pipeline + ["tests/integration/test_trace.c"], ["-DROBOT_TRACE"], []),
        ("test_axis_replay", pipeline + uart + ["tests/robot_calibration/test_axis_replay.c"], [],
         ["etc/uart_pose_stream.bin", str(output / "axis_replay.csv")]),
        ("test_forearm_calibration", forearm_a2 + ["tests/robot_calibration/test_forearm_calibration.c"], [], []),
        ("test_forearm_safety_check", ["src/robot_calibration/forearm_safety_check.c",
         "tests/robot_calibration/test_forearm_safety_check.c"], [], []),
        ("test_forearm_replay", forearm_a1 + forearm_a2 + ["tests/robot_calibration/test_forearm_replay.c"], [],
         ["etc/example_pose2d_1280x720_20hz.csv"]),
    ]
    flags = [compiler, "-std=c99", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Iinclude", "-Iconfig"]
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
