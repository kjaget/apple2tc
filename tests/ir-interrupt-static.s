        org     $300
        brk     #0
done:   jmp     done

        org     $340
        lda     #$55
        sta     $0200
        rti

        org     $fffe
        dw      $0340
