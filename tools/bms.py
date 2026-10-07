"""Which (bank, program) pairs, and which keys, a JAudio2 BMS sequence plays notes with, following its control flow.

Decoding follows Petari's JASSeqParser.cpp: parse() (note on < 0x80, note off 0x8x, register-arg prefix 0x9x),
parseCommand() (arg widths 2 bits each: byte, u16, u24, register byte), sCmdInfo (opcodes 0xA0-0xFF), and
JASSeqReader::readMidiValue (note gate time). Bank/program come from 0xE1 (u16: bank hi, prog lo), 0xE2 (bank),
0xE3 (prog) and register writes 0x27/0x28 (writeReg). Values read from registers are unknown statically: notes
played then are counted as "dynamic". Jump tables (0xC9/0xCA) have no length in the data: their targets are
counted as unexplored (`tables`). Two commands read more than their table entry says: cmdWait (0xF0) a MIDI varint
and cmdPrintf (0xFD) a string plus one byte per %-value (JASSeqParser.cpp cmdWait, cmdPrintf).
"""
import struct

# opcode -> (numArgs, argWidths) for 0xA0-0xFF, from JASSeqParser.cpp sCmdInfo (missing = no args)
CMD = {0xB1: (3, 0), 0xB2: (1, 0), 0xB3: (4, 0x40), 0xB4: (1, 0), 0xB8: (2, 0), 0xB9: (2, 4), 0xBA: (3, 0x10),
       0xBB: (3, 0x14), 0xC1: (2, 8), 0xC2: (1, 0), 0xC3: (1, 2), 0xC4: (2, 8), 0xC5: (0, 0), 0xC6: (1, 0),
       0xC7: (1, 2), 0xC8: (2, 8), 0xC9: (2, 0xB), 0xCA: (2, 0xB), 0xCB: (1, 1), 0xCC: (0, 0), 0xD0: (2, 0),
       0xD1: (2, 0xC), 0xD2: (1, 0), 0xD3: (1, 0), 0xD4: (2, 0xC), 0xD5: (2, 0xC), 0xD6: (2, 0), 0xD7: (2, 0),
       0xD8: (2, 4), 0xD9: (3, 0x30), 0xDA: (3, 0x10), 0xDB: (2, 0), 0xDC: (4, 0xE0), 0xE0: (1, 1), 0xE1: (1, 1),
       0xE2: (1, 0), 0xE3: (1, 0), 0xE7: (2, 4), 0xE8: (2, 8), 0xE9: (5, 0x155), 0xEA: (2, 4), 0xEB: (1, 0),
       0xEC: (4, 0x55), 0xED: (1, 2), 0xF0: (0, 0), 0xF1: (1, 0), 0xF3: (1, 2), 0xF4: (1, 1), 0xF5: (1, 1),
       0xF6: (0, 0), 0xF7: (0, 0), 0xF8: (2, 4), 0xF9: (1, 1), 0xFD: (0, 0), 0xFE: (0, 0), 0xFF: (0, 0)}
DYN = "dyn"   # value came from a register


