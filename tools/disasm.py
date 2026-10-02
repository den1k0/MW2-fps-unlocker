#!/usr/bin/env python3
"""Disassemble part of a PE, with every operand resolved to something readable.

The other tools read bytes:

    dumpatrva.ps1   the bytes at an RVA
    xref.ps1        which code refers to an address

This one reads instructions, which is what the chasing actually needs. Finding
a value in a stripped 64-bit binary is almost entirely a matter of following
operands: a `lea` names a table, a `mov` out of a table names an object, a call
takes a function pointer. Doing that by hand means decoding ModRM and
RIP-relative displacements in your head, and that is exactly the situation in
which a wrong answer looks perfectly convincing.

So for every instruction it prints what the operands point at:

  * a RIP-relative operand that lands in .rdata and begins with printable
    characters is printed as the string it is
  * a RIP-relative operand whose target holds a pointer into an executable
    section is printed as that function (sub_<rva>) - this is what turns a
    table of data pointers into a table of function names
  * a direct call or jump is printed as sub_<rva>

Modes:

    <rva>                disassemble instructions from <rva>
    --xref <va>          every RIP-relative reference to a VA, with context
    --qwords <rva>       print the qwords at an RVA and resolve each one
    --bytes <rva>        print raw bytes (dumpatrva.ps1, in Python)

Requires capstone:  python -m pip install capstone

Usage:

    python tools/disasm.py iw4mp.exe 0xF0FE0 --count 60
    python tools/disasm.py iw4mp.exe --xref 0x1403781B0 --count 12
    python tools/disasm.py iw4mp.exe --qwords 0x3779E8 --count 16
"""

import argparse
import re
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_IMM, CS_OP_MEM, CS_OP_REG
    # The register constants are per-architecture and live in their own module;
    # only the generic ones (the operand kinds, the modes) are on the package.
    from capstone.x86 import X86_REG_RIP
except ImportError:  # pragma: no cover - dependency advice beats a traceback
    sys.exit("capstone is required:  python -m pip install capstone")


IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_MEM_EXECUTE = 0x20000000


class Pe:
    """A file-backed PE, addressed by RVA.

    Sections carry a raw size that can be much smaller than the virtual size
    (.data in this game is 0x0905BC14 bytes of address space backed by 0xE800
    bytes of file). Anything past the raw size is therefore zero at load time,
    not absent, and the reader here says so rather than pretending the address
    does not exist.
    """

    def __init__(self, path):
        with open(path, "rb") as handle:
            self.data = handle.read()
        b = self.data
        pe = struct.unpack_from("<I", b, 0x3C)[0]
        count = struct.unpack_from("<H", b, pe + 6)[0]
        opt_size = struct.unpack_from("<H", b, pe + 20)[0]
        opt = pe + 24
        self.image_base = struct.unpack_from("<Q", b, opt + 24)[0]
        self.sections = []
        for i in range(count):
            s = opt + opt_size + i * 40
            name = b[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, rsize, raw = struct.unpack_from("<IIII", b, s + 8)
            chars = struct.unpack_from("<I", b, s + 36)[0]
            self.sections.append(
                {"name": name, "vsize": vsize, "rva": va, "rsize": rsize,
                 "raw": raw, "chars": chars}
            )

    def section_of_rva(self, rva):
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + max(s["vsize"], s["rsize"]):
                return s
        return None

    def rva_of_offset(self, offset):
        for s in self.sections:
            if s["raw"] <= offset < s["raw"] + s["rsize"]:
                return s["rva"] + (offset - s["raw"])
        return None

    def offset_of_rva(self, rva):
        s = self.section_of_rva(rva)
        if s is None:
            return None
        delta = rva - s["rva"]
        if delta >= s["rsize"]:
            return None
        return s["raw"] + delta

    def read_rva(self, rva, size):
        """Bytes at an RVA, or None if the address is outside the image.

        Past a section's raw size the bytes are described as zero-filled, which
        is what the loader does, so a zero result is not mistaken for a missing
        section.
        """
        s = self.section_of_rva(rva)
        if s is None:
            return None
        delta = rva - s["rva"]
        if delta >= s["rsize"]:
            return b"\0" * size
        available = min(size, s["rsize"] - delta)
        chunk = self.data[s["raw"] + delta:s["raw"] + delta + available]
        return chunk + b"\0" * (size - available)

    def read_va(self, va, size):
        return self.read_rva(va - self.image_base, size)

    def is_code(self, va):
        s = self.section_of_rva(va - self.image_base)
        return bool(s and s["chars"] & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE))


