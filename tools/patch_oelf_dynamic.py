#!/usr/bin/env python3
import struct
import sys
from pathlib import Path


PT_DYNAMIC = 2


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_oelf_dynamic.py <oelf>")

    path = Path(sys.argv[1])
    data = bytearray(path.read_bytes())

    if data[:4] != b"\x7fELF":
        raise SystemExit(f"{path}: not an ELF/OELF")
    if data[4] != 2 or data[5] != 1:
        raise SystemExit(f"{path}: expected ELF64 little-endian")

    e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x36)[0]
    e_phnum = struct.unpack_from("<H", data, 0x38)[0]

    patched = 0
    for index in range(e_phnum):
        phoff = e_phoff + index * e_phentsize
        p_type = struct.unpack_from("<I", data, phoff)[0]
        if p_type != PT_DYNAMIC:
            continue

        p_offset = struct.unpack_from("<Q", data, phoff + 0x08)[0]
        p_vaddr = struct.unpack_from("<Q", data, phoff + 0x10)[0]
        if p_offset and p_vaddr != p_offset:
            struct.pack_into("<Q", data, phoff + 0x10, p_offset)
            struct.pack_into("<Q", data, phoff + 0x18, p_offset)
            patched += 1
            print(
                f"patched PT_DYNAMIC phdr #{index}: "
                f"vaddr/paddr 0x{p_vaddr:x} -> 0x{p_offset:x}"
            )

    if patched == 0:
        print("PT_DYNAMIC already has matching offset/vaddr; no patch needed")
    path.write_bytes(data)


if __name__ == "__main__":
    main()
