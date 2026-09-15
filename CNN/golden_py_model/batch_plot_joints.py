"""images_npy/ 의 모든 프레임 + run_output/<frame>/results.json 을 짝지어
관절 좌표 오버레이 이미지를 만들어 overlays/ 폴더 하나에 전부 저장한다.

사용법:
    python batch_plot_joints.py
    python batch_plot_joints.py --out-dir overlays --show-invalid
"""
import argparse
import json
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")  # 화면 없이 파일로만 저장 (배치용)
import matplotlib.pyplot as plt

JOINT_NAMES = [
    "nose", "l_eye", "r_eye", "l_ear", "r_ear",
    "l_shoulder", "r_shoulder", "l_elbow", "r_elbow",
    "l_wrist", "r_wrist", "l_hip", "r_hip",
    "l_knee", "r_knee", "l_ankle", "r_ankle",
]

def plot_one(img, result, title, out_path, show_invalid):
    fig, ax = plt.subplots(figsize=(img.shape[1] / 100, img.shape[0] / 100), dpi=100)
    ax.imshow(img)

    for j in result["joints"]:
        x, y, valid = j["x"], j["y"], j["valid"]
        name = JOINT_NAMES[j["joint"]]
        if valid:
            ax.plot(x, y, "o", color="lime", markersize=8, markeredgecolor="black", markeredgewidth=1)
            ax.annotate(f"{j['joint']}:{name}", (x, y), textcoords="offset points",
                        xytext=(6, 6), fontsize=7, color="yellow")
        elif show_invalid:
            ax.plot(x, y, "x", color="gray", markersize=6)

    for m_idx, label, color in [(0, "RED marker", "red"), (1, "BLUE marker", "blue")]:
        m = result["markers"][m_idx]
        if m["found"]:
            ax.plot(m["x"], m["y"], "s", color=color, markersize=10, markeredgecolor="white")
            ax.annotate(label, (m["x"], m["y"]), textcoords="offset points",
                        xytext=(8, -10), fontsize=8, color=color)

    ax.set_title(title)
    ax.axis("off")
    plt.tight_layout()
    plt.savefig(out_path, bbox_inches="tight")
    plt.close(fig)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--images", default="images_npy", help=".npy 프레임 폴더")
    p.add_argument("--results", default="run_output", help="프레임별 results.json이 있는 상위 폴더")
    p.add_argument("--out-dir", default="overlays", help="오버레이 이미지를 저장할 폴더")
    p.add_argument("--show-invalid", action="store_true", help="valid=false 관절도 회색 X로 표시")
    args = p.parse_args()

    img_dir = Path(args.images)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(exist_ok=True)

    npy_files = sorted(img_dir.glob("*.npy"))
    if not npy_files:
        print(f"'{img_dir}'에 .npy 파일이 없습니다.")
        return

    done, skipped = 0, []
    for i, npy_path in enumerate(npy_files, 1):
        stem = npy_path.stem
        result_path = Path(args.results) / stem / "results.json"
        if not result_path.exists():
            skipped.append(stem)
            continue

        img = np.load(npy_path)
        result = json.loads(result_path.read_text())
        out_path = out_dir / f"{stem}_overlay.png"
        plot_one(img, result, stem, out_path, args.show_invalid)
        done += 1
        print(f"[{i}/{len(npy_files)}] {stem} -> {out_path.name}")

    print(f"\n완료: {done}개 저장됨 -> {out_dir}/")
    if skipped:
        print(f"results.json 없어서 건너뜀: {len(skipped)}개")
        for s in skipped:
            print(f"  - {s}")

if __name__ == "__main__":
    main()
