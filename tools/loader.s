; AppleTree serial test loader - Super Serial Card in slot 1
; Lives at $0300.  Protocol (binary):
;   'P'                       -> 'K'                  ping
;   'W' lo hi nlo nhi data..  -> 'K' sum              write memory
;   'R' lo hi nlo nhi         -> data..               read memory
;   'J' lo hi                 -> jump (as ProDOS SYS program)
        .setcpu "6502"
        .org $0300
SDATA = $C098         ; Super Serial Card in slot 1
SSTAT = $C099
AL = $FA
AH = $FB
NL = $FC
NH = $FD
SUM = $FE

start:  jmp main
        .byte "ATL"
main:   sei                     ; IRQs would stall us past a 520us byte time -> SSC overrun
loop:   jsr getb
        cmp #'P'
        beq ping
        cmp #'W'
        beq write
        cmp #'R'
        beq read
        cmp #'J'
        beq jump
        bne loop
ping:   lda #'K'
        jsr putb
        jmp loop
write:  jsr hdr
        lda #0
        sta SUM
        tay
@l:     lda NL
        ora NH
        beq @d
        jsr getb
        sta (AL),y
        clc
        adc SUM
        sta SUM
        jsr step
        jmp @l
@d:     lda #'K'
        jsr putb
        lda SUM
        jsr putb
        jmp loop
read:   jsr hdr
        ldy #0
@l:     lda NL
        ora NH
        beq loop
        lda (AL),y
        jsr putb
        jsr step
        jmp @l
jump:   jsr getb
        sta AL
        jsr getb
        sta AH
        ldx #$17                ; reset ProDOS memory bitmap like a SYS launch
        lda #0
@b:     sta $BF58,x
        dex
        bpl @b
        lda #$CF
        sta $BF58
        lda #$01
        sta $BF6F
        jmp (AL)
step:   inc AL
        bne @1
        inc AH
@1:     lda NL
        bne @2
        dec NH
@2:     dec NL
        rts
hdr:    jsr getb
        sta AL
        jsr getb
        sta AH
        jsr getb
        sta NL
        jsr getb
        sta NH
        rts
getb:   lda SSTAT
        and #$08
        beq getb
        lda SDATA
        rts
putb:   pha
@w:     lda SSTAT
        and #$10
        beq @w
        pla
        sta SDATA
        rts
