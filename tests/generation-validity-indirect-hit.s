; An indirect store changes the next opcode in the current basic block.
        org     $300
        lda     #$0e
        sta     $10
        lda     #$03
        sta     $11
        ldy     #$00
        lda     #$a2
        sta     ($10),y
target: lda     #$11
        sta     $0200
        jmp     done

        org     $380
done:   jmp     done
