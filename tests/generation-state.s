; The second state lengthens the instruction at $0400 and moves its RTS.
; It also changes the JMP at $0303 so the old $0402 target becomes stale.
        org     $300
        jsr     $0400
        jmp     $0402

        org     $400
        lda     #$11
        rts
