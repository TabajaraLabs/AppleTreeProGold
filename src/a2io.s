; AppleTree Pro Gold - Apple II low level I/O
; 80-column screen drivers (IIe aux-memory / Videx Videoterm), keyboard,
; ProDOS MLI call, printer output.  NMOS 6502 code (runs on II+).

        .export _scr_init, _scr_done, _scr_puts, _scr_fill
        .export _key_get, _key_avail
        .export _scr_inv, _key_oa, _scr_mach, _mli
        .export _prn_begin, _prn_char, _prn_end
        .export _test_mode, _scr_mtext
        .import popa
        .importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

CSWL    = $36
CSWH    = $37
KBD     = $C000
KBDSTRB = $C010
OPNAPPL = $C061
COUT    = $FDED
HOME    = $FC58
VIDREG  = $C0B0         ; Videx in slot 3: CRTC address / bank select
VIDDAT  = $C0B1
SSCDATA = $C098         ; Super Serial Card in slot 1
SSCSTAT = $C099

        .data
_scr_inv:  .byte 0
_key_oa:   .byte 0
_scr_mach: .byte 0
_test_mode: .byte 0     ; 1 = keyboard also read from SSC slot 1 (test harness)
_scr_mtext: .byte 0     ; 1 = MouseText (enhanced IIe / IIc / IIgs)
gsav:      .byte 0
cx:        .byte 0
vbank4:    .byte 0
savcsw:    .word 0

        .rodata
baselo: .repeat 24, I
        .byte <($400 + (I .mod 8) * $80 + (I / 8) * $28)
        .endrep
basehi: .repeat 24, I
        .byte >($400 + (I .mod 8) * $80 + (I / 8) * $28)
        .endrep
m80lo:  .repeat 24, I
        .byte <(I * 80)
        .endrep
m80hi:  .repeat 24, I
        .byte >(I * 80)
        .endrep

; box glyphs (see plat.h G_*): screen code with MouseText, ASCII without
NGLYPH = 16
gmt:    .byte $A0, $DF, $4C, $5A, $5F, $53, $A0, $DF, $4C, $5A, $5F, $53, $5A, $5A, $54, $56
gasc:   .byte ' ', '-', '-', '|', '|', '-', '+', '+', '+', '+', '+', '+', '|', '+', '`', '.'

        .code

; ---------------------------------------------------------------- init
_scr_init:
        lda $BF98               ; ProDOS MACHID: %10xxxxxx = IIe/IIc/IIgs
        and #$C0
        cmp #$80
        bne @videx
        lda #0
        sta _scr_mach
        sta $C001               ; 80STORE on
        sta $C00D               ; 80COL on
        sta $C00F               ; ALTCHARSET on (lowercase + inverse lc)
        sta $C054               ; PAGE1
        sta $C051               ; TEXT on
        bit $C082               ; ROM: $FBC0 = $E0 enhanced IIe/IIgs, $00 IIc, $EA old IIe
        ldx $FBC0
        bit $C080               ; back to LC bank 2 (cc65 runtime)
        txa                     ; (BIT clobbered Z)
        beq @mt
        cmp #$E0
        bne @nomt
@mt:    lda #1
        sta _scr_mtext
@nomt:  lda $0303               ; serial test loader resident at $0300?
        cmp #'A'
        bne @nt
        lda $0304
        cmp #'T'
        bne @nt
        lda $0305
        cmp #'L'
        bne @nt
        lda #1
        sta _test_mode
@nt:    jmp clear
@videx: lda #1
        sta _scr_mach
        lda #$00                ; PR#3: let the card firmware initialise
        sta CSWL
        lda #$C3
        sta CSWH
        lda #$8C                ; form feed = clear screen
        jsr romcout
        lda #12                 ; CRTC start address = 0
        sta VIDREG
        lda #0
        sta VIDDAT
        lda #13
        sta VIDREG
        lda #0
        sta VIDDAT
        lda #10                 ; cursor off
        sta VIDREG
        lda #$20
        sta VIDDAT
        jmp clear

