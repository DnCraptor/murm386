#!/usr/bin/env python3
"""Write SELFLOAD.EXE: a self-loading Win16 program (NE flag 0800h, as Word
for Windows 1.x and Excel 2.x are) made of OWSEGS (mkne_watcom.py): its
segments after a loader of its own, segment 1, and 196 more small data
segments (201 in all: past 128, Word for Windows 1.0 has 196). On disk the
program's segments, their relocations' counts and records are XOR 5Ah: only
the loader reads them. Its SELFLOADHEADER at offset 0: BootApp allocates
the segments from the third on through MyAlloc into the module database's
segment table (hModule's), LoadAppSeg allocates the second itself, reads
each (_llseek, _lread through AllocCStoDSAlias), relocates it (internal
references through the database's handles, entries through EntryAddrProc,
imports through GetProcAddress of the module references' handles, by
ordinal and by name) and answers its handle. The program then runs as
OWSEGS does: "C8CA765F".

    mkne_selfload.py OWSEGS.EXE SELFLOAD.EXE

The loader (wasm -q loader.asm; wlink format raw bin option offset=0):

    ; SELFLOAD's loader: segment 1 of a self-loading NE (SELFLOADHEADER at 0)
    .286
    _TEXT segment word public 'CODE'
    assume cs:_TEXT
            org 0
    hdr     db 'A', '0'
            dw 0
            dw 0FFFFh, 0FFFFh       ; 4 BootApp (an internal fixup)
            dw 0FFFFh, 0FFFFh       ; 8 LoadAppSeg (an internal fixup)
            dd 0                    ; 12
    p_alloc dd 0                    ; 16 MyAlloc (Windows')
    p_entry dd 0                    ; 20 EntryAddrProc (Windows')
            dd 0                    ; 24 ExitProc
            dw 4 dup (0)            ; 28
    p_owner dd 0                    ; 36 SetOwner (Windows')
    p_seek  dw 0FFFFh, 0            ; 40 KERNEL._llseek
    p_read  dw 0FFFFh, 0            ; 44 KERNEL._lread
    p_proc  dw 0FFFFh, 0            ; 48 KERNEL.GetProcAddress
    p_alias dw 0FFFFh, 0            ; 52 KERNEL.AllocCStoDSAlias
    p_free  dw 0FFFFh, 0            ; 56 KERNEL.FreeSelector
    offs    dw BootApp, LoadAppSeg  ; 60: for the generator

    ; BootApp(hModule, hFile): the segments after the second allocated (the
    ; second LoadAppSeg allocates)
    BootApp proc far
            push bp
            mov bp, sp
            push si
            push di
            mov es, [bp+8]
            cmp word ptr es:[0], 454Eh
            jne boot_done
            mov si, 3
    boot_seg:
            mov es, [bp+8]
            cmp si, es:[1Ch]
            ja boot_done
            mov di, si
            dec di
            imul di, di, 10
            add di, es:[22h]
            push word ptr es:[di+4]         ; flags
            push word ptr es:[di+6]         ; size
            push 0                          ; elem
            call dword ptr cs:[p_alloc]
            mov es, [bp+8]
            mov es:[di+8], ax               ; its handle
            inc si
            jmp boot_seg
    boot_done:
            pop di
            pop si
            pop bp
            ret 4
    BootApp endp

    ; LoadAppSeg(hModule, hFile, seg): its handle
    hmod    equ word ptr [bp+10]
    hfile   equ word ptr [bp+8]
    segno   equ word ptr [bp+6]
    slr     equ word ptr [bp-2]
    als   equ word ptr [bp-4]
    cnt   equ word ptr [bp-6]
    ent   equ word ptr [bp-8]
    wher   equ word ptr [bp-10]
    tsel    equ word ptr [bp-12]
    toff    equ word ptr [bp-14]
    rec     equ -24                         ; 8 bytes
    namebuf equ -64                         ; 40 bytes

    LoadAppSeg proc far
            push bp
            mov bp, sp
            sub sp, 64
            push si
            push di
            mov es, hmod
            mov di, segno
            dec di
            imul di, di, 10
            add di, es:[22h]
            mov ent, di
            mov ax, es:[di+8]
            mov slr, ax
            or ax, ax
            jnz have_sel
            push word ptr es:[di+4]
            push word ptr es:[di+6]
            push 0
            call dword ptr cs:[p_alloc]
            mov slr, ax
    have_sel:
            mov es, hmod
            mov di, ent
            mov ax, es:[di]                 ; sector
            or ax, ax
            jz load_done
            xor dx, dx
            mov cx, es:[32h]
    sh_loop:  jcxz sh_done
            shl ax, 1
            rcl dx, 1
            dec cx
            jmp sh_loop
    sh_done:
            push hfile
            push dx
            push ax
            push 0
            call dword ptr cs:[p_seek]
            push slr
            call dword ptr cs:[p_alias]
            mov als, ax
            mov es, hmod
            mov di, ent
            push hfile
            push als
            push 0
            push word ptr es:[di+2]
            call dword ptr cs:[p_read]
            mov es, hmod
            mov di, ent
            mov cx, es:[di+2]
            mov es, als
            xor bx, bx
    descramble:
            jcxz descrambled
            xor byte ptr es:[bx], 5Ah
            inc bx
            dec cx
            jmp descramble
    descrambled:
            mov es, hmod
            mov di, ent
            test word ptr es:[di+4], 100h
            jz relocated
            push hfile
            push ss
            lea ax, cnt
            push ax
            push 2
            call dword ptr cs:[p_read]
            xor cnt, 5A5Ah
    next_rec:
            cmp cnt, 0
            je relocated
            dec cnt
            push hfile
            push ss
            lea ax, [bp+rec]
            push ax
            push 8
            call dword ptr cs:[p_read]
            lea si, [bp+rec]
            mov cx, 8
    unscramble:
            xor byte ptr ss:[si], 5Ah
            inc si
            loop unscramble
            mov al, [bp+rec+1]
            and al, 3
            cmp al, 3
            je next_rec                     ; an OS fixup: nothing
            cmp al, 0
            jne import
            cmp byte ptr [bp+rec+4], 0FFh
            jne internal
            push hmod                       ; an ent
            push word ptr [bp+rec+6]
            call dword ptr cs:[p_entry]
            jmp target
    internal:
            mov es, hmod
            mov bl, [bp+rec+4]
            xor bh, bh
            dec bx
            imul bx, bx, 10
            add bx, es:[22h]
            mov dx, es:[bx+8]
            mov ax, [bp+rec+6]
            jmp target
    import:
            mov es, hmod
            mov bx, [bp+rec+4]              ; the module's number
            dec bx
            shl bx, 1
            add bx, es:[28h]
            mov si, es:[bx]                 ; its handle
            cmp al, 1
            jne by_name
            push si
            push 0
            push word ptr [bp+rec+6]
            call dword ptr cs:[p_proc]
            jmp target
    by_name:
            mov bx, es:[2Ah]
            add bx, [bp+rec+6]
            mov cl, es:[bx]
            xor ch, ch
            inc bx
            lea di, [bp+namebuf]
    copy:   jcxz copied
            mov al, es:[bx]
            mov ss:[di], al
            inc bx
            inc di
            dec cx
            jmp copy
    copied: mov byte ptr ss:[di], 0
            push si
            push ss
            lea ax, [bp+namebuf]
            push ax
            call dword ptr cs:[p_proc]
    target:
            mov tsel, dx
            mov toff, ax
            mov es, als
            mov bx, [bp+rec+2]
            mov cl, [bp+rec]                ; the source's type
            test byte ptr [bp+rec+1], 4
            jz chain
            call apply_add
            jmp next_rec
    chain:  mov si, es:[bx]                 ; the next in the chain
            call apply
            cmp si, 0FFFFh
            je next_rec
            mov bx, si
            jmp chain
    relocated:
            push als
            call dword ptr cs:[p_free]
    load_done:
            mov ax, slr
            pop di
            pop si
            mov sp, bp
            pop bp
            ret 6
    LoadAppSeg endp

    ; at ES:BX, the source type CL: the target tsel:toff
    apply   proc near
            mov ax, toff
            mov dx, tsel
            cmp cl, 2
            jne ap3
            mov es:[bx], dx
            ret
    ap3:    cmp cl, 3
            jne ap5
            mov es:[bx], ax
            mov es:[bx+2], dx
            ret
    ap5:    cmp cl, 5
            jne ap0
            mov es:[bx], ax
            ret
    ap0:    mov es:[bx], al
            ret
    apply   endp

    apply_add proc near
            mov ax, toff
            mov dx, tsel
            cmp cl, 2
            jne aa3
            add es:[bx], dx
            ret
    aa3:    cmp cl, 3
            jne aa5
            add es:[bx], ax
            mov es:[bx+2], dx
            ret
    aa5:    cmp cl, 5
            jne aa0
            add es:[bx], ax
            ret
    aa0:    add es:[bx], al
            ret
    apply_add endp

    _TEXT ends
    end
"""
import struct
import sys

