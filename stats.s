@ stats.s

.global death_count
.global timer

death_count:
    @ TODO write this function
    add r0, r0, #1
    mov r0, r0, lsr #2
    mov pc, lr

timer:
    @TODO write this function
     add r0, r0, #1
     mov r0, r0, lsr #2
     mov pc, lr