def printable_run(data):
    """The leading printable ASCII run, as a str (empty if shorter than 4)."""
    out = []
    for byte in data:
        if 32 <= byte < 127:
            out.append(chr(byte))
        else:
            break
    if len(out) < 4:
        return ""
    return "".join(out)


def annotate(pe, va, data=None):
    """One line describing what lives at a VA, for use as a comment."""
    if pe.section_of_rva(va - pe.image_base) is None:
        return ""
    raw = pe.read_va(va, 96)
    if raw is None:
        return ""
    parts = ["va 0x%X (rva 0x%X)" % (va, va - pe.image_base)]

    text = printable_run(raw)
    if text:
        parts.append('"%s"' % text.replace("\n", "\\n")[:60])
    else:
        parts.append(" ".join("%02X" % x for x in raw[:8]))

    # A pointer in a table is how the game stores most of its named functions.
    if len(raw) >= 8:
        qword = struct.unpack_from("<Q", raw, 0)[0]
        if pe.is_code(qword):
            parts.append("-> sub_%X" % (qword - pe.image_base))
        elif pe.section_of_rva(qword - pe.image_base) is not None:
            inner = pe.read_va(qword, 32)
            label = printable_run(inner) if inner else ""
            if label:
                parts.append('-> "%s"' % label[:40])
    return "  ; ".join(parts)


def render(pe, insn):
    """The instruction, its bytes, and one comment line per meaningful operand."""
    code = pe.read_va(insn.address, insn.size)
    hexes = " ".join("%02X" % x for x in (code or b""))
    head = "  %X  %-24s %s %s" % (
        insn.address - pe.image_base, hexes, insn.mnemonic, insn.op_str)

    notes = []
    for op in insn.operands:
        if op.type == CS_OP_MEM and op.mem.base == X86_REG_RIP:
            # RIP is the address of the *next* instruction, which is why the
            # instruction length never has to be known to resolve a reference.
            target = insn.address + insn.size + op.mem.disp
            note = annotate(pe, target)
            if note:
                notes.append("    -> [%s]" % note)
        elif op.type == CS_OP_IMM and op.size == 8:
            if pe.section_of_rva(op.imm - pe.image_base) is not None:
                note = annotate(pe, op.imm)
                if note:
                    notes.append("    -> 0x%X: [%s]" % (op.imm, note))

    # A direct call names the function far better than a bare address does.
    if insn.mnemonic in ("call", "jmp") and insn.operands and \
            insn.operands[0].type == CS_OP_IMM and \
            pe.is_code(insn.operands[0].imm):
        notes.append("    -> sub_%X" % (insn.operands[0].imm - pe.image_base))

    return "\n".join([head] + notes)


def disassemble(pe, start_rva, count, length):
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    start_va = pe.image_base + start_rva
    window = pe.read_rva(start_rva, max(length or 0, count * 16 + 32))
    if window is None:
        sys.exit("rva 0x%X is not inside the image" % start_rva)

    lines = []
    offset = 0
    while offset < len(window) and len(lines) < count:
        if length and offset >= length:
            break
        found = list(md.disasm(window[offset:offset + 16], start_va + offset, count=1))
        if not found:
            lines.append("  %X  %02X                    db" % (start_rva + offset, window[offset]))
            offset += 1
            continue
        insn = found[0]
        lines.append(render(pe, insn))
        offset += insn.size
    return "\n".join(lines)


