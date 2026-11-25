; XBENCH.COM - fixed-workload CPU benchmark for murm386 (XRIP / FDOS xlat).
;
; Each test runs its loop in chunks for 36 BIOS ticks (about 2 s), starting
; on a tick edge, and reports loop iterations per second. The result is
; only as coarse as one chunk (well under 1 ms) against 2 s, unlike
; benchmarks that time a fixed amount of work in a handful of 55 ms ticks.
; MIPS is iterations * instructions per iteration (counted by hand for each
; loop) per second.
;
; Build: nasm -f bin -o XBENCH.COM xbench.asm
; Usage: XBENCH       all tests
;        XBENCH n     test n only (1..8)

        cpu     386
        org     100h

TICKS   equ     36

start:
        cld
        mov     dx, title
        call    puts
        ; optional test number on the command line
        mov     si, 81h
.skip:  lodsb
        cmp     al, ' '
        je      .skip
        sub     al, '1'
        cmp     al, NTESTS
        jae     .all
        mov     [only], al
.all:
        xor     bp, bp                  ; test index
.next:
        cmp     bp, NTESTS
        jae     .done
        movzx   ax, byte [only]
        cmp     al, 0ffh
        je      .run
        cmp     ax, bp
        jne     .skipt
.run:
        mov     bx, bp
        imul    bx, bx, TEST_SIZE
        add     bx, tests
        mov     [cur], bx
        call    measure
        call    report
.skipt:
        inc     bp
        jmp     .next
.done:
        mov     ax, 4c00h
        int     21h

; ---------------------------------------------------------------------------
; measure: run [cur]'s chunk until TICKS ticks have passed.
; out: [chunks] (dword), [ticks]
measure:
        push    bp
        mov     dword [chunks], 0
        mov     ax, 40h
        mov     es, ax
        mov     ax, [es:6ch]
.edge:  cmp     ax, [es:6ch]            ; start on a tick edge
        je      .edge
        mov     ax, [es:6ch]
        mov     [t0], ax
.chunk:
        mov     bx, [cur]
        call    [bx + T_FN]
        add     dword [chunks], 1
        mov     ax, 40h
        mov     es, ax
        mov     ax, [es:6ch]
        sub     ax, [t0]
        cmp     ax, TICKS
        jb      .chunk
        mov     [ticks], ax
        push    cs
        pop     es
        pop     bp
        ret

; report: "NAME  kiter/s  MIPS"
report:
        mov     bx, [cur]
        lea     dx, [bx + T_NAME]
        call    puts
        ; iterations = chunks * N
        mov     eax, [chunks]
        movzx   ecx, word [bx + T_N]
        mul     ecx
        mov     [iters], eax
        ; iterations per second = iters * 182065 / (ticks * 10000)
        mov     ecx, 182065
        mul     ecx
        movzx   ecx, word [ticks]
        imul    ecx, ecx, 10000
        div     ecx                     ; iterations per second
        mov     cx, 10
        call    putu_w
        mov     dx, s_kips
        call    puts
        ; MIPS * 100 = iters * ipi * 182065 / (ticks * 1e8)
        mov     bx, [cur]
        movzx   ecx, word [bx + T_IPI]
        jecxz   .nomips
        mov     eax, [iters]
        mul     ecx
        mov     ecx, 182065
        mul     ecx
        movzx   ecx, word [ticks]
        imul    ecx, ecx, 100000000
        div     ecx
        xor     edx, edx
        mov     ecx, 100
        div     ecx                     ; eax = MIPS, edx = hundredths
        push    edx
        mov     cx, 7
        call    putu_w
        mov     dl, '.'
        call    putc
        pop     eax
        cmp     eax, 10
        jae     .two
        push    eax
        mov     dl, '0'
        call    putc
        pop     eax
.two:   xor     cx, cx
        call    putu_w
        mov     dx, s_mips
        call    puts
.nomips:
        mov     dx, crlf
        call    puts
        ret

; putu_w: print eax right-aligned in cx columns (cx = 0: no padding)
putu_w:
        mov     di, numbuf + 11
        mov     byte [di], '$'
        mov     ebx, 10
.dig:   xor     edx, edx
        div     ebx
        add     dl, '0'
        dec     di
        mov     [di], dl
        test    eax, eax
        jnz     .dig
        mov     ax, numbuf + 11
        sub     ax, di                  ; digits
