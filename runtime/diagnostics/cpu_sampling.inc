 
#if I2A_DEVELOPMENT
#include <sys/time.h>
#include <ucontext.h>
struct CpuSample { Word pc,lr,cls; const char *selector; int tid; unsigned ready; };
static struct CpuSample cpu_samples[4096];
static unsigned cpu_sample_count;
static int cpu_sampling_active,cpu_sampling_installed;
static struct sigaction cpu_sampling_previous;
static void cpu_sample_signal(int signal,siginfo_t *info,void *context) {
    if (!__atomic_load_n(&cpu_sampling_active,__ATOMIC_RELAXED)) return;
    unsigned slot=__atomic_fetch_add(&cpu_sample_count,1,__ATOMIC_RELAXED);
    if (slot>=COUNT(cpu_samples)) return;
    ucontext_t *u=context;
    cpu_samples[slot].pc=u->uc_mcontext.pc; cpu_samples[slot].lr=u->uc_mcontext.regs[30]; cpu_samples[slot].tid=gettid();
    cpu_samples[slot].selector=sampling_selector; cpu_samples[slot].cls=sampling_class;
    __atomic_store_n(&cpu_samples[slot].ready,1,__ATOMIC_RELEASE);
}
JNIEXPORT void JNICALL HOST_JNI(HostActivity_sampleCpu)(JNIEnv *env,jobject object,jboolean enabled) {
    if (enabled) {
        if (cpu_sampling_installed) return;
        struct itimerval existing; getitimer(ITIMER_PROF,&existing);
        sigaction(SIGPROF,NULL,&cpu_sampling_previous);
        if (existing.it_value.tv_sec || existing.it_value.tv_usec || cpu_sampling_previous.sa_handler!=SIG_DFL) {
            LOG("[DIAG-cpu] Sampling unavailable: SIGPROF or timer is already owned"); return;
        }
        memset(cpu_samples,0,sizeof(cpu_samples)); cpu_sample_count=0;
        struct sigaction action={.sa_sigaction=cpu_sample_signal,.sa_flags=SA_SIGINFO|SA_RESTART}; sigemptyset(&action.sa_mask);
        if (sigaction(SIGPROF,&action,NULL)) { LOG("[DIAG-cpu] sigaction failed errno=%d",errno); return; }
        cpu_sampling_installed=1; __atomic_store_n(&cpu_sampling_active,1,__ATOMIC_RELEASE);
        struct itimerval interval={.it_interval={0,10000},.it_value={0,10000}};
        if (setitimer(ITIMER_PROF,&interval,NULL)) { cpu_sampling_active=0; LOG("[DIAG-cpu] timer failed errno=%d",errno); }
        else LOG("[DIAG-cpu] Sampling started at 100Hz CPU time");
        return;
    }
    if (!cpu_sampling_installed) return;
    struct itimerval stopped={0}; setitimer(ITIMER_PROF,&stopped,NULL);
    __atomic_store_n(&cpu_sampling_active,0,__ATOMIC_RELEASE);
    unsigned observed=__atomic_load_n(&cpu_sample_count,__ATOMIC_ACQUIRE);
    unsigned count=observed>COUNT(cpu_samples) ? COUNT(cpu_samples) : observed;
    /* Every stored sample can have a distinct PC/LR/thread. A 256-entry table
     * silently discarded later addresses and biased the reported hotspots. */
    struct CpuSampleBucket { Word pc,lr; int tid; unsigned count; };
    struct CpuSampleBucket *buckets=calloc(count ? count : 1,sizeof(*buckets));
    if (!buckets) { LOG("[DIAG-cpu] Cannot allocate sample summary"); return; }
    unsigned used=0,bucketed=0;
    for (unsigned i=0;i<count;++i) {
        if (!__atomic_load_n(&cpu_samples[i].ready,__ATOMIC_ACQUIRE)) continue;
        struct CpuSample s=cpu_samples[i]; unsigned j;
        for (j=0;j<used;++j) if (buckets[j].pc==s.pc && buckets[j].lr==s.lr && buckets[j].tid==s.tid) break;
        if (j==used) { buckets[used].pc=s.pc; buckets[used].lr=s.lr; buckets[used].tid=s.tid; ++used; }
        ++buckets[j].count; ++bucketed;
    }
    LOG("[DIAG-cpu] Samples=%u distinct=%u bucketed=%u overflow=%u",count,used,bucketed,observed-count);
    struct { const char *selector; Word cls; unsigned count; } lookups[128]={0}; unsigned lookup_count=0;
    for (unsigned i=0;i<count;++i) if (cpu_samples[i].ready && cpu_samples[i].selector) {
        unsigned j; for (j=0;j<lookup_count;++j) if (lookups[j].cls==cpu_samples[i].cls && lookups[j].selector==cpu_samples[i].selector) break;
        if (j==lookup_count) { if (lookup_count==COUNT(lookups)) continue; lookups[j].cls=cpu_samples[i].cls; lookups[j].selector=cpu_samples[i].selector; ++lookup_count; }
        ++lookups[j].count;
    }
    for (unsigned k=0;k<16 && k<lookup_count;++k) {
        unsigned best=0; for (unsigned j=1;j<lookup_count;++j) if (lookups[j].count>lookups[best].count) best=j;
        if (!lookups[best].count) break;
        LOG("[DIAG-cpu] lookup count=%u cls=%p selector=%s",lookups[best].count,(void *)lookups[best].cls,lookups[best].selector);
        lookups[best].count=0;
    }
    for (unsigned k=0;k<40 && k<used;++k) {
        unsigned best=0; for (unsigned j=1;j<used;++j) if (buckets[j].count>buckets[best].count) best=j;
        if (!buckets[best].count) break;
        Word pc=buckets[best].pc,lr=buckets[best].lr; Dl_info symbol={0}; dladdr((void *)pc,&symbol);
        LOG("[DIAG-cpu] tid=%d count=%u pc=%p guest=0x%lx lr=0x%lx module=%s offset=0x%lx symbol=%s",buckets[best].tid,buckets[best].count,(void *)pc,
            in_image(pc) ? pc-aslr+GUEST_IMAGE_BASE : 0UL,in_image(lr) ? lr-aslr+GUEST_IMAGE_BASE : lr,symbol.dli_fname ? symbol.dli_fname : "?",
            symbol.dli_fbase ? pc-(Word)symbol.dli_fbase : 0UL,symbol.dli_sname ? symbol.dli_sname : "?");
        buckets[best].count=0;
    }
    free(buckets);
     
}
#endif
