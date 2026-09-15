"""images_npy/ 안의 모든 .npy 프레임에 대해 cnn_model_v4.py run을 순차 실행한다.
각 프레임의 결과는 run_output/<프레임이름>/ 에 저장된다.

사용법:
    python run_all_frames.py
    python run_all_frames.py --dump          # 중간 연산 결과(29개 op)까지 저장
    python run_all_frames.py --limit 5        # 앞 5장만 시험 실행
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--script", default="cnn_model_v4.py", help="모델 스크립트 경로")
    p.add_argument("--images", default="images_npy", help=".npy 프레임이 있는 폴더")
    p.add_argument("--packed", default="generated/weights_v4.bin")
    p.add_argument("--manifest", default="generated/manifest.json")
    p.add_argument("--out", default="run_output", help="프레임별 결과를 저장할 상위 폴더")
    p.add_argument("--dump", action="store_true", help="29개 연산 중간 결과까지 저장(용량 큼)")
    p.add_argument("--limit", type=int, default=None, help="테스트용: 앞 N장만 실행")
    args = p.parse_args()

    npy_dir = Path(args.images)
    npy_files = sorted(npy_dir.glob("*.npy"))
    if args.limit:
        npy_files = npy_files[: args.limit]

    if not npy_files:
        print(f"'{npy_dir}'에 .npy 파일이 없습니다.")
        sys.exit(1)

    print(f"총 {len(npy_files)}개 프레임 처리 시작")
    t0 = time.time()
    failed = []

    for i, npy_path in enumerate(npy_files, 1):
        out_dir = Path(args.out) / npy_path.stem
        cmd = [
            sys.executable, args.script, "run",
            "--packed", args.packed,
            "--manifest", args.manifest,
            "--image", str(npy_path),
            "--out", str(out_dir),
        ]
        if args.dump:
            cmd.append("--dump")

        print(f"[{i}/{len(npy_files)}] {npy_path.name} ...", end=" ", flush=True)
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print("실패")
            print(result.stderr.strip())
            failed.append(npy_path.name)
        else:
            print("완료")

    elapsed = time.time() - t0
    print(f"\n전체 완료: {len(npy_files) - len(failed)}/{len(npy_files)}성공, {elapsed:.1f}초 소요")
    if failed:
        print("실패한 프레임:")
        for name in failed:
            print(f"  - {name}")

if __name__ == "__main__":
    main()
