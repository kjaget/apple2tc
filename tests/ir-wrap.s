; The recording starts with LDA #$5A at $FFFF, whose operand is at $0000.
        org     $0000
        dfb     $5a
        sta     $0200
done:   jmp     done
