mov cn, 0
mov wave, 2
mov vol, 1
mov freq, 440.0
loop:
sub fc, 1
jmp loop
