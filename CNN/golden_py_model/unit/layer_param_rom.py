"""Independent software reference for layer_param_rom.

Source of truth:
- canonical manifest.json
- CNN-v4 cfg_desc256 field contract

This module intentionally does NOT inspect or parse Verilog RTL.
"""

from pathlib import Path
import argparse
import json

KIND_ENC = {"conv0": 0, "dw": 1, "pw": 2}
MODE_ENC = {"body": 0, "heatmap": 1, "offset": 2}

FIELDS = {
    "op_id":         (0,   5),
    "kind":          (5,   2),
    "mode":          (7,   2),
    "hin":           (9,   9),
    "win":           (18,  9),
    "hout":          (27,  9),
    "wout":          (36,  9),
    "cin":           (45,  9),
    "cout":          (54,  9),
    "stride":        (63,  2),
    "dilation":      (65,  2),
    "pad":           (67,  2),
    "shift":         (69,  6),
    "weight_offset": (75, 32),
    "param_offset":  (107,18),
    "dma_bytes":     (125,20),
    "stage_id":      (145, 4),
}

DEFAULT_MANIFEST = Path(__file__).resolve().parents[1] / "generated" / "manifest.json"


def _put(desc, value, lsb, width, name):
    value = int(value)
    if value < 0 or value >= (1 << width):
        raise ValueError(f"{name}={value} does not fit unsigned {width} bits")
    return desc | (value << lsb)


def pack_descriptor(op):
    """Pack one manifest operation into the canonical 256-bit cfg_desc."""
    desc = 0

    values = dict(op)
    values["kind"] = KIND_ENC[op["kind"]]
    values["mode"] = MODE_ENC[op["mode"]]

    for name, (lsb, width) in FIELDS.items():
        if name not in values:
            raise KeyError(f"manifest op is missing required field: {name}")
        desc = _put(desc, values[name], lsb, width, name)

    # bits [255:149] are intentionally never written => reserved = 0
    if (desc >> 149) != 0:
        raise AssertionError("reserved[255:149] must be zero")
    return desc


def unpack_descriptor(desc):
    desc = int(desc)
    out = {}
    for name, (lsb, width) in FIELDS.items():
        out[name] = (desc >> lsb) & ((1 << width) - 1)
    out["reserved"] = (desc >> 149) & ((1 << 107) - 1)
    return out


def load_manifest(path=DEFAULT_MANIFEST):
    path = Path(path)
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("version") != "CNN-v4.0":
        raise ValueError("expected CNN-v4.0 manifest")
    ops = manifest.get("ops", [])
    if len(ops) != 29:
        raise ValueError(f"expected 29 operations, got {len(ops)}")
    for i, op in enumerate(ops):
        if op.get("op_id") != i:
            raise ValueError(f"op_id sequence mismatch at index {i}")
    return manifest


def layer_param_rom_ref(op_id, manifest):
    """Software reference function: op_id -> cfg_desc256."""
    op_id = int(op_id)
    if not 0 <= op_id <= 28:
        raise ValueError("software reference accepts only valid op_id 0..28")
    op = manifest["ops"][op_id]
    if op["op_id"] != op_id:
        raise ValueError("manifest ordering/op_id mismatch")
    return pack_descriptor(op)


def all_descriptors(manifest):
    return [layer_param_rom_ref(i, manifest) for i in range(29)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--out", type=Path, default=None,
                    help="optional TSV output: op_id<TAB>descriptor_hex")
    args = ap.parse_args()

    manifest = load_manifest(args.manifest)
    descs = all_descriptors(manifest)

    lines = []
    for op_id, desc in enumerate(descs):
        line = f"{op_id:02d}\t{desc:064x}"
        lines.append(line)
        print(line)

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
