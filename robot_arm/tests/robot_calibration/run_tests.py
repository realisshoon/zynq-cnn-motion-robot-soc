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
    a2 = [f"src/robot_calibration/{name}.c" for name in (
        "motion_control", "motion_limits", "motion_smoothing", "robot_calibration",
        "robot_calibration_config", "safety_check")]
    a3 = [f"src/output_controller/{name}.c" for name in (
        "output_control", "servo_config", "servo_control", "servo_hal")]
    a3 += ["src/drivers/servo_pwm_driver.c"]
    pipeline = a1 + a2 + a3 + ["src/integration/agent_pipeline.c"]
    uart = ["src/uart_pose/uart_pose_protocol.c"]
    cases = []
    for name in ("test_robot_calibration", "test_motion_limits", "test_motion_smoothing", "test_safety_check"):
        cases.append((name, a2 + [f"tests/robot_calibration/{name}.c"], [], []))
    for name in ("test_pose_mapping", "test_body_frame"):
        cases.append((name, a1 + [f"tests/human_target_angle/{name}.c"], [], []))
    cases += [
        ("test_integration_smoke", pipeline + uart + ["tests/integration/test_integration_smoke.c"], [], []),
        ("test_trace", pipeline + ["tests/integration/test_trace.c"], ["-DROBOT_TRACE"], []),
        ("test_axis_replay", pipeline + uart + ["tests/robot_calibration/test_axis_replay.c"], [],
         ["etc/uart_pose_stream.bin", str(output / "axis_replay.csv")]),
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
