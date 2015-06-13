; Caesar cipher

jmp 100 ; main

halt:   halt
ret:    ret
mod26:  ; r1 %= 26
        cmp r1, 26
        jl ret
        sub r1, 26
        jmp mod26

nextc:  putc r0
100:
        getc r0
        jl halt         ; terminate at eof
        mov r1, r0
        and r1, 0xdf    ; mask out lowercase bit
        cmp r1, 65
        jl nextc
        cmp r1, 90
        jg nextc
        and r0, 32      ; save case
        ; XXX for rot-n, add 13+n to r1 here
        call mod26
        add r1, 65
        or r0, r1       ; restore case
        jmp nextc

