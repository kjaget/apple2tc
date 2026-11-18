; The recording's first $0300 instruction has operand $80, not $81.
; The operand at $0380 changes via an indirect store.
        org     $300
        lda     #$81
        sta     $00
        lda     #$03
        sta     $01
        jsr     $0380
        sta     $0200
        lda     #$22
        ldy     #$01
        sta     ($00),y
        jsr     $0380
        sta     $0201
done:   jmp     done

        org     $380
        lda     #$11
        rts
