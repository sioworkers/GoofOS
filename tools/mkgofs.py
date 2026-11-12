#!/usr/bin/env python3
"""write a small GoFS image into a raw disk image."""

import os
import struct
import sys

SECTOR_SIZE = 512
NAME_SIZE = 24


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: mkgofs.py image file start-sector", file=sys.stderr)
        return 2

    image_path, file_path, start_text = sys.argv[1:]
    start_sector = int(start_text)
    with open(file_path, "rb") as source:
        data = source.read()
    name = file_path.rsplit("/", 1)[-1].encode("ascii")
    if not name or len(name) >= NAME_SIZE:
        print("GoFS filename must be 1 to 23 ASCII bytes", file=sys.stderr)
        return 1

    # GoFS takes everything from start_sector to the end of the image
    image_sectors = os.path.getsize(image_path) // SECTOR_SIZE
    total_sectors = image_sectors - start_sector
    file_sectors = (len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    if file_sectors > total_sectors - 2:
        print("file does not fit in GoFS", file=sys.stderr)
        return 1

    superblock = bytearray(SECTOR_SIZE)
    superblock[:4] = b"GOFS"
    struct.pack_into("<HHIII", superblock, 4, 1, 1, 1, 2, total_sectors)
    directory = bytearray(SECTOR_SIZE)
    directory[:NAME_SIZE] = name + bytes(NAME_SIZE - len(name))
    struct.pack_into("<II", directory, NAME_SIZE, 2, len(data))

    with open(image_path, "r+b") as image:
        _ = image.seek(start_sector * SECTOR_SIZE)
        _ = image.write(superblock)
        _ = image.write(directory)
        _ = image.write(data)
        _ = image.write(bytes(file_sectors * SECTOR_SIZE - len(data)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
