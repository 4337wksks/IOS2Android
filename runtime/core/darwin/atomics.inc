 
static int32_t atomic_add32(int32_t delta,volatile int32_t *value) { return __atomic_add_fetch(value,delta,__ATOMIC_RELAXED); }
static int32_t atomic_add32_barrier(int32_t delta,volatile int32_t *value) { return __atomic_add_fetch(value,delta,__ATOMIC_SEQ_CST); }
static int32_t atomic_increment32(volatile int32_t *value) { return atomic_add32(1,value); }
static int32_t atomic_increment32_barrier(volatile int32_t *value) { return atomic_add32_barrier(1,value); }
static int32_t atomic_decrement32(volatile int32_t *value) { return atomic_add32(-1,value); }
static int32_t atomic_decrement32_barrier(volatile int32_t *value) { return atomic_add32_barrier(-1,value); }
static unsigned char atomic_swap32(int32_t expected,int32_t desired,volatile int32_t *value) {
    return __atomic_compare_exchange_n(value,&expected,desired,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED);
}
static unsigned char atomic_swap32_barrier(int32_t expected,int32_t desired,volatile int32_t *value) {
    return __atomic_compare_exchange_n(value,&expected,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
}
static int64_t atomic_add64(int64_t delta,volatile int64_t *value) { return __atomic_add_fetch(value,delta,__ATOMIC_RELAXED); }
static int64_t atomic_add64_barrier(int64_t delta,volatile int64_t *value) { return __atomic_add_fetch(value,delta,__ATOMIC_SEQ_CST); }
static int64_t atomic_increment64(volatile int64_t *value) { return atomic_add64_barrier(1,value); }
static int64_t atomic_decrement64(volatile int64_t *value) { return atomic_add64_barrier(-1,value); }
static unsigned char atomic_swap64(int64_t expected,int64_t desired,volatile int64_t *value) {
    return __atomic_compare_exchange_n(value,&expected,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
}
static unsigned char atomic_swap_pointer(void *expected,void *desired,void *volatile *value) {
    return __atomic_compare_exchange_n(value,&expected,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
}
static void spin_lock(volatile int32_t *value) {
    for (;;) {
        if (!__atomic_exchange_n(value,1,__ATOMIC_ACQUIRE)) return;
        while (__atomic_load_n(value,__ATOMIC_RELAXED)) sched_yield();
    }
}
static void spin_unlock(volatile int32_t *value) { __atomic_store_n(value,0,__ATOMIC_RELEASE); }
static Word resolve_atomic_import(const char *name) {
    if (!strcmp(name,"_OSAtomicAdd32")) return (Word)atomic_add32;
    if (!strcmp(name,"_OSAtomicAdd32Barrier")) return (Word)atomic_add32_barrier;
    if (!strcmp(name,"_OSAtomicIncrement32")) return (Word)atomic_increment32;
    if (!strcmp(name,"_OSAtomicIncrement32Barrier")) return (Word)atomic_increment32_barrier;
    if (!strcmp(name,"_OSAtomicDecrement32")) return (Word)atomic_decrement32;
    if (!strcmp(name,"_OSAtomicDecrement32Barrier")) return (Word)atomic_decrement32_barrier;
    if (!strcmp(name,"_OSAtomicCompareAndSwap32")) return (Word)atomic_swap32;
    if (!strcmp(name,"_OSAtomicCompareAndSwap32Barrier")) return (Word)atomic_swap32_barrier;
    if (!strcmp(name,"_OSAtomicAdd64")) return (Word)atomic_add64;
    if (!strcmp(name,"_OSAtomicAdd64Barrier")) return (Word)atomic_add64_barrier;
    if (!strcmp(name,"_OSAtomicIncrement64Barrier")) return (Word)atomic_increment64;
    if (!strcmp(name,"_OSAtomicDecrement64Barrier")) return (Word)atomic_decrement64;
    if (!strcmp(name,"_OSAtomicCompareAndSwap64Barrier")) return (Word)atomic_swap64;
    if (!strcmp(name,"_OSAtomicCompareAndSwapPtrBarrier")) return (Word)atomic_swap_pointer;
    if (!strcmp(name,"_OSSpinLockLock")) return (Word)spin_lock;
    if (!strcmp(name,"_OSSpinLockUnlock")) return (Word)spin_unlock;
    return 0;
}