def function_range(pe, rva):
    """The [begin, end) of the function containing an RVA, from .pdata.

    A stripped binary still has .pdata: one 12-byte RUNTIME_FUNCTION per
    function, holding its begin RVA, its end RVA, and where to find the unwind
    info. For working out what a stray instruction belongs to, that is much
    better than guessing where the previous `ret` was, because it is exact.
    """
    section = next((s for s in pe.sections if s["name"] == ".pdata"), None)
    if section is None:
        return None
    count = section["rsize"] // 12
    best = None
    for i in range(count):
        begin, end, _ = struct.unpack_from("<III", pe.data, section["raw"] + i * 12)
        if begin <= rva < end and (best is None or end - begin < best[1] - best[0]):
            best = (begin, end)
    return best


def print_function(pe, rva, limit):
    """Disassemble the whole function that contains an RVA."""
    span = function_range(pe, rva)
    if span is None:
        print("no .pdata entry covers rva 0x%X (a leaf function may have none)" % rva)
        return
    begin, end = span
    print("function sub_%X .. sub_%X (%d bytes), containing rva 0x%X"
          % (begin, end, end - begin, rva))
    print(disassemble(pe, begin, limit, end - begin))


def print_qwords(pe, start_rva, count, stride=8):
    """A table of pointers, one per line, each resolved to what it points at.

    The stride is worth being able to set: the profile field table is a 0x18-byte
    entry (four ints and a name pointer), so its name pointers are 0x18 apart,
    not 8.
    """
    for i in range(count):
        rva = start_rva + i * stride
        raw = pe.read_rva(rva, 8)
        if raw is None:
            print("0x%X  <outside the image>" % rva)
            continue
        qword = struct.unpack_from("<Q", raw, 0)[0]
        note = annotate(pe, qword) if qword else ""
        print("0x%08X  %016X  %s" % (rva, qword, note))


def print_bytes(pe, start_rva, count):
    raw = pe.read_rva(start_rva, count)
    if raw is None:
        sys.exit("rva 0x%X is not inside the image" % start_rva)
    print("rva 0x%X, %d bytes" % (start_rva, count))
    print("  ascii: %s" % "".join(chr(x) if 32 <= x < 127 else "." for x in raw))
    print("  bytes: %s" % " ".join("%02X" % x for x in raw))


def instruction_at_disp(pe, md, p):
    """The instruction whose disp32 sits at file offset p, or None.

    Scanning for a disp32 that resolves to the target finds the reference, but
    the offset of the displacement is not the offset of the instruction - the
    opcode and the ModRM byte come first, and their length varies (a 7-byte
    `lea`, an 8-byte `movss`, a 6-byte `mov`). Guessing how far back to start
    therefore prints a window that begins mid-instruction, and instructions
    read from the middle of another instruction look perfectly plausible while
    being fiction.

    So instead of guessing, try each possible start and keep the one where the
    instruction ends exactly where the displacement ends. A coincidence in raw
    bytes has to satisfy that as well.
    """
    end_va = pe.image_base + pe.rva_of_offset(p + 4)
    for start_p in range(p - 7, p - 1):
        if start_p < 0:
            continue
        start_rva = pe.rva_of_offset(start_p)
        if start_rva is None:
            continue
        code = pe.read_rva(start_rva, 16)
        if code is None:
            continue
        found = list(md.disasm(code, pe.image_base + start_rva, count=1))
        if found and found[0].address + found[0].size == end_va:
            return start_rva
    return None


def instructions(pe, start_rva, count):
    """Yield up to count decoded instructions from an RVA."""
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    window = pe.read_rva(start_rva, count * 16 + 32)
    if window is None:
        return
    offset = 0
    emitted = 0
    while offset < len(window) and emitted < count:
        found = list(md.disasm(window[offset:offset + 16],
                              pe.image_base + start_rva + offset, count=1))
        if not found:
            offset += 1
            continue
        yield found[0]
        offset += found[0].size
        emitted += 1


