; Execute preloaded code, then replace it with a longer instruction.
        org     $300
        ldx     #$ff
        txs
        ldy     #$00
        cld
        clc
        jsr     $0400
        sta     $0200

        lda     #$22
        sta     $0210
        lda     #$ae            ; LDX $0210
        sta     $0400
        lda     #$10
        sta     $0401
        lda     #$02
        sta     $0402
        lda     #$60
        sta     $0403
        ldx     #$00
        jsr     $0400
        stx     $0201
        jmp     done

        org     $380
done:   jmp     done

        org     $400
        lda     #$11
        rts
