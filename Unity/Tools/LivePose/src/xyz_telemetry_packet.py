import struct


SYNC0 = 0xA5
SYNC1 = 0x5A
VERSION = 1
TYPE_XYZ = 2

PAYLOAD_SIZE = 78
PACKET_SIZE = 86

LEFT_VALID  = 1 << 0
LEFT_FRESH  = 1 << 1
RIGHT_VALID = 1 << 2
RIGHT_FRESH = 1 << 3


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF

    for b in data:
        crc ^= b << 8

        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF

    return crc


def point_or_zero(data, name, fallback_name=None):
    if data is None:
        return (0.0, 0.0, 0.0)

    if name in data:
        point = data[name]
    elif fallback_name is not None and fallback_name in data:
        point = data[fallback_name]
    else:
        raise KeyError(
            f"missing point '{name}'"
            + (
                f" (fallback '{fallback_name}')"
                if fallback_name is not None
                else ""
            )
        )

    return tuple(float(v) for v in point)


def make_xyz_packet(frame_id, left_fresh, right_fresh,
                    left_data, right_data):

    flags = 0

    if left_data is not None:
        flags |= LEFT_VALID
    if left_fresh:
        flags |= LEFT_FRESH

    if right_data is not None:
        flags |= RIGHT_VALID
    if right_fresh:
        flags |= RIGHT_FRESH

    # Robot C reconstruction returns both shoulders in each side context.
    # LEFT telemetry uses the anatomical left shoulder.
    # RIGHT telemetry uses the anatomical right shoulder.
    # "shoulder" fallback keeps the standalone packet self-test compatible.
    ls = point_or_zero(left_data, "shoulder_l", "shoulder")
    le = point_or_zero(left_data, "elbow")
    lw = point_or_zero(left_data, "wrist")

    rs = point_or_zero(right_data, "shoulder_r", "shoulder")
    re = point_or_zero(right_data, "elbow")
    rw = point_or_zero(right_data, "wrist")

    xyz = (
        *ls, *le, *lw,
        *rs, *re, *rw,
    )

    payload = struct.pack(
        "<IBB18f",
        frame_id & 0xFFFFFFFF,
        flags,
        0,
        *xyz
    )

    assert len(payload) == PAYLOAD_SIZE

    packet = bytearray([
        SYNC0,
        SYNC1,
        VERSION,
        TYPE_XYZ,
        PAYLOAD_SIZE,
        0,
    ])

    packet.extend(payload)

    crc = crc16_ccitt(bytes(packet[2:]))
    packet.extend(struct.pack("<H", crc))

    assert len(packet) == PACKET_SIZE

    return bytes(packet)


def decode_xyz_packet(packet: bytes):
    if len(packet) != PACKET_SIZE:
        raise ValueError(
            f"bad packet size {len(packet)}"
        )

    if packet[0] != SYNC0 or packet[1] != SYNC1:
        raise ValueError("bad sync")

    if packet[2] != VERSION:
        raise ValueError("bad version")

    if packet[3] != TYPE_XYZ:
        raise ValueError("bad packet type")

    if packet[4] != PAYLOAD_SIZE:
        raise ValueError("bad payload size")

    rx_crc = struct.unpack_from("<H", packet, PACKET_SIZE - 2)[0]
    calc_crc = crc16_ccitt(packet[2:PACKET_SIZE - 2])

    if rx_crc != calc_crc:
        raise ValueError("CRC mismatch")

    values = struct.unpack_from(
        "<IBB18f",
        packet,
        6
    )

    frame_id = values[0]
    flags = values[1]
    xyz = values[3:]

    return {
        "frame_id": frame_id,
        "flags": flags,

        "left_valid": bool(flags & LEFT_VALID),
        "left_fresh": bool(flags & LEFT_FRESH),
        "right_valid": bool(flags & RIGHT_VALID),
        "right_fresh": bool(flags & RIGHT_FRESH),

        "left_shoulder": tuple(xyz[0:3]),
        "left_elbow":    tuple(xyz[3:6]),
        "left_wrist":    tuple(xyz[6:9]),

        "right_shoulder": tuple(xyz[9:12]),
        "right_elbow":    tuple(xyz[12:15]),
        "right_wrist":    tuple(xyz[15:18]),
    }


if __name__ == "__main__":
    left = {
        "shoulder": (0.1, 0.2, 3.0),
        "elbow":    (0.4, 0.5, 2.7),
        "wrist":    (0.6, 0.7, 2.3),
    }

    right = {
        "shoulder": (-0.1, 0.2, 3.0),
        "elbow":    (-0.4, 0.5, 2.7),
        "wrist":    (-0.6, 0.7, 2.3),
    }

    packet = make_xyz_packet(
        123,
        True,
        False,
        left,
        right
    )

    print("PACKET SIZE =", len(packet))
    print("HEADER      =", packet[:6].hex(" "))
    print("CRC         =", packet[-2:].hex(" "))

    decoded = decode_xyz_packet(packet)

    for k, v in decoded.items():
        print(k, "=", v)