def references(pe, md, target_va):
    """The instruction start RVAs of every RIP-relative reference to a VA.

    Scanning for a disp32 that resolves to the target finds the reference; each
    candidate then has to be confirmed as a real instruction boundary, which is
    what throws out displacement-shaped coincidences.
    """
    b = pe.data
    starts = []
    skipped = 0
    for p in range(1, len(b) - 4):
        if (b[p - 1] & 0xC7) != 0x05:
            continue
        next_rva = pe.rva_of_offset(p + 4)
        if next_rva is None:
            continue
        disp = struct.unpack_from("<i", b, p)[0]
        if pe.image_base + next_rva + disp != target_va:
            continue
        start_rva = instruction_at_disp(pe, md, p)
        if start_rva is None:
            skipped += 1
            continue
        if start_rva not in starts:
            starts.append(start_rva)
    return starts, skipped


def find_name_string(pe, name):
    """The RVA of `name` where it begins a string, not where it sits inside one.

    A cvar whose name is the tail of another string matches inside it -
    "sensitivity" is a suffix of the description "Mouse sensitivity" - and the
    address of that interior position is referenced by nothing, which reads as
    "registered but never used". Requiring the preceding byte not to be
    printable rules that out.
    """
    needle = name.encode("ascii") + b"\0"
    index = 0
    while True:
        index = pe.data.find(needle, index)
        if index < 0:
            return None
        if index == 0 or not 32 <= pe.data[index - 1] < 127:
            rva = pe.rva_of_offset(index)
            if rva is not None:
                return rva
        index += 1


def cached_dvar_pointer(pe, site_rva):
    """The static a registration site caches the returned dvar_t* in.

    A registration is `lea rcx, [name]; call Dvar_Register...; mov [static],
    rax`, so the store to look for is the first one *after the call*. Finding it
    is what makes the rest possible: from then on that one dvar can be followed
    around the binary by address alone, without knowing anything about the dvar
    pool it lives in.

    The call is not a detail. The integer registrar takes flags in r8d and calls
    almost immediately, but the float one takes its default, minimum and maximum
    in xmm1-xmm3 and takes several instructions to set up - and the previous
    cvar's store lands in the middle of that. Searching from the name alone then
    reports the neighbouring dvar, which is how a whole family of glow tweaks
    appeared to share one address.
    """
    after_call = None
    for insn in instructions(pe, site_rva, 14):
        if insn.mnemonic == "call":
            after_call = insn.address + insn.size
            break
    if after_call is None:
        return None

    for insn in instructions(pe, after_call - pe.image_base, 8):
        if insn.mnemonic != "mov" or len(insn.operands) != 2:
            continue
        dest, source = insn.operands
        if dest.type != CS_OP_MEM or dest.mem.base != X86_REG_RIP:
            continue
        if source.type != CS_OP_REG or source.size != 8:
            continue
        if insn.reg_name(source.reg) not in ("rax", "rbx", "rcx", "rdx"):
            continue
        return insn.address + insn.size + dest.mem.disp
    return None


def dvar_report(pe, name):
    """The whole deadcvar.ps1 chain in one run: name -> cache -> readers.

    A cvar can be registered - so it appears in cvar lists and gets written to
    your config - while nothing ever reads it. From the outside that is
    invisible and looks exactly like a broken feature. Counting the references
    to the cached pointer settles it: the store is the registration, so
    anything beyond it is a reader.
    """
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    print("dvar: %s" % name)

    name_rva = find_name_string(pe, name)
    if name_rva is None:
        print("  the name string does not exist as a whole string in this file")
        return
    print("  name string at rva 0x%X" % name_rva)

    # A pointer to the name is not on its own a dvar. Anything that locates a
    # cvar by scanning for one - which is what the unlocker does at run time, and
    # what the runtime addresses of the pool make necessary - can land on a
    # different structure that also begins with that pointer. This build keeps
    # such a table for r_glow: {&name, 0, 0, {&string, index, flag}...}, sitting
    # at a lower address than the real dvar, so a first-match scan finds it and a
    # write through it clobbers a pointer. Listing every image pointer to the
    # name makes that trap visible before anything is written to it.
    needle = struct.pack("<Q", pe.image_base + name_rva)
    offset = 0
    while True:
        offset = pe.data.find(needle, offset)
        if offset < 0:
            break
        rva = pe.rva_of_offset(offset)
        if rva is not None and not pe.is_code(pe.image_base + rva):
            print("  ! image pointer to the name at rva 0x%X - a structure, not the dvar" % rva)
        offset += 1

    sites, _ = references(pe, md, pe.image_base + name_rva)
    print("  %d reference(s) to the name string" % len(sites))

    statics = []
    for site in sites:
        static_va = cached_dvar_pointer(pe, site)
        if static_va is None:
            print("    registration at rva 0x%X: no cached pointer seen" % site)
            continue
        static_rva = static_va - pe.image_base
        print("    registration at rva 0x%X caches the dvar at rva 0x%X"
              % (site, static_rva))
        if static_rva not in statics:
            statics.append(static_rva)

    for static_rva in statics:
        refs, _ = references(pe, md, pe.image_base + static_rva)
        readers = max(len(refs) - 1, 0)   # the caching store is not a reader
        print("  0x%X: %d reader(s) of the cached pointer" % (static_rva, readers))
        for r in refs:
            print("    rva 0x%X" % r)
        if readers <= 0:
            print("    VERDICT: registered but never read - setting it does nothing")
        else:
            print("    VERDICT: read by code, so setting it has an effect")


