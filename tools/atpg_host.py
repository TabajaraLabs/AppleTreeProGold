#!/usr/bin/env python3
"""AppleTree Pro Gold - serial test host for an Apple IIe with a Super Serial Card in slot 1.

Requires:  pip install pyserial
On the Apple (ProDOS + BASIC.SYSTEM prompt), type once:   PR#1   then   IN#1

  python atpg_host.py COM9 boot              type the loader into the Monitor ($0300) and verify it
  python atpg_host.py COM9 upload ATPG.SYSTEM  start the loader, send the file (ACK per 256-byte block), check S=
  python atpg_host.py COM9 run               after a good upload: start the program at $2000
  python atpg_host.py COM9 keys "{DOWN}{RET}T"   send keystrokes ({UP} {DOWN} {LEFT} {RIGHT}
                                             {RET} {ESC} {TAB} {^X}=ctrl-X {@C}=Open-Apple+C {SNAP} {WAIT})
  python atpg_host.py COM9 snap              print the 80x24 screen
  python atpg_host.py COM9 all ATPG.SYSTEM "keys..."   boot + upload + run + keys + snap
The loader (tools/mkloader.py) uses the CFFA carga_v1 protocol: the Apple sends $06 before
each block, so nothing is sent while it is busy (the SSC has no flow control).
"""
import sys, time, os, struct, re
import serial

LOADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'loader.bin')
KEYS = {'UP': 0x0B, 'DOWN': 0x0A, 'LEFT': 0x08, 'RIGHT': 0x15, 'RET': 0x0D, 'ESC': 0x1B, 'TAB': 0x09}

def opn(port):
    # 2 stop bits: back-to-back bytes at 1 stop bit arrive corrupted at the SSC (seen 2026-11-22)
    return serial.Serial(port, 19200, bytesize=8, parity='N', stopbits=2, timeout=2,
                         rtscts=False, dsrdtr=False)

def read_until_star(s, timeout=3.0):
    t0 = time.time(); buf = b''
    while time.time() - t0 < timeout:
        c = s.read(1)
        if c:
            buf += c
            if (c[0] & 0x7F) == ord('*'): return buf
    return buf

def typ(s, text, delay=0.015):
    # the Apple drops characters while echoing; type slowly and wait for the prompt
    for ch in text:
        s.write(ch.encode()); time.sleep(delay)
    return bytes(b & 0x7F for b in read_until_star(s)).decode('ascii', 'replace')

def mon_dump(s, a, n):
    import re
    s.reset_input_buffer(); out = typ(s, '%04X.%04X\r' % (a, a + n - 1)); mem = {}
    for m in re.finditer(r'([0-9A-F]{4})-((?: [0-9A-F]{2})+)', out):
        for i, h in enumerate(m.group(2).split()): mem[int(m.group(1), 16) + i] = int(h, 16)
    return mem

