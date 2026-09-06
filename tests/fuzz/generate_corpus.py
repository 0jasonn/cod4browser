"""Generate GPL-3.0 synthetic IWI/IWD seeds, without game data or compression tools."""
import argparse
import binascii
import hashlib
import json
import struct
from pathlib import Path


def iwi(fmt=1, width=2, height=2, payload=bytes(range(16))):
    size = 28 + len(payload)
    return b"IWi\x06" + struct.pack("<BBHHH4I", fmt, 2, width, height, 1, *([size] * 4)) + payload


def iwd(deflated=False):
    name = b"synthetic.cfg"
    payload = b"Synthetic public parser fixture.\n" * 16
    # A single raw DEFLATE stored block is deterministic across zlib versions.
    compressed = b"\x01" + struct.pack("<HH", len(payload), len(payload) ^ 0xffff) + payload if deflated else payload
    crc = binascii.crc32(payload)
    method = 8 if deflated else 0
    local = struct.pack("<I5H3I2H", 0x04034b50, 20, 0, method, 0, 0,
                        crc, len(compressed), len(payload), len(name), 0) + name + compressed
    central = struct.pack("<I6H3I5H2I", 0x02014b50, 20, 20, 0, method, 0, 0,
                          crc, len(compressed), len(payload), len(name), 0, 0, 0, 0, 0, 0) + name
    return local + central + struct.pack("<I4H2IH", 0x06054b50, 0, 0, 1, 1, len(central), len(local), 0)


def seeds():
    image = iwi()
    archive = iwd(True)
    bad_count = bytearray(archive)
    struct.pack_into("<H", bad_count, len(bad_count) - 12, 65535)
    bad_size = bytearray(archive)
    # Advertise one decoded byte in both headers while DEFLATE emits 512.
    struct.pack_into("<I", bad_size, 22, 1)
    struct.pack_into("<I", bad_size, archive.index(b"PK\x01\x02") + 24, 1)
    bad_crc = bytearray(archive)
    bad_crc[14] ^= 1
    bad_crc[archive.index(b"PK\x01\x02") + 16] ^= 1
    return {
        "iwi-rgba": image,
        "iwi-dxt1": iwi(11, 4, 4, bytes(8)),
        "iwi-truncated-header": image[:15],
        "iwi-truncated-payload": image[:-1],
        "iwi-unsupported-format": iwi(255),
        "iwi-dimension-overflow": iwi(1, 65535, 65535),
        "iwd-stored": iwd(),
        "iwd-deflated": archive,
        "iwd-truncated-central": archive[:-24],
        "iwd-count-overflow": bytes(bad_count),
        "iwd-decompression-bound": bytes(bad_size),
        "iwd-crc-failure": bytes(bad_crc),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    identity = {}
    for name, data in sorted(seeds().items()):
        (args.directory / (name + ".seed")).write_bytes(data)
        identity[name] = hashlib.sha256(data).hexdigest()
    print(json.dumps(identity, indent=2, sort_keys=True))
