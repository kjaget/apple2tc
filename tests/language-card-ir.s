; Reads must stay before both direct and indexed language-card switches.
        org     $300
        lda     $c083
        lda     $c083
        lda     #$11
        sta     $d100
        lda     $c08b
        lda     $c08b
        lda     #$22
        sta     $d100
        lda     $d100
        bit     $c080
        sta     $0200
        lda     $d100
        sta     $c088
        sta     $0201
        ldx     #$80
        ldy     $d100
        lda     $c000,x
        sty     $0202

; Write bank 2 while ROM is visible, then verify protection.
        lda     $c081
        lda     $c081
        lda     #$33
        sta     $d100
        lda     $d100
        sta     $0203
        lda     $c080
        lda     $d100
        sta     $0204
        lda     $c082
        lda     #$44
        sta     $d100
        lda     $c080
        lda     $d100
        sta     $0205

; A modified (zero-page),Y operand is one byte, including at $FF.
        lda     #$00
        sta     $20
        sta     $22
        sta     $ff
        lda     #$05
        sta     $21
        lda     #$06
        sta     $23
        lda     #$07
        sta     $00
        lda     #$51
        sta     $0500
        lda     #$52
        sta     $0600
        lda     #$53
        sta     $0700
        ldy     #$00
        jsr     $03e0
        sta     $0206
        lda     #$22
        sta     $03e1
        jsr     $03e0
        sta     $0207
        lda     #$ff
        sta     $03e1
        jsr     $03e0
        sta     $0208
        jmp     done
        org     $3e0
        lda     ($20),y
        rts
        org     $3f0
done:   jmp     done
