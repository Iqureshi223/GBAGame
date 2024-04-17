@ stats.s

.global death_count
.global speed


death_count:
    add r0, r0, #1
    mov pc, lr

speed:
    mov r1, r0
    mov pc, lr


