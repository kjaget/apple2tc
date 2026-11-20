; PHA writes a routine into the stack page before execution.
        org     $300
        ldx     #$40
        txs
        lda     #$03
        pha
        lda     #$80
        pha
        lda     #$4c
        pha
        lda     #$11
        pha
        lda     #$a9
        pha
        ldx     #$ff
        txs
        jmp     $013c

        org     $380
done:   jmp     done
