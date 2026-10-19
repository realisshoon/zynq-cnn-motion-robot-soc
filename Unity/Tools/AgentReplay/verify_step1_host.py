"""Canonical 원본 테스트 + CSV parser + 동일 output의 physical/Unity trace 검증."""
import concurrent.futures
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
from build_canonical_replay import HERE, PIN, SOURCES

def main():
    build=HERE/"build"
    arm=build/"canonical"/PIN/"robot_arm"
    compiler=shutil.which("gcc") or "C:/msys64/ucrt64/bin/gcc.exe"
    env=dict(os.environ);env["PATH"]=str(Path(compiler).parent)+os.pathsep+env.get("PATH","")
    sources=[str(arm/"src"/s) for s in SOURCES]
    tests=["human_target_angle/test_pose_mapping.c", "robot_calibration/test_robot_calibration.c",
           "robot_calibration/test_motion_limits.c", "robot_calibration/test_motion_smoothing.c",
           "robot_calibration/test_safety_check.c", "output_controller/test_servo_control.c",
           "output_controller/test_servo_hal.c", "output_controller/test_output_control.c",
           "integration/test_integration_smoke.c", "LOCAL_PARSER"]
    def run_test(name):
        if name=="output_controller/test_output_control.c" and not (arm/"include/output_controller/motion_record.h").exists():
            return {"test":name,"result":"SKIP","stdout":"canonical 테스트가 삭제된 motion_record.h/.c를 참조함. 현재 Agent3 경로는 실제 trace 전체 및 servo_control/servo_hal 테스트로 검증.","stderr":""}
        source=HERE/"test_csv_parser.c" if name=="LOCAL_PARSER" else arm/"tests"/name
        flags=["-DROBOT_TRACE"] if name=="LOCAL_PARSER" else []
        extra=[str(arm/"src/uart_pose/uart_pose_protocol.c")] if "integration" in name else []
        if name=="output_controller/test_output_control.c": extra.append(str(arm/"src/output_controller/motion_record.c"))
        exe=build/(source.stem+".exe")
        compiled=subprocess.run([compiler,"-std=c99","-O2","-Wall","-Wextra","-Wpedantic",*flags,
                        "-I"+str(arm/"include"),"-I"+str(arm/"config"),*sources,*extra,str(source),"-lm","-o",str(exe)],env=env,cwd=build,capture_output=True,text=True,encoding="utf-8",errors="replace")
        if compiled.returncode: raise RuntimeError(name+" compile: "+compiled.stderr)
        result=subprocess.run([str(exe)],check=True,env=env,cwd=build,capture_output=True,text=True,encoding="utf-8",errors="replace",timeout=30)
        return {"test":name,"result":"PASS","stdout":result.stdout,"stderr":result.stderr}
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results=list(pool.map(run_test,tests))
    def read(name):
        with (build/name).open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
    unity=read("joint_command_trace.csv");physical=read("physical_command_pwm_trace.csv")
    assert len(unity)==len(physical)
    fields=("base_deg","shoulder_deg","elbow_deg","wrist_pitch_deg","wrist_roll_deg","gripper_norm")
    limits=((10,170),(20,160),(10,170),(20,160),(0,180),(0,1))
    max_delta=(3,3,4,5,5)
    changing_same_source=0
    for i,(u,p) in enumerate(zip(unity,physical)):
        assert int(u["frame_id"])==i+1 and abs(float(u["time_sec"])-i*.02)<1e-6
        for key in u:assert u[key]==p[key],(i,key,u[key],p[key])
        for key,(low,high) in zip(fields,limits):
            value=float(u[key]);assert math.isfinite(value) and low<=value<=high
        assert u["valid"]=="1"
        for key in p:
            if key.endswith("_pwm_us"):assert 500<=int(p[key])<=2500
        if i:
            prev=unity[i-1]
            for key,delta in zip(fields,max_delta):assert abs(float(u[key])-float(prev[key]))<=delta+.0001
            if u["source_frame_id"]==prev["source_frame_id"] and any(u[k]!=prev[k] for k in fields[:5]):changing_same_source+=1
    assert changing_same_source>0,"Pose 도착 사이에 smoothing output이 계속 생성되어야 합니다."
    # 빌드 복사본이 원본 Git blob과 동일한지 manifest로 재검사한다.
    manifest=json.loads((build/"canonical_manifest.json").read_text())
    assert manifest["canonical_commit"]==PIN
    for path,sha in manifest["source_sha256"].items():
        assert hashlib.sha256((build/"canonical"/PIN/path).read_bytes()).hexdigest()==sha,path
    summary=json.loads((build/"replay_summary.json").read_text())
    summary.update({"host_tests":results,"physical_unity_equal_rows":len(unity),"changing_ticks_same_source":changing_same_source})
    (build/"step1_host_results.json").write_text(json.dumps(summary,indent=2,ensure_ascii=False),encoding="utf-8")
    lines=["# STEP 1 Host 검증", "", "canonical commit: `"+PIN+"`",""]
    lines += ["- "+r["result"]+": "+r["test"]+(" — "+r["stdout"] if r["result"]=="SKIP" else "") for r in results]
    lines += [f"- PASS: physical/Unity final command {len(unity)}행 전 필드 일치",
              f"- PASS: 동일 source frame 사이에서 변화한 smoothing output {changing_same_source}개",
              "- PASS: 20 ms sequence / 유한값 / joint limits / tick별 속도 제한 / PWM 500~2500 µs",
              "- PASS: canonical blob SHA256 불변", "- 하드웨어: HOST_MOCK_ONLY. 실제 PWM 핀/서보 움직임은 미검증."]
    (HERE.parents[1]/"Validation/Step1HostValidation.md").write_text("\n".join(lines)+"\n",encoding="utf-8")
    print("\n".join(lines));print(json.dumps({k:v for k,v in summary.items() if k!="host_tests"},indent=2))

if __name__=="__main__":main()
