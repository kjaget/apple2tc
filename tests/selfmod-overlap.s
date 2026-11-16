; Writes to operands in long blocks hidden by shorter, misaligned blocks.
        org     $300
        lda     #$12
        sta     $0343
        sty     $0354
        sta     $0361
        jsr     $0341
        jsr     $0351
        jmp     $0340

        org     $340
        lda     #$02            ; $0341 is an invalid opcode
        sta     $abac,y         ; only the low operand byte is written
        jmp     $0350

        org     $350
        lda     #$02            ; $0351 is an invalid opcode
        sta     $ac00,y         ; only the high operand byte is written
        jmp     $0360

        org     $360
        sta     $1234,y         ; modified without an overlapping block
        sta     $5678,y         ; not modified
        rts
