 
__attribute__((naked,returns_twice)) static int darwin_setjmp(void *buffer) {
    __asm__ volatile(
        "stp x19,x20,[x0,#0]\n"
        "stp x21,x22,[x0,#16]\n"
        "stp x23,x24,[x0,#32]\n"
        "stp x25,x26,[x0,#48]\n"
        "stp x27,x28,[x0,#64]\n"
        "adrp x9,global_guard\n"
        "ldr x9,[x9,:lo12:global_guard]\n"
        "eor x10,x29,x9\n"
        "eor x11,x30,x9\n"
        "stp x10,x11,[x0,#80]\n"
        "mov x10,sp\n"
        "eor x10,x10,x9\n"
        "str x10,[x0,#96]\n"
        "stp d8,d9,[x0,#112]\n"
        "stp d10,d11,[x0,#128]\n"
        "stp d12,d13,[x0,#144]\n"
        "stp d14,d15,[x0,#160]\n"
        "mov w0,wzr\n"
        "ret\n");
}
__attribute__((naked,noreturn)) static void darwin_longjmp(void *buffer,int value) {
    __asm__ volatile(
        "adrp x9,global_guard\n"
        "ldr x9,[x9,:lo12:global_guard]\n"
        "ldp x10,x11,[x0,#80]\n"
        "eor x29,x10,x9\n"
        "eor x30,x11,x9\n"
        "ldr x10,[x0,#96]\n"
        "eor x10,x10,x9\n"
        "ldp d8,d9,[x0,#112]\n"
        "ldp d10,d11,[x0,#128]\n"
        "ldp d12,d13,[x0,#144]\n"
        "ldp d14,d15,[x0,#160]\n"
        "ldp x19,x20,[x0,#0]\n"
        "ldp x21,x22,[x0,#16]\n"
        "ldp x23,x24,[x0,#32]\n"
        "ldp x25,x26,[x0,#48]\n"
        "ldp x27,x28,[x0,#64]\n"
        "mov sp,x10\n"
        "mov w0,w1\n"
        "cbnz w0,1f\n"
        "mov w0,#1\n"
        "1: ret\n");
}
