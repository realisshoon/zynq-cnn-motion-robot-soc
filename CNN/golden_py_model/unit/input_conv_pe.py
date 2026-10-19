"""Run only the input_conv_pe (Conv0) numerical reference from a downsampled .npy."""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

BASE_DIR = Path(__file__).resolve().parent.parent
RESULT_DIR = Path(__file__).resolve().parent / "result"
sys.path.insert(0, str(BASE_DIR))

from cnn_model_v4 import PackedWeights, conv_acc, requant

MANIFEST_PATH = BASE_DIR / "generated" / "manifest.json"
WEIGHTS_PATH = BASE_DIR / "generated" / "weights_v4.bin"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def find_conv0(manifest):
    matches = [op for op in manifest["ops"] if op.get("op_id") == 0]
    if not matches:
        matches = [op for op in manifest["ops"] if op.get("name") == "features.conv0.conv"]
    require(len(matches) == 1, "exactly one Conv0 operation is required")
    op = matches[0]
    require(op["kind"] == "conv0", "Conv0 operation must have kind 'conv0'")
    return op


def run_conv0(downsampled, manifest, weights):
    op = find_conv0(manifest)
    require(downsampled.shape == (op["hin"], op["win"], op["cin"]), "Conv0 input shape mismatch")
    w, m, b = weights.layer(op["name"])
    result = requant(conv_acc(downsampled, w, b, op), m, op["shift"], op["mode"])
    require(result.shape == (op["hout"], op["wout"], op["cout"]), "Conv0 output shape mismatch")
    require(result.dtype == np.int8, "Conv0 output dtype must be int8")
    require(0 <= int(result.min()) and int(result.max()) <= 127, "Conv0 output outside 0..127")
    return result


def validate_saved_conv0(path, downsampled, manifest, op):
    """Compare the saved artifact with a fresh call to the original Golden ops."""
    actual = np.load(path, allow_pickle=False)
    expected_shape = (op["hout"], op["wout"], op["cout"])
    shape_check = actual.shape == expected_shape and actual.size == int(np.prod(expected_shape))
    dtype_check = actual.dtype == np.int8
    range_check = bool(np.all(np.isfinite(actual)) and np.all((actual >= 0) & (actual <= 127)))

    reference_weights = PackedWeights(WEIGHTS_PATH, manifest)
    w, m, b = reference_weights.layer(op["name"])
    reference = requant(conv_acc(downsampled, w, b, op), m, op["shift"], op["mode"])
    exact_match = bool(np.array_equal(actual, reference))
    mismatch_count = int(np.count_nonzero(actual != reference)) if actual.shape == reference.shape else max(actual.size, reference.size)
    return actual, {
        "shape": list(actual.shape), "dtype": str(actual.dtype),
        "min": int(actual.min()), "max": int(actual.max()),
        "shape_check": shape_check, "dtype_check": dtype_check,
        "range_check": range_check, "golden_exact_match": exact_match,
        "mismatch_count": mismatch_count,
    }


def conv0_samples_and_stats(conv0):
    h, w, channels = conv0.shape
    samples = {f"{y},{x}": [int(v) for v in conv0[y, x]]
               for y, x in ((0, 0), (1, 1), (64, 64), (127, 127))
               if y < h and x < w}
    stats = []
    for channel in range(channels):
        pixels = conv0[:, :, channel]
        stats.append({"channel": channel, "min": int(pixels.min()),
                      "max": int(pixels.max()), "mean": float(pixels.mean()),
                      "zero_count": int(np.count_nonzero(pixels == 0)),
                      "sat127_count": int(np.count_nonzero(pixels == 127))})
    return samples, stats


def save_conv0_images(conv0, out_dir):
    """Visualize 0..127 as 0..255 without changing the Golden int8 array."""
    height, width, channels = conv0.shape
    scaled = ((conv0.astype(np.uint16) * 255 + 63) // 127).astype(np.uint8)
    channel_dir = out_dir / "conv0_channels"
    channel_dir.mkdir(parents=True, exist_ok=True)
    columns = 6
    label_height = 20
    grid = Image.new("L", (columns * width, ((channels + columns - 1) // columns) * (height + label_height)))
    draw = ImageDraw.Draw(grid)
    for channel in range(channels):
        image = Image.fromarray(scaled[:, :, channel], mode="L")
        image.save(channel_dir / f"conv0_ch{channel:02d}.png")
        x = (channel % columns) * width
        y = (channel // columns) * (height + label_height)
        draw.text((x + 4, y + 3), f"CH{channel}", fill=255)
        grid.paste(image, (x, y + label_height))
    grid_path = out_dir / "02_conv0_channels_grid.png"
    grid.save(grid_path)
    return channel_dir, grid_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=RESULT_DIR / "01_downsample.npy",
                        help="downsample_module output, 256x256x3 uint8 .npy")
    parser.add_argument("--out", type=Path, default=RESULT_DIR)
    args = parser.parse_args()

    sampled = np.load(args.input, allow_pickle=False)
    if sampled.dtype != np.uint8:
        raise ValueError("Conv0 input must be uint8")
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    op = find_conv0(manifest)
    conv0 = run_conv0(sampled, manifest, PackedWeights(WEIGHTS_PATH, manifest))

    args.out.mkdir(parents=True, exist_ok=True)
    output_path = args.out / "02_conv0.npy"
    np.save(output_path, conv0)
    saved, validation = validate_saved_conv0(output_path, sampled, manifest, op)
    samples, stats = conv0_samples_and_stats(saved)
    channel_dir, grid_path = save_conv0_images(saved, args.out)
    (args.out / "conv0_validation.json").write_text(json.dumps({
        "operation": op["name"], "input_shape": list(sampled.shape),
        "conv0_validation": validation, "conv0_samples": samples,
        "conv0_channel_stats": stats,
    }, indent=2), encoding="utf-8")

    passed = all(validation[key] for key in
                 ("shape_check", "dtype_check", "range_check", "golden_exact_match"))
    print("[input_conv_pe / Conv0]")
    print(f"Operation   : {op['name']}")
    print(f"Input       : {sampled.shape} {sampled.dtype}")
    print(f"Output      : {saved.shape} {saved.dtype}, min/max={saved.min()}/{saved.max()}")
    print(f"Golden match: {'PASS' if validation['golden_exact_match'] else 'FAIL'}; mismatches={validation['mismatch_count']}")
    print(f"Saved       : {output_path}")
    print(f"Channel PNGs: {channel_dir}")
    print(f"Channel grid: {grid_path}")
    print("PASS" if passed else "FAIL")
    if not passed:
        raise ValueError("Conv0 validation failed")


if __name__ == "__main__":
    main()
