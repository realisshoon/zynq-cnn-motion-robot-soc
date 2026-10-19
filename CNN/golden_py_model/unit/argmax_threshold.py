"""Independent software reference for argmax_threshold.

Algorithm-only model:
- input heatmap: 16x16x17 signed INT8
- input offset : 16x16x34 signed INT16
- one result per joint 0..16
- strict '>' update => first row-major tie wins
- NO confidence threshold and NO image-bounds check here
  (those belong to coord_restore in CNN-v4)

This module intentionally does NOT model valid/ready/done/fault timing.
"""

import numpy as np

H = 16
W = 16
JOINTS = 17


def _check_tensor(name, a, shape, lo, hi):
    a = np.asarray(a)
    if a.shape != shape:
        raise ValueError(f"{name}.shape must be {shape}, got {a.shape}")
    if np.any(a < lo) or np.any(a > hi):
        raise ValueError(f"{name} contains values outside [{lo}, {hi}]")
    return a


def pack_result_data64(grid_col, grid_row, offset_x, offset_y, score):
    """Pack the argmax_threshold output payload.

    [ 7: 0] grid_col
    [15: 8] grid_row
    [31:16] offset_x signed16
    [47:32] offset_y signed16
    [55:48] score signed8
    [63:56] reserved = 0
    """
    return (
        (int(grid_col) & 0xFF)
        | ((int(grid_row) & 0xFF) << 8)
        | ((int(offset_x) & 0xFFFF) << 16)
        | ((int(offset_y) & 0xFFFF) << 32)
        | ((int(score) & 0xFF) << 48)
    )


def argmax_threshold_ref(heatmap, offset):
    """Return 17 software-reference results.

    This is intentionally written as explicit nested loops rather than using
    np.argmax so the first-row-major tie rule is visible in the source.
    """
    heat = _check_tensor("heatmap", heatmap, (H, W, JOINTS), -128, 127)
    off  = _check_tensor("offset",  offset,  (H, W, JOINTS * 2), -32768, 32767)

    results = []

    for joint in range(JOINTS):
        seen = False
        best_score = -128
        best_row = 0
        best_col = 0

        # row-major scan: row -> col
        for row in range(H):
            for col in range(W):
                score = int(heat[row, col, joint])
                if (not seen) or (score > best_score):
                    seen = True
                    best_score = score
                    best_row = row
                    best_col = col

        # CNN-v4 head channel mapping:
        # 0..16 = y offsets, 17..33 = x offsets
        offset_y = int(off[best_row, best_col, joint])
        offset_x = int(off[best_row, best_col, JOINTS + joint])

        word = pack_result_data64(
            best_col, best_row, offset_x, offset_y, best_score
        )

        results.append({
            "joint": joint,
            "grid_row": best_row,
            "grid_col": best_col,
            "offset_y": offset_y,
            "offset_x": offset_x,
            "score": best_score,
            "result_data64": word,
        })

    return results


if __name__ == "__main__":
    heat = np.full((H, W, JOINTS), -128, dtype=np.int8)
    off = np.zeros((H, W, JOINTS * 2), dtype=np.int16)

    for joint in range(JOINTS):
        heat[0, 0, joint] = 10 + joint
        off[0, 0, joint] = -100 + joint
        off[0, 0, JOINTS + joint] = 200 - joint

    results = argmax_threshold_ref(heat, off)
    print(f"argmax_threshold_ref: {len(results)} results")
    for r in results[:3]:
        print(r)
