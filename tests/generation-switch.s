; Install, replace, and restore an instruction at $0900.
        org     $300
        ldx     #$ff
        txs
        ldy     #$00
        cld
        clc
        lda     #$a9
        sta     $0900
        lda     #$11
        sta     $0901
        lda     #$60
        sta     $0902
        jsr     $0900
        sta     $0200

        lda     #$a2
        sta     $0900
        lda     #$22
        sta     $0901
        ldx     #$00
        jsr     $0900
        stx     $0201

        lda     #$a9
        sta     $0900
        lda     #$11
        sta     $0901
        lda     #$00
        jsr     $0900
        sta     $0202
        jmp     done

        org     $380
done:   jmp     done
