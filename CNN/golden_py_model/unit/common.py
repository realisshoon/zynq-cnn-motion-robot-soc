"""Common integer helpers for CNN-v4 module-level reference models.

This file models the *numeric contract*, not RTL handshakes/cycles.
"""

def require_int_range(value, lo, hi, name):
    value = int(value)
    if not lo <= value <= hi:
        raise ValueError(f"{name}={value} outside [{lo}, {hi}]")
    return value


def rshift_rne_ties_even(value, n):
    """Signed arithmetic right shift with round-to-nearest, ties-to-even.

    Matches the CNN-v4 integer contract:
      q = p >> n
      r = p - (q << n)
      h = 1 << (n-1)
      result = q + ((r > h) or ((r == h) and (q & 1)))

    Python's >> is arithmetic/flooring for negative integers, so r is always
    in [0, 2**n - 1], matching the contract.
    """
    value = int(value)
    n = int(n)
    if n < 0:
        raise ValueError("n must be >= 0")
    if n == 0:
        return value

    q = value >> n
    r = value - (q << n)
    half = 1 << (n - 1)
    if r > half or (r == half and (q & 1)):
        q += 1
    return q


def twos(value, bits):
    """Return the low `bits` bits of signed/unsigned integer as two's complement."""
    return int(value) & ((1 << bits) - 1)
