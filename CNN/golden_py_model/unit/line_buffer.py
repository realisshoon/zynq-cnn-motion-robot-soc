"""Run only the first depthwise line_buffer reference from a Conv0 feature map."""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

BASE_DIR = Path(__file__).resolve().parent.parent
RESULT_DIR = Path(__file__).resolve().parent / "result"
MANIFEST_PATH = BASE_DIR / "generated" / "manifest.json"
LANES = 32


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


def find_first_dw(manifest, conv0_op):
    following = False
    for op in manifest["ops"]:
        if following and op.get("kind") == "dw":
            return op
        if op is conv0_op:
            following = True
    raise ValueError("no depthwise operation follows Conv0")


def check_linebuffer_input(feature_map, op):
    require(op["kind"] == "dw", "line buffer operation must be depthwise")
    require(op["kernel"] == 3, "line buffer supports a 3x3 kernel only")
    require(op["dilation"] == 1, "line buffer supports dilation == 1 only")
    require(feature_map.shape == (op["hin"], op["win"], op["cin"]), "line buffer input shape mismatch")
    require(feature_map.dtype == np.int8, "line buffer input dtype must be int8")


def num_batches(op):
    return (op["cin"] + LANES - 1) // LANES


def batch_mask(op, batch):
    require(0 <= batch < num_batches(op), "channel batch outside valid range")
    valid_channels = min(LANES, op["cin"] - batch * LANES)
    return (1 << valid_channels) - 1


def extract_linebuffer_rows(feature_map, op, oy):
    """Logical ky rows, with zero spatial padding and zero unused channel lanes."""
    check_linebuffer_input(feature_map, op)
    require(0 <= oy < op["hout"], "output row outside valid range")
    rows = np.zeros((3, op["win"], num_batches(op), LANES), dtype=np.int8)
    for ky in range(3):
        iy = oy * op["stride"] - op["pad"] + ky
        if 0 <= iy < op["hin"]:
            for batch in range(num_batches(op)):
                start = batch * LANES
                end = min(start + LANES, op["cin"])
                rows[ky, :, batch, :end - start] = feature_map[iy, :, start:end]
    return rows


def extract_linebuffer_taps(feature_map, op, oy, ox, batch):
    """Nine sequential ky-major taps, each containing 32 signed 8-bit lanes."""
    check_linebuffer_input(feature_map, op)
    require(0 <= oy < op["hout"] and 0 <= ox < op["wout"], "output position outside valid range")
    batch_mask(op, batch)
    taps = np.zeros((9, LANES), dtype=np.int8)
    start = batch * LANES
    end = min(start + LANES, op["cin"])
    for tap in range(9):
        ky, kx = divmod(tap, 3)
        iy = oy * op["stride"] - op["pad"] + ky
        ix = ox * op["stride"] - op["pad"] + kx
        if 0 <= iy < op["hin"] and 0 <= ix < op["win"]:
            taps[tap, :end - start] = feature_map[iy, ix, start:end]
    return taps


def pack_lanes_256(lanes, mask):
    """Lane 0 occupies bits 7:0; negative int8 values use two's complement."""
    require(len(lanes) == LANES, "256-bit word requires 32 lanes")
    word = sum((int(value) & 0xFF) << (8 * lane) for lane, value in enumerate(lanes))
    return {"lanes": [int(value) for value in lanes], "mask": f"0x{mask:08X}", "data_hex": f"0x{word:064X}"}


