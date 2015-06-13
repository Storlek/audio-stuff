; maybe this will become a piano or something at some point?
; for now, here's a scale

_start: jmp 100

; major scale, two octaves.
scale: db 2, 2, 1, 2, 2, 2, 1, 2, 2, 1, 2, 2, 2, 1, 0

semitone:               ; calculate r0'th semitone from r1
        div r0, 12.0
        ; TODO r0 <- pow(2, r0)
        mul r0, r1      ; r0 *= r1
        ret

end:                    ; delay slightly and then terminate
        tick 2
        halt

100:
main:
        mov tl, 172     ; set tick length
        shl tl, 6       ; 172<<6 is about 1/4 second at 44khz
        mov cn, 0       ; a sine wave at 100% volume
        mov wave, 2
        mov vol, 64
        mov r1, 130.81  ; start at C.
        mov r2, scale   ; init scale table
next:
        lda r0, r2      ; copy the value
        jz end          ; if it's zero, stop
        call semitone   ; r0 <- pow(2, r0 / 12.0) * r1
        mov freq, r0    ; and set the frequency
        add r1, 1       ; next note byte
        tick            ; wait a bit
        jmp next        ; and next note!

