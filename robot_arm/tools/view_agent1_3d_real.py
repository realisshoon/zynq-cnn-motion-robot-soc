#!/usr/bin/env python3
import argparse
import csv
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="?", default="agent1_real_result.csv")
    args = ap.parse_args()

    with open(args.csv, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise SystemExit("CSV empty")

    names = ["shoulder_l", "shoulder_r", "elbow", "wrist", "finger1", "finger2"]

    fig = plt.figure()
    ax = fig.add_subplot(111, projection="3d")

    def getp(r, n):
        return [float(r[n + "_x3d"]), float(r[n + "_y3d"]), float(r[n + "_z"])]

    def get_int(r, key, default):
        try:
            return int(float(r.get(key, default)))
        except (TypeError, ValueError):
            return default

    def upd(i):
        ax.cla()
        r = rows[i]
        target_valid = get_int(r, "target_valid", 0)
        update_ret = get_int(r, "update_ret", 1 if target_valid else -1)
        major_fresh = get_int(r, "major_fresh", -1)

        if update_ret == 1:
            state = "UPDATE"
        elif update_ret == 0:
            state = "HOLD"
        else:
            state = "INVALID"

        # INVALID frame은 ctx에 남아 있는 과거 3D 좌표를 새 reconstruction처럼
        # 보여주지 않는다. 기존 viewer는 이 구분이 없어 stale pose가 움직이는
        # 것처럼 보일 수 있었다.
        if not target_valid:
            ax.set_xlim(0, 1)
            ax.set_ylim(0, 1)
            ax.set_zlim(0, 1)
            ax.set_xlabel("X")
            ax.set_ylabel("Y")
            ax.set_zlabel("Relative Z")
            ax.text2D(0.15, 0.55, "TARGET INVALID", transform=ax.transAxes)
            ax.text2D(0.15, 0.48,
                      f"frame={r['frame_id']}  update_ret={update_ret}  major_fresh={major_fresh}",
                      transform=ax.transAxes)
            return

        p = {n: getp(r, n) for n in names}

        ax.plot([p["shoulder_l"][0], p["shoulder_r"][0]],
                [p["shoulder_l"][1], p["shoulder_r"][1]],
                [p["shoulder_l"][2], p["shoulder_r"][2]], marker="o")

        chain = ["shoulder_r", "elbow", "wrist", "finger1"]
        ax.plot([p[n][0] for n in chain],
                [p[n][1] for n in chain],
                [p[n][2] for n in chain], marker="o")
        ax.plot([p["wrist"][0], p["finger2"][0]],
                [p["wrist"][1], p["finger2"][1]],
                [p["wrist"][2], p["finger2"][2]], marker="o")

        for n in names:
            ax.text(p[n][0], p[n][1], p[n][2], n)

        allp = list(p.values())
        xs = [q[0] for q in allp]
        ys = [q[1] for q in allp]
        zs = [q[2] for q in allp]
        cx = (min(xs) + max(xs)) / 2
        cy = (min(ys) + max(ys)) / 2
        cz = (min(zs) + max(zs)) / 2
        span = max(max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs), 1.0)

        ax.set_xlim(cx-span*.7, cx+span*.7)
        ax.set_ylim(cy-span*.7, cy+span*.7)
        ax.set_zlim(cz-span*.7, cz+span*.7)
        ax.set_xlabel("X")
        ax.set_ylabel("Y")
        ax.set_zlabel("Relative Z")
        ax.set_title(
            f"frame={r['frame_id']}  {state}  major_fresh={major_fresh}\n"
            f"wrist Z={float(r['wrist_z']):.3f}  elbow={float(r['elbow_deg']):.1f} deg"
        )

    FuncAnimation(fig, upd, frames=len(rows), interval=67, repeat=True)
    plt.show()


if __name__ == "__main__":
    main()
