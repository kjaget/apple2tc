; Install and execute code without replacing it.
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
        jsr     $0900
        sta     $0201
        jmp     done

        org     $380
done:   jmp     done
