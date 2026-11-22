 
struct DisplayLink {
    Word isa,target;
    const char *selector;
    struct ScheduledWork *work;
    unsigned modes,paused,invalid,interval,counter,preferred;
    double timestamp,duration;
    struct DisplayLink *next;
};
static struct DisplayLink *display_links;
static void display_links_changed(void) {
    int active=0;
    pthread_mutex_lock(&scheduling_mutex);
    for (struct DisplayLink *link=display_links;link;link=link->next)
        if (!link->invalid && !link->paused && link->modes && link->work->loop && !link->work->loop->failed) active=1;
    pthread_mutex_unlock(&scheduling_mutex);
    JNIEnv *env=attached_environment("CADisplayLink registration");
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID configure=(*env)->GetMethodID(env,type,"setDisplayLinksActive","(Z)V");
    (*env)->CallVoidMethod(env,activity,configure,active ? JNI_TRUE : JNI_FALSE);
    (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Android vsync registration failed","CADisplayLink"); }
}
static Word display_link_create(Word cls,const char *selector,Word target,const char *action) {
    if (!target || !action) stop("Invalid display callback",selector);
    struct DisplayLink *link=calloc(1,sizeof(*link));
    if (!link) stop("Allocation failed",selector);
    link->isa=external_class("_OBJC_CLASS_$_CADisplayLink"); link->target=target; link->selector=action; link->interval=1;
    link->work=work_create();
    link->work->target=target; link->work->selector=action; link->work->argument=(Word)link; link->work->external_source=1;
    work_publish(link->work);
    pthread_mutex_lock(&scheduling_mutex); link->next=display_links; display_links=link; pthread_mutex_unlock(&scheduling_mutex);
    return (Word)link;
}
static unsigned display_mode(Word mode,const char *selector) {
    if (!scheduling_mode(mode)) stop("Unsupported display runloop mode",selector);
    return !strcmp(string_bytes(mode,selector),"kCFRunLoopCommonModes") ? 2 : 1;
}
static void display_link_add(struct DisplayLink *link,const char *selector,Word loop,Word mode) {
    unsigned bits=display_mode(mode,selector);
    struct NativeLoop *native=native_loop(loop);
    pthread_mutex_lock(&scheduling_mutex);
    if (link->invalid || (link->work->loop && link->work->loop!=native)) {
        pthread_mutex_unlock(&scheduling_mutex); stop("Invalid display runloop registration",selector);
    }
    if (!link->modes) ++native->ports;
    link->work->loop=native; link->modes|=bits; link->work->valid=1;
    pthread_cond_broadcast(&scheduling_condition); pthread_mutex_unlock(&scheduling_mutex);
    display_links_changed();
}
static void display_link_remove(struct DisplayLink *link,const char *selector,Word loop,Word mode) {
    unsigned bits=display_mode(mode,selector);
    pthread_mutex_lock(&scheduling_mutex);
    if (link->work->loop && link->work->loop->object==loop && (link->modes&bits)) {
        link->modes&=~bits;
        if (!link->modes) {
            if (link->work->loop->ports) --link->work->loop->ports;
            link->work->scheduled=0; ++link->work->generation; link->work->loop=NULL;
        }
    }
    pthread_cond_broadcast(&scheduling_condition); pthread_mutex_unlock(&scheduling_mutex);
    display_links_changed();
}
static void display_link_invalidate(struct DisplayLink *link) {
    pthread_mutex_lock(&scheduling_mutex);
    if (link->modes && link->work->loop && link->work->loop->ports) --link->work->loop->ports;
    link->modes=0; link->invalid=1; link->work->valid=0; link->work->scheduled=0; ++link->work->generation;
    pthread_cond_broadcast(&scheduling_condition); pthread_mutex_unlock(&scheduling_mutex);
    display_links_changed();
}
static void display_link_pause(struct DisplayLink *link,const char *selector,unsigned char paused) {
    pthread_mutex_lock(&scheduling_mutex);
    link->paused=paused!=0; link->counter=0;
    if (paused) { link->work->scheduled=0; ++link->work->generation; }
    pthread_mutex_unlock(&scheduling_mutex); display_links_changed();
}
static void display_link_rate(struct DisplayLink *link,const char *selector,Word value) {
    int preferred=!strcmp(selector,"setPreferredFramesPerSecond:");
    if ((!preferred && !value) || value>1000) stop("Invalid display rate",selector);
    pthread_mutex_lock(&scheduling_mutex);
    if (preferred) link->preferred=value; else { link->interval=value; link->preferred=0; }
    link->counter=0; pthread_mutex_unlock(&scheduling_mutex);
    if (render_diagnostics_requested) LOG("[DIAG-render] Display link rate: interval=%u preferred=%u",link->interval,link->preferred);
}
static Word display_link_integer(struct DisplayLink *link,const char *selector) {
    pthread_mutex_lock(&scheduling_mutex);
    Word value=!strcmp(selector,"isPaused") ? link->paused : !strcmp(selector,"frameInterval") ? link->interval : link->preferred;
    pthread_mutex_unlock(&scheduling_mutex); return value;
}
static double display_link_time(struct DisplayLink *link,const char *selector) {
    pthread_mutex_lock(&scheduling_mutex);
    double value=!strcmp(selector,"duration") ? link->duration : !strcmp(selector,"targetTimestamp") ? link->timestamp+link->duration*link->interval : link->timestamp;
    pthread_mutex_unlock(&scheduling_mutex); return value;
}
JNIEXPORT jstring JNICALL HOST_JNI(FrameScheduler_frame)(JNIEnv *env,jclass cls,jlong nanos,jdouble refresh) {
    if (!isfinite(refresh) || refresh<=0) return NULL;
    may_unwind=1; snprintf(stage,sizeof(stage),"Android display source");
    int failed=setjmp(boundary);
    if (!failed) {
        pthread_mutex_lock(&scheduling_mutex);
        for (struct DisplayLink *link=display_links;link;link=link->next) {
            struct ScheduledWork *work=link->work;
            if (link->invalid || link->paused || !link->modes || !work->loop || work->loop->failed) continue;
            unsigned interval=link->preferred ? (unsigned)fmax(1,round(refresh/link->preferred)) : link->interval;
            static unsigned logged;
            if (render_diagnostics_requested && __atomic_fetch_add(&logged,1,__ATOMIC_RELAXED)<3)
                LOG("[DIAG-render] Display source: refresh=%g interval=%u target=%s",refresh,interval,link->selector);
            if (++link->counter<interval) continue;
            link->counter=0;
            if (work->running || work->scheduled) continue;
            link->timestamp=nanos*1e-9; link->duration=1.0/refresh;
            work->deadline=scheduling_now(); work->valid=1; work->scheduled=1; work->done=0; ++work->generation;
        }
        pthread_cond_broadcast(&scheduling_condition); pthread_mutex_unlock(&scheduling_mutex);
        for (struct DisplayLink *link=display_links;link;link=link->next) schedule_main_work(link->work);
    }
    may_unwind=0;
    return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static Method display_link_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if ((!strcmp(receiver,"_OBJC_CLASS_$_CADisplayLink") && class_receiver) || (!strcmp(receiver,"_OBJC_CLASS_$_UIScreen") && !class_receiver))
        if (!strcmp(selector,"displayLinkWithTarget:selector:")) return (Method)display_link_create;
    if (class_receiver || strcmp(receiver,"_OBJC_CLASS_$_CADisplayLink")) return NULL;
    if (!strcmp(selector,"addToRunLoop:forMode:")) return (Method)display_link_add;
    if (!strcmp(selector,"removeFromRunLoop:forMode:")) return (Method)display_link_remove;
    if (!strcmp(selector,"invalidate")) return (Method)display_link_invalidate;
    if (!strcmp(selector,"setPaused:")) return (Method)display_link_pause;
    if (!strcmp(selector,"setFrameInterval:") || !strcmp(selector,"setPreferredFramesPerSecond:")) return (Method)display_link_rate;
    if (!strcmp(selector,"isPaused") || !strcmp(selector,"frameInterval") || !strcmp(selector,"preferredFramesPerSecond")) return (Method)display_link_integer;
    if (!strcmp(selector,"timestamp") || !strcmp(selector,"duration") || !strcmp(selector,"targetTimestamp")) return (Method)display_link_time;
    return NULL;
}