.pad:   cmp     ax, cx
        jae     .out
        dec     di
        mov     byte [di], ' '
        inc     ax
        jmp     .pad
.out:   mov     dx, di
        jmp     puts

puts:   mov     ah, 9
        int     21h
        ret

putc:   mov     ah, 2
        int     21h
        ret

; ---------------------------------------------------------------------------
; Tests. Each chunk function runs N iterations of its loop.

; 1. Register ALU: 6 instructions per iteration.
t_reg:
        mov     cx, 400
.l:     add     ax, bx
        xor     dx, ax
        sub     bx, si
        inc     si
        dec     di
        loop    .l
        ret

; 2. Locals on the stack, as compiled C (SI's loop is of this kind): 7.
t_mem:
        push    bp
        mov     bp, sp
        sub     sp, 16
        mov     word [bp-2], 3
        mov     word [bp-4], 5
        mov     word [bp-8], 0
        mov     word [bp-10], 300
.l:     mov     ax, [bp-2]
        add     ax, [bp-4]
        mov     [bp-6], ax
        inc     word [bp-8]
        mov     ax, [bp-8]
        cmp     ax, [bp-10]
        jl      .l
        mov     sp, bp
        pop     bp
        ret

; 3. Near call with a stack frame: 10.
t_call:
        mov     cx, 200
.l:     push    ax
        call    .f
        pop     ax
        dec     cx
        jnz     .l
        ret
.f:     push    bp
        mov     bp, sp
        mov     ax, [bp+4]
        pop     bp
        ret

; 4. Byte loop through memory, ds:si -> es:di: 4.
t_lods:
        push    cs
        pop     es
        mov     si, buf1
        mov     di, buf2
        mov     cx, 256
.l:     lodsb
        xor     al, 5
        stosb
        loop    .l
        ret

; 5. Block copy: REP MOVSW of 128 words (MIPS not given).
t_movs:
        push    cs
        pop     es
        mov     dx, 16
.l:     mov     si, buf1
        mov     di, buf2
        mov     cx, 128
        rep movsw
        dec     dx
        jnz     .l
        ret

; 6. MUL / DIV: 5.
t_muldiv:
        mov     bx, 1234
        mov     si, 1
        mov     cx, 200
.l:     mov     ax, si
        mul     bx
        div     bx
        add     si, 3
        loop    .l
        ret

; 7. Shifts and rotates: 7.
t_shift:
        mov     cl, 3
        mov     di, 300
.l:     shl     ax, 1
        rol     dx, 1
        sar     bx, 1
        shl     si, cl
        xor     ax, dx
        dec     di
        jnz     .l
        ret

; 8. Compare-and-branch chains (switch-like), path varies (MIPS not given).
t_branch:
        mov     di, 300
.l:     mov     ax, si
        and     ax, 7
        cmp     ax, 3
        je      .a
        cmp     ax, 5
        jb      .b
        inc     dx
        jmp     .c
.a:     add     dx, 2
        jmp     .c
.b:     dec     dx
.c:     inc     si
        dec     di
        jnz     .l
        ret

; ---------------------------------------------------------------------------
T_FN    equ     0
T_N     equ     2
T_IPI   equ     4
T_NAME  equ     6
TEST_SIZE equ   16

%macro  TEST 4
        dw      %1, %2, %3
        db      %4, '$'
        times   TEST_SIZE - 6 - %strlen(%4) - 1 db 0
%endmacro

tests:
        TEST    t_reg,    400, 6, "1 reg    "
        TEST    t_mem,    300, 7, "2 mem    "
        TEST    t_call,   200, 10, "3 call   "
        TEST    t_lods,   256, 4, "4 lods   "
        TEST    t_movs,   16, 0, "5 movsw  "
        TEST    t_muldiv, 200, 5, "6 muldiv "
        TEST    t_shift,  300, 7, "7 shift  "
        TEST    t_branch, 300, 0, "8 branch "
NTESTS  equ     ($ - tests) / TEST_SIZE

title   db      'XBENCH 1.0: 36 ticks (2 s) per test', 13, 10, '$'
s_kips  db      ' iter/s$'
s_mips  db      ' MIPS$'
crlf    db      13, 10, '$'
only    db      0ffh

        align   4
chunks  dd      0
iters   dd      0
t0      dw      0
ticks   dw      0
cur     dw      0
numbuf  times 12 db 0
buf1    times 256 db 0
buf2    times 256 db 0