def print_static(pe, rva):
    """What dvar is cached in a static, and what reads it.

    The other direction from --dvar: given a `mov [rip + d], rax` a
    registration left behind, walk back through the enclosing function for the
    `lea rcx, [name]` that fed the call. A dvar registration is always
    `lea rcx, [name]; lea rax, [description]; ...args...; call register; mov
    [static], rax`, so the names seen on the way in are that dvar's - the last
    one is the name, the one before the description.
    """
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    sites, _ = references(pe, md, pe.image_base + rva)

    for site in sites:
        print("reference at rva 0x%X" % site)
        span = function_range(pe, site)
        if span is None:
            print("  (no .pdata entry; cannot walk back reliably)")
            continue
        # From the function start, not from an arbitrary offset: disassembling
        # from the middle of an instruction leaves the decoder out of step, and
        # the strings it then reports would be fiction.
        before = [insn for insn in instructions(pe, span[0], 2000)
                  if insn.address <= pe.image_base + site]
        for insn in before[-45:]:
            for op in insn.operands:
                if op.type != CS_OP_MEM or op.mem.base != X86_REG_RIP:
                    continue
                target = insn.address + insn.size + op.mem.disp
                text = printable_run(pe.read_va(target, 96) or b"")
                if text:
                    print("    %X  %s %s   ; \"%s\""
                          % (insn.address - pe.image_base, insn.mnemonic,
                             insn.op_str, text[:60]))

    print("  %d reference(s) in total, so %d reader(s) besides the store"
          % (len(sites), max(len(sites) - 1, 0)))


def print_grep(pe, text, limit):
    """Every string in the file containing some text, by fragment.

    Cvar names are guessed, not known: writing `snd_musicVolume` from memory gets
    "the name string does not exist in this file" and no idea what the real
    spelling is. Searching for a fragment inside whole strings answers it - and
    only whole strings, because a fragment of a description ("...music volume")
    reads as a cvar and is not one. The preceding byte is printed for that
    reason: a printable one means the match is inside a longer string.
    """
    needle = text.lower().encode("ascii")
    found = []
    index = 0
    while index < len(pe.data):
        byte = pe.data[index]
        if not 32 <= byte < 127:
            index += 1
            continue
        start = index
        while index < len(pe.data) and 32 <= pe.data[index] < 127:
            index += 1
        run = pe.data[start:index]
        if len(run) >= 4 and needle in run.lower():
            found.append((start, run.decode("ascii", "replace")))
        # index now sits on the terminator, so the loop continues past it

    for offset, run in found[:limit]:
        rva = pe.rva_of_offset(offset)
        previous = pe.data[offset - 1] if offset > 0 else 0
        inside = " (inside a longer string)" if 32 <= previous < 127 else ""
        print("  rva 0x%-8X %s%s" % (rva, run, inside))
    print("  %d string(s) contain '%s'" % (len(found), text))


