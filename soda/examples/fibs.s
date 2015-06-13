; Fibonacci sequence generator

jmp 100 ; main

pdend:
        add r0, 48      ; '0'
        add r2, 1       ; count the digits
        push r0
        swap
        ret
pd:
        cmp r0, 10      ; r0 %= 10
        jl pdend
        sub r0, 10
        jmp pd
printnum:
        push r0
        push r1
        push r2
        mov r2, 0
        mov r1, r0
nd:
        call pd
        div r1, 10
        mov r0, r1
        jnz nd
outd:
        pop r0
        putc r0
        sub r2, 1
        jnz outd
        mov r0, 10      ; \n
        putc r0
        pop r2
        pop r1
        pop r0
        ret

100:
        mov r0, 0
        mov r1, 1
fib:
        call printnum
        mov r2, r0
        add r2, r1
        mov r0, r1
        mov r1, r2
        jmp fib

