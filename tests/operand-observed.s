; Change a low-memory operand through an indirect store, then execute different
; operands at the same upper-memory PC in the two language-card banks.
        org     $300
        lda     #$80
        sta     $00
        lda     #$03
        sta     $01
        jsr     $0380
        sta     $0200
        lda     #$22
        ldy     #$01
        sta     ($00),y
        lda     #$80
        ldy     #$03
        sta     ($00),y
        jsr     $0380
        sta     $0201
        lda     #$11
        ldy     #$01
        sta     ($00),y
        jsr     $0380
        sta     $0202
        jsr     $0390

        lda     #$d0
        sta     $01
        lda     #$00
        sta     $00
        lda     $c083
        lda     $c083
        ldy     #$00
        lda     #$a9
        sta     ($00),y
        iny
        lda     #$11
        sta     ($00),y
        iny
        lda     #$60
        sta     ($00),y
        lda     $c08b
        lda     $c08b
        ldy     #$00
        lda     #$a9
        sta     ($00),y
        iny
        lda     #$22
        sta     ($00),y
        iny
        lda     #$60
        sta     ($00),y
        lda     $c080
        jsr     $d000
        sta     $0203
        lda     $c088
        jsr     $d000
        sta     $0204
done:   jmp     done

        org     $380
        lda     #$11
        rts
        nop

        org     $390
        lda     #$45
        sta     $1234
        lda     #$67
        sta     $5634
        jsr     $03c0
        sta     $0205
        lda     #$c0
        sta     $00
        lda     #$56
        ldy     #$02
        sta     ($00),y
        jsr     $03c0
        sta     $0206
        rts

        org     $3c0
        lda     $1234
        rts