_scr_done:
        lda _scr_mach
        bne @v
        sta $C000               ; 80STORE off
        sta $C00C               ; 80COL off
        sta $C00E               ; ALTCHARSET off
        bit $C082               ; ROM
        jsr HOME
        bit $C080               ; back to LC bank 2 (cc65 runtime)
        rts
@v:     lda #$8C
romcout:
        bit $C082
        jsr COUT
        bit $C080
        rts

clear:  lda #0
        sta tmp2
@lp:    ldx #0
        ldy tmp2
        jsr setpos
        ldx #80
@c:     lda #' '
        jsr conv
        jsr put
        dex
        bne @c
        inc tmp2
        lda tmp2
        cmp #24
        bne @lp
        lda #0
        tax
        rts

; ---------------------------------------------------------------- position
; X = column, Y = row
setpos: stx cx
        lda _scr_mach
        bne @v
        lda baselo,y
        sta ptr1
        lda basehi,y
        sta ptr1+1
        rts
@v:     lda $CFFF               ; release C800 space
        lda $C300               ; select slot 3 C800 space (Videx RAM at $CC00)
        txa
        clc
        adc m80lo,y
        sta tmp3
        lda m80hi,y
        adc #0
        sta tmp4
        lda tmp3
        sta ptr1
        lda tmp4
        and #1
        ora #$CC
        sta ptr1+1
        lda tmp4
        lsr a
        asl a
        asl a
        sta vbank4
        rts

; A = ascii -> A = screen code (uses X? no: preserves X,Y)
conv:   and #$7F
        cmp #$20
        bcs @p
        cmp #NGLYPH
        bcs @sp
        sty gsav                ; box glyph 1..NGLYPH-1
        tay
        lda _scr_mtext
        beq @asc
        lda gmt,y               ; ready-made screen code (MouseText / normal)
        ldy gsav
        rts
@asc:   lda gasc,y
        ldy gsav
        jmp conv
@sp:    lda #$20
@p:     pha
        lda _scr_mach
        bne @vid
        lda _scr_inv
        bne @inv
        pla
        ora #$80
        rts
@inv:   pla
        cmp #$40
        bcc @r
        cmp #$60
        bcs @r
        and #$3F
@r:     rts
@vid:   lda _scr_inv
        beq @vn
        pla
        ora #$80
        rts
@vn:    pla
        rts

; store screen code A at cursor, advance (preserves X)
put:    sta tmp1
        lda _scr_mach
        bne @v
        lda cx
        lsr a
        tay
        lda tmp1
        bcs @main
        sta $C055               ; PAGE2 -> aux text memory (even columns)
        sta (ptr1),y
        sta $C054
        inc cx
        rts
@main:  sta (ptr1),y
        inc cx
        rts
@v:     txa
        pha
        ldx vbank4
        lda VIDREG,x            ; select 512-byte bank
        ldy #0
        lda tmp1
        sta (ptr1),y
        inc ptr1
        bne @ok
        inc ptr1+1
        lda ptr1+1
        cmp #$CE
        bne @ok
        lda #$CC
        sta ptr1+1
        lda vbank4
        clc
        adc #4
        sta vbank4
@ok:    inc cx
        pla
        tax
        rts

; void __fastcall__ scr_puts(u8 x, u8 y, const char *s)
_scr_puts:
        sta ptr2
        stx ptr2+1
        jsr popa
        pha
        jsr popa
        tax
        pla
        tay
        jsr setpos
@lp:    lda cx
        cmp #80
        bcs @done
        ldy #0
        lda (ptr2),y
        beq @done
        jsr conv
        jsr put
        inc ptr2
        bne @lp
        inc ptr2+1
        jmp @lp
@done:  rts

; void __fastcall__ scr_fill(u8 x, u8 y, u8 n, u8 c)
_scr_fill:
        sta tmp2                ; char
        jsr popa
        sta ptr2                ; count
        jsr popa
        pha
        jsr popa
        tax
        pla
        tay
        jsr setpos
