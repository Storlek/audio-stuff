; the Fibonacci Butt Sort algorithm from /prog/, implemented in soda.
; it isn't perfect - a blank line produces an empty set of start/end tags
; (this is particularly noticeable at eof) and the code could be better
; written if the assembler handled using label above their definitions,
; but hey, it more or less works.

_start:
        ;db 5 ; stack dump
        db 6 ; trace
        jmp 200

butt_start: db "[b][i]", 0
butt_end: db "[/i][/b]", 10, 0
butt_mid: db "[u]", 0, "[/u]", 0, "[o]", 0, "[/o]", 0, 0


putstr_end:
        add r0, 1
        pop r1
        ret
putstr:
        push r1
putstr_next:
        lda r1, r0
        jz putstr_end
        putc r1
        add r0, 1
        jmp putstr_next


bol: ; print the start tag
        mov r0, butt_start      ; putstr butt_start
        jmp putstr
eol: ; print the end tag
        mov r0, butt_end        ; putstr butt_end (this includes \n)
        jmp putstr
eof: ; print the end tag and exit
        ; XXX if we haven't printed bol, don't print eol!
        call eol
        halt
eofcheck:
        ; check for eof (-1)
        ; (negative constants aren't handled, else this would be much simpler)
        add r2, 1
        jz eof                  ; if EOF, exit
        sub r2, 1
        ret

butt_eol:
        call eol                ; print the end tag
buttsort:
        getc r2                 ; read a char (*before* printing)
        call bol                ; print the start tag
        call eofcheck           ; check for eof (*after* start tag, so they balance)
butt_inner:
        cmp r2, 10              ; if \n, print end tag and loop again
        jz butt_eol

print_mid:
        ; wrap r2 in mid tags
        mov r0, r1              ; copy butt_mid pointer
        call putstr             ; print open mid tag
        putc r2                 ; the char we just read
        call putstr             ; close mid tag
        mov r1, r0              ; put updated butt_mid pointer back
        ; does butt_mid need reset?
        lda r0, r1              ; check first byte value of butt_mid pointer
        jz 210                  ; reset if 0 (can't use semantic label - s/b jz reset_mid)
print_mid_end:
        getc r2                 ; read another char
        call eofcheck           ; check for eof
        jmp butt_inner


; these should be semantic labels, but can't be
200:    mov r1, butt_mid        ; set up r1 with the first 'mid' pointer
        jmp buttsort
210:    mov r1, butt_mid        ; this line should be labeled reset_mid
        jmp print_mid_end