LOADER = bytes.fromhex(
    '41300000ffffffffffffffff00000000000000000000000000000000000000000000000000000000ffff0000'
    'ffff0000ffff0000ffff0000ffff000040008800558bec56578e460826813e00004e457531be03008e460826'
    '3b361c0077248bfe4f6bff0a26033e220026ff750426ff75066a002eff1e10008e46082689450846ebd25f5e'
    '5dca0400558bec83ec4056578e460a8b7e064f6bff0a26033e2200897ef8268b45088946fe0bc0751226ff75'
    '0426ff75066a002eff1e10008946fe8e460a8b7ef8268b050bc07503e9620133d2268b0e3200e307d1e0d1d2'
    '49ebf7ff760852506a002eff1e2800ff76fe2eff1e34008946fc8e460a8b7ef8ff7608ff76fc6a0026ff7502'
    '2eff1e2c008e460a8b7ef8268b4d028e46fc33dbe3082680375a4349ebf68e460a8b7ef826f7450400017503'
    'e9f200ff7608168d46fa506a022eff1e2c008176fa5a5a837efa007503e9d500ff4efaff7608168d46e8506a'
    '082eff1e2c008d76e8b908003680345a46e2f98a46e924033c0374cf3c00752d807eecff750dff760aff76ee'
    '2eff1e1400eb698e460a8a5eec32ff4b6bdb0a26031e2200268b57088b46eeeb4f8e460a8b5eec4bd1e32603'
    '1e2800268b373c01750d566a00ff76ee2eff1e3000eb2d268b1e2a00035eee268a0f32ed438d7ec0e30b268a'
    '07368805434749ebf336c6050056168d46c0502eff1e30008956f48946f28e46fc8b5eea8a4ee8f646e90474'
    '06e85100e934ff268b37e81f0083feff7503e926ff8bdeebeeff76fc2eff1e38008b46fe5f5e8be55dca0600'
    '8b46f28b56f480f9027504268917c380f903750826890726895702c380f9057504268907c3268807c38b46f2'
    '8b56f480f9027504260117c380f903750826010726895702c380f9057504260107c3260007c3'
)


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def selfload(exe, loader, offs, extra=0):
    lf = struct.unpack_from('<I', exe, 0x3c)[0]
    h = bytearray(exe[lf:lf + 64])
    nseg, nmod, shift = u16(h, 0x1c), u16(h, 0x1e), u16(h, 0x32) or 9
    segtab, rsrc, restab, modtab, imptab = (u16(h, o) for o in (0x22, 0x24, 0x26, 0x28, 0x2a))
    enttab, entlen = u16(h, 4), u16(h, 6)
    nres_at, nres_len = struct.unpack_from('<I', h, 0x2c)[0], u16(h, 0x20)
    segs = []
    for i in range(nseg):
        sector, length, flags, minsize = struct.unpack_from('<4H', exe, lf + segtab + 8 * i)
        at = sector << shift
        n = length or 0x10000
        data = exe[at:at + n] if sector else b''
        relocs = []
        if flags & 0x100:
            cnt = u16(exe, at + n)
            relocs = [bytearray(exe[at + n + 2 + 8 * k:at + n + 10 + 8 * k]) for k in range(cnt)]
        segs.append([length, flags, minsize, data, relocs])
    # the program's segments after the loader's: internal targets one on
    for s in segs:
        for r in s[4]:
            if r[1] & 3 == 0 and r[4] != 0xff:
                r[4] += 1
    ent = bytearray(exe[lf + enttab:lf + enttab + entlen])
    i = 0
    while i < len(ent) and ent[i]:
        cnt, typ = ent[i], ent[i + 1]
        i += 2
        if typ == 0:
            continue
        if typ == 0xff:
            for k in range(cnt):
                ent[i + 6 * k + 3] += 1
            i += 6 * cnt
        else:
            if typ != 0xfe:
                ent[i - 1] = typ + 1
            i += 3 * cnt
    names = exe[lf + modtab:lf + modtab + 2 * nmod]
    imp = exe[lf + imptab:lf + enttab]
    kernel = None
    for k in range(nmod):
        o = u16(names, 2 * k)
        if imp[o + 1:o + 1 + imp[o]] == b'KERNEL':
            kernel = k + 1
    assert kernel
    # the loader: its header's procedures (internal), its imports
    ld = bytearray(loader)
    lrel = []
    for at, off in ((4, offs[0]), (8, offs[1])):
        lrel.append(bytes([3, 0]) + struct.pack('<HBBH', at, 1, 0, off))
    for at, ordinal in ((40, 84), (44, 82), (48, 50), (52, 170), (56, 176)):
        lrel.append(bytes([3, 1]) + struct.pack('<HHH', at, kernel, ordinal))
    segs.insert(0, [len(ld), 0x0150, len(ld), bytes(ld), [bytearray(r) for r in lrel]])
    # more data segments, past Windows' usual number (Word for Windows 1.0 has 196)
    for k in range(extra):
        segs.append([16, 0x0d51, 32, bytes((k + j) & 0xff for j in range(16)), []])
    nseg += 1 + extra
    # the header and its tables
    out = bytearray(exe[:lf])
    assert rsrc == restab, 'a program without resources'
    new_segtab = 0x40
    new_rsrc = new_segtab + 8 * nseg
    resid = exe[lf + restab:lf + modtab]
    new_res = new_rsrc
    new_mod = new_res + len(resid)
    new_imp = new_mod + 2 * nmod
    new_ent = new_imp + len(imp)
    tables = bytearray(8 * nseg) + resid + names + imp + ent
    nh = bytearray(h)
    struct.pack_into('<H', nh, 4, new_ent)
    struct.pack_into('<H', nh, 0x0c, u16(h, 0x0c) | 0x0800)
    struct.pack_into('<H', nh, 0x0e, u16(h, 0x0e) + 1)
    struct.pack_into('<H', nh, 0x16, u16(h, 0x16) + 1)
    struct.pack_into('<H', nh, 0x1a, u16(h, 0x1a) + 1)
    struct.pack_into('<H', nh, 0x1c, nseg)
    for o, v in ((0x22, new_segtab), (0x24, new_rsrc), (0x26, new_res), (0x28, new_mod), (0x2a, new_imp)):
        struct.pack_into('<H', nh, o, v)
    body = nh + tables
    nres = exe[nres_at:nres_at + nres_len]
    struct.pack_into('<I', body, 0x2c, lf + len(body))
    body += nres
    out += body
    # the segments, the program's scrambled (data, count, records: XOR 5Ah)
    align = 1 << shift
    for i, (length, flags, minsize, data, relocs) in enumerate(segs):
        while len(out) % align:
            out.append(0)
        sector = len(out) >> shift if data else 0
        raw = bytearray(data)
        rel = struct.pack('<H', len(relocs)) + b''.join(relocs) if flags & 0x100 else b''
        if i:
            raw = bytearray(b ^ 0x5a for b in raw)
            rel = bytes(b ^ 0x5a for b in rel)
        out += raw + rel
        struct.pack_into('<4H', out, lf + new_segtab + 8 * i, sector, length, flags, minsize)
    return bytes(out)


if len(sys.argv) != 3:
    raise SystemExit('usage: mkne_selfload.py OWSEGS.EXE SELFLOAD.EXE')
with open(sys.argv[1], 'rb') as f:
    program = f.read()
with open(sys.argv[2], 'wb') as f:
    f.write(selfload(program, LOADER, (u16(LOADER, 60), u16(LOADER, 62)), 196))
