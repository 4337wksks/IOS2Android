 
struct MachSemaphoreWaiter { pthread_cond_t condition; int ready,result; struct MachSemaphoreWaiter *next; };
struct MachSemaphore { uint32_t name,count; struct MachSemaphoreWaiter *first,*last; struct MachSemaphore *next; };
static pthread_mutex_t mach_semaphore_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct MachSemaphore *mach_semaphores;
static uint32_t mach_semaphore_next=0x40000000;
static struct MachSemaphore **mach_semaphore_link(uint32_t name) {
    struct MachSemaphore **link=&mach_semaphores;
    while (*link && (*link)->name!=name) link=&(*link)->next;
    return link;
}
static int guest_semaphore_create(uint32_t task,uint32_t *name,int policy,int value) {
    if (task!=self_task_port || !name || policy!=0 || value<0) return 4;
    struct MachSemaphore *semaphore=calloc(1,sizeof(*semaphore)); if (!semaphore) return 3;
    pthread_mutex_lock(&mach_semaphore_mutex);
    if (mach_semaphore_next>=0x7fffffff) { pthread_mutex_unlock(&mach_semaphore_mutex); free(semaphore); return 3; }
    semaphore->name=mach_semaphore_next++; semaphore->count=(uint32_t)value;
    semaphore->next=mach_semaphores; mach_semaphores=semaphore; *name=semaphore->name;
    pthread_mutex_unlock(&mach_semaphore_mutex); return 0;
}
static int guest_semaphore_wait(uint32_t name) {
    pthread_mutex_lock(&mach_semaphore_mutex);
    struct MachSemaphore *semaphore=*mach_semaphore_link(name);
    if (!semaphore) { pthread_mutex_unlock(&mach_semaphore_mutex); return 15; }
    if (semaphore->count) { --semaphore->count; pthread_mutex_unlock(&mach_semaphore_mutex); return 0; }
    struct MachSemaphoreWaiter waiter={0};
    if (pthread_cond_init(&waiter.condition,NULL)) { pthread_mutex_unlock(&mach_semaphore_mutex); return 3; }
    if (semaphore->last) semaphore->last->next=&waiter; else semaphore->first=&waiter;
    semaphore->last=&waiter;
    while (!waiter.ready) pthread_cond_wait(&waiter.condition,&mach_semaphore_mutex);
    pthread_mutex_unlock(&mach_semaphore_mutex);
    pthread_cond_destroy(&waiter.condition);
    return waiter.result;
}
static int guest_semaphore_signal(uint32_t name) {
    pthread_mutex_lock(&mach_semaphore_mutex);
    struct MachSemaphore *semaphore=*mach_semaphore_link(name);
    if (!semaphore) { pthread_mutex_unlock(&mach_semaphore_mutex); return 15; }
    if (semaphore->first) {
        struct MachSemaphoreWaiter *waiter=semaphore->first;
        semaphore->first=waiter->next; if (!semaphore->first) semaphore->last=NULL;
        waiter->ready=1; waiter->result=0; pthread_cond_signal(&waiter->condition);
    } else {
        if (semaphore->count==INT32_MAX) { pthread_mutex_unlock(&mach_semaphore_mutex); return 3; }
        ++semaphore->count;
    }
    pthread_mutex_unlock(&mach_semaphore_mutex); return 0;
}
static int guest_semaphore_destroy(uint32_t task,uint32_t name) {
    if (task!=self_task_port) return 4;
    pthread_mutex_lock(&mach_semaphore_mutex);
    struct MachSemaphore **link=mach_semaphore_link(name),*semaphore=*link;
    if (!semaphore) { pthread_mutex_unlock(&mach_semaphore_mutex); return 15; }
    *link=semaphore->next;
    for (struct MachSemaphoreWaiter *waiter=semaphore->first;waiter;waiter=waiter->next) {
        waiter->result=37; waiter->ready=1; pthread_cond_signal(&waiter->condition);
    }
    pthread_mutex_unlock(&mach_semaphore_mutex);
    free(semaphore); return 0;
}
static Word resolve_semaphore_import(const char *name) {
    if (!strcmp(name,"_semaphore_create")) return (Word)guest_semaphore_create;
    if (!strcmp(name,"_semaphore_destroy")) return (Word)guest_semaphore_destroy;
    if (!strcmp(name,"_semaphore_signal")) return (Word)guest_semaphore_signal;
    if (!strcmp(name,"_semaphore_wait")) return (Word)guest_semaphore_wait;
    return 0;
}
