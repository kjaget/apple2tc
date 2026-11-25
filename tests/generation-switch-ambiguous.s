; The run-data overlays replace the LDA at $0300 with jumps to independent
; RTS routines at $0400 and $0500.
        org     $300
        lda     #$00
        rts

        org     $400
        rts

        org     $500
        rts
