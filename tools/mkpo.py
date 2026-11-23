#!/usr/bin/env python3
"""Build a ProDOS order (.po) disk image.  usage: mkpo.py out.po VOLNAME file:TYPE:AUX [...]
cc65 AppleSingle headers are stripped automatically.  No boot code / PRODOS is
included: copy PRODOS onto the image (e.g. with CiderPress) if it must boot."""
import sys, os, struct, time

def strip_applesingle(d):
    if d[:4] == b'\x00\x05\x16\x00':
        n = struct.unpack('>H', d[24:26])[0]
        for i in range(n):
            eid, off, ln = struct.unpack('>III', d[26 + i*12: 38 + i*12])
            if eid == 1: return d[off:off+ln]
    return d

def pdate(t):
    lt = time.localtime(t)
    return ((lt.tm_year % 100) << 9) | (lt.tm_mon << 5) | lt.tm_mday, (lt.tm_hour << 8) | lt.tm_min

def main():
    out, vol = sys.argv[1], sys.argv[2].upper()
    nblk = 280
    img = bytearray(512 * nblk)
    used = set(range(0, 7))
    nxt = [7]
    def alloc():
        b = nxt[0]; nxt[0] += 1; used.add(b); return b
    d, t = pdate(time.time())
    entries = []
    for spec in sys.argv[3:]:
        path, ftype, aux = spec.split(':')
        data = strip_applesingle(open(path, 'rb').read())
        name = os.path.basename(path).upper()[:15]
        nb = (len(data) + 511) // 512 or 1
        if nb == 1:
            key = alloc(); img[key*512:key*512+len(data)] = data; st, bu = 1, 1
        else:
            key = alloc(); st, bu = 2, nb + 1
            for i in range(nb):
                b = alloc()
                img[b*512:b*512+512] = data[i*512:(i+1)*512].ljust(512, b'\0')
                img[key*512+i] = b & 255; img[key*512+256+i] = b >> 8
        e = bytearray(39)
        e[0] = (st << 4) | len(name); e[1:1+len(name)] = name.encode()
        e[16] = int(ftype, 16); struct.pack_into('<HH', e, 17, key, bu)
        e[21:24] = len(data).to_bytes(3, 'little')
        struct.pack_into('<HH', e, 24, d, t); e[30] = 0xE3
        struct.pack_into('<H', e, 31, int(aux, 16)); struct.pack_into('<HH', e, 33, d, t)
        struct.pack_into('<H', e, 37, 2)
        entries.append(e)
    for b in range(2, 6):
        struct.pack_into('<HH', img, b*512, b - 1 if b > 2 else 0, b + 1 if b < 5 else 0)
    h = bytearray(39)
    h[0] = 0xF0 | len(vol); h[1:1+len(vol)] = vol.encode()
    struct.pack_into('<HH', h, 24, d, t); h[30] = 0xC3; h[31] = 0x27; h[32] = 0x0D
    struct.pack_into('<HHH', h, 33, len(entries), 6, nblk)
    img[2*512+4:2*512+43] = h
    slot = 1
    for e in entries:
        blk, idx = 2 + slot // 13, slot % 13
        off = blk*512 + 4 + idx*39; img[off:off+39] = e; slot += 1
    for b in range(nblk):
        if b not in used: img[6*512 + b // 8] |= 0x80 >> (b % 8)
    open(out, 'wb').write(img)
    print(f"{out}: {len(entries)} file(s), {nxt[0]} blocks used")

main()
