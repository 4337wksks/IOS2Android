/* SCNetworkReachability's route flags come from Android's active default
 * network. Reachable describes a route, not HTTP success or DNS validation. */
struct ReachContextFields { long version; Word info; Word (*retain)(Word); void (*release)(Word); Word (*describe)(Word); };
struct ReachContext { struct ReachContextFields fields; unsigned references; };
struct Reachability {
    Word tag,queue,runloop;
    void (*callback)(void *,uint32_t,Word);
    struct ReachContext *context;
    char *hostname;
    unsigned generation;
    uint32_t flags;
    int loopback,has_flags;
    struct Reachability *next;
};
struct ReachEvent { struct Reachability *target; struct ReachContext *context; void (*callback)(void *,uint32_t,Word); uint32_t flags; unsigned generation; };
static pthread_mutex_t reachability_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct Reachability *reachabilities;
static _Thread_local int configuration_error;
static void reach_context_release(struct ReachContext *context) {
    if (!context || __atomic_sub_fetch(&context->references,1,__ATOMIC_ACQ_REL)) return;
    if (context->fields.release) context->fields.release(context->fields.info);
    free(context);
}
static void reach_event(void *data) {
    struct ReachEvent *event=data;
    pthread_mutex_lock(&reachability_mutex);
    int current=event->generation==event->target->generation;
    pthread_mutex_unlock(&reachability_mutex);
    if (current) event->callback(event->target,event->flags,event->context ? event->context->fields.info : 0);
    reach_context_release(event->context); free(event);
}
static int android_network_flags(uint32_t *flags) {
    JNIEnv *env=attached_environment("Network reachability");
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"networkFlags","()I");
    jint value=(*env)->CallIntMethod(env,activity,method); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); configuration_error=1001; return 0; }
    *flags=(uint32_t)value; return 1;
}
static void reach_emit(struct Reachability *target,uint32_t flags,int force) {
    if (target->loopback) flags=2|0x10000|0x20000;
    pthread_mutex_lock(&reachability_mutex);
    int changed=force || !target->has_flags || target->flags!=flags;
    target->flags=flags; target->has_flags=1;
    if (!changed || !target->callback || (!target->queue && !target->runloop)) { pthread_mutex_unlock(&reachability_mutex); return; }
    struct ReachEvent *event=calloc(1,sizeof(*event));
    if (!event) { pthread_mutex_unlock(&reachability_mutex); stop("Allocation failed","Reachability event"); }
    *event=(struct ReachEvent){target,target->context,target->callback,flags,target->generation};
    if (event->context) __atomic_add_fetch(&event->context->references,1,__ATOMIC_RELAXED);
    Word queue=target->queue;
    if (queue) ((void (*)(Word))dispatch_export("dispatch_retain"))(queue);
    pthread_mutex_unlock(&reachability_mutex);
    if (queue) {
        ((void (*)(Word,void *,void (*)(void *)))dispatch_export("dispatch_async_f"))(queue,event,reach_event);
        ((void (*)(Word))dispatch_export("dispatch_release"))(queue);
    } else schedule_main_function(reach_event,event);
}
static struct Reachability *reach_create(void) {
    struct Reachability *target=calloc(1,sizeof(*target));
    if (!target) stop("Allocation failed","SCNetworkReachability");
    target->tag=(Word)&reachability_tag;
    pthread_mutex_lock(&reachability_mutex); target->next=reachabilities; reachabilities=target; pthread_mutex_unlock(&reachability_mutex);
    return target;
}
static Word reach_name(Word allocator,const char *name) {
    if (allocator || !name || !*name) { configuration_error=1002; return 0; }
    struct Reachability *target=reach_create(); target->hostname=strdup(name);
    target->loopback=!strcasecmp(name,"localhost"); return (Word)target;
}
static Word reach_address(Word allocator,const unsigned char *address) {
    if (allocator || !address || (address[1]!=2 && address[1]!=30) || address[0]<(address[1]==2 ? 16 : 28)) { configuration_error=1002; return 0; }
    struct Reachability *target=reach_create();
    if (address[1]==2) target->loopback=address[4]==127;
    else { static const unsigned char loopback[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}; target->loopback=!memcmp(address+8,loopback,16); }
    return (Word)target;
}
static int reach_valid(struct Reachability *target) { return target && target->tag==(Word)&reachability_tag; }
static unsigned char reach_flags(struct Reachability *target,uint32_t *flags) {
    if (!reach_valid(target) || !flags) { configuration_error=1002; return 0; }
    if (target->loopback) { *flags=2|0x10000|0x20000; return 1; }
    return (unsigned char)android_network_flags(flags);
}
static unsigned char reach_callback(struct Reachability *target,void (*callback)(void *,uint32_t,Word),const struct ReachContextFields *fields) {
    if (!reach_valid(target) || (fields && fields->version)) { configuration_error=1002; return 0; }
    struct ReachContext *context=NULL;
    if (callback && fields) {
        context=calloc(1,sizeof(*context)); if (!context) return 0;
        context->references=1; context->fields=*fields;
        if (fields->retain) context->fields.info=fields->retain(fields->info);
    }
    pthread_mutex_lock(&reachability_mutex);
    struct ReachContext *previous=target->context;
    target->context=context; target->callback=callback; ++target->generation;
    pthread_mutex_unlock(&reachability_mutex);
    reach_context_release(previous);
    return 1;
}
static unsigned char reach_runloop(struct Reachability *target,Word loop,Word mode) {
    if (!reach_valid(target) || loop!=core_main_runloop()) { configuration_error=1002; return 0; }
    pthread_mutex_lock(&reachability_mutex);
    if (target->queue) { pthread_mutex_unlock(&reachability_mutex); configuration_error=1002; return 0; }
    target->runloop=loop; ++target->generation;
    pthread_mutex_unlock(&reachability_mutex);
    uint32_t flags; if (reach_flags(target,&flags)) reach_emit(target,flags,1);
    return 1;
}
static unsigned char reach_unschedule(struct Reachability *target,Word loop,Word mode) {
    if (!reach_valid(target)) return 0;
    pthread_mutex_lock(&reachability_mutex);
    int matched=target->runloop==loop;
    if (matched) { target->runloop=0; ++target->generation; }
    pthread_mutex_unlock(&reachability_mutex); return matched;
}
static unsigned char reach_queue(struct Reachability *target,Word queue) {
    if (!reach_valid(target)) return 0;
    if (queue) ((void (*)(Word))dispatch_export("dispatch_retain"))(queue);
    pthread_mutex_lock(&reachability_mutex);
    if (target->runloop && queue) {
        pthread_mutex_unlock(&reachability_mutex);
        ((void (*)(Word))dispatch_export("dispatch_release"))(queue); configuration_error=1002; return 0;
    }
    Word previous=target->queue; target->queue=queue; ++target->generation;
    pthread_mutex_unlock(&reachability_mutex);
    if (previous) ((void (*)(Word))dispatch_export("dispatch_release"))(previous);
    uint32_t flags; if (queue && reach_flags(target,&flags)) reach_emit(target,flags,1);
    return 1;
}
JNIEXPORT void JNICALL HOST_JNI(HostActivity_networkChanged)(JNIEnv *env,jobject activity,jint flags) {
    if (!java_vm || !started) return;
    pthread_mutex_lock(&reachability_mutex); struct Reachability *head=reachabilities; pthread_mutex_unlock(&reachability_mutex);
    for (struct Reachability *target=head;target;target=target->next) reach_emit(target,(uint32_t)flags,0);
}
static int configuration_last_error(void) { return configuration_error; }
static Word resolve_reachability_import(const char *name) {
    if (!strcmp(name,"_SCNetworkReachabilityCreateWithAddress")) return (Word)reach_address;
    if (!strcmp(name,"_SCNetworkReachabilityCreateWithName")) return (Word)reach_name;
    if (!strcmp(name,"_SCNetworkReachabilityGetFlags")) return (Word)reach_flags;
    if (!strcmp(name,"_SCNetworkReachabilitySetCallback")) return (Word)reach_callback;
    if (!strcmp(name,"_SCNetworkReachabilityScheduleWithRunLoop")) return (Word)reach_runloop;
    if (!strcmp(name,"_SCNetworkReachabilityUnscheduleFromRunLoop")) return (Word)reach_unschedule;
    if (!strcmp(name,"_SCNetworkReachabilitySetDispatchQueue")) return (Word)reach_queue;
    if (!strcmp(name,"_SCError")) return (Word)configuration_last_error;
    return 0;
}