def print_candidates(pe, name):
    """Every whole-string occurrence of a name, ranked the way the DLL ranks them.

    This mirrors pattern::FindCandidates, which is the code that decides which
    string a cvar lookup keeps. That ranking is not a detail: searching
    case-insensitively for "sensitivity" in this binary finds a profile field
    label "Sensitivity" as well as the cvar, and a lookup that stops at the first
    match finds nothing pointing at the label and reports the cvar as missing.
    """
    encoded = name.encode("ascii")
    matches = [m.start() for m in re.finditer(re.escape(encoded) + rb"\0", pe.data,
                                              re.IGNORECASE)]

    buckets = {3: [], 2: [], 1: [], 0: []}
    for index in matches:
        same_case = pe.data[index:index + len(encoded)] == encoded
        previous = pe.data[index - 1] if index > 0 else 0
        begins_string = index == 0 or not 32 <= previous < 127
        buckets[(2 if same_case else 0) + (1 if begins_string else 0)].append(index)

    print("candidates for '%s', in the order a lookup tries them:" % name)
    rank = 0
    for score in (3, 2, 1, 0):
        for index in buckets[score]:
            rank += 1
            rva = pe.rva_of_offset(index)
            note = {3: "exact case, starts a string (this is what a cvar name looks like)",
                    2: "exact case, but inside a longer string",
                    1: "different case, starts a string",
                    0: "different case, inside a longer string"}[score]
            print("  %d. rva 0x%X  va 0x%X  %s"
                  % (rank, rva, pe.image_base + rva, note))
    print("  %d occurrence(s)" % len(matches))


def xref(pe, target_va, count):
    """Print every reference to a VA with a little context around each."""
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    starts, skipped = references(pe, md, target_va)
    for start_rva in starts:
        print("ref at rva 0x%X (va 0x%X)" % (start_rva, pe.image_base + start_rva))
        print(disassemble(pe, start_rva, count, None))
        print()
    print("%d reference(s) found (%d coincidence(s) discarded)" % (len(starts), skipped))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe", help="the PE to read (iw4mp.exe)")
    parser.add_argument("rva", nargs="?", help="address to start at, in hex")
    parser.add_argument("--count", type=int, default=None,
                        help="instructions (default 40), or qwords/bytes for the dump modes")
    parser.add_argument("--length", type=lambda v: int(v, 16), default=None,
                        help="disassemble this many bytes instead of --count instructions")
    parser.add_argument("--xref", help="VA to find references to")
    parser.add_argument("--dvar", help="cvar name: find its cache, then its readers")
    parser.add_argument("--candidates", help="name: rank every string a lookup would try")
    parser.add_argument("--grep", help="text: every string containing this fragment")
    parser.add_argument("--function", dest="function_at",
                        help="RVA inside a function: print the whole function")
    parser.add_argument("--static", dest="static_at",
                        help="RVA of a cached dvar pointer: find which dvar it is")
    parser.add_argument("--qwords", help="RVA to dump qwords at")
    parser.add_argument("--stride", type=lambda v: int(v, 16), default=8,
                        help="byte distance between qwords for --qwords (hex, default 8)")
    parser.add_argument("--bytes", dest="bytes_at", help="RVA to print raw bytes at")
    args = parser.parse_args()

    pe = Pe(args.exe)

    if args.static_at:
        print_static(pe, int(args.static_at, 16))
        return
    if args.function_at:
        print_function(pe, int(args.function_at, 16), args.count or 200)
        return
    if args.candidates:
        print_candidates(pe, args.candidates)
        return
    if args.grep:
        print_grep(pe, args.grep, args.count or 60)
        return
    if args.dvar:
        dvar_report(pe, args.dvar)
        return
    if args.xref:
        xref(pe, int(args.xref, 16), args.count or 12)
        return
    if args.qwords:
        print_qwords(pe, int(args.qwords, 16), args.count or 16, args.stride)
        return
    if args.bytes_at:
        print_bytes(pe, int(args.bytes_at, 16), args.count or 80)
        return
    if not args.rva:
        parser.error("give an rva, or one of --xref/--qwords/--bytes")

    print(disassemble(pe, int(args.rva, 16), args.count or 40, args.length))


if __name__ == "__main__":
    main()
