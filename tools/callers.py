#!/usr/bin/env python3
"""Which rel32 call/jmp targets an RVA?

`disasm.py --xref` finds RIP-relative *data* references, which is the right
question for a cached dvar pointer and the wrong one for a function: nothing
refers to a function by address in memory, it is reached with `call rel32`. So a
getter or a handler that is registered in a table, or called from code, looks
completely uncalled from --xref alone.

This was written to answer one question that came out of a list of names to
check: `laserForceOn` is registered in iw4sp.exe and has a getter, so "is it
read?" needed the getter's callers, and there were none. It is also how a command
handler registered through a table is told apart from one nothing reaches.

    python tools/callers.py "<exe>" <target-rva-in-hex>

Scans .text for E8/E9 with a rel32 that lands exactly on the target. The scan is
byte-level, so it also sees 0xE8 bytes that are operands of other instructions -
but a coincidence has to encode an offset landing on the target, which at four
bytes of range is not worth filtering.
"""
import struct
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1

    path, target = sys.argv[1], int(sys.argv[2], 16)
    with open(path, "rb") as handle:
        data = handle.read()

    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections_count = struct.unpack_from("<H", data, pe + 6)[0]
    options_size = struct.unpack_from("<H", data, pe + 20)[0]
    options = pe + 24

    hits = []
    for index in range(sections_count):
        entry = options + options_size + index * 40
        name = data[entry:entry + 8].rstrip(b"\0").decode("latin1")
        if not name.startswith(".text"):
            continue
        virtual, _, raw_size, raw = struct.unpack_from("<IIII", data, entry + 12)
        for offset in range(raw, raw + raw_size - 5):
            opcode = data[offset]
            if opcode not in (0xE8, 0xE9):
                continue
            rel = struct.unpack_from("<i", data, offset + 1)[0]
            here = virtual + (offset - raw)
            if here + 5 + rel == target:
                hits.append((opcode, here))

    print("%d reference(s) to rva 0x%X" % (len(hits), target))
    for opcode, here in hits:
        print("  %s at rva 0x%X" % ("call" if opcode == 0xE8 else "jmp", here))
    return 0


if __name__ == "__main__":
    sys.exit(main())
