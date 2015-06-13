; I was going to make a little interactive softsynth, but I ran out of time

mov cn, 0
mov wave, 3
mov vol, 44
mov freq, 32.7

mov cn, 1
mov wave, 1
mov vol, 32
mov freq, 32.5

mov cn, 2
mov wave, 1
mov vol, 32
mov freq, 32.9

mov cn, 3
mov wave, 2
mov vol, 64
mov freq, 32.7
shl freq, 1

; play for a really long time
sub fc, 1

halt

