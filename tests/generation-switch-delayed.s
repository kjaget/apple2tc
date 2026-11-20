; Replace and restore code after a delay, using indexed and indirect stores.
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
        jsr     delay

        lda     #$a2
        ldx     #$01
        sta     $08ff,x
        lda     #$00
        sta     $0010
        lda     #$09
        sta     $0011
        ldy     #$01
        lda     #$22
        sta     ($10),y
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

        org     $500
delay:  ldx     #$04
delay_loop:
        dex
        bne     delay_loop
        rts
