/* Darwin signal numbers, mask bits, action flags and stack layout differ from
 * Bionic. SA_SIGINFO requires a Darwin ucontext bridge and reports ENOTSUP
 * until that bridge exists; it must never receive a Linux context by accident. */
static int native_signal_number(int number) {
    static const int map[32]={0,SIGHUP,SIGINT,SIGQUIT,SIGILL,SIGTRAP,SIGABRT,0,SIGFPE,SIGKILL,SIGBUS,SIGSEGV,SIGSYS,
        SIGPIPE,SIGALRM,SIGTERM,SIGURG,SIGSTOP,SIGTSTP,SIGCONT,SIGCHLD,SIGTTIN,SIGTTOU,SIGIO,SIGXCPU,SIGXFSZ,
        SIGVTALRM,SIGPROF,SIGWINCH,0,SIGUSR1,SIGUSR2};
    return number>0 && number<32 ? map[number] : 0;
}
static int darwin_signal_number(int native) {
    for (int i=1;i<32;++i) if (native_signal_number(i)==native) return i;
    return 0;
}
static void native_signal_mask(uint32_t source,sigset_t *destination) {
    sigemptyset(destination);
    for (int i=1;i<32;++i) if ((source&(1U<<(i-1))) && native_signal_number(i)) sigaddset(destination,native_signal_number(i));
}
static uint32_t darwin_signal_mask(const sigset_t *source) {
    uint32_t result=0;
    for (int i=1;i<32;++i) if (native_signal_number(i) && sigismember(source,native_signal_number(i))==1) result|=1U<<(i-1);
    return result;
}
struct DarwinSignalAction { void (*handler)(int); uint32_t mask,flags; };
struct DarwinSignalStack { void *pointer; size_t size; int flags; };
static struct DarwinSignalAction signal_actions[32];
static void guest_simple_signal(int number) {
    int guest=darwin_signal_number(number);
    if (!guest) return;
    void (*handler)(int)=__atomic_load_n(&signal_actions[guest].handler,__ATOMIC_ACQUIRE);
    if ((Word)handler>1) handler(guest);
}
static int guest_signal_action(int number,const struct DarwinSignalAction *action,struct DarwinSignalAction *previous) {
    int native_number=native_signal_number(number);
    if (!native_number) { errno=EINVAL; return -1; }
    if (action && (action->flags&0x40)) {
        LOG("Darwin SA_SIGINFO context bridge unavailable (signal=%d)",number);
        errno=ENOTSUP; return -1;
    }
    if (action && (action->flags&~(uint32_t)0x3f)) { errno=EINVAL; return -1; }
    struct sigaction native={0},old;
    if (action) {
        native_signal_mask(action->mask,&native.sa_mask);
        if (action->flags&1) native.sa_flags|=SA_ONSTACK;
        if (action->flags&2) native.sa_flags|=SA_RESTART;
        if (action->flags&4) native.sa_flags|=SA_RESETHAND;
        if (action->flags&8) native.sa_flags|=SA_NOCLDSTOP;
        if (action->flags&16) native.sa_flags|=SA_NODEFER;
        if (action->flags&32) native.sa_flags|=SA_NOCLDWAIT;
        native.sa_handler=(Word)action->handler>1 ? guest_simple_signal : action->handler;
    }
     
    if (sigaction(native_number,NULL,&old)) return -1;
    if (previous && (old.sa_flags&SA_SIGINFO) && (Word)old.sa_handler>1) { errno=ENOTSUP; return -1; }
    if (previous) {
        if (old.sa_handler==guest_simple_signal) *previous=signal_actions[number];
        else {
            previous->handler=old.sa_handler; previous->mask=darwin_signal_mask(&old.sa_mask); previous->flags=0;
            if (old.sa_flags&SA_ONSTACK) previous->flags|=1;
            if (old.sa_flags&SA_RESTART) previous->flags|=2;
            if (old.sa_flags&SA_RESETHAND) previous->flags|=4;
            if (old.sa_flags&SA_NOCLDSTOP) previous->flags|=8;
            if (old.sa_flags&SA_NODEFER) previous->flags|=16;
            if (old.sa_flags&SA_NOCLDWAIT) previous->flags|=32;
        }
    }
    if (!action) return 0;
    struct DarwinSignalAction saved=signal_actions[number];
    signal_actions[number].mask=action->mask; signal_actions[number].flags=action->flags;
    __atomic_store_n(&signal_actions[number].handler,action->handler,__ATOMIC_RELEASE);
    if (!sigaction(native_number,&native,NULL)) return 0;
    signal_actions[number]=saved;
    return -1;
}
static void (*guest_signal(int number,void (*handler)(int)))(int) {
    struct DarwinSignalAction action={handler,0,2},previous;
    return guest_signal_action(number,&action,&previous) ? SIG_ERR : previous.handler;
}
static int guest_signal_stack(const struct DarwinSignalStack *stack,struct DarwinSignalStack *previous) {
    stack_t native={0},old;
    if (stack) {
        if (stack->flags&~5) { errno=EINVAL; return -1; }
        native.ss_sp=stack->pointer; native.ss_size=stack->size;
        native.ss_flags=(stack->flags&1 ? SS_ONSTACK : 0)|(stack->flags&4 ? SS_DISABLE : 0);
    }
    if (sigaltstack(stack ? &native : NULL,previous ? &old : NULL)) return -1;
    if (previous) *previous=(struct DarwinSignalStack){old.ss_sp,old.ss_size,(old.ss_flags&SS_ONSTACK ? 1 : 0)|(old.ss_flags&SS_DISABLE ? 4 : 0)};
    return 0;
}
static int guest_signal_mask(int how,const uint32_t *set,uint32_t *previous) {
    if (how<1 || how>3) { errno=EINVAL; return -1; }
    sigset_t native,old;
    if (set) native_signal_mask(*set,&native);
    if (sigprocmask(how==1 ? SIG_BLOCK : how==2 ? SIG_UNBLOCK : SIG_SETMASK,set ? &native : NULL,previous ? &old : NULL)) return -1;
    if (previous) *previous=darwin_signal_mask(&old);
    return 0;
}
static int guest_pthread_kill(pthread_t thread,int number) {
    int signal=number ? native_signal_number(number) : 0;
    if (number && !signal) return EINVAL;
    pthread_mutex_lock(&thread_records_lock);
    int error=registered_thread(thread) ? pthread_kill(thread,signal) : ESRCH;
    pthread_mutex_unlock(&thread_records_lock);
    return darwin_pthread_error(error);
}
/* Foundation owns the guest uncaught-exception callback independently of
 * POSIX signal handlers. This registration does not translate exception ABI. */
static Word uncaught_exception_handler;
static Word guest_get_uncaught_exception_handler(void) {
    return __atomic_load_n(&uncaught_exception_handler,__ATOMIC_ACQUIRE);
}
static void guest_set_uncaught_exception_handler(Word handler) {
    __atomic_store_n(&uncaught_exception_handler,handler,__ATOMIC_RELEASE);
}
static Word resolve_signal_import(const char *name) {
    if (!strcmp(name,"_NSGetUncaughtExceptionHandler")) return (Word)guest_get_uncaught_exception_handler;
    if (!strcmp(name,"_NSSetUncaughtExceptionHandler")) return (Word)guest_set_uncaught_exception_handler;
    if (!strcmp(name,"_sigaltstack")) return (Word)guest_signal_stack;
    if (!strcmp(name,"_sigaction")) return (Word)guest_signal_action;
    if (!strcmp(name,"_sigprocmask")) return (Word)guest_signal_mask;
    if (!strcmp(name,"_signal")) return (Word)guest_signal;
    if (!strcmp(name,"_pthread_kill")) return (Word)guest_pthread_kill;
    return 0;
}
