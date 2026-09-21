"""Run only the downsample_module numerical reference and save its RGB images."""

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

BASE_DIR = Path(__file__).resolve().parent.parent
RESULT_DIR = Path(__file__).resolve().parent / "result"
sys.path.insert(0, str(BASE_DIR))

from cnn_model_v4 import downsample


def run_downsample(frame):
    result = downsample(frame)
    if result.shape != (256, 256, 3) or result.dtype != np.uint8:
        raise ValueError("downsample output must be 256x256x3 uint8")
    if not np.all(result[:56] == 0) or not np.all(result[200:] == 0):
        raise ValueError("downsample padding rows are not zero")
    if not np.array_equal(result[56:200], frame[::5, ::5]):
        raise ValueError("downsample pixels differ from input")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=BASE_DIR / "images_npy" / "frame_00121.npy",
                        help="720x1280x3 RGB uint8 .npy input")
    parser.add_argument("--out", type=Path, default=RESULT_DIR)
    args = parser.parse_args()

    frame = np.load(args.image, allow_pickle=False)
    sampled = run_downsample(frame)
    args.out.mkdir(parents=True, exist_ok=True)
    np.save(args.out / "01_downsample.npy", sampled)
    Image.fromarray(frame, mode="RGB").save(args.out / "00_input_original.png")
    Image.fromarray(sampled, mode="RGB").save(args.out / "01_downsample.png")

    print("[downsample_module]")
    print(f"Input       : {frame.shape} {frame.dtype}")
    print(f"Output      : {sampled.shape} {sampled.dtype}")
    print(f"Saved       : {args.out / '01_downsample.npy'}")
    print(f"Visualization: {args.out / '01_downsample.png'}")
    print("PASS")


if __name__ == "__main__":
    main()