def analyse(seq, start=0, bank=None, prog=None, max_steps=200000):
    """Truncated reads (a path running off the data) count as `bad` and end that path only."""
    """-> dict(pairs={(bank, prog): notes}, tables=n, bad=n). bank/prog may be None (never set) or DYN."""
    pairs, keys, tables, bad = {}, {}, 0, 0
    seen = set()
    todo = [(start, bank, prog, ())]           # pc, bank, prog, call stack
    steps = 0
    while todo and steps < max_steps:
        pc, bk, pg, stack = todo.pop()
        try:
            while 0 <= pc < len(seq) and steps < max_steps:
                if (pc, bk, pg, stack[-1:] ) in seen:
                    break
                seen.add((pc, bk, pg, stack[-1:]))
                steps += 1
                op = seq[pc]; pc += 1
                if op < 0x80:                                           # note on: ctrl, velocity[, gate time]
                    ctrl = seq[pc]; pc += 2
                    if ctrl & 7 == 0:
                        while pc < len(seq) and seq[pc] & 0x80:
                            pc += 1
                        pc += 1
                    pairs[(bk, pg)] = pairs.get((bk, pg), 0) + 1
                    keys.setdefault((bk, pg), set()).add(op)        # note pitch = key (transpose ignored)
                    continue
                if op & 0xF0 == 0x80:                                   # note off
                    continue
                widths = 0
                if op & 0xF0 == 0x90:                                   # register-arg prefix
                    n, flags = (op & 7) + 1, seq[pc]
                    pc += 1
                    for i in range(n):
                        if flags & (0x80 >> i):
                            widths |= 3 << (2 * i)
                    op = seq[pc]; pc += 1
                if op < 0xA0:
                    bad += 1
                    break
                if op == 0xB0:                                          # extended: one index byte, no known args
                    pc += 1
                    continue
                n, w = CMD.get(op, (0, 0))
                w |= widths
                args = []
                for _ in range(n):
                    t = w & 3
                    if t == 3:
                        args.append(DYN); pc += 1
                    else:
                        size = t + 1
                        args.append(int.from_bytes(seq[pc:pc + size], "big")); pc += size
                    w >>= 2
                if op == 0xF0:                                      # cmdWait reads a MIDI varint itself
                    while seq[pc] & 0x80:
                        pc += 1
                    pc += 1
                elif op == 0xFD:                                    # cmdPrintf: string (\ escapes, %x specs), then one byte per value
                    nvals = 0
                    while True:
                        ch = seq[pc]; pc += 1
                        if ch == 0:
                            break
                        if ch in (0x5C, 0x25):
                            nxt = seq[pc]; pc += 1
                            if nxt == 0:
                                break
                            nvals += ch == 0x25
                    pc += nvals
                a0 = args[0] if args else None
                if op in (0xB1, 0xB3):                              # cmdNoteOn (key = args[0]) / cmdNote (args[1])
                    k = args[0] if op == 0xB1 else args[1]
                    pairs[(bk, pg)] = pairs.get((bk, pg), 0) + 1
                    keys.setdefault((bk, pg), set()).add(k if k == DYN else k & 0x7F)
                elif op == 0xE1:
                    bk, pg = (DYN, DYN) if a0 == DYN else (a0 >> 8 & 0xFF, a0 & 0xFF)
                elif op == 0xE2:
                    bk = a0 if a0 == DYN else a0 & 0xFF
                elif op == 0xE3:
                    pg = a0 if a0 == DYN else a0 & 0xFF
                elif op == 0xD8 and a0 in (0x27, 0x28):                 # cmdRegLoad -> writeReg
                    v = args[1] if args[1] == DYN else args[1] & 0xFF
                    bk, pg = (v, pg) if a0 == 0x27 else (bk, v)
                elif op in (0xD9, 0xDA) and args[1] in (0x27, 0x28):    # cmdReg arithmetic on bank/prog
                    bk, pg = (DYN, pg) if args[1] == 0x27 else (bk, DYN)
                elif op == 0xC1 and args[1] != DYN:                     # open child track at offset
                    todo.append((args[1], bk, pg, ()))
                elif op == 0xC3 and a0 != DYN:                          # call
                    todo.append((pc, bk, pg, stack))
                    stack, pc = stack + (pc,), a0
                elif op == 0xC4 and args[1] != DYN:                     # conditional call: both paths
                    todo.append((args[1], bk, pg, stack + (pc,)))
                elif op in (0xC5, 0xC6):                                # ret (conditional: also fall through)
                    if op == 0xC6:
                        todo.append((pc, bk, pg, stack))
                    break
                elif op == 0xC7:                                        # jump
                    if a0 == DYN:
                        break
                    pc = a0
                elif op == 0xC8 and args[1] != DYN:                     # conditional jump: both paths
                    todo.append((args[1], bk, pg, stack))
                elif op in (0xC9, 0xCA):
                    tables += 1
                    if op == 0xC9:
                        break
                elif op == 0xFF:                                        # finish
                    break
        except IndexError:
            bad += 1
    return dict(pairs=pairs, keys=keys, tables=tables, bad=bad)


def selftest():
    # main: bank 3 prog 5 (E1 0305), note, open child at 0x10 (C1 01 000010), jump to 0x0C -> finish
    s = bytearray(0x20)
    s[0:3] = b"\xE1\x03\x05"
    s[3:6] = bytes([60, 0x01, 100])                  # note with voice id 1 (no gate time)
    s[6:11] = b"\xC1\x01\x00\x00\x10"
    s[11:12] = b"\xC7"; s[12:15] = b"\x00\x00\x0F"   # jmp 0x0F
    s[15] = 0xFF
    s[0x10:0x12] = b"\xE2\x07"                       # child: bank 7, prog stays 5
    s[0x12:0x16] = bytes([62, 0x00, 90, 0x10])       # note with gate time 0x10
    s[0x16] = 0x98; s[0x17] = 0x80; s[0x18] = 0xE2; s[0x19] = 0x05   # E2 with register arg -> dynamic bank
    s[0x1A:0x1D] = bytes([64, 0x01, 80])
    s[0x1D] = 0xFF
    r = analyse(bytes(s))
    w = analyse(bytes([0xF0, 0x01, 0xE1, 0x40, 0x0A, 60, 0x01, 100, 0xFF]))   # wait 1 is not a note
    assert w["pairs"] == {(64, 10): 1}, w
    assert r["pairs"] == {(3, 5): 1, (7, 5): 1, (DYN, 5): 1} and r["bad"] == 0, r
    assert r["keys"] == {(3, 5): {60}, (7, 5): {62}, (DYN, 5): {64}}, r
    print("selftest ok")


if __name__ == "__main__":
    selftest()