@lp:    lda ptr2
        beq @done
        lda cx
        cmp #80
        bcs @done
        lda tmp2
        jsr conv
        jsr put
        dec ptr2
        jmp @lp
@done:  rts

; ---------------------------------------------------------------- keyboard
_key_get:
@w:     lda KBD
        bmi @kbd
        lda _test_mode
        beq @w
        lda SSCSTAT
        and #$08                ; receive register full?
        beq @w
        lda SSCDATA
        and #$7F
        cmp #$1E                ; ^^ = send screen dump to the test host
        bne @nd
        jsr dump
        jmp @w
@nd:    cmp #$1D                ; ^] = Open-Apple prefix from the test host
        bne @ser
        lda #$80
        sta _key_oa
@w2:    lda SSCSTAT
        and #$08
        beq @w2
        lda SSCDATA
        and #$7F
        ldx #0
        rts
@ser:   ldx #0
        stx _key_oa
        rts
@kbd:   sta KBDSTRB
        and #$7F
        pha
        lda #0
        sta _key_oa
        lda _scr_mach
        bne @r
        lda OPNAPPL
        and #$80
        sta _key_oa
@r:     pla
        ldx #0
        rts

_key_avail:
        lda KBD
        and #$80
        bne @r
        lda _test_mode
        beq @r
        lda SSCSTAT
        and #$08
@r:     ldx #0
        rts

; send 'S' + 1024 bytes of main text page + 1024 bytes of aux text page
dump:   lda #'S'
        jsr sendb
        jsr dpage
        sta $C055
        jsr dpage
        sta $C054
        rts
dpage:  lda #$00
        sta ptr1
        lda #$04
        sta ptr1+1
        ldy #0
@l:     lda (ptr1),y
        jsr sendb
        iny
        bne @l
        inc ptr1+1
        lda ptr1+1
        cmp #$08
        bne @l
        rts
sendb:  pha
@w:     lda SSCSTAT
        and #$10
        beq @w
        pla
        sta SSCDATA
        rts

; ---------------------------------------------------------------- ProDOS
; u8 __fastcall__ mli(u8 cmd, void *parms)
_mli:   sta mparm
        stx mparm+1
        jsr popa
        sta mcmd
        jsr $BF00
mcmd:   .byte 0
mparm:  .word 0
        ldx #0
        rts

; ---------------------------------------------------------------- printer
_prn_begin:
        lda CSWL
        sta savcsw
        lda CSWH
        sta savcsw+1
        lda #0
        sta CSWL
        lda #$C1
        sta CSWH
        rts
_prn_char:
        ora #$80
        jmp romcout
_prn_end:
        lda savcsw
        sta CSWL
        lda savcsw+1
        sta CSWH
        rts

; ---------------------------------------------------------------- I/O buffers
; Replace cc65's heap based ProDOS buffers with 2 fixed 1K buffers at
; $1800 and $1C00 (directory tree lives at $0800-$13FF, tables at $1400).
        .export iobuf_alloc, iobuf_free
        .import popax
        .bss
bufmap: .res 1
        .code
iobuf_alloc:                    ; posix_memalign(void **p, size_t align, size_t size)
        jsr popax               ; alignment (ignored)
        jsr popax               ; memptr
        sta ptr1
        stx ptr1+1
        ldx #0
        lda #1
@f:     bit bufmap
        beq @got
        asl a
        inx
        cpx #2
        bne @f
        lda #2                  ; ENOMEM
        ldx #0
        rts
@got:   ora bufmap
        sta bufmap
        txa
        asl a
        asl a
        clc
        adc #$18
        ldy #1
        sta (ptr1),y
        dey
        lda #0
        sta (ptr1),y
        tax
        rts
iobuf_free:                     ; A/X = pointer
        txa
        sec
        sbc #$18
        lsr a
        lsr a
        tax
        lda #1
@s:     dex
        bmi @c
        asl a
        jmp @s
@c:     eor #$FF
        and bufmap
        sta bufmap
        rts
