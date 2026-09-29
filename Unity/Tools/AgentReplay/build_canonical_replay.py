"""외부 Git을 수정하지 않고 고정 commit의 C 원본을 Unity build 폴더에서 컴파일한다."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
PIN = "240007988aac24049e3775fb25d69999f6d30552"
SOURCES = [
    "human_target_angle/agent1_stage.c", "human_target_angle/pose_hand.c",
    "human_target_angle/pose_joint.c", "human_target_angle/pose_mapping.c",
    "human_target_angle/pose_math.c", "human_target_angle/pose_reconstruction.c",
    "human_target_angle/pose_tracking.c", "robot_calibration/motion_control.c",
    "robot_calibration/motion_limits.c", "robot_calibration/motion_smoothing.c",
    "robot_calibration/robot_calibration.c", "robot_calibration/robot_calibration_config.c",
    "robot_calibration/safety_check.c", "output_controller/output_control.c",
    "output_controller/servo_config.c", "output_controller/servo_control.c",
    "output_controller/servo_hal.c", "drivers/servo_pwm_driver.c", "integration/agent_pipeline.c",
]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--repo", type=Path, default=HERE.parents[3]/"zynq-cnn-motion-robot-soc")
    ap.add_argument("--commit", default=PIN)
    ap.add_argument("--input", type=Path, default=HERE/"example_pose2d_1280x720_20hz.csv")
    ap.add_argument("--settle-ms", type=int, default=1000)
    args = ap.parse_args()
    repo = args.repo.resolve()
    env = dict(os.environ, GIT_OPTIONAL_LOCKS="0")
    def git(*argv):
        return subprocess.check_output(["git", "-C", str(repo), *argv], env=env)
    commit = git("rev-parse", "--verify", args.commit+"^{commit}").decode().strip()
    before = (git("rev-parse", "HEAD"), git("status", "--porcelain=v1", "--untracked-files=all"))
    compiler = shutil.which("gcc") or "C:/msys64/ucrt64/bin/gcc.exe"
    if not Path(compiler).is_file():
        raise SystemExit("GCC가 필요합니다. MinGW/MSYS2 gcc를 PATH에 추가하세요.")
    # 다른 툴이 제공하는 동명 DLL보다 이 GCC의 runtime DLL을 우선한다.
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + os.environ.get("PATH", "")
    build = HERE/"build"
    snapshot = build/"canonical"/commit
    hashes = {}
    # checkout/fetch/worktree 생성 없이 Git blob을 읽어 Unity 내부에만 저장한다.
    for name in git("ls-tree", "-r", "--name-only", commit, "robot_arm").decode().splitlines():
        if not name.endswith((".c", ".h")):
            continue
        data = git("show", commit+":"+name)
        target = snapshot/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        hashes[name] = hashlib.sha256(data).hexdigest()
    arm = snapshot/"robot_arm"
    exe = build/"csv_pose_to_joint_trace.exe"
    converter = HERE/"csv_pose_to_joint_trace.c"
    cmd = [compiler, "-std=c99", "-Wall", "-Wextra", "-Wpedantic", "-O2", "-DROBOT_TRACE",
           "-I"+str(arm/"include"), "-I"+str(arm/"config"),
           *(str(arm/"src"/s) for s in SOURCES), str(converter), "-lm", "-o", str(exe)]
    subprocess.run(cmd, check=True, cwd=build, env=env)
    trace = build/"joint_command_trace.csv"
    subprocess.run([str(exe), str(args.input.resolve()), str(trace), "--settle-ms", str(args.settle_ms)], check=True, cwd=build, env=env)
    after = (git("rev-parse", "HEAD"), git("status", "--porcelain=v1", "--untracked-files=all"))
    if after != before:
        raise SystemExit("경고: 외부 저장소 HEAD/status가 작업 중 변경되었습니다. 성공으로 처리하지 않습니다.")
    manifest = {"canonical_commit": commit, "checkout_head": before[0].decode().strip(),
                "source_sha256": hashes, "converter_sha256": hashlib.sha256(converter.read_bytes()).hexdigest(),
                "input_sha256": hashlib.sha256(args.input.read_bytes()).hexdigest(),
                "trace_sha256": hashlib.sha256(trace.read_bytes()).hexdigest(),
                "control_tick_ms": 20, "output": "AgentPipelineContext.output", "external_status_unchanged": True}
    (build/"canonical_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print("PASS: canonical commit="+commit+"; trace="+str(trace))

if __name__ == "__main__":
    main()
