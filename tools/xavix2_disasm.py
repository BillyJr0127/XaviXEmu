# SPDX-License-Identifier: BSD-3-Clause
# MAME-derived portions copyright-holders: Olivier Galibert, Nathan Gilbert
# XaviXEmu port and modifications:
# Copyright (c) 2026 Billy Jr. and contributors
"""Small standalone XaviX2 ROM disassembler for driver investigation.

The instruction descriptions follow MAME's xavix2d.cpp.  This tool only reads
the selected ROM or ZIP member; it does not extract or alter the source file.
"""

from __future__ import annotations

import argparse
import pathlib
import zipfile


REGISTERS = ("r0", "r1", "r2", "r3", "r4", "r5", "sp", "lnk")
BYTES_PER_OPCODE = (4, 3, 3, 2, 2, 2, 2, 1)


def signed(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def offset_text(value: int, bits: int) -> str:
    value = signed(value, bits)
    return "" if value == 0 else f" {'+' if value > 0 else '-'} {abs(value):x}"


def disassemble(data: bytes, offset: int, base: int) -> tuple[int, str, str]:
    first = data[offset]
    size = BYTES_PER_OPCODE[first >> 5]
    raw = data[offset : offset + size]
    if len(raw) != size:
        return len(raw), raw.hex(" ").upper(), "<truncated>"

    opcode = int.from_bytes(raw, "big") << (8 * (4 - size))
    op = opcode >> 24
    r1 = REGISTERS[(opcode >> 22) & 7]
    r2 = REGISTERS[(opcode >> 19) & 7]
    r3 = REGISTERS[(opcode >> 16) & 7]
    pc = base + offset
    text = f"?{op:02x}"

    if op <= 0x01:
        text = f"{r1} = {r2} + {signed(opcode & 0x7ffff, 19):x}"
    elif op <= 0x03:
        text = f"{r1} = {(opcode << 10) & 0xffffffff:08x}"
    elif op <= 0x05:
        text = f"{r1} = {r2} - {signed(opcode & 0x7ffff, 19):x}"
    elif op <= 0x07:
        text = f"{r1} = {signed(opcode & 0x7ffff, 19):x}"
    elif op == 0x08:
        text = f"jmp {base + (opcode & 0xffffff):08x}"
    elif op == 0x09:
        text = f"jsr {base + (opcode & 0xffffff):08x}"
    elif 0x0A <= op <= 0x0F:
        symbol = ("&", "|", "^")[(op - 0x0A) // 2]
        text = f"{r1} = {r2} {symbol} {opcode & 0x7ffff:x}"
    elif 0x10 <= op <= 0x1F:
        kinds = ("bs", "bu", "ws", "wu", "l", "b", "w", "l")
        kind = kinds[(op - 0x10) // 2]
        off = offset_text(opcode & 0x7ffff, 19)
        text = (f"{r1} = ({r2}{off}).{kind}" if op < 0x1A
                else f"({r2}{off}).{kind} = {r1}")
    elif 0x20 <= op <= 0x21:
        text = f"{r1} = {r2} + {signed((opcode >> 8) & 0x7ff, 11):x}"
    elif 0x22 <= op <= 0x23:
        text = f"{r1} = {(((opcode >> 8) & 0x3fff) << 18):08x}"
    elif 0x24 <= op <= 0x25:
        text = f"{r1} = {r2} - {signed((opcode >> 8) & 0x7ff, 11):x}"
    elif 0x26 <= op <= 0x27:
        text = f"cmp {r1}, {signed((opcode >> 8) & 0x3fff, 14):x}"
    elif op == 0x28:
        text = f"bra {pc + signed((opcode >> 8) & 0xffff, 16):08x}"
    elif op == 0x29:
        text = f"bsr {pc + signed((opcode >> 8) & 0xffff, 16):08x}"
    elif 0x2A <= op <= 0x2F:
        symbol = ("&", "|", "^")[(op - 0x2A) // 2]
        text = f"{r1} = {r2} {symbol} {(opcode >> 8) & 0x7ff:x}"
    elif 0x30 <= op <= 0x3F:
        kinds = ("bs", "bu", "ws", "wu", "l", "b", "w", "l")
        kind = kinds[(op - 0x30) // 2]
        off = offset_text((opcode >> 8) & 0x3fff, 14)
        text = (f"{r1} = (sp{off}).{kind}" if op < 0x3A
                else f"(sp{off}).{kind} = {r1}")
    elif 0x40 <= op <= 0x4F:
        kinds = ("bs", "bu", "ws", "wu", "l", "b", "w", "l")
        kind = kinds[(op - 0x40) // 2]
        off = offset_text((opcode >> 8) & 0x7ff, 11)
        text = (f"{r1} = ({r2}{off}).{kind}" if op < 0x4A
                else f"({r2}{off}).{kind} = {r1}")
    elif 0x50 <= op <= 0x5F:
        kinds = ("bs", "bu", "ws", "wu", "l", "b", "w", "l")
        kind = kinds[(op - 0x50) // 2]
        address = signed((opcode >> 8) & 0x3fff, 14) & 0xffffffff
        text = (f"{r1} = {address:08x}.{kind}" if op < 0x5A
                else f"{address:08x}.{kind} = {r1}")
    elif 0x60 <= op <= 0x67:
        value = signed((opcode >> 16) & 0x3f, 6)
        if op < 0x62:
            text = f"{r1} += {value:x}"
        elif op < 0x64:
            text = f"{r1} = {value:x}"
        elif op < 0x66:
            text = f"{r1} -= {value:x}"
        else:
            text = f"cmp {r1}, {value:x}"
    elif 0x6A <= op <= 0x6F:
        symbol = (">>s", ">>", "<<")[(op - 0x6A) // 2]
        text = f"{r1} = {r2} {symbol} {(opcode >> 16) & 7}"
    elif 0x70 <= op <= 0x7F:
        kinds = ("bs", "bu", "ws", "wu", "l", "b", "w", "l")
        kind = kinds[(op - 0x70) // 2]
        off = offset_text((opcode >> 16) & 0x3f, 6)
        text = (f"{r1} = (sp{off}).{kind}" if op < 0x7A
                else f"(sp{off}).{kind} = {r1}")
    elif 0x80 <= op <= 0x81:
        text = f"{r1} = {r2} + {r3}"
    elif 0x84 <= op <= 0x85:
        text = f"{r1} = {r2} - {r3}"
    elif op == 0x88:
        text = f"jmp ({r2})"
    elif 0x8A <= op <= 0x8F:
        symbol = ("&", "|", "^")[(op - 0x8A) // 2]
        text = f"{r1} = {r2} {symbol} {r3}"
    elif 0x90 <= op <= 0x9F:
        kinds = ("bs", "bu", "ws", "wu", "l", "b", "w", "l")
        kind = kinds[(op - 0x90) // 2]
        off = offset_text((opcode >> 16) & 7, 3)
        text = (f"{r1} = ({r2}{off}).{kind}" if op < 0x9A
                else f"({r2}{off}).{kind} = {r1}")
    elif 0xA0 <= op <= 0xA7:
        symbol = ("~", "=", "-", "cmp")[(op - 0xA0) // 2]
        text = (f"{r1} {symbol} {r2}" if symbol == "=" else
                f"{r1} = {symbol}{r2}" if symbol != "cmp"
                else f"cmp {r1}, {r2}")
    elif op == 0xA8:
        text = f"jsr ({r2})"
    elif 0xAA <= op <= 0xAF:
        symbol = (">>s", ">>", "<<")[(op - 0xAA) // 2]
        text = f"{r1} = {r2} {symbol} {r3}"
    elif 0xB0 <= op <= 0xB7:
        signedness = "s" if op & 2 else "u"
        pair = "01:00" if op & 4 else "00"
        text = f"hreg[{pair}] = {r1} *{signedness} {r2}"
    elif 0xBC <= op <= 0xBF:
        text = f"hreg[03],hreg[02] = {r1} /{'u' if op & 2 else 's'} {r2}"
    elif 0xC8 <= op <= 0xC9:
        text = f"{r1} = hreg[{(opcode >> 16) & 0x3f:02x}]"
    elif 0xCA <= op <= 0xCB:
        text = f"hreg[{(opcode >> 16) & 0x3f:02x}] = {r1}"
    elif 0xD0 <= op <= 0xDF:
        names = ("bvs", "bltu", "beq", "bleu", "bmi", "bra", "blts", "bles",
                 "bvc", "bgeu", "bne", "bgtu", "bpl", "bnv", "bges", "bgts")
        text = f"{names[op - 0xD0]} {pc + signed((opcode >> 16) & 0xff, 8):08x}"
    elif 0xE0 <= op <= 0xE3:
        text = ("jmp lnk", "rti1", "rti2", "rti3")[op - 0xE0]
    elif 0xF0 <= op <= 0xF9:
        text = ("clc", "stc", "clz", "stz", "cln", "stn",
                "clz_alias", "stz_alias", "di", "ei")[op - 0xF0]
    elif op == 0xFC:
        text = "nop"
    elif op == 0xFE:
        text = "wait"

    return size, raw.hex(" ").upper(), text


def read_image(path: pathlib.Path, member: str | None) -> bytes:
    if path.suffix.lower() != ".zip":
        return path.read_bytes()
    with zipfile.ZipFile(path) as archive:
        if member is None:
            members = [name for name in archive.namelist()
                       if name.lower().endswith((".bin", ".rom"))]
            if len(members) != 1:
                raise SystemExit("select a ZIP member with --member")
            member = members[0]
        return archive.read(member)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=pathlib.Path)
    parser.add_argument("start", type=lambda value: int(value, 0))
    parser.add_argument("length", type=lambda value: int(value, 0))
    parser.add_argument("--member")
    parser.add_argument("--base", type=lambda value: int(value, 0), default=0x40000000)
    args = parser.parse_args()

    data = read_image(args.image, args.member)
    offset = args.start
    end = min(len(data), offset + args.length)
    while offset < end:
        size, raw, text = disassemble(data, offset, args.base)
        print(f"{args.base + offset:08X}: {raw:<11} {text}")
        if size == 0:
            break
        offset += size


if __name__ == "__main__":
    main()
