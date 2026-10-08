"""Native split-board protocol checks against the shared runtime sources."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    source = root
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    if compiler is None:
        raise SystemExit("Set CC to native GCC, Clang, or the Zig executable.")
    command = [compiler]
    if Path(compiler).stem.lower() == "zig":
        command += ["cc"]
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment.get("PATH", "")
    output = Path(tempfile.mkdtemp(prefix="robot-follow-tests-"))
    flags = command + ["-std=c99", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                       "-I", str(source / "include"), "-I", str(source / "config"),
                       "-I", str(source / "src/cnn_firmware"),
                       "-I", str(root / "tests/integration/stereo_stubs"),
                       "-I", str(root / "tests/integration/stubs")]
    protocol = [str(source / f"src/stereo_vision/{name}.c") for name in
                ("stereo_uart_protocol", "robot_follow_protocol")]
    stereo = [str(source / f"src/stereo_vision/{name}.c") for name in
              ("stereo_geometry", "stereo_calibration", "stereo_pose", "stereo_link")]
    cases = [
        ("robot_follow_protocol", protocol + [str(root / "tests/integration/test_robot_follow_protocol.c")], []),
        ("legacy_coordinate_protocol", protocol + [str(root / "tests/integration/test_stereo_protocol.c")], []),
    ]
    for role in ("LEFT", "RIGHT"):
        cases.append((f"robot_follow_board_{role.lower()}", protocol + stereo +
                      [str(source / "src/integration/stereo_board.c"),
                       str(root / "tests/integration/test_robot_follow_board.c")],
                      [f"-DROBOT_STEREO_{role}"]))
    cases.append(("robot_follow_board_disabled", protocol +
                  [str(source / "src/integration/stereo_board.c"),
                   str(root / "tests/integration/test_robot_follow_board.c")], []))
    print(f"Artifacts: {output}", flush=True)
    for name, files, extra in cases:
        print(name, flush=True)
        executable = output / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run(flags + extra + files + ["-lm", "-o", str(executable)],
                       env=environment, check=True)
        subprocess.run([str(executable)], env=environment, check=True)
    print(f"PASS: {len(cases)} native suites", flush=True)


if __name__ == "__main__":
    main()