def pack_pixel_256(feature_map, row, col, batch):
    """Pack one in-bounds HWC pixel; unused channel lanes are zero."""
    h, w, channels = feature_map.shape
    require(0 <= row < h and 0 <= col < w, "pixel position outside feature map")
    require(0 <= batch < (channels + LANES - 1) // LANES, "channel batch outside valid range")
    start = batch * LANES
    valid = min(LANES, channels - start)
    lanes = np.zeros(LANES, dtype=np.int8)
    lanes[:valid] = feature_map[row, col, start:start + valid]
    return pack_lanes_256(lanes, (1 << valid) - 1)


def check_window(feature_map, op, oy, ox, batch):
    """Check each tap against its source pixel and the logical 3-row view."""
    rows = extract_linebuffer_rows(feature_map, op, oy)
    taps = extract_linebuffer_taps(feature_map, op, oy, ox, batch)
    start = batch * LANES
    valid_channels = min(LANES, op["cin"] - start)
    for tap in range(9):
        ky, kx = divmod(tap, 3)
        iy = oy * op["stride"] - op["pad"] + ky
        ix = ox * op["stride"] - op["pad"] + kx
        valid = 0 <= iy < op["hin"] and 0 <= ix < op["win"]
        if valid:
            require(np.array_equal(taps[tap, :valid_channels], feature_map[iy, ix, start:start + valid_channels]), f"tap {tap} differs from feature map")
            require(np.array_equal(taps[tap], rows[ky, ix, batch]), f"tap {tap} differs from logical rows")
        else:
            require(np.all(taps[tap] == 0), f"padding tap {tap} is not zero")
        require(np.all(taps[tap, valid_channels:] == 0), f"unused lanes in tap {tap} are not zero")
    return rows, taps


def expected_linebuffer_tap(feature_map, op, oy, ox, batch, tap):
    """Read the expected lane values directly from the source feature map."""
    ky, kx = divmod(tap, 3)
    iy = oy * op["stride"] - op["pad"] + ky
    ix = ox * op["stride"] - op["pad"] + kx
    spatial_valid = 0 <= iy < op["hin"] and 0 <= ix < op["win"]
    expected = np.zeros(LANES, dtype=np.int8)
    if spatial_valid:
        start = batch * LANES
        count = min(LANES, op["cin"] - start)
        expected[:count] = feature_map[iy, ix, start:start + count]
    return expected, iy, ix, spatial_valid


def unpack_lanes_256(data_hex):
    word = int(data_hex, 16)
    return np.array([((word >> (8 * lane)) & 0xFF) -
                     (256 if ((word >> (8 * lane)) & 0x80) else 0)
                     for lane in range(LANES)], dtype=np.int8)


def validate_linebuffer_outputs(out_dir, feature_map, op, oy, ox, batch, manifest):
    """Validate saved rows/taps against source pixels, including edge windows."""
    rows = np.load(out_dir / "03_linebuf_3rows.npy", allow_pickle=False)
    taps = np.load(out_dir / "04_linebuf_9taps.npy", allow_pickle=False)
    batches = (op["cin"] + LANES - 1) // LANES
    valid_channels = min(LANES, op["cin"] - batch * LANES)
    expected_mask = (1 << valid_channels) - 1
    rows_shape = (3, op["win"], batches, LANES)
    geometry_check = (op["kind"] == "dw" and op["kernel"] == 3 and
                      feature_map.shape == (op["hin"], op["win"], op["cin"]) and
                      rows.shape == rows_shape and taps.shape == (9, LANES))
    dilation_check = op["dilation"] == 1
    mask_check = (batches == num_batches(op) and expected_mask == batch_mask(op, batch))
    if op["cin"] == 24:
        mask_check = mask_check and batches == 1 and expected_mask == 0x00FFFFFF

    row_details = []
    valid_lane_check = bool(geometry_check)
    invalid_lanes_zero = bool(geometry_check)
    for ky in range(3):
        iy = oy * op["stride"] - op["pad"] + ky
        expected_row = np.zeros((op["win"], batches, LANES), dtype=np.int8)
        if 0 <= iy < op["hin"]:
            for b in range(batches):
                start = b * LANES
                count = min(LANES, op["cin"] - start)
                expected_row[:, b, :count] = feature_map[iy, :, start:start + count]
        match = bool(geometry_check and np.array_equal(rows[ky], expected_row))
        row_details.append({"row": ky, "source_iy": iy, "spatial_valid": 0 <= iy < op["hin"], "match": match})
        if geometry_check:
            for b in range(batches):
                count = min(LANES, op["cin"] - b * LANES)
                valid_lane_check = valid_lane_check and bool(np.array_equal(rows[ky, :, b, :count], expected_row[:, b, :count]))
                invalid_lanes_zero = invalid_lanes_zero and bool(np.all(rows[ky, :, b, count:] == 0))

    tap_details = []
    tap_order_check = bool(taps.shape == (9, LANES))
    selected_window_check = bool(taps.shape == (9, LANES))
    pack_unpack_check = bool(taps.shape == (9, LANES))
    for tap in range(9):
        ky, kx = divmod(tap, 3)
        expected, iy, ix, spatial_valid = expected_linebuffer_tap(feature_map, op, oy, ox, batch, tap)
        actual = taps[tap] if taps.shape == (9, LANES) else np.zeros(LANES, dtype=np.int8)
        match = bool(taps.shape == (9, LANES) and np.array_equal(actual, expected))
        packed = pack_lanes_256(actual, expected_mask)
        unpack_match = bool(np.array_equal(unpack_lanes_256(packed["data_hex"]), actual))
        tap_order_check = tap_order_check and tap == ky * 3 + kx and match
        selected_window_check = selected_window_check and match
        pack_unpack_check = pack_unpack_check and unpack_match
        valid_lane_check = valid_lane_check and bool(np.array_equal(actual[:valid_channels], expected[:valid_channels]))
        invalid_lanes_zero = invalid_lanes_zero and bool(np.all(actual[valid_channels:] == 0))
        tap_details.append({"tap": tap, "ky": ky, "kx": kx, "iy": iy, "ix": ix,
                            "spatial_valid": spatial_valid, "expected_lanes": expected.tolist(),
                            "lanes": actual.tolist(), "data256_hex": packed["data_hex"],
                            "match": match, "pack_unpack_match": unpack_match})

    # Conv0 is nonnegative; also test signed two's-complement packing explicitly.
    signed_test = np.arange(-16, 16, dtype=np.int8)
    signed_hex = pack_lanes_256(signed_test, 0xFFFFFFFF)["data_hex"]
    pack_unpack_check = pack_unpack_check and bool(np.array_equal(unpack_lanes_256(signed_hex), signed_test))

    boundary_windows = []
    for boundary_oy, boundary_ox in ((0, 0), (1, 1), (op["hout"] - 1, op["wout"] - 1)):
        if not (0 <= boundary_oy < op["hout"] and 0 <= boundary_ox < op["wout"]):
            continue
        actual_taps = extract_linebuffer_taps(feature_map, op, boundary_oy, boundary_ox, batch)
        details = []
        for tap in range(9):
            expected, iy, ix, spatial_valid = expected_linebuffer_tap(feature_map, op, boundary_oy, boundary_ox, batch, tap)
            details.append({"tap": tap, "iy": iy, "ix": ix, "spatial_valid": spatial_valid,
                            "match": bool(np.array_equal(actual_taps[tap], expected))})
        boundary_windows.append({"oy": boundary_oy, "ox": boundary_ox, "taps": details,
                                 "match": all(item["match"] for item in details)})

    stride2_dw = next((candidate for candidate in manifest["ops"]
                       if candidate["kind"] == "dw" and candidate["stride"] == 2), None)
    checks = {"geometry": bool(geometry_check), "dilation": bool(dilation_check),
              "three_rows": all(row["match"] for row in row_details),
              "mask": bool(mask_check), "valid_lanes": bool(valid_lane_check),
              "invalid_lanes_zero": bool(invalid_lanes_zero), "tap_order": bool(tap_order_check),
              "selected_window": bool(selected_window_check),
              "top_left_boundary": bool(boundary_windows[0]["match"]),
              "bottom_right_boundary": bool(boundary_windows[-1]["match"]),
              "pack_unpack_256": bool(pack_unpack_check)}
    return {
        "operation": op["name"], "input_shape": list(feature_map.shape),
        "kernel": op["kernel"], "stride": op["stride"], "pad": op["pad"],
        "dilation": op["dilation"], "cin": op["cin"], "num_batches": batches,
        "rows_shape": list(rows.shape), "taps_shape": list(taps.shape),
        "rows": row_details,
        "selected_window": {"oy": oy, "ox": ox, "batch": batch,
                            "mask": f"0x{expected_mask:08X}", "taps": tap_details},
        "boundary_windows": boundary_windows,
        "stride2_depthwise": {"operation": stride2_dw["name"] if stride2_dw else None,
                              "geometry_helper_logic_available": True,
                              "actual_feature_map_validation": "skipped (no layer input feature map)"},
        "checks": checks, "overall_pass": all(checks.values()),
    }


def print_linebuffer_validation(report):
    checks = report["checks"]
    verdict = lambda passed: "PASS" if passed else "FAIL"
    print("[Line Buffer Validation]")
    print(f"Operation\n  name     : {report['operation']}\n  input    : {tuple(report['input_shape'])}\n  kernel   : {report['kernel']}x{report['kernel']}\n  stride   : {report['stride']}\n  pad      : {report['pad']}\n  dilation : {report['dilation']}\n  batches  : {report['num_batches']}")
    selected = report["selected_window"]
    print(f"Selected Window\n  oy       : {selected['oy']}\n  ox       : {selected['ox']}\n  batch    : {selected['batch']}\n  mask     : {selected['mask']}")
    print("Input coordinates\n          kx0       kx1       kx2")
    for ky in range(3):
        coords = [f"({selected['taps'][ky * 3 + kx]['iy']},{selected['taps'][ky * 3 + kx]['ix']})" for kx in range(3)]
        print(f"ky{ky}     {coords[0]:<10}{coords[1]:<10}{coords[2]}")
    for row in report["rows"]:
        print(f"row{row['row']} source iy = {row['source_iy']}, match : {verdict(row['match'])}")
    for label, key in (("geometry check", "geometry"), ("dilation check", "dilation"),
                       ("3-row check", "three_rows"), ("channel mask check", "mask"),
                       ("valid lane check", "valid_lanes"), ("invalid lane zero check", "invalid_lanes_zero"),
                       ("tap order check", "tap_order")):
        print(f"{label:<25}: {verdict(checks[key])}")
    for tap in selected["taps"]:
        print(f"Tap{tap['tap']} -> ({tap['iy']},{tap['ix']}), valid={tap['spatial_valid']}, first_lanes={tap['lanes'][:8]}, mask={selected['mask']}, data256={tap['data256_hex']}, match={verdict(tap['match'])}")
    for boundary in report["boundary_windows"]:
        print(f"Boundary window ({boundary['oy']},{boundary['ox']}) : {verdict(boundary['match'])}")
        if (boundary["oy"], boundary["ox"]) == (0, 0):
            for tap in boundary["taps"]:
                print(f"  Tap{tap['tap']} {'data' if tap['spatial_valid'] else 'padding'} : {verdict(tap['match'])}")
    print(f"256bit pack/unpack      : {verdict(checks['pack_unpack_256'])}")
    print(f"stride=2 DW {report['stride2_depthwise']['operation']}: {report['stride2_depthwise']['actual_feature_map_validation']}")
    print(f"Line Buffer validation : {verdict(report['overall_pass'])}")


def visual_font(size):
    font_path = Path("C:/Windows/Fonts/arial.ttf")
    return ImageFont.truetype(str(font_path), size) if font_path.is_file() else ImageFont.load_default()


def channel_grayscale(feature_map, channel):
    """Expand body values for display only; leave the int8 source untouched."""
    values = np.clip(feature_map[:, :, channel], 0, 127).astype(np.uint16)
    return ((values * 255 + 63) // 127).astype(np.uint8)


def draw_tap_grid(feature_map, op, oy, ox, channel, taps, destination, title):
    batch, lane = divmod(channel, LANES)
    canvas = Image.new("RGB", (720, 590), (245, 248, 251))
    draw = ImageDraw.Draw(canvas)
    draw.text((24, 14), title, fill=(25, 35, 48), font=visual_font(23))
    draw.text((24, 44), f"oy={oy}  ox={ox}  CH{channel}  batch={batch} lane={lane}",
              fill=(45, 60, 78), font=visual_font(16))
    tile_w, tile_h, gap = 216, 158, 10
    for tap in range(9):
        expected, iy, ix, valid = expected_linebuffer_tap(feature_map, op, oy, ox, batch, tap)
        value = int(taps[tap, lane])
        require(value == int(expected[lane]), f"visual tap {tap} differs from source feature map")
        x = 24 + (tap % 3) * (tile_w + gap)
        y = 82 + (tap // 3) * (tile_h + gap)
        shade = 45 + int(np.clip(value, 0, 127)) * 135 // 127
        fill = (shade, shade, shade) if valid else (255, 220, 195)
        border = (38, 158, 111) if valid else (219, 77, 53)
        ink = (255, 255, 255) if valid else (80, 33, 24)
        draw.rectangle((x, y, x + tile_w, y + tile_h), fill=fill, outline=border, width=5)
        draw.text((x + 13, y + 10), f"Tap{tap}", fill=ink, font=visual_font(23))
        draw.text((x + 13, y + 48), f"iy={iy}  ix={ix}", fill=ink, font=visual_font(17))
        draw.text((x + 13, y + 75), f"CH{channel}  value={value}", fill=ink, font=visual_font(17))
        draw.text((x + 13, y + 111), "VALID" if valid else "PADDING",
                  fill=ink, font=visual_font(19))
    canvas.save(destination)


def draw_feature_context(grayscale, feature_map, op, oy, ox, channel, taps, destination, rows_only):
    """Show the source map, selected rows, and the tap locations together."""
    height, width = grayscale.shape
    scale = 4
    left, top = 24, 85
    canvas = Image.new("RGB", (1130, 650), (245, 248, 251))
    overview = Image.fromarray(grayscale, mode="L").convert("RGB")
    canvas.paste(overview.resize((width * scale, height * scale), Image.NEAREST), (left, top))
    draw = ImageDraw.Draw(canvas)
    heading = "Three logical input rows" if rows_only else "Selected 3x3 window on Conv0 feature map"
    draw.text((24, 15), heading, fill=(25, 35, 48), font=visual_font(23))
    draw.text((24, 48), f"CH{channel}  output (oy, ox)=({oy}, {ox})  stride={op['stride']} pad={op['pad']}",
              fill=(45, 60, 78), font=visual_font(16))
    colors = ((238, 75, 62), (33, 182, 111), (45, 130, 235))
    for ky, color in enumerate(colors):
        iy = oy * op["stride"] - op["pad"] + ky
        if 0 <= iy < height:
            y = top + iy * scale
            draw.rectangle((left, y, left + width * scale - 1, y + scale - 1), outline=color, width=3)
    draw.rectangle((left - 1, top - 1, left + width * scale, top + height * scale),
                   outline=(68, 78, 92), width=2)
    right = 566
    if rows_only:
        draw.text((right, 88), "ky row  ->  source row", fill=(25, 35, 48), font=visual_font(19))
        for ky, color in enumerate(colors):
            iy = oy * op["stride"] - op["pad"] + ky
            y = 140 + ky * 145
            label = f"ky{ky} -> iy={iy}" if 0 <= iy < height else f"ky{ky} -> PADDING (iy={iy})"
            draw.text((right, y), label, fill=color, font=visual_font(20))
            if 0 <= iy < height:
                strip = Image.fromarray(np.repeat(grayscale[iy:iy + 1, :], 34, axis=0), mode="L")
                canvas.paste(strip.resize((width * scale, 68), Image.NEAREST).convert("RGB"), (right, y + 33))
                draw.rectangle((right, y + 33, right + width * scale - 1, y + 100),
                               outline=color, width=3)
            else:
                draw.rectangle((right, y + 33, right + width * scale - 1, y + 100),
                               fill=(255, 220, 195), outline=color, width=3)
                draw.text((right + 14, y + 52), "zero-filled logical row", fill=(95, 45, 35),
                          font=visual_font(17))
    else:
        draw.text((right, 88), "Tap order: ky-major, then kx", fill=(25, 35, 48), font=visual_font(19))
        batch, lane = divmod(channel, LANES)
        for tap in range(9):
            _, iy, ix, valid = expected_linebuffer_tap(feature_map, op, oy, ox, batch, tap)
            value = int(taps[tap, lane])
            require(value == (int(feature_map[iy, ix, channel]) if valid else 0),
                    f"visual window tap {tap} differs from source feature map")
            x = right + (tap % 3) * 174
            y = 132 + (tap // 3) * 145
            fill = (230, 246, 238) if valid else (255, 220, 195)
            border = (38, 158, 111) if valid else (219, 77, 53)
            draw.rectangle((x, y, x + 164, y + 132), fill=fill, outline=border, width=3)
            draw.text((x + 9, y + 8), f"T{tap}  ({iy},{ix})", fill=(30, 42, 56), font=visual_font(17))
            draw.text((x + 9, y + 47), f"CH{channel} = {value}", fill=(30, 42, 56), font=visual_font(17))
            draw.text((x + 9, y + 86), "VALID" if valid else "PADDING",
                      fill=border, font=visual_font(17))
            if valid:
                px, py = left + ix * scale, top + iy * scale
                draw.rectangle((px - 2, py - 2, px + scale + 1, py + scale + 1),
                               outline=(250, 223, 45), width=2)
        valid_positions = [(oy * op["stride"] - op["pad"] + ky,
                            ox * op["stride"] - op["pad"] + kx)
                           for ky in range(3) for kx in range(3)
                           if 0 <= oy * op["stride"] - op["pad"] + ky < height
                           and 0 <= ox * op["stride"] - op["pad"] + kx < width]
        if valid_positions:
            ys, xs = zip(*valid_positions)
            draw.rectangle((left + min(xs) * scale - 4, top + min(ys) * scale - 4,
                            left + (max(xs) + 1) * scale + 3, top + (max(ys) + 1) * scale + 3),
                           outline=(255, 230, 45), width=3)
        draw.text((right, 578), "Orange = out-of-bounds zero padding", fill=(85, 58, 45),
                  font=visual_font(16))
    canvas.save(destination)


def draw_32lane_matrix(taps, op, batch, destination):
    cell_w, cell_h = 30, 38
    left, top = 86, 98
    canvas = Image.new("RGB", (1100, 505), (245, 248, 251))
    draw = ImageDraw.Draw(canvas)
    draw.text((24, 13), "Tap0-Tap8: 32 lanes x 8 bits = 256 bits per tap",
              fill=(25, 35, 48), font=visual_font(22))
    draw.text((24, 48), f"batch={batch}  active lanes follow mask 0x{batch_mask(op, batch):08X}  |  blue lanes are unused zeros",
              fill=(45, 60, 78), font=visual_font(16))
    valid_count = min(LANES, op["cin"] - batch * LANES)
    for lane in range(LANES):
        draw.text((left + lane * cell_w + 6, top - 25), str(lane),
                  fill=(40, 50, 65), font=visual_font(12))
    for tap in range(9):
        y = top + tap * (cell_h + 3)
        draw.text((20, y + 7), f"Tap{tap}", fill=(30, 40, 55), font=visual_font(15))
        for lane in range(LANES):
            value = int(taps[tap, lane])
            active = lane < valid_count
            shade = 45 + int(np.clip(value, 0, 127)) * 160 // 127
            fill = (shade, shade, shade) if active else (78, 129, 185)
            ink = (255, 255, 255) if shade < 125 or not active else (20, 30, 40)
            x = left + lane * cell_w
            draw.rectangle((x, y, x + cell_w - 2, y + cell_h - 2), fill=fill)
            draw.text((x + 3, y + 11), str(value), fill=ink, font=visual_font(12))
    canvas.save(destination)


def save_linebuffer_visuals(feature_map, op, oy, ox, batch, visual_channel, out_dir):
    require(0 <= visual_channel < feature_map.shape[2],
            f"visual channel must be 0..{feature_map.shape[2] - 1}")
    visual_dir = out_dir / "linebuffer_visual"
    visual_dir.mkdir(parents=True, exist_ok=True)
    suffix = f"ch{visual_channel:02d}"
    gray = channel_grayscale(feature_map, visual_channel)
    paths = {
        "feature_map": visual_dir / f"conv0_{suffix}_feature_map.png",
        "three_rows": visual_dir / f"linebuffer_3rows_{suffix}.png",
        "selected_window": visual_dir / f"linebuffer_window_{suffix}.png",
        "tap_grid": visual_dir / f"linebuffer_taps_{suffix}.png",
        "boundary": visual_dir / f"boundary_00_taps_{suffix}.png",
        "lanes32": visual_dir / "linebuffer_taps_32lanes.png",
    }
    Image.fromarray(gray, mode="L").save(paths["feature_map"])
    visual_batch = visual_channel // LANES
    selected_taps = extract_linebuffer_taps(feature_map, op, oy, ox, visual_batch)
    boundary_taps = extract_linebuffer_taps(feature_map, op, 0, 0, visual_batch)
    draw_feature_context(gray, feature_map, op, oy, ox, visual_channel,
                         selected_taps, paths["three_rows"], rows_only=True)
    draw_feature_context(gray, feature_map, op, oy, ox, visual_channel,
                         selected_taps, paths["selected_window"], rows_only=False)
    draw_tap_grid(feature_map, op, oy, ox, visual_channel, selected_taps,
                  paths["tap_grid"], "Selected Line Buffer taps")
    draw_tap_grid(feature_map, op, 0, 0, visual_channel, boundary_taps,
                  paths["boundary"], "Boundary window (0,0): padding taps in orange")
    draw_32lane_matrix(extract_linebuffer_taps(feature_map, op, oy, ox, batch),
                       op, batch, paths["lanes32"])
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=RESULT_DIR / "02_conv0.npy",
                        help="input_conv_pe output, HWC int8 .npy")
    parser.add_argument("--out", type=Path, default=RESULT_DIR)
    parser.add_argument("--oy", type=int, default=1)
    parser.add_argument("--ox", type=int, default=1)
    parser.add_argument("--batch", type=int, default=0)
    parser.add_argument("--visual-channel", type=int, default=0)
    args = parser.parse_args()

    feature_map = np.load(args.input, allow_pickle=False)
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    op = find_first_dw(manifest, find_conv0(manifest))
    check_linebuffer_input(feature_map, op)
    mask = batch_mask(op, args.batch)
    if not 0 <= args.visual_channel < feature_map.shape[2]:
        raise ValueError(f"visual channel must be 0..{feature_map.shape[2] - 1}")
    rows, taps = check_window(feature_map, op, args.oy, args.ox, args.batch)

    args.out.mkdir(parents=True, exist_ok=True)
    np.save(args.out / "03_linebuf_3rows.npy", rows)
    np.save(args.out / "04_linebuf_9taps.npy", taps)
    report = validate_linebuffer_outputs(args.out, feature_map, op, args.oy,
                                         args.ox, args.batch, manifest)
    (args.out / "linebuffer_validation.json").write_text(
        json.dumps(report, indent=2), encoding="utf-8")
    visuals = save_linebuffer_visuals(feature_map, op, args.oy, args.ox, args.batch,
                                      args.visual_channel, args.out)

    print("[line_buffer / First DW]")
    print(f"Operation : {op['name']}")
    print(f"Input     : {feature_map.shape} {feature_map.dtype}")
    print(f"3 rows    : {rows.shape}; {args.out / '03_linebuf_3rows.npy'}")
    print(f"9 taps    : {taps.shape}; {args.out / '04_linebuf_9taps.npy'}")
    print(f"Window    : oy={args.oy}, ox={args.ox}, batch={args.batch}, mask=0x{mask:08X}")
    print(f"Visualization: CH{args.visual_channel} at {visuals['tap_grid']}")
    print_linebuffer_validation(report)
    if not report["overall_pass"]:
        raise ValueError("Line Buffer validation failed")
    print("PASS")


if __name__ == "__main__":
    main()
