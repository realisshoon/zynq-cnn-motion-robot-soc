"""Independent software reference for coord_restore.

Source of truth: CNN-v4 coordinate math contract.

Algorithm-only model:
- consumes one argmax_threshold result
- exact signed integer arithmetic
- final Q16 -> integer conversion uses RNE ties-to-even
- threshold + image bounds decide good
- invalid output forces x=y=0 but preserves score
- does NOT model handshake/latency
"""

try:
    from .common import rshift_rne_ties_even
except ImportError:
    from common import rshift_rne_ties_even

GRID_STEP_ORIG = 80        # model stride16 * external stride5
PAD_TOP_ORIG = 280
Q16 = 1 << 16
OFFSET_SCALE_Q16 = 3096
EXTERNAL_STRIDE = 5
OFFSET_COEF = OFFSET_SCALE_Q16 * EXTERNAL_STRIDE  # 15480
IMAGE_W = 1280
IMAGE_H = 720
DEFAULT_THRESHOLD = -46


def pack_joint_word(score, x, y):
    """32-bit top-level JOINT register word.

    [11:0]  x
    [23:12] y
    [31:24] score_raw signed8 (two's-complement bits)
    """
    return (
        (int(x) & 0xFFF)
        | ((int(y) & 0xFFF) << 12)
        | ((int(score) & 0xFF) << 24)
    )


def coord_restore_one(
    joint,
    grid_col,
    grid_row,
    offset_x,
    offset_y,
    score,
    threshold=DEFAULT_THRESHOLD,
):
    joint = int(joint)
    grid_col = int(grid_col)
    grid_row = int(grid_row)
    offset_x = int(offset_x)
    offset_y = int(offset_y)
    score = int(score)
    threshold = int(threshold)

    if not 0 <= joint <= 16:
        raise ValueError("joint must be 0..16")
    if not 0 <= grid_col <= 15 or not 0 <= grid_row <= 15:
        raise ValueError("grid row/col must be 0..15")
    if not -32768 <= offset_x <= 32767 or not -32768 <= offset_y <= 32767:
        raise ValueError("offset must fit signed16")
    if not -128 <= score <= 127 or not -128 <= threshold <= 127:
        raise ValueError("score/threshold must fit signed8")

    # Keep all fractional information in signed integer Q16 form.
    x_q16 = grid_col * GRID_STEP_ORIG * Q16 + offset_x * OFFSET_COEF
    y_q16 = (grid_row * GRID_STEP_ORIG - PAD_TOP_ORIG) * Q16 + offset_y * OFFSET_COEF

    x_raw = rshift_rne_ties_even(x_q16, 16)
    y_raw = rshift_rne_ties_even(y_q16, 16)

    good = (
        score >= threshold
        and 0 <= x_raw < IMAGE_W
        and 0 <= y_raw < IMAGE_H
    )

    x = x_raw if good else 0
    y = y_raw if good else 0
    word = pack_joint_word(score, x, y)

    return {
        "joint": joint,
        "grid_col": grid_col,
        "grid_row": grid_row,
        "offset_x": offset_x,
        "offset_y": offset_y,
        "score": score,
        "threshold": threshold,
        "x_q16": x_q16,
        "y_q16": y_q16,
        "x_raw": x_raw,
        "y_raw": y_raw,
        "x": x,
        "y": y,
        "good": bool(good),
        "joint_data32": word,
    }


def coord_restore_ref(argmax_results, threshold=DEFAULT_THRESHOLD):
    """Convert the 17 argmax results into 17 final coordinate results."""
    out = []
    for r in argmax_results:
        out.append(coord_restore_one(
            joint=r["joint"],
            grid_col=r["grid_col"],
            grid_row=r["grid_row"],
            offset_x=r["offset_x"],
            offset_y=r["offset_y"],
            score=r["score"],
            threshold=threshold,
        ))
    return out

if __name__ == "__main__":

    result = coord_restore_one(
        joint=0,
        grid_col=5,
        grid_row=7,
        offset_x=-100,
        offset_y=200,
        score=-20,
        threshold=-46
    )

    print("=== coord_restore reference result ===")

    for key, value in result.items():
        print(f"{key}: {value}")