def boot(s):
    code = open(LOADER, 'rb').read()
    typ(s, '\rCALL -151\r'); s.reset_input_buffer(); typ(s, '\r')
    todo = range(0, len(code), 8)
    for attempt in range(5):
        for i in todo:
            typ(s, '%04X:' % (0x300 + i) + ' '.join('%02X' % b for b in code[i:i+8]) + '\r')
        mem = mon_dump(s, 0x300, len(code))
        todo = sorted({(a // 8) * 8 for a in range(len(code)) if mem.get(0x300 + a) != code[a]})
        if not todo: break
        print('retyping', ['%04X' % (0x300 + i) for i in todo])
    else:
        print('loader verify failed'); return False
    print('loader verified in memory ($0300-$%04X)' % (0x2FF + len(code)))
    return True

def strip_as(d):
    if d[:4] == b'\x00\x05\x16\x00':
        n = struct.unpack('>H', d[24:26])[0]
        for i in range(n):
            eid, off, ln = struct.unpack('>III', d[26 + i*12:38 + i*12])
            if eid == 1: return d[off:off+ln]
    return d

def wait_ack(s, timeout):
    t0 = time.time(); seen = b''
    while time.time() - t0 < timeout:
        c = s.read(1)
        if c == b'\x06': return True
        seen += c
    print('\nno ACK; Apple sent %r' % bytes(b & 0x7F for b in seen)[-40:])
    return False

def upload(s, path, addr=0x2000):
    data = strip_as(open(path, 'rb').read())
    nblk = (len(data) + 255) // 256
    data = data + bytes(nblk * 256 - len(data))
    assert addr & 0xFF == 0 and 0 < nblk < 256
    s.reset_input_buffer(); typ(s, '\r')
    typ(s, '306:%02X %02X\r' % (addr >> 8, nblk))
    s.reset_input_buffer()
    for ch in '300G\r': s.write(ch.encode()); time.sleep(0.03)
    want = sum(data) & 0xFFFF
    for attempt in range(3):
        s.timeout = 0.5                   # before any write (see snap)
        if attempt: s.reset_input_buffer(); s.write(b'L'); s.flush()   # loader: load again
        t0 = time.time()
        for i in range(nblk):
            if not wait_ack(s, 5 if i == 0 else 3):
                print('upload aborted before block %d/%d' % (i, nblk)); s.timeout = 2; return False
            for j in range(i*256, (i+1)*256, 16):   # small gaps every 16 bytes: long
                s.write(data[j:j+16]); s.flush()    # back-to-back bursts arrive corrupted
                time.sleep(0.002)
            sys.stdout.write('\rblock %d/%d' % (i + 1, nblk)); sys.stdout.flush()
        buf = b''; t1 = time.time()
        while time.time() - t1 < 8 and not re.search(rb'S=[0-9A-F]{4}', buf):
            buf += bytes(b & 0x7F for b in s.read(1))
        if not re.search(rb'S=[0-9A-F]{4}', buf):          # a byte of the last block was lost:
            for _ in range(300):                             # pad until the loader reports
                s.write(b'\x00'); s.flush(); buf += bytes(b & 0x7F for b in s.read(8))
                if re.search(rb'S=[0-9A-F]{4}', buf): break
        s.timeout = 2
        m = re.search(rb'S=([0-9A-F]{4})', buf)
        got = int(m.group(1), 16) if m else None
        print('\nsent %d bytes ($%04X-$%04X) in %.1f s; Apple S=%s, expected %04X: %s' % (
            len(data), addr, addr + len(data) - 1, time.time() - t0,
            '%04X' % got if got is not None else '?', want, 'OK' if got == want else 'MISMATCH'))
        if got == want: return True
        if got is None: return False
        s.timeout = 0.5
    return False

def run(s):
    s.write(b'J')                         # loader resets the ProDOS bitmap and jumps to the load address

def snap(s):
    # set the timeout BEFORE writing: on Windows changing it reconfigures the port and
    # can drop a byte still in the transmit FIFO
    s.timeout = 0.3
    while s.read(4096): pass              # let a dump still in flight finish (stale bytes)
    s.timeout = 5; s.write(b'\x1e'); s.flush()
    d = s.read(2049); s.timeout = 2
    if len(d) != 2049 or d[0] != ord('S'):
        print('snap failed (%d bytes)' % len(d)); return
    main, aux = d[1:1025], d[1025:]
    print('+' + '-' * 80 + '+')
    for r in range(24):
        base = (r & 7) * 128 + (r >> 3) * 40
        txt = ''
        for c in range(80):
            b = (aux if c % 2 == 0 else main)[base + c // 2]
            if b >= 0xA0: ch = chr(b & 0x7F)
            elif b >= 0x80: ch = chr((b & 0x7F) + 0x40)
            elif b < 0x20: ch = chr(b + 0x40).lower() if chr(b + 0x40).isalpha() else chr(b + 0x40)
            elif b < 0x40: ch = '#' if b == 0x20 else chr(b)
            elif b < 0x60: ch = {0x5A: '[', 0x5F: ']', 0x53: '=', 0x4C: '~', 0x54: 'L'}.get(b, '@')   # MouseText
            else: ch = chr(b).upper() if chr(b).isalpha() else chr(b)
            txt += ch
        print('|' + txt + '|')
    print('+' + '-' * 80 + '+   (lowercase / # = inverse video; MouseText: [ ] = ~ lines, @ other)')

def keys(s, k):
    i = 0
    while i < len(k):
        if k[i] == '{':
            j = k.index('}', i); t = k[i+1:j]; i = j + 1
            if t == 'SNAP': snap(s); continue
            if t == 'WAIT': time.sleep(1); continue
            if t in KEYS: s.write(bytes([KEYS[t]]))
            elif t[0] == '^': s.write(bytes([ord(t[1].upper()) & 0x1F]))
            elif t[0] == '@': s.write(bytes([0x1D, ord(t[1].upper())]))
        else:
            s.write(k[i].encode()); i += 1
        time.sleep(0.6)     # AT reads the SSC only while waiting for a key; one-byte buffer

if __name__ == '__main__':
    s = opn(sys.argv[1]); cmd = sys.argv[2]
    if cmd == 'boot': boot(s)
    elif cmd == 'upload': upload(s, sys.argv[3])
    elif cmd == 'run': run(s)
    elif cmd == 'snap': snap(s)
    elif cmd == 'keys': keys(s, sys.argv[3])
    elif cmd == 'all':
        if boot(s) and upload(s, sys.argv[3]):
            run(s); time.sleep(4)
            keys(s, (sys.argv[4] if len(sys.argv) > 4 else '') + '{SNAP}')
