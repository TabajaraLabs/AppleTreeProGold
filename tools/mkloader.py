#!/usr/bin/env python3
"""Builds the AppleTree serial loader (tools/loader.bin, for $0300) without cc65.

Same protocol as carga_v1 of the CFFA project (proven on this IIe + Super Serial in slot 1):
the Apple sends ACK ($06) before each 256-byte block, so the PC never sends while the Apple
is busy (the SSC has no flow control and drops bytes otherwise).

  $0300  JMP start
  $0303  "ATL"        signature: AppleTree turns on its serial test mode when it sees it
  $0306  PAGE         first page to load            (monitor: 306:PP NN  then 300G)
  $0307  NBLK         number of 256-byte blocks
After each block a '.' is stored on the bottom text line ($07D0, 40 columns).
At the end it sends "S=xxxx<CR>" (16-bit sum, plain ASCII) straight to the SSC -- not through
COUT: the load overwrites BASIC.SYSTEM ($9A00+), which owns the PR#1 hook -- and waits for:
  'J'  -> reset the ProDOS memory bitmap (as for a SYS launch) and JMP to PAGE*256
  'L'  -> load again (same PAGE/NBLK)
"""
import os, sys

SD, SS = 0xC098, 0xC099
P, SUM, DOTX = 0xFA, 0xFC, 0xFE
ORG = 0x0300

OPS = {  # (mnemonic, mode) -> opcode
 ('lda','imm'):0xA9, ('lda','abs'):0xAD, ('lda','zp'):0xA5, ('sta','abs'):0x8D, ('sta','zp'):0x85,
 ('sta','izy'):0x91, ('sta','absy'):0x99, ('sta','absx'):0x9D, ('ldx','imm'):0xA2, ('ldx','abs'):0xAE,
 ('ldy','imm'):0xA0, ('ldy','zp'):0xA4, ('sty','zp'):0x84, ('cmp','imm'):0xC9, ('cpy','imm'):0xC0,
 ('and','imm'):0x29, ('adc','zp'):0x65, ('inc','zp'):0xE6, ('adc','imm'):0x69,
 ('beq','rel'):0xF0, ('bne','rel'):0xD0, ('bpl','rel'):0x10, ('bcc','rel'):0x90,
 ('jsr','abs'):0x20, ('jmp','abs'):0x4C, ('jmp','ind'):0x6C,
 ('rts',''):0x60, ('clc',''):0x18, ('cld',''):0xD8, ('iny',''):0xC8, ('dex',''):0xCA, ('pha',''):0x48, ('pla',''):0x68, ('lsr',''):0x4A,
}
SIZE = {'imm':2, 'zp':2, 'izy':2, 'rel':2, 'abs':3, 'absy':3, 'absx':3, 'ind':3, '':1}

src = [
 ('jmp','abs','start'),
 ('db', b'ATL'),
 'PAGE:', ('db', b'\x20'),
 'NBLK:', ('db', b'\x88'),
 'start:', ('cld',),
  ('lda','abs','PAGE'), ('sta','zp',P+1), ('lda','imm',0), ('sta','zp',P),
  ('sta','zp',SUM), ('sta','zp',SUM+1), ('sta','zp',DOTX),
  ('ldx','abs','NBLK'),
 'blk:', ('lda','abs',SS), ('and','imm',0x10), ('beq','rel','blk'),    # transmitter empty?
  ('lda','imm',0x06), ('sta','abs',SD),                                # ACK: send the block
  ('ldy','imm',0),
 'rcv:', ('lda','abs',SS), ('and','imm',0x08), ('beq','rel','rcv'),    # byte received?
  ('lda','abs',SD), ('sta','izy',P),
  ('clc',), ('adc','zp',SUM), ('sta','zp',SUM), ('bcc','rel','nc'), ('inc','zp',SUM+1),
 'nc:', ('iny',), ('bne','rel','rcv'),
  ('ldy','zp',DOTX), ('lda','imm',0xAE), ('sta','absy',0x07D0),        # '.' on the bottom line
  ('iny',), ('cpy','imm',40), ('bne','rel','dk'), ('ldy','imm',0),
 'dk:', ('sty','zp',DOTX),
  ('inc','zp',P+1), ('dex',), ('bne','rel','blk'),
  ('lda','imm',ord('S')), ('jsr','abs','putb'), ('lda','imm',ord('=')), ('jsr','abs','putb'),
  ('lda','zp',SUM+1), ('jsr','abs','hex'), ('lda','zp',SUM), ('jsr','abs','hex'),
  ('lda','imm',0x0D), ('jsr','abs','putb'),
 'cmd:', ('lda','abs',SS), ('and','imm',0x08), ('beq','rel','cmd'),
  ('lda','abs',SD), ('cmp','imm',ord('J')), ('beq','rel','go'),
  ('cmp','imm',ord('L')), ('bne','rel','cmd'), ('jmp','abs','start'),
 'go:', ('ldx','imm',0x17), ('lda','imm',0),                           # reset ProDOS memory bitmap
 'bm:', ('sta','absx',0xBF58), ('dex',), ('bpl','rel','bm'),
  ('lda','imm',0xCF), ('sta','abs',0xBF58), ('lda','imm',0x01), ('sta','abs',0xBF6F),
  ('lda','abs','PAGE'), ('sta','zp',P+1), ('lda','imm',0), ('sta','zp',P),
  ('jmp','ind',P),
 'hex:', ('pha',), ('lsr',), ('lsr',), ('lsr',), ('lsr',), ('jsr','abs','nib'), ('pla',), ('and','imm',0x0F),
 'nib:', ('cmp','imm',10), ('bcc','rel','dg'), ('adc','imm',6),
 'dg:', ('adc','imm',ord('0')),
 'putb:', ('pha',),
 'pw:', ('lda','abs',SS), ('and','imm',0x10), ('beq','rel','pw'), ('pla',), ('sta','abs',SD), ('rts',),
]

def isize(it):
    if it[0] == 'db': return len(it[1])
    return SIZE[it[1] if len(it) > 1 else '']

def assemble():
    labels, pc = {}, ORG
    for it in src:
        if isinstance(it, str): labels[it[:-1]] = pc
        else: pc += isize(it)
    out, pc = bytearray(), ORG
    for it in src:
        if isinstance(it, str): continue
        if it[0] == 'db': out += it[1]; pc += len(it[1]); continue
        m = it[0]; mode = it[1] if len(it) > 1 else ''
        v = it[2] if len(it) > 2 else None
        if isinstance(v, str): v = labels[v]
        out.append(OPS[(m, mode)])
        if mode == 'rel':
            d = v - (pc + 2); assert -128 <= d <= 127, (it, d); out.append(d & 255)
        elif SIZE[mode] == 2: out.append(v & 255)
        elif SIZE[mode] == 3: out += bytes([v & 255, v >> 8])
        pc += SIZE[mode]
    return bytes(out), labels

if __name__ == '__main__':
    code, labels = assemble()
    assert code[3:6] == b'ATL' and labels['PAGE'] == 0x306 and labels['NBLK'] == 0x307
    assert ORG + len(code) <= 0x3D0          # $03D0+ = DOS/ProDOS vectors
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'loader.bin')
    open(out, 'wb').write(code)
    print('%s: %d bytes @$0300-$%04X' % (out, len(code), ORG + len(code) - 1))
