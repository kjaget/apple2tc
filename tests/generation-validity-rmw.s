; Exercise generation validity tracking through a generated read-modify-write.
        org     $300
        ldx     #$ff
        txs
        cld
        inc     $0200
        jmp     done

        org     $380
done:   jmp     done
