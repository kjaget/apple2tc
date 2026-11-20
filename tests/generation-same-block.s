; The store at $0302 changes the opcode of the next instruction in this block.
        org     $300
        lda     #$a2
        sta     $0305
target: lda     #$11
        sta     $0200
        jmp     done

        org     $380
done:   jmp     done
