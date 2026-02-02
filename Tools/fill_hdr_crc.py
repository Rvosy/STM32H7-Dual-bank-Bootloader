#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import struct
from pathlib import Path

POLY = 0x04C11DB7
INIT = 0xFFFFFFFF

def crc32_stm32_words_ffpad(data: bytes) -> int:
    """
    Mimic STM32 CRC peripheral default (poly 0x04C11DB7, init 0xFFFFFFFF),
    feed as 32-bit WORDS, MSB-first per word.
    Tail bytes are padded with 0xFF to form the last word.
    No input/output reflection, no xorout.
    """
    crc = INIT
    n = len(data)
    i = 0

    # process 4-byte words
    while i + 4 <= n:
        # little-endian word as it appears in memory when cast to uint32_t*
        w = struct.unpack_from("<I", data, i)[0]
        crc = _crc_update_word_msbfirst(crc, w)
        i += 4

    # tail
    if i < n:
        tail = data[i:] + b"\xFF" * (4 - (n - i))
        w = struct.unpack("<I", tail)[0]
        crc = _crc_update_word_msbfirst(crc, w)

    return crc & 0xFFFFFFFF

def _crc_update_word_msbfirst(crc: int, word: int) -> int:
    for bit in range(32):
        msb_crc = (crc >> 31) & 1
        msb_dat = (word >> (31 - bit)) & 1
        crc = ((crc << 1) & 0xFFFFFFFF)
        if msb_crc ^ msb_dat:
            crc ^= POLY
    return crc

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("in_bin", help="input .bin (must include header at offset 0)")
    ap.add_argument("--out", default=None, help="output .bin (default: in-place overwrite)")
    ap.add_argument("--hdr-size", default="0x200", help="HDR_SIZE in hex/dec (default 0x200)")
    ap.add_argument("--img-size-off", default="20", help="img_size field offset in header (default 20)")
    ap.add_argument("--crc-off", default="24", help="crc32 field offset in header (default 24)")
    ap.add_argument("--pad", default="0xFF", help="padding byte for tail (default 0xFF)")
    args = ap.parse_args()

    hdr_size = int(args.hdr_size, 0)
    img_size_off = int(args.img_size_off, 0)
    crc_off = int(args.crc_off, 0)
    pad_byte = int(args.pad, 0) & 0xFF

    p = Path(args.in_bin)
    b = bytearray(p.read_bytes())

    if len(b) < hdr_size + 8:
        raise SystemExit(f"BIN too small: {len(b)} bytes, hdr_size={hdr_size}")

    # image region starts at vector table = HDR_SIZE
    img = bytes(b[hdr_size:])

    # compute image size (exclude header)
    img_size = len(img)

    # compute crc over image bytes, but padding tail with pad_byte before turning into last word
    # (we implement by temporarily padding bytes to multiple of 4)
    if img_size % 4 != 0:
        img_padded = img + bytes([pad_byte]) * (4 - (img_size % 4))
    else:
        img_padded = img

    crc = crc32_stm32_words_ffpad(img_padded)

    # write back img_size and crc32 as little-endian u32
    struct.pack_into("<I", b, img_size_off, img_size)
    struct.pack_into("<I", b, crc_off, crc)

    outp = Path(args.out) if args.out else p
    outp.write_bytes(b)

    print(f"[OK] img_size={img_size} crc32=0x{crc:08X} -> {outp}")

if __name__ == "__main__":
    main()